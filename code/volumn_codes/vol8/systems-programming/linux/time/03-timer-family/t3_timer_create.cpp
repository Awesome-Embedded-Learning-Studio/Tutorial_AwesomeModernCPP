// E3: timer_create 的四种通知形态 —— SIGEV_SIGNAL 带载荷 / SIGEV_NONE 只倒计时 / SIGEV_THREAD 回调
// / SIGEV_THREAD_ID 定向 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra -pthread,
// 计时 CLOCK_MONOTONIC
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace {

std::atomic<int> g_sig_hits{0};
std::atomic<int> g_cb_hits{0};
std::atomic<int> g_tid_hits{0};
std::atomic<pid_t> target_tid{0}; // E3d 用:非零时,信号落在该线程就计入定向计数

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

void on_siginfo(int, siginfo_t* si, void*) {
    // SIGEV_SIGNAL 走 SA_SIGINFO:si_code=SI_TIMER,si_value 带着出发时塞的载荷
    pid_t self = (pid_t)syscall(SYS_gettid);
    if (target_tid.load() != 0 && self == target_tid.load()) {
        g_tid_hits.store(g_tid_hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        std::printf("  SIGEV_THREAD_ID 到货: 目标线程自己处理 tid %ld\n", (long)self);
    } else {
        g_sig_hits.store(g_sig_hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        std::printf("  SIGEV_SIGNAL 到货: si_code=%s si_value.sival_int=%d\n",
                    si->si_code == SI_TIMER ? "SI_TIMER" : "别的", si->si_value.sival_int);
    }
}

void on_thread_cb(union sigval sv) {
    g_cb_hits.store(g_cb_hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    std::printf("  SIGEV_THREAD 回调: 载荷=%d,跑在 tid %ld\n", sv.sival_int,
                (long)syscall(SYS_gettid));
}

} // namespace

int main() {
    // --- 形态一: SIGEV_SIGNAL,信号带载荷 ---
    std::printf("== E3a: SIGEV_SIGNAL(100ms × 3,载荷 20261004) ==\n");
    struct sigaction sa{};
    sa.sa_sigaction = on_siginfo;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, nullptr);

    timer_t tid{};
    sigevent sev{};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    sev.sigev_value.sival_int = 20261004;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    itimerspec its{};
    its.it_value.tv_nsec = 100000000;
    its.it_interval.tv_nsec = 100000000;
    timer_settime(tid, 0, &its, nullptr);
    while (g_sig_hits.load() < 3)
        usleep(20000);
    timer_delete(tid);

    // --- 形态二: SIGEV_NONE,只倒计时不通知,自己拿 timer_gettime 问 ---
    std::printf("\n== E3b: SIGEV_NONE(300ms 一次性,每 50ms 问一次剩多少) ==\n");
    sev = {};
    sev.sigev_notify = SIGEV_NONE;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    its = {};
    its.it_value.tv_nsec = 300000000;
    timer_settime(tid, 0, &its, nullptr);
    uint64_t t0 = mono_ns();
    for (int i = 0; i < 7; ++i) {
        usleep(50000);
        timer_gettime(tid, &its);
        double elapsed = double(mono_ns() - t0) / 1e6;
        std::printf("  墙钟 %5.1f ms: it_value 剩 %lld.%03lld ms\n", elapsed,
                    (long long)its.it_value.tv_sec, (long long)its.it_value.tv_nsec / 1000000);
    }
    timer_delete(tid);
    std::printf("  到期后 it_value 归零、无任何信号 —— 到没到点,全靠自己看表\n");

    // --- 形态三: SIGEV_THREAD,glibc 派一个辅助线程跑回调 ---
    std::printf("\n== E3c: SIGEV_THREAD(120ms × 2) ==\n");
    sev = {};
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = on_thread_cb;
    sev.sigev_value.sival_int = 77;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    its = {};
    its.it_value.tv_nsec = 120000000;
    its.it_interval.tv_nsec = 120000000;
    timer_settime(tid, 0, &its, nullptr);
    while (g_cb_hits.load() < 2)
        usleep(20000);
    timer_delete(tid);

    // --- 形态四: SIGEV_THREAD_ID,只投给指定线程 ---
    std::printf("\n== E3d: SIGEV_THREAD_ID(定向投递,150ms × 2) ==\n");
    std::thread worker([&] {
        target_tid.store((pid_t)syscall(SYS_gettid));
        while (g_tid_hits.load() < 2)
            usleep(20000);
    });
    while (target_tid.load() == 0)
        usleep(1000);

    sev = {};
    sev.sigev_notify = SIGEV_THREAD_ID; // Linux 专有;sigev_notify_thread_id 宏需要 _GNU_SOURCE
    sev.sigev_signo = SIGUSR1;
    sev._sigev_un._tid = target_tid.load();
    // 主线程把 SIGUSR1 屏蔽,证明信号只进目标线程
    sigset_t block{};
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &block, nullptr);

    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    its = {};
    its.it_value.tv_nsec = 150000000;
    its.it_interval.tv_nsec = 150000000;
    timer_settime(tid, 0, &its, nullptr);
    while (g_tid_hits.load() < 2)
        usleep(20000);
    timer_delete(tid);
    worker.join();
    std::printf("  两次都由目标线程处理;主线程全程屏蔽同一个信号、一次没被吵醒 —— 定向投递成立\n"
                "  (计数核对: 主线程侧 g_sig_hits=%d 是 E3a 的 3 次,工作线程侧 g_tid_hits=%d)\n",
                g_sig_hits.load(), g_tid_hits.load());
    return 0;
}
