// E1 实时信号:范围、排队、带数据、到达顺序(《信号(下):实时信号、signalfd 与 pidfd》E1)
//
// 五组观察。handler 只做「记录进静态数组」一件事(不 printf,异步信号安全约束,
// 上篇 Lproc04 讲过);全部投递完毕回到主流程后统一打印。
//   a) 范围:libc 的 SIGRTMIN/SIGRTMAX 与内核 __SIGRTMIN——glibc(NPTL)内部占用
//      __SIGRTMIN 与 __SIGRTMIN+1 两个号,所以 SIGRTMIN = __SIGRTMIN+2;用户代码从
//      libc 的 SIGRTMIN 起用才安全
//   b) 排队:阻塞期连发 3 次 SIGRTMIN+1(sigqueue 带编号)→ 解阻塞后 handler 跑 3 次,
//      编号 FIFO 不丢;对照:标准信号 SIGUSR1 阻塞期连发 3 次 → handler 只跑 1 次
//   c) 带数据:sigqueue 的 sival_int 在 SA_SIGINFO handler 的 si_value 原样读回;
//      sival_ptr 传的是指针值,同进程内可解引用、跨进程只是个数字
//   d) 到达顺序,三个小口径(本机 6.18 实测,和 man signal(7) 的口径对账):
//      d1 空 sa_mask 一次放行一批 {SIGRTMAX, SIGRTMIN+1×3} → handler 执行序是
//         大号先跑(SIGRTMAX 先),三个同号仍 FIFO——这不是「内核挑大号」,
//         而是解阻塞瞬间多个信号帧按出队序(小号优先)叠栈、后叠的先执行;
//      d2 sa_mask=全屏蔽同批 {SIGUSR1,SIGUSR2,SIGRTMIN,SIGRTMIN+1} → 执行序严格
//         10→12→34→35 小号优先(handler 期间其他号都被挡住,只能逐个出队逐个跑,
//         这才是内核出队顺序的直接证据);
//      d3 同一批 pending 用 sigwaitinfo 消费 → 同样小号优先 10→12→34→35。
//      结论:选择顺序 POSIX 口径「小号优先」成立;空 sa_mask 批量放行时的「大号先跑」
//      是帧叠加的表现。跨号顺序别依赖 handler,要顺序消费用 sigwaitinfo/signalfd(E2)。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 rt_signal.cpp -o rt_signal && ./rt_signal
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <initializer_list>
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

// handler 的投递账本:写 plain 数组 + sig_atomic_t 计数(handler 上下文的可重入姿势)
struct event {
    int signo;
    int value; // si_value.sival_int
    void* ptr; // si_value.sival_ptr
    int code;  // si_code
    int seq;   // 进 handler 的次序
};
event g_events[16];
volatile sig_atomic_t g_count = 0;

void on_signal(int sig, siginfo_t* info, void*) {
    if (g_count < 16 && info != nullptr) {
        g_events[g_count] = {sig, info->si_value.sival_int, info->si_value.sival_ptr, info->si_code,
                             g_count + 1};
    }
    g_count = g_count + 1; // 超出容量也计数,事后对账
}

void install(int sig, bool block_all_in_handler = false) {
    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = on_signal;
    sigemptyset(&sa.sa_mask);
    if (block_all_in_handler)
        sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}

void queue_sig(int sig, int v) {
    union sigval sv{};
    sv.sival_int = v;
    if (sigqueue(getpid(), sig, sv) != 0)
        std::printf("    sigqueue(signo=%d) 失败:%s\n", sig, std::strerror(errno));
}

void drain_events(const char* title) {
    std::printf("    %s → handler 共跑 %d 次\n", title, (int)g_count);
    for (int i = 0; i < g_count && i < 16; ++i) {
        const event& e = g_events[i];
        std::printf("      第 %d 次:signo=%d si_code=%d sival_int=%d", e.seq, e.signo, e.code,
                    e.value);
        if (e.ptr != nullptr)
            std::printf(" sival_ptr=%p", e.ptr);
        std::printf("\n");
    }
    g_count = 0;
}

} // namespace

