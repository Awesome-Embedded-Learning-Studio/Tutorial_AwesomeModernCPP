// E4: 错过的到期不排队 —— 信号形态的定时器把过期档合并,timer_getoverrun 报漏了几档
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra, 计时 CLOCK_MONOTONIC
// 对照: timerfd 是把错过档数写进 read 的返回值(ch04 L02 的 E3 讲过,本篇不重测)
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

} // namespace

int main() {
    timer_t tid{};
    sigevent sev{};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);

    itimerspec its{};
    its.it_value.tv_nsec = 10000000;    // 10ms 起步
    its.it_interval.tv_nsec = 10000000; // 每 10ms 一档

    // 先把 SIGUSR1 屏蔽,让第一次到期悬着(pending),后面的到期全部变 overrun
    sigset_t block{}, old{};
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &block, &old);

    uint64_t t0 = mono_ns();
    timer_settime(tid, 0, &its, nullptr);

    usleep(58000); // 睡 58ms:理论上 10/20/30/40/50ms 共 5 档到期

    siginfo_t si{};
    sigwaitinfo(&block, &si); // 收走悬着的那一次
    uint64_t t1 = mono_ns();
    int over = timer_getoverrun(tid);
    double slept = double(t1 - t0) / 1e6;

    std::printf("10ms 一档,屏蔽信号睡 %.1f ms 后取走信号:\n", slept);
    std::printf("  sigwaitinfo 收到 1 次(si_code=%s) —— 5 档到期只送来 1 个信号\n",
                si.si_code == SI_TIMER ? "SI_TIMER" : "别的");
    std::printf("  timer_getoverrun = %d(这一信号到货时,被合并掉的多余档数)\n", over);
    std::printf("  核对: 已到期的档数 %d = 送出的 1 + 合并掉的 %d\n",
                (int)(slept / 10.0) /* 向下取整:未满一档的不算 */, over);

    timer_delete(tid);

    std::printf("\n语义: 信号是有限资源,内核不排队补送,「你迟到了多久」折成一个整数交给你;\n"
                "要一档不漏地补,选 timerfd(read 返回值就是累计档数)或 SIGEV_NONE 自查。\n");
    return 0;
}
