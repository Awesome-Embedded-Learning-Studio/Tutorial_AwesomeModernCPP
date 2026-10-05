// E7:信号速览表的三条"硬事实"实测——SIGKILL/SIGSTOP 不可捕获不可忽略,
//     SIGTERM 可忽略但 SIGKILL 不可。SIGPIPE 默认杀进程、忽略后变成 EPIPE
// 编译:g++ -std=c++20 -Wall -Wextra -O2 signal_facts.cpp -o signal_facts && ./signal_facts
//
// 表格其余条目(默认动作/典型用途)是手册事实,整理进 README 的速览表,
// 本程序只验"能实测的三条"。
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

void msleep(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

int try_sigaction(int sig, const char* name) {
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    int r = sigaction(sig, &sa, nullptr);
    printf("    sigaction(%-8s, SIG_IGN) -> %d errno=%d (%s)\n", name, r, r == -1 ? errno : 0,
           r == -1 ? strerror(errno) : "OK, accepted");
    return r;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // ------------------------------------------- [E7.1] 两位"不可捕获不可忽略"
    std::printf("[E7.1] catch/ignore SIGKILL & SIGSTOP: kernel refuses at "
                "sigaction time\n");
    try_sigaction(SIGKILL, "SIGKILL");
    try_sigaction(SIGSTOP, "SIGSTOP");
    try_sigaction(SIGTERM, "SIGTERM"); // 对照:普通信号可装 handler/可忽略
    try_sigaction(SIGSEGV, "SIGSEGV"); // 对照:硬件故障信号也可装 handler(Lmem02 的
                                       // guarded_buffer 正是这么做的)

    // ------------------------- [E7.2] 忽略得了 SIGTERM,忽略不了 SIGKILL(子进程实测)
    std::printf("\n[E7.2] child ignores SIGTERM, still dies to SIGKILL\n");
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGTERM, SIG_IGN); // "温和关门请求"可以被拒绝
        printf("    CHILD %d: SIGTERM ignored, ready\n", (int)getpid());
        for (int i = 0; i < 100; ++i) { // 活 1 秒等被杀
            msleep(10);
            if (i == 30)
                printf("    CHILD: survived a SIGTERM at t=300ms\n");
        }
        _exit(0);
    }
    msleep(100);
    kill(pid, SIGTERM); // 第一下:被忽略
    msleep(400);
    kill(pid, SIGKILL); // 第二下:没得商量
    int st = 0;
    waitpid(pid, &st, 0);
    printf("[E7.2] child end: WIFSIGNALED=%d WTERMSIG=%d (SIGKILL=%d)\n", WIFSIGNALED(st),
           WTERMSIG(st), SIGKILL);

    // ------------------------------- [E7.3] SIGPIPE:默认动作是杀进程,忽略后变成 EPIPE
    std::printf("\n[E7.3] SIGPIPE default kills; ignored -> write returns EPIPE\n");
    int fds[2];
    pipe(fds);

    pid = fork(); // 孩子甲:默认处置,往读端已关的管道写
    if (pid == 0) {
        close(fds[0]); // 关掉读端
        msleep(50);    // 等父进程也关掉
        char c = 'x';
        ssize_t r = write(fds[1], &c, 1); // RST 语义:读者没了,内核发 SIGPIPE
        printf("    CHILD-A: write returned %zd (never printed: dead)\n", r);
        _exit(0);
    }
    close(fds[0]); // 父进程也关
    waitpid(pid, &st, 0);
    printf("[E7.3] child-A end: WIFSIGNALED=%d WTERMSIG=%d (SIGPIPE=%d)\n", WIFSIGNALED(st),
           WTERMSIG(st), SIGPIPE);
    close(fds[1]);

    pipe(fds);
    pid = fork(); // 孩子乙:SIGPIPE 忽略,同样的写
    if (pid == 0) {
        signal(SIGPIPE, SIG_IGN);
        close(fds[0]);
        msleep(50);
        char c = 'x';
        errno = 0;
        ssize_t r = write(fds[1], &c, 1);
        printf("    CHILD-B: write returned %zd errno=%d (%s) -> alive, error "
               "is visible\n",
               r, errno, strerror(errno));
        _exit(0);
    }
    close(fds[0]);
    waitpid(pid, &st, 0);
    printf("[E7.3] child-B end: WIFEXITED=%d code=%d\n", WIFEXITED(st), WEXITSTATUS(st));
    close(fds[1]);
    return 0;
}