int main() {
    std::printf("== E1 实时信号:排队、带数据、到达顺序 ==\n");

    // ---- a) 范围 -----------------------------------------------------------------
    std::printf("\n[a] 范围:SIGRTMIN=%d SIGRTMAX=%d __SIGRTMIN=%d\n", SIGRTMIN, SIGRTMAX,
                __SIGRTMIN);
    std::printf("    libc 的 SIGRTMIN 比内核 __SIGRTMIN 大 %d:NPTL 内部占用 %d/%d 两号,\n",
                SIGRTMIN - __SIGRTMIN, __SIGRTMIN, __SIGRTMIN + 1);
    std::printf("    用户可用的实时信号共 %d 个(%d..%d)。本实验用 SIGRTMIN+1=%d 与 SIGRTMAX=%d\n",
                SIGRTMAX - SIGRTMIN + 1, SIGRTMIN, SIGRTMAX, SIGRTMIN + 1, SIGRTMAX);

    // ---- b) 排队:实时 3 次都在,标准信号 3 次只记 1 次 ---------------------------
    install(SIGRTMIN + 1);
    install(SIGUSR1);
    install(SIGRTMAX); // 后面 [d] 要往它发信号:不装 handler 就是默认动作(终止)

    std::printf("\n[b] 阻塞期连发 3 次,解阻塞后:\n");
    sigset_t rt, old;
    sigemptyset(&rt);
    sigaddset(&rt, SIGRTMIN + 1);
    sigprocmask(SIG_BLOCK, &rt, &old);
    queue_sig(SIGRTMIN + 1, 101);
    queue_sig(SIGRTMIN + 1, 102);
    queue_sig(SIGRTMIN + 1, 103);
    sigprocmask(SIG_SETMASK, &old, nullptr); // 解阻塞 → 3 次全部投递后才回到这里
    drain_events("SIGRTMIN+1(sigqueue×3,编号 101/102/103)");

    sigemptyset(&rt);
    sigaddset(&rt, SIGUSR1);
    sigprocmask(SIG_BLOCK, &rt, &old);
    queue_sig(SIGUSR1, 1);
    queue_sig(SIGUSR1, 2);
    queue_sig(SIGUSR1, 3);
    sigprocmask(SIG_SETMASK, &old, nullptr); // 解阻塞 → 只投递 1 次
    drain_events("SIGUSR1(kill 语义×3,编号 1/2/3)");

    // ---- c) 带数据:sival_int / sival_ptr ----------------------------------------
    std::printf("\n[c] 带数据(sigqueue → SA_SIGINFO 的 si_value 读回):\n");
    static int payload = 4242;
    sigemptyset(&rt);
    sigaddset(&rt, SIGRTMIN + 1);
    sigprocmask(SIG_BLOCK, &rt, &old);
    {
        union sigval sv{};
        sv.sival_ptr = &payload; // 同进程:传地址,handler 读回后可解引用
        sigqueue(getpid(), SIGRTMIN + 1, sv);
    }
    sigprocmask(SIG_SETMASK, &old, nullptr);
    drain_events("sival_ptr=&payload");
    if (g_events[0].ptr == &payload)
        std::printf("      si_value.sival_ptr 指回 &payload,解引用得 %d(同进程有效;\n"
                    "      若发送方在别的进程,这个值只是个地址数字,解引用无意义)\n",
                    payload);
    else
        std::printf("      sival_ptr 未对上(异常!)\n");

    // ---- d) 到达顺序:三个小口径 ---------------------------------------------------
    std::printf("\n[d] 到达顺序\n");
    std::printf("  d1) 空 sa_mask,一次解阻塞 {SIGRTMAX, SIGRTMIN+1×3}:\n");
    std::printf("      入队顺序:先 SIGRTMAX(=900),后 SIGRTMIN+1(=201/202/203)\n");
    sigemptyset(&rt);
    sigaddset(&rt, SIGRTMIN + 1);
    sigaddset(&rt, SIGRTMAX);
    sigprocmask(SIG_BLOCK, &rt, &old);
    queue_sig(SIGRTMAX, 900);
    queue_sig(SIGRTMIN + 1, 201);
    queue_sig(SIGRTMIN + 1, 202);
    queue_sig(SIGRTMIN + 1, 203);
    sigset_t pending;
    sigpending(&pending);
    std::printf("      解阻塞前 sigpending:SIGRTMIN+1=%s, SIGRTMAX=%s(两号都在账上)\n",
                sigismember(&pending, SIGRTMIN + 1) ? "pending" : "-",
                sigismember(&pending, SIGRTMAX) ? "pending" : "-");
    sigprocmask(SIG_SETMASK, &old, nullptr);
    drain_events("实际执行顺序");

    std::printf("  d2) sa_mask=全屏蔽(handler 期间其他号进不来),同批 "
                "{SIGUSR1,SIGUSR2,SIGRTMIN,SIGRTMIN+1}:\n");
    install(SIGUSR1, true);
    install(SIGUSR2, true);
    install(SIGRTMIN, true);
    install(SIGRTMIN + 1, true);
    sigemptyset(&rt);
    for (int s : {SIGUSR1, SIGUSR2, SIGRTMIN, SIGRTMIN + 1})
        sigaddset(&rt, s);
    sigprocmask(SIG_BLOCK, &rt, &old);
    // 故意乱序发:12,35,10,34
    queue_sig(SIGUSR2, 12);
    queue_sig(SIGRTMIN + 1, 35);
    queue_sig(SIGUSR1, 10);
    queue_sig(SIGRTMIN, 34);
    sigprocmask(SIG_SETMASK, &old, nullptr);
    drain_events("实际执行顺序");
    std::printf("      (入队 12→35→10→34,执行却是小号 10→12→34→35:出队顺序的直接证据)\n");

    std::printf("  d3) 同一批 pending,不走 handler,用 sigwaitinfo 逐个取:\n");
    sigprocmask(SIG_BLOCK, &rt, nullptr);
    queue_sig(SIGUSR2, 12);
    queue_sig(SIGRTMIN + 1, 35);
    queue_sig(SIGUSR1, 10);
    queue_sig(SIGRTMIN, 34);
    for (int i = 0; i < 4; ++i) {
        siginfo_t si{};
        int s = sigwaitinfo(&rt, &si);
        std::printf("      第 %d 次取到 signo=%d(带值 %d)\n", i + 1, s, si.si_value.sival_int);
    }
    std::printf("      (同样小号优先 10→12→34→35,和 man signal(7) 的口径一致)\n");

    std::printf("\nE1 done,t=%ldms\n", ms());
    return 0;
}
