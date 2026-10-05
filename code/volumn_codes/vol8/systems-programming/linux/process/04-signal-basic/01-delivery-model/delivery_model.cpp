// E1:信号投递模型——三条发送路径 / handler 在指令边界插入 / pending 期不排队 / sigpending 位图
// 编译:g++ -std=c++20 -Wall -Wextra -O2 delivery_model.cpp -o delivery_model
//
// 三段:
//   [E1.1] kill(getpid(),...) / raise() / 子进程 kill() 三条发送路径,handler 都能收到
//   [E1.2] handler 在主循环两条 write 之间插入执行(信号在指令边界/系统调用返回点被受理,
//          不会把主循环正在 write 的那一行劈成两半)
//   [E1.3] SIG_BLOCK 阻塞期间连发 3 次 SIGUSR1:sigpending 位图能看到它 pending,
//          计数 handler 一次没跑。解阻塞后 handler 只跑 1 次——标准信号不排队,只记 1 位
//
// handler 内一律用 write(2) 报数(异步信号安全的唯一法定动作,详见 E3)。
#include <csignal>
#include <cstdio>
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

long g_t0 = 0;
long elapsed_ms() {
    return now_ms() - g_t0;
}

// handler 里可用的安全输出:write(2) 直写 fd,不碰 stdio 缓冲
void hwrite(const char* s) {
    write(STDOUT_FILENO, s, strlen(s));
}

volatile sig_atomic_t g_usr1_count = 0;

void on_usr1(int) {
    g_usr1_count += 1;
    hwrite("    [H] SIGUSR1 handler entered\n");
}

void msleep(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

void dump_pending(const char* tag) {
    sigset_t pend{};
    sigpending(&pend);
    std::printf("[E1.3] %-22s pending: SIGUSR1=%d SIGUSR2=%d SIGALRM=%d\n", tag,
                sigismember(&pend, SIGUSR1), sigismember(&pend, SIGUSR2),
                sigismember(&pend, SIGALRM));
}

} // namespace

int main() {
    // 混用 printf 与 handler 内 write(2):把 stdout 调成行缓冲,保证 .out 里两类输出按时间序落盘
    setvbuf(stdout, nullptr, _IOLBF, 0);
    g_t0 = now_ms();
    struct sigaction sa{};
    sa.sa_handler = on_usr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // 刻意不带 SA_RESTART:让"被打断"可见(thinking/02 已讲重启语义)
    sigaction(SIGUSR1, &sa, nullptr);

    // ------------------------------------------------ [E1.1] 三条发送路径
    std::printf("[E1.1] three send paths, pid=%ld uid=%ld\n", (long)getpid(), (long)getuid());

    g_usr1_count = 0;
    kill(getpid(), SIGUSR1); // 路径一:kill 指定 pid(自己)
    std::printf("[E1.1] path kill(getpid())      -> handler count=%d\n", (int)g_usr1_count);

    g_usr1_count = 0;
    raise(SIGUSR1); // 路径二:raise 对自己发(等价 kill(getpid(), sig))
    std::printf("[E1.1] path raise(SIGUSR1)      -> handler count=%d\n", (int)g_usr1_count);

    g_usr1_count = 0;
    pid_t pid = fork(); // 路径三:别的进程 kill 你
    if (pid == 0) {
        kill(getppid(), SIGUSR1); // 子进程给父进程发
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0); // 收尸,顺带给信号一点送达时间(阻塞 wait 自身也会先跑 handler)
    std::printf("[E1.1] path child kill(parent) -> handler count=%d\n", (int)g_usr1_count);

    // --------------------------- [E1.2] handler 插在主循环两条输出之间(指令边界)
    std::printf("\n[E1.2] main loop prints via write(2), child sends SIGUSR1 "
                "at staggered times:\n");
    pid = fork();
    if (pid == 0) {
        msleep(40);
        kill(getppid(), SIGUSR1);
        msleep(60);
        kill(getppid(), SIGUSR1);
        msleep(60);
        kill(getppid(), SIGUSR1);
        _exit(0);
    }
    for (int i = 1; i <= 8; ++i) {
        char buf[64];
        int n = std::snprintf(buf, sizeof buf, "MAIN iter %d (t=%ldms)\n", i, elapsed_ms());
        write(STDOUT_FILENO, buf, n); // 主循环也用 write:一行一次系统调用,原子落盘
        msleep(20);
    }
    waitpid(pid, &st, 0);
    std::printf("[E1.2] observation: handler lines always sit BETWEEN main "
                "lines, never split one\n");

    // ---------------------------------------- [E1.3] 阻塞期间连发 3 次,只记 1 次
    std::printf("\n[E1.3] block SIGUSR1, send it 3 times while blocked:\n");
    sigset_t block{}, old{};
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, &old); // 阻塞:信号不再投递,进 pending

    g_usr1_count = 0;
    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR1);
    msleep(20); // 给内核足够时间——信号已 pending,但 handler 跑不了
    dump_pending("after 3 sends:");
    std::printf("[E1.3] handler count while blocked = %d (never ran)\n", (int)g_usr1_count);

    sigprocmask(SIG_SETMASK, &old, nullptr); // 解阻塞:pending 的信号此刻投递
    std::printf("[E1.3] after unblock: handler count = %d  <- 3 sends, ONE "
                "delivery\n",
                (int)g_usr1_count);
    dump_pending("after unblock:");

    // 换个说法验证"不排队":再阻塞,这次只发 1 次,对比 handler 计数——与发 3 次时一致
    sigprocmask(SIG_BLOCK, &block, nullptr);
    g_usr1_count = 0;
    kill(getpid(), SIGUSR1);
    msleep(20);
    sigprocmask(SIG_SETMASK, &old, nullptr);
    std::printf("[E1.3] control run: 1 send while blocked -> handler count = "
                "%d (same as 3 sends)\n",
                (int)g_usr1_count);
    return 0;
}
