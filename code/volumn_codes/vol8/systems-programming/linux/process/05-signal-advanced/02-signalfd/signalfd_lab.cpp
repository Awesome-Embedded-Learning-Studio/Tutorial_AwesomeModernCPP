// E2 signalfd:把信号变成一个可读的 fd(《信号(下):实时信号、signalfd 与 pidfd》E2)
//
// 六组观察:
//   a) 纪律「先阻塞再创建」:不阻塞的信号走传统路(handler/默认动作),signalfd 永远
//      看不到它——装一个计数 handler 当「传统路」的探针,信号发出后 handler 计数=1、
//      poll(signalfd) 200ms 超时 0 事件:信号漏掉了
//   b) 互斥:同一信号,一旦从 signalfd 读走,传统 handler 就不会再触发(读=消费);
//      反过来 handler 先消费了,signalfd 也读不到——两条出路抢的是同一个 pending 队列
//   c) 批量读+字段:read 一次带回多条 signalfd_siginfo(实时信号排队才有多的);
//      ssi_signo/ssi_pid/ssi_uid/ssi_code/ssi_int 解读——kill 走 SI_USER(0)、
//      sigqueue 走 SI_QUEUE(-1)、跨进程发送 ssi_pid 是发送方 pid;跨号按小号出队
//   d) SFD_NONBLOCK 进 poll:signalfd 与 timerfd 挂同一个事件循环,信号与定时器
//      统一调度(self-pipe trick 的内核原生版,对照上篇 E5)
//   e) 动态加信号:对已有 sfd 再调一次 signalfd(2) 改掩码(改掩码的 flags 动不了
//      FD_CLOEXEC 位,创建时定性,详见 205 行注)
//   f) 补一刀:signalfd 的出队顺序也是小号优先(和 E1 d3 的 sigwaitinfo 一致)
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 signalfd_lab.cpp -o signalfd_lab && ./signalfd_lab
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
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

volatile sig_atomic_t g_handler_count = 0;
void on_usr2(int, siginfo_t*, void*) {
    g_handler_count = g_handler_count + 1;
}

void install(int sig) {
    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = on_usr2;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

void block_these(sigset_t* set, std::initializer_list<int> sigs) {
    sigemptyset(set);
    for (int s : sigs)
        sigaddset(set, s);
    sigprocmask(SIG_BLOCK, set, nullptr);
}

const char* si_code_name(int code) {
    // kill/sigqueue 两个来源码的数值:SI_USER=0, SI_QUEUE=-1(asm-generic/siginfo.h)
    return code == SI_USER    ? "SI_USER(0,来自 kill)"
           : code == SI_QUEUE ? "SI_QUEUE(-1,来自 sigqueue)"
                              : "其他";
}

// 从 sfd 尽量读(循环到 EAGAIN),打印每条 signalfd_siginfo 的关键位
void drain_sfd(int sfd, const char* tag) {
    signalfd_siginfo buf[8];
    for (;;) {
        ssize_t n = ::read(sfd, buf, sizeof buf);
        if (n <= 0) {
            if (errno == EAGAIN)
                break; // SFD_NONBLOCK:读干净了
            std::printf("      read 失败:%s\n", std::strerror(errno));
            break;
        }
        int cnt = (int)(n / sizeof(signalfd_siginfo));
        std::printf("      %s:一次 read 返回 %ld 字节 = %d 条记录\n", tag, (long)n, cnt);
        for (int i = 0; i < cnt; ++i)
            std::printf("        [%d] ssi_signo=%d ssi_code=%s ssi_pid=%d ssi_uid=%d ssi_int=%d\n",
                        i + 1, buf[i].ssi_signo, si_code_name((int)buf[i].ssi_code),
                        (int)buf[i].ssi_pid, (unsigned)buf[i].ssi_uid, (int)buf[i].ssi_int);
    }
}

} // namespace

