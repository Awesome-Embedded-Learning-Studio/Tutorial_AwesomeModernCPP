// E6:SIGCHLD 的 reap 模式——为什么 handler 里必须循环 waitpid(WNOHANG),
//     SA_NOCLDSTOP / SA_NOCLDWAIT 语义实测
// 编译:g++ -std=c++20 -Wall -Wextra -O2 sigchld_reap.cpp -o sigchld_reap
// 跑法:./sigchld_reap loop   handler 里 while(waitpid(-1,WNOHANG)>0) 循环收尸(经典写法)
//      ./sigchld_reap naive  handler 里只 waitpid 一次(反例:剩下僵尸)
//
// 与 Lproc01(进程创建与生命周期)的分工:那边讲 waitpid 本身,这里讲信号驱动的收尸。
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void msleep(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

// 读 /proc/<pid>/stat 的 state 字段(括号后第一个字符):Z = 僵尸
char proc_state(pid_t pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE* f = fopen(path, "r");
    if (!f)
        return '?'; // 进程已彻底消失(已被收尸/自动回收)
    char line[256];
    char* ok = fgets(line, sizeof line, f);
    fclose(f);
    if (!ok)
        return '?';
    char* rp = strrchr(line, ')'); // 跳过 comm 字段(里面可能含空格)
    return rp && rp[1] == ' ' ? rp[2] : '?';
}

// ------------------------------------------------ Part A:SA_NOCLDSTOP
volatile sig_atomic_t g_chld_count = 0;
void count_chld(int) {
    g_chld_count = g_chld_count + 1;
}

void run_stop_scenario(bool no_cldstop) {
    struct sigaction sa{};
    sa.sa_handler = count_chld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = no_cldstop ? SA_NOCLDSTOP : 0;
    sigaction(SIGCHLD, &sa, nullptr);

    const long t0 = now_ms();
    g_chld_count = 0;
    pid_t pid = fork();
    if (pid == 0) {
        msleep(50);
        raise(SIGSTOP); // 自己停下
        msleep(50);     // 被恢复后活 50ms
        _exit(0);
    }
    // 父进程轮询 waitpid,观察 stop/continue/exit 三种状态迁移与 SIGCHLD 计数
    bool stopped = false, continued = false, reaped = false;
    while (!reaped) {
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG | WUNTRACED | WCONTINUED);
        if (r == pid) {
            if (WIFSTOPPED(st) && !stopped) {
                stopped = true;
                printf("    t=%3ldms child STOPPED (WIFSTOPPED, stopsig=%d); "
                       "SIGCHLD count now %d\n",
                       now_ms() - t0, WSTOPSIG(st), (int)g_chld_count);
                kill(pid, SIGCONT); // 恢复它
            } else if (WIFCONTINUED(st) && !continued) {
                continued = true;
                printf("    t=%3ldms child CONTINUED (WIFCONTINUED); SIGCHLD "
                       "count now %d\n",
                       now_ms() - t0, (int)g_chld_count);
            } else if (WIFEXITED(st)) {
                reaped = true;
                printf("    t=%3ldms child EXITED (code %d); SIGCHLD count now "
                       "%d\n",
                       now_ms() - t0, WEXITSTATUS(st), (int)g_chld_count);
            }
        }
        msleep(5);
    }
}

// ------------------------------------------------ Part B:合流 + 循环收尸
bool g_loop_reap = true; // argv 决定:经典循环写法 or 反例单次
volatile sig_atomic_t g_handler_entries = 0;
volatile sig_atomic_t g_reaped_in_handler = 0;
pid_t g_kids[8] = {};
volatile sig_atomic_t g_nkids = 0;

void reap_chld(int) {
    g_handler_entries = g_handler_entries + 1; // 进来一次数一次
    int st = 0;
    if (g_loop_reap) {
        while (waitpid(-1, &st, WNOHANG) > 0) // 经典写法:一口气收干净
            g_reaped_in_handler = g_reaped_in_handler + 1;
    } else {
        if (waitpid(-1, &st, WNOHANG) > 0) // 反例:只收一个
            g_reaped_in_handler = g_reaped_in_handler + 1;
    }
}

