// E6:尺寸与生命周期——TIOCSWINSZ 谁看得见、SIGWINCH 谁收、对端没了 read/write 报什么
//   1) 新开 pty 的 winsize 是 0x0(没有窗口概念)——终端仿真器必须主动 TIOCSWINSZ,
//      否则 ncurses 一类按尺寸画界面的程序直接趴窝
//   2) 父进程改 master 的 winsize,slave 侧 TIOCGWINSZ 立刻看得见;
//      同时内核给 pty 的前台进程组发 SIGWINCH(forkpty 的孩子正好是前台组)
//   3) 孩子退场(slave 全关)后,父进程 read(master) -> EIO,而 write(master) 照样
//      成功、刚写的字节还会被行规程回显回来:EIO 只出现在读端,判据是“队列空+对端关”
//      (与 daemon 篇 E3 的 close(master) -> slave 侧收 SIGHUP 是一对镜像,那边已入册,本篇不重跑)
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

static double now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static volatile sig_atomic_t g_winch = 0;
static void on_winch(int) {
    g_winch = g_winch + 1;
}

struct Snapshot {
    double t_ms;
    unsigned rows, cols;
    int winch;
    int done;
};

int main() {
    int rp[2];
    pipe(rp);

    int mfd = -1;
    double t0 = now_ms();
    pid_t pid = forkpty(&mfd, nullptr, nullptr, nullptr);
    if (pid < 0) {
        perror("forkpty");
        return 1;
    }
    if (pid == 0) {
        close(mfd);
        close(rp[0]);
        signal(SIGWINCH, on_winch);
        for (int i = 0; i < 14; ++i) { // 每 100ms 报一次,共 1.4s 后退场
            usleep(100 * 1000);
            struct winsize wz{};
            ioctl(0, TIOCGWINSZ, &wz);
            Snapshot s{now_ms() - t0, wz.ws_row, wz.ws_col, (int)g_winch, 0};
            if (write(rp[1], &s, sizeof s) < 0)
                break;
        }
        Snapshot s{now_ms() - t0, 0, 0, (int)g_winch, 1};
        (void)write(rp[1], &s, sizeof s);
        close(rp[1]);
        _exit(0); // slave 的所有 fd 随进程关闭
    }
    close(rp[1]);

    struct winsize wz{};
    ioctl(mfd, TIOCGWINSZ, &wz);
    std::printf("[1] 新开 pty 的 winsize: rows=%u cols=%u —— 0x0,没人替你设\n", wz.ws_row,
                wz.ws_col);

    usleep(200 * 1000);
    wz = {30, 100, 0, 0};
    ioctl(mfd, TIOCSWINSZ, &wz);
    std::printf("[2] t=200ms TIOCSWINSZ(30x100)\n");
    usleep(300 * 1000);
    wz = {40, 120, 0, 0};
    ioctl(mfd, TIOCSWINSZ, &wz);
    std::printf("[2] t=500ms TIOCSWINSZ(40x120)\n");

    Snapshot s;
    while (read(rp[0], &s, sizeof s) == (ssize_t)sizeof s) {
        if (s.done)
            break;
        std::printf("    孩子报告 t=%5.0fms TIOCGWINSZ=%ux%u SIGWINCH 计数=%d\n", s.t_ms, s.rows,
                    s.cols, s.winch);
    }
    waitpid(pid, nullptr, 0);
    std::printf("[3] 孩子退场(slave 全关),父进程这边:\n");
    char b[64];
    errno = 0;
    ssize_t n = read(mfd, b, sizeof b);
    std::printf("    read(master)  -> n=%zd errno=%d(%s)  <- 队列空+对端关,才报 EIO\n", n, errno,
                std::strerror(errno));
    errno = 0;
    ssize_t w = write(mfd, "x", 1);
    std::printf("    write(master) -> n=%zd errno=%d(%s)  <- 写不报错\n", w, errno,
                w < 0 ? std::strerror(errno) : "Success");
    usleep(150 * 1000);
    errno = 0;
    n = read(mfd, b, sizeof b);
    std::printf("    再 read(master) -> n=%zd %s  <- 刚写的 x 被行规程回显回来了(ECHO 还开着)\n", n,
                n > 0   ? "(回显)"
                : n < 0 ? std::strerror(errno)
                        : "(EOF)");
    close(rp[0]);
    close(mfd);
    return 0;
}
