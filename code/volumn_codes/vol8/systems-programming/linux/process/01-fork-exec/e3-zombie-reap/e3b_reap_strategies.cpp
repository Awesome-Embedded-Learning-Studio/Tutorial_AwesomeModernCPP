// E3b 三种收尸姿势:阻塞 wait / waitpid(WNOHANG) 轮询 / SIGCHLD 处理器自动收
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e3b_reap_strategies e3b_reap_strategies.cpp
// 场景:三个子进程错峰退出(200/400/600ms),同一个问题三种解法。
//      姿势一:阻塞 wait,父进程全程干等;
//      姿势二:WNOHANG 轮询,父进程不死等但空转;
//      姿势三:SIGCHLD 处理器 + SA_RESTART,父进程去读管道干正事,孩子死了内核来敲门,
//              处理器里 while(waitpid(...,WNOHANG)>0) 一网打尽;read 被打断后自动续上。
//      附送:SA_RESTART 关掉时 read 返回 EINTR 的对照;以及 SIGCHLD=SIG_IGN 的一刀切。
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/wait.h>
#include <unistd.h>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) * 1e3 + double(ts.tv_nsec) / 1e6;
}
static double g_t0 = 0;
static double el() {
    return now_ms() - g_t0;
}

// 处理器里只能用异步信号安全的函数:write/waitpid/clock_gettime 可以,printf/snprintf 不行
// (它们带用户态缓冲与锁,主流程正用到一半时会被打断)。所以这里手工拼数字。
static char* put_int(char* cur, char* end, long v) {
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = char('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (n > 0 && cur < end)
        *cur++ = tmp[--n];
    return cur;
}
static char* put_str(char* cur, char* end, const char* s) {
    while (*s && cur < end)
        *cur++ = *s++;
    return cur;
}
static volatile sig_atomic_t g_reaped = 0;
static void sigchld_handler(int) {
    int saved = errno;
    for (;;) {
        int st = 0;
        pid_t p = waitpid(-1, &st, WNOHANG);
        if (p <= 0)
            break;               // 没有再可收的了(0)或出错(-1)
        g_reaped = g_reaped + 1; // C++20 里 volatile 自增已弃用,拆开写
        char msg[96], *cur = msg, *end = msg + sizeof msg - 2;
        cur = put_str(cur, end, "    [SIGCHLD 处理器] 收尸 pid=");
        cur = put_int(cur, end, p);
        cur = put_str(cur, end, WIFEXITED(st) ? " exit=" : " signal=");
        cur = put_int(cur, end, WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        cur = put_str(cur, end, "\n");
        *cur = '\0';
        write(STDOUT_FILENO, msg, (size_t)(cur - msg));
    }
    errno = saved;
}

static pid_t fork_sleeper(int ms, int code) {
    pid_t p = fork();
    if (p == 0) {
        usleep(ms * 1000);
        _exit(code);
    }
    return p;
}

static void strategy_blocking() {
    std::printf("=== 姿势一:阻塞 wait —— 父进程什么都干不了,子进程死一个醒一次 ===\n");
    std::fflush(stdout);
    pid_t a = fork_sleeper(200, 11), b = fork_sleeper(400, 22), c = fork_sleeper(600, 33);
    for (int i = 0; i < 3; ++i) {
        int st = 0;
        pid_t p = wait(&st);
        std::printf("  t=%.0fms 收到 pid=%d exit=%d\n", el(), p, WEXITSTATUS(st));
        std::fflush(stdout);
    }
    (void)a;
    (void)b;
    (void)c;
}

static void strategy_polling() {
    std::printf("\n=== 姿势二:waitpid(WNOHANG) 轮询 —— 不阻塞,但要自己一遍遍问 ===\n");
    std::fflush(stdout);
    fork_sleeper(200, 11);
    fork_sleeper(400, 22);
    fork_sleeper(600, 33);
    int reaped = 0, polls = 0, empty = 0;
    while (reaped < 3) {
        ++polls;
        int st = 0;
        pid_t p = waitpid(-1, &st, WNOHANG);
        if (p > 0) {
            ++reaped;
            std::printf("  t=%.0fms 第 %d 次轮询:收到 pid=%d exit=%d\n", el(), polls, p,
                        WEXITSTATUS(st));
            std::fflush(stdout);
        } else { // p==0:有子进程但都没死,白问一趟
            ++empty;
            usleep(20000); // 轮询间隔 20ms
        }
    }
    std::printf("  合计轮询 %d 次,其中空手而归 %d 次——轮询的代价是空转与延迟的折中\n", polls,
                empty);
    std::fflush(stdout);
}

// 姿势三的公共部分:三个子进程各自往管道写一个字节再退出;父进程靠 read 收字节干活,
// 孩子退出触发 SIGCHLD,处理器收尸。restart=true 时 read 自动续命,false 时拿到 EINTR。
static void strategy_sigchld_pass(bool restart) {
    std::printf("\n=== 姿势三:SIGCHLD 处理器 + %s ===\n",
                restart ? "SA_RESTART(read 自动重启)" : "无 SA_RESTART(read 返回 EINTR,自己重试)");
    std::fflush(stdout);
    g_reaped = 0;

    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP | (restart ? SA_RESTART : 0);
    sigaction(SIGCHLD, &sa, nullptr);

    int p[2];
    if (pipe(p)) {
        std::perror("pipe");
        return;
    }
    for (int i = 0; i < 3; ++i) {
        pid_t c = fork();
        if (c == 0) {
            close(p[0]);
            usleep((200 + i * 200) * 1000);
            char msg = char('a' + i);
            if (write(p[1], &msg, 1) != 1)
                _exit(90);
            _exit(20 + i);
        }
    }
    close(p[1]);

    int got = 0, eintr = 0;
    while (got < 3) {
        char b = 0;
        std::fflush(stdout); // 进 read 前把缓冲冲干净,避免输出和处理器抢顺序
        ssize_t r = read(p[0], &b, 1);
        if (r == 1) {
            ++got;
            std::printf("  t=%.0fms read 拿到字节 %c(%s)%s\n", el(), b,
                        restart ? "SA_RESTART 把被信号打断的 read 自动续上了"
                                : "这次 read 正常返回",
                        g_reaped > 0 ? ";处理器已顺手收尸" : "");
            std::fflush(stdout);
        } else if (r < 0 && errno == EINTR) {
            ++eintr;
            std::printf("  t=%.0fms read 被打断了:返回 -1,errno=EINTR(手动 for 循环重试)\n", el());
            std::fflush(stdout);
        } else {
            std::printf("  read 异常:%s\n", std::strerror(errno));
            break;
        }
    }
    close(p[0]);
    usleep(100000); // 给最后一个孩子的 SIGCHLD 一点送达时间
    std::printf("  管道活干完(3 字节),处理器共收尸 %d 个,EINTR 发生 %d 次,僵尸清零\n",
                (int)g_reaped, eintr);
    std::fflush(stdout);
    signal(SIGCHLD, SIG_DFL);
}

static void bonus_sigign() {
    std::printf("\n=== 附:SIGCHLD=SIG_IGN 一刀切 —— 内核自动收尸,wait 反而拿不到 ===\n");
    std::fflush(stdout);
    signal(SIGCHLD, SIG_IGN); // 明确设成 SIG_IGN(不是默认的忽略)才触发自动收尸
    pid_t c = fork();
    if (c == 0)
        _exit(55);
    usleep(200000); // 给内核时间收
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/status", c);
    FILE* f = std::fopen(path, "r");
    std::printf("  子进程 _exit(55) 后 200ms,没有调过任何 wait:%s\n",
                f ? "/proc 里还在(不该发生)" : "/proc 已消失——内核替我们收了,没留僵尸");
    if (f)
        std::fclose(f);
    int st = 0;
    pid_t r = waitpid(c, &st, 0);
    std::printf("  再调 waitpid:返回 %d,errno=%d(%s)——孩子已被内核收走,没得等了\n", r, errno,
                std::strerror(errno));
    std::fflush(stdout);
}

int main() {
    g_t0 = now_ms();
    strategy_blocking();
    strategy_polling();
    strategy_sigchld_pass(true);
    strategy_sigchld_pass(false);
    bonus_sigign();
    return 0;
}