void run_coalesce_scenario() {
    struct sigaction sa{};
    sa.sa_handler = reap_chld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGCHLD, &sa, nullptr);

    // 先阻塞 SIGCHLD:让 3 个孩子的退出信号在 pending 里合流成 1 位(E1 已证不排队)
    sigset_t blk{}, old{};
    sigemptyset(&blk);
    sigaddset(&blk, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blk, &old);

    g_handler_entries = 0;
    g_reaped_in_handler = 0;
    g_nkids = 0;
    for (int i = 0; i < 3; ++i) {
        pid_t pid = fork();
        if (pid == 0)
            _exit(100 + i); // 立刻退出
        g_kids[i] = pid;
        g_nkids = g_nkids + 1;
        msleep(10); // 错开一点,但都在阻塞窗口内
    }
    msleep(150); // 3 个全死了,SIGCHLD 只 pending 1 位

    sigset_t pend{};
    sigpending(&pend);
    printf("    3 children dead; pending SIGCHLD=%d (one bit, not three)\n",
           sigismember(&pend, SIGCHLD));

    sigprocmask(SIG_SETMASK, &old, nullptr); // 解锁:此刻 handler 才跑
    msleep(50);
    printf("    handler entries=%d, reaped in handler=%d (of %d kids)\n", (int)g_handler_entries,
           (int)g_reaped_in_handler, (int)g_nkids);

    if (g_reaped_in_handler < g_nkids) {
        // 反例分支:看剩下的孩子什么状态
        for (int i = 0; i < g_nkids; ++i)
            printf("    kid %d (pid %d): /proc state = '%c'%s\n", i, g_kids[i],
                   proc_state(g_kids[i]),
                   proc_state(g_kids[i]) == 'Z' ? "  <- ZOMBIE left behind" : "");
        // 收尾清干净,别真给系统留僵尸
        int st = 0;
        while (waitpid(-1, &st, WNOHANG) > 0) {
        }
    }
}

// ------------------------------------------------ Part C:SA_NOCLDWAIT
void run_noclrdwait_scenario(bool use_flag) {
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL; // 处置保持默认,只动 flags——SA_NOCLDWAIT 独立生效
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = use_flag ? SA_NOCLDWAIT : 0;
    sigaction(SIGCHLD, &sa, nullptr);

    pid_t pid = fork();
    if (pid == 0)
        _exit(7);
    msleep(100); // 给孩子时间退出

    if (use_flag) {
        printf("    SA_NOCLDWAIT: /proc state='%c' ('?'=already gone); ", proc_state(pid));
        errno = 0;
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        printf("waitpid -> %d errno=%d(%s)\n", (int)r, errno, strerror(errno));
    } else {
        printf("    default:     /proc state='%c' (Z=zombie waiting for reap); ", proc_state(pid));
        errno = 0;
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        printf("waitpid -> %d WEXITSTATUS=%d\n", (int)r, r == pid ? WEXITSTATUS(st) : -1);
    }
    // 清干净
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    g_loop_reap = !(argc > 1 && strcmp(argv[1], "naive") == 0);

    std::printf("[E6.A] SA_NOCLDSTOP=0 (default): stop/continue ALSO raise SIGCHLD\n");
    run_stop_scenario(false);
    std::printf("[E6.A] SA_NOCLDSTOP=1: stop/continue are SILENT, exit still signals\n");
    run_stop_scenario(true);

    std::printf("\n[E6.B] %s: 3 kids exit while SIGCHLD blocked\n",
                g_loop_reap ? "loop waitpid (classic)" : "single waitpid (naive)");
    run_coalesce_scenario();

    std::printf("\n[E6.C] zombie fate: default vs SA_NOCLDWAIT\n");
    run_noclrdwait_scenario(false);
    run_noclrdwait_scenario(true);
    return 0;
}
