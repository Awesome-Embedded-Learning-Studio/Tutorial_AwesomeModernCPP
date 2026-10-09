// E5 sigwaitinfo/sigtimedwait:不开 handler,在指定点同步取信号(《信号(下)》E5)
//
// 五组观察:
//   a) 基本款:阻塞 {SIGUSR1,SIGUSR2},子进程发来两个信号(一个 sigqueue 带值、一个 kill),
//      主进程 sigwaitinfo 两次取回:si_pid 是发送方、si_value 原样到账 —— 全程零 handler
//   b) sigtimedwait 超时:没信号时等 200ms → -1/EAGAIN,计时对表
//   c) 排队对照:阻塞期连发 3 次 SIGRTMIN+2 → sigwaitinfo 连取 3 次(FIFO);
//      标准信号 SIGUSR2 连发 3 次 → 只取到 1 次(和 E1 的 handler 版同款结论)
//   d) 和 signalfd 抢饭碗:同一个阻塞集合,pending 队列只有一条 ——
//      d1 先 sigwaitinfo 吃掉一条,read(signalfd) 只剩第二条;
//      d2 反过来先 read(signalfd),sigtimedwait 只剩第二条。
//      两种接口是同一队列的两个消费者,谁读了谁消费,不重复、不打架
//   e) 三条出路对齐:handler(异步抢跑)/sigwaitinfo(同步点)/signalfd(事件循环里的 fd)
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 sigwait_lab.cpp -o sigwait_lab && ./sigwait_lab
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

timespec t0 = [] {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}();

long ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec - t0.tv_sec) * 1000 + (ts.tv_nsec - t0.tv_nsec) / 1000000;
}

void block_these(sigset_t* set, std::initializer_list<int> sigs) {
    sigemptyset(set);
    for (int s : sigs)
        sigaddset(set, s);
    sigprocmask(SIG_BLOCK, set, nullptr);
}

void show_info(const char* when, int sig, const siginfo_t* si) {
    std::printf("    %s 取到 signo=%d si_code=%s si_pid=%d si_value=%d\n", when, sig,
                si->si_code == SI_USER ? "SI_USER" : "SI_QUEUE", (int)si->si_pid,
                si->si_value.sival_int);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("== E5 sigwaitinfo/sigtimedwait:同步取信号 ==\n");

    // ---- a) 基本款 ----------------------------------------------------------------
    sigset_t watch;
    block_these(&watch, {SIGUSR1, SIGUSR2});
    pid_t kid = fork();
    if (kid == 0) {
        union sigval sv{};
        sv.sival_int = 11;
        sigqueue(getppid(), SIGUSR1, sv); // 带值
        kill(getppid(), SIGUSR2);         // 裸发
        _exit(0);
    }
    waitpid(kid, nullptr, 0);
    std::printf("\n[a] 阻塞 {SIGUSR1,SIGUSR2},子进程(pid=%d)发了两个信号,主进程同步取:\n",
                (int)kid);
    {
        siginfo_t si{};
        int s = sigwaitinfo(&watch, &si);
        show_info("第 1 次", s, &si);
        s = sigwaitinfo(&watch, &si);
        show_info("第 2 次", s, &si);
    }
    std::printf("    (没装任何 handler:信号被挡在门外,由我在指定的点亲手取)\n");

    // ---- b) sigtimedwait 超时 ------------------------------------------------------
    std::printf("\n[b] sigtimedwait 空等 200ms 超时:\n");
    {
        siginfo_t si{};
        timespec d{0, 200 * 1000000L};
        long before = ms();
        int s = sigtimedwait(&watch, &si, &d);
        std::printf("    返回 %d,errno=%s(EAGAIN),耗时 %ldms(标称 200ms)\n", s,
                    s == -1 ? std::strerror(errno) : "无", ms() - before);
    }

    // ---- c) 排队对照 --------------------------------------------------------------
    sigset_t rtset;
    block_these(&rtset, {SIGRTMIN + 2});
    std::printf("\n[c] 阻塞期连发 3 次:\n");
    for (int v : {1, 2, 3}) {
        union sigval sv{};
        sv.sival_int = v;
        sigqueue(getpid(), SIGRTMIN + 2, sv);
    }
    for (int i = 0; i < 3; ++i) {
        siginfo_t si{};
        int s = sigwaitinfo(&rtset, &si);
        std::printf("    实时信号第 %d 次取到 signo=%d si_value=%d\n", i + 1, s,
                    si.si_value.sival_int);
    }
    for (int i = 0; i < 3; ++i)
        kill(getpid(), SIGUSR2);
    {
        siginfo_t si{};
        int n = 0;
        timespec zero{0, 0};
        while (sigtimedwait(&watch, &si, &zero) > 0)
            ++n; // 非阻塞清点
        std::printf("    标准信号 SIGUSR2 连发 3 次,非阻塞清点取到 %d 次(只记 1 次)\n", n);
    }

    // ---- d) 与 signalfd 互吃:同一队列两个消费者 ------------------------------------
    std::printf("\n[d] sigwaitinfo 与 signalfd 抢的是同一条 pending 队列:\n");
    sigset_t one;
    block_these(&one, {SIGRTMIN + 2});
    int sfd = signalfd(-1, &one, SFD_NONBLOCK | SFD_CLOEXEC);

    std::printf("  d1) 先 sigwaitinfo 一条,再 read(signalfd):\n");
    for (int v : {21, 22}) {
        union sigval sv{};
        sv.sival_int = v;
        sigqueue(getpid(), SIGRTMIN + 2, sv);
    }
    {
        siginfo_t si{};
        sigwaitinfo(&one, &si);
        std::printf("      sigwaitinfo 取到 si_value=%d\n", si.si_value.sival_int);
        signalfd_siginfo got{};
        ssize_t n = read(sfd, &got, sizeof got);
        std::printf("      read(signalfd) 返回 %ld 条,ssi_int=%d —— 剩下那条也在这儿\n",
                    (long)(n / (ssize_t)sizeof got), (int)got.ssi_int);
    }

    std::printf("  d2) 反过来,先 read(signalfd) 一条,再 sigtimedwait:\n");
    for (int v : {31, 32}) {
        union sigval sv{};
        sv.sival_int = v;
        sigqueue(getpid(), SIGRTMIN + 2, sv);
    }
    {
        signalfd_siginfo got{};
        ssize_t n = read(sfd, &got, sizeof got);
        std::printf("      read(signalfd) 返回 %ld 条,ssi_int=%d\n",
                    (long)(n / (ssize_t)sizeof got), (int)got.ssi_int);
        siginfo_t si{};
        sigtimedwait(&one, &si, nullptr);
        std::printf("      sigtimedwait 取到 si_value=%d —— 队列不重复,谁读谁消费\n",
                    si.si_value.sival_int);
    }
    close(sfd);

    std::printf("\n[e] 信号处理三条出路:handler 异步抢跑(上篇)/ sigwaitinfo 定点同步取(本篇)/\n"
                "    signalfd 事件循环里当 fd 用(本篇 E2)—— 同一阻塞集合,三种消费姿势\n");
    std::printf("\nE5 done,t=%ldms\n", ms());
    return 0;
}