int main() {
    std::printf("== E2 signalfd:先阻塞,再创建,然后 read ==\n");

    // ---- a) 不阻塞的信号到不了 signalfd ------------------------------------------
    install(SIGUSR2); // 探针:传统路上的 handler(计数)
    sigset_t usr2;
    sigemptyset(&usr2);
    sigaddset(&usr2, SIGUSR2);
    int sfd = signalfd(-1, &usr2, SFD_NONBLOCK | SFD_CLOEXEC); // 故意不先阻塞
    kill(getpid(), SIGUSR2);                                   // 信号没阻塞 → 当场走传统路
    pollfd p{sfd, POLLIN, 0};
    int pr = ::poll(&p, 1, 200);
    std::printf("\n[a] 未阻塞就创建 signalfd,再发 1 个 SIGUSR2:\n");
    std::printf("    handler 计数=%d(信号被传统路消费了)\n", (int)g_handler_count);
    std::printf("    poll(signalfd, 200ms) 返回 %d,revents=%#x → signalfd 上什么都没有\n", pr,
                p.revents);
    ::close(sfd);

    // ---- b) 互斥:读走即消费,handler 永不触发 ------------------------------------
    install(SIGUSR2);
    g_handler_count = 0;
    sigprocmask(SIG_BLOCK, &usr2, nullptr); // 这次先阻塞
    sfd = signalfd(-1, &usr2, SFD_NONBLOCK | SFD_CLOEXEC);
    kill(getpid(), SIGUSR2);
    kill(getpid(), SIGUSR2);
    kill(getpid(), SIGUSR2); // 标准信号:pending 只记 1 次
    pollfd q{sfd, POLLIN, 0};
    pr = ::poll(&q, 1, 100);
    std::printf("\n[b] 先阻塞再创建,连发 3 个 SIGUSR2(标准信号只记 1 次):\n");
    std::printf("    poll 返回 %d,revents=%#x(POLLIN=%#x)\n", pr, q.revents, POLLIN);
    drain_sfd(sfd, "读 signalfd");
    sigprocmask(SIG_UNBLOCK, &usr2, nullptr); // 读走了,解阻塞也不会再投递
    std::printf("    解阻塞后 handler 计数仍=%d —— 从 signalfd 读走的信号不会再进 handler\n",
                (int)g_handler_count);

    // ---- c) 批量读+字段:子进程跨进程发送 ----------------------------------------
    std::printf("\n[c] 字段解读:fork 子进程发信号(跨进程,ssi_pid 有看头):\n");
    sigset_t mix;
    block_these(&mix, {SIGUSR2, SIGRTMIN + 2, SIGRTMIN + 3});
    int sfd2 = signalfd(-1, &mix, SFD_NONBLOCK | SFD_CLOEXEC);
    pid_t child = ::fork();
    if (child == 0) {
        // 子进程:kill 一个标准信号 + 按大号到小号乱序 sigqueue 两个实时信号
        kill(getppid(), SIGUSR2);
        union sigval sv{};
        sv.sival_int = 31;
        sigqueue(getppid(), SIGRTMIN + 3, sv); // 先发大号 36
        sv.sival_int = 22;
        sigqueue(getppid(), SIGRTMIN + 2, sv); // 后发小号 35
        _exit(0);
    }
    waitpid(child, nullptr, 0); // 子进程发完就退,pending 已入账
    drain_sfd(sfd2, "子进程发完后的 read");
    std::printf(
        "    (ssi_pid=%d 是发送方=子进程;36/37 乱序入队(先 37 后 36),出队按小号先——同 E1)\n",
        (int)child);

    // ---- d) SFD_NONBLOCK 进 poll:signalfd + timerfd 一个事件循环 ----------------
    std::printf("\n[d] signalfd+timerfd 混挂事件循环(时间戳相对循环启动):\n");
    // 子进程当「外部事件源」:200ms 时 sigqueue(SIGUSR2, val=7),400ms 时 val=8
    pid_t sender = ::fork();
    if (sender == 0) {
        timespec d{0, 200 * 1000000L};
        nanosleep(&d, nullptr);
        union sigval sv{};
        sv.sival_int = 7;
        sigqueue(getppid(), SIGUSR2, sv);
        d = {0, 200 * 1000000L};
        nanosleep(&d, nullptr);
        sv.sival_int = 8;
        sigqueue(getppid(), SIGUSR2, sv);
        _exit(0);
    }
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    itimerspec its{};
    its.it_interval = {0, 150 * 1000000L};
    its.it_value = its.it_interval;
    timerfd_settime(tfd, 0, &its, nullptr);

    int ticks = 0, sig_events = 0;
    long loop_base = ms(); // 时间戳改以循环启动为 0 点,交错关系一目了然
    for (;;) {
        pollfd pf[2]{pollfd{tfd, POLLIN, 0}, pollfd{sfd2, POLLIN, 0}};
        int r = ::poll(pf, 2, 700);
        if (r <= 0)
            break; // 超时/出错:窗口结束
        if (pf[0].revents & POLLIN) {
            uint64_t exp{};
            ::read(tfd, &exp, sizeof exp);
            ++ticks;
            std::printf("    t=%3ldms 事件循环:timerfd 第 %d 次到期\n", ms() - loop_base, ticks);
        }
        if (pf[1].revents & POLLIN) {
            signalfd_siginfo info[4];
            ssize_t n = ::read(sfd2, info, sizeof info);
            for (int i = 0; i < n / (ssize_t)sizeof(signalfd_siginfo); ++i) {
                ++sig_events;
                std::printf("    t=%3ldms 事件循环:signalfd 读到 signo=%d ssi_int=%d ssi_pid=%d\n",
                            ms() - loop_base, info[i].ssi_signo, (int)info[i].ssi_int,
                            (int)info[i].ssi_pid);
            }
        }
        if (ticks >= 4 && sig_events >= 2 && ms() - loop_base > 500)
            break; // 证据到齐
    }
    std::printf("    (同一循环里定时器与信号各自就位——self-pipe 的内核原生版)\n");
    int st = 0;
    waitpid(sender, &st, 0);
    ::close(tfd);

    // ---- e) 动态加信号:对已有 sfd 再调 signalfd(2) ------------------------------
    std::printf("\n[e] 动态扩掩码:sfd2 原来只挂 {SIGUSR2,SIGRTMIN+2,SIGRTMIN+3}(=12/36/37),现在加 "
                "SIGRTMIN+4(%d):\n",
                SIGRTMIN + 4);
    block_these(&mix, {SIGUSR2, SIGRTMIN + 2, SIGRTMIN + 3, SIGRTMIN + 4});
    // 同一个 fd 上改掩码。实测(fcntl 探针):改掩码的 flags 动不了 FD_CLOEXEC 位,创建时定性;
    // man 2 signalfd 对此无说,这里照旧带上 flags 只是把 SFD_NONBLOCK 维持住
    if (signalfd(sfd2, &mix, SFD_NONBLOCK | SFD_CLOEXEC) != sfd2)
        std::printf("    signalfd 改掩码失败:%s\n", std::strerror(errno));
    union sigval sv{};
    sv.sival_int = 44;
    sigqueue(getpid(), SIGRTMIN + 4, sv);
    drain_sfd(sfd2, "扩容后的 read");
    std::printf("    (老三样没来,新号 44 一发即中:掩码是活的,fd 不换)\n");
    ::close(sfd2);

    std::printf("\nE2 done,t=%ldms\n", ms());
    return 0;
}
