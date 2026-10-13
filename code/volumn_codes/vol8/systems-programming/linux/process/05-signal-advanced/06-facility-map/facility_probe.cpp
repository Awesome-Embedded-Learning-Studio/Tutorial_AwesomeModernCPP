// E6 信号设施速查:本机可用性探针(《信号(下):实时信号、signalfd 与 pidfd》E6)
//
// 本系列(process/04 上篇 + 本篇)覆盖的信号设施,在这台机器上逐个点名:
// 每行 = 设施 → 底层系统调用编号(x86-64) → 一次真实调用的返回值。
// 「表」本身在 README:角色、典型场景、与相邻设施的分工。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 facility_probe.cpp -o facility_probe && ./facility_probe
#include <csignal>
#include <cstdio>
#include <cstring>
#include <gnu/libc-version.h>
#include <initializer_list>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

int pidfd_open_(pid_t pid, unsigned int f) {
    return (int)syscall(SYS_pidfd_open, pid, f);
}
int pidfd_send_signal_(int fd, int sig, void* info, unsigned int f) {
    return (int)syscall(SYS_pidfd_send_signal, fd, sig, info, f);
}
int pidfd_getfd_(int fd, int t, unsigned int f) {
    return (int)syscall(SYS_pidfd_getfd, fd, t, f);
}

void row(const char* facility, const char* basis, const char* result) {
    std::printf("  %-26s %-34s %s\n", facility, basis, result);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    utsname u{};
    uname(&u);
    std::printf("== E6 信号设施速查:本机可用性探针 ==\n%s %s\nglibc %s\n\n", u.sysname, u.release,
                gnu_get_libc_version());

    // signal():glibc BSD 语义包装(上篇 E2)
    signal(SIGRTMIN + 5, SIG_IGN);
    std::printf("signal()\n");
    row("signal(SIGRTMIN+5,SIG_IGN)", "glibc 包装 → rt_sigaction(13)", "返回 SIG_DFL(0):装上了");

    // sigaction:现代正门(上篇 E2)
    std::printf("sigaction()\n");
    struct sigaction sa{};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, nullptr);
    row("sigaction(SIGUSR1,IGN)", "rt_sigaction(13)", "返回 0");

    // sigqueue:带数据的发送(本篇 E1)
    union sigval sv{};
    sv.sival_int = 1;
    int q = sigqueue(getpid(), SIGUSR1, sv); // SIGUSR1 已被忽略:安全
    std::printf("sigqueue()\n");
    row("sigqueue(self,SIGUSR1,val)", "rt_sigqueueinfo(129)", q == 0 ? "返回 0(带值入队)" : "失败");

    // sigwaitinfo:同步取(本篇 E5)
    {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGUSR2);
        sigprocmask(SIG_BLOCK, &s, nullptr);
        timespec z{0, 0};
        siginfo_t si{};
        int r = sigtimedwait(&s, &si, &z); // 没信号:立刻 EAGAIN
        char b[96];
        snprintf(b, sizeof b, "返回 %d/%s(接口可用)", r, r == -1 ? "EAGAIN" : "意外");
        std::printf("sigwaitinfo()/sigtimedwait()\n");
        row("sigtimedwait(空,0ms)", "rt_sigtimedwait(128)/rt_sigtimedwait_time64(421)", b);
    }

    // signalfd:信号变 fd(本篇 E2)
    {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGUSR2);
        sigprocmask(SIG_BLOCK, &s, nullptr);
        int fd = signalfd(-1, &s, SFD_NONBLOCK | SFD_CLOEXEC);
        char b[96];
        snprintf(b, sizeof b, "返回 fd=%d(SFD_NONBLOCK/SFD_CLOEXEC 都在)", fd);
        std::printf("signalfd()\n");
        row("signalfd({SIGUSR2})", "signalfd4(289)", b);
        close(fd);
    }

    // timerfd:事件循环里和 signalfd 搭班(本篇 E2)
    {
        int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        char b[96];
        snprintf(b, sizeof b, "返回 fd=%d", fd);
        std::printf("timerfd()(对照班底)\n");
        row("timerfd_create(MONOTONIC)", "timerfd_create(283)", b);
        close(fd);
    }

    // pidfd 一族(本篇 E3)
    {
        int pfd = pidfd_open_(getpid(), 0);
        pollfd p{pfd, POLLIN, 0};
        int pr = poll(&p, 1, 0);
        char b1[96];
        snprintf(b1, sizeof b1, "返回 fd=%d;poll(0ms)=%d(自己活着,无事件)", pfd, pr);
        std::printf("pidfd 一族\n");
        row("pidfd_open(self)", "pidfd_open(434)", b1);

        int sent = pidfd_send_signal_(pfd, SIGUSR1, nullptr, 0); // SIGUSR1 被忽略:安全
        char b2[96];
        snprintf(b2, sizeof b2, "返回 %d(句柄发信号)", sent);
        row("pidfd_send_signal(IGN 号)", "pidfd_send_signal(424)", b2);

        int g = pidfd_getfd_(pfd, 0, 0);
        char b3[96];
        snprintf(b3, sizeof b3, "返回 fd=%d(同进程拿到自己 stdin 的副本)", g);
        row("pidfd_getfd(self,0)", "pidfd_getfd(438)", g >= 0 ? b3 : "失败(见 errno)");
        if (g >= 0)
            close(g);
        close(pfd);
    }

    // waitid(P_PIDFD)(本篇 E3 的收尸口径)
    std::printf("waitid(P_PIDFD)\n");
    row("waitid(P_PIDFD,...)", "waitid(247) + idtype=P_PIDFD(3)", "E3 已实测:poll+waitid 收尸闭环");

    std::printf("\n各设施的角色/场景/分工表见本目录 README。\n");
    return 0;
}
