// E2: setitimer 三兄弟 —— ITIMER_REAL 数墙钟,ITIMER_VIRTUAL 只数本进程用户态,ITIMER_PROF
// 数用户+系统 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra, 计时 CLOCK_MONOTONIC
// 负载: 2.0s 里交替「忙转 10ms + 睡 10ms」→ 约一半墙钟在烧 CPU,一半在睡
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

std::atomic<int> g_hits{0};
std::atomic<unsigned long long> g_sink{0};

void on_sig(int) {
    g_hits.store(g_hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

void workload_ms(double seconds) {
    // 交替: 忙 10ms → 睡 10ms
    uint64_t t0 = mono_ns();
    for (;;) {
        uint64_t b = mono_ns();
        while (mono_ns() - b < 10000000ull)
            g_sink.fetch_add(1, std::memory_order_relaxed);
        if (mono_ns() - t0 >= uint64_t(seconds * 1e9))
            break;
        usleep(10000);
        if (mono_ns() - t0 >= uint64_t(seconds * 1e9))
            break;
    }
}

double rusage_cpu_ms() {
    struct rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0 +
           (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
}

void run_one(int which, const char* name, int signo) {
    struct sigaction sa{};
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sigaction(signo, &sa, nullptr);
    g_hits.store(0);

    struct itimerval it{};
    it.it_value.tv_usec = 10000;    // 首档 10ms
    it.it_interval.tv_usec = 10000; // 之后每 10ms
    setitimer(which, &it, nullptr);

    double ru0 = rusage_cpu_ms();
    uint64_t t0 = mono_ns();
    workload_ms(2.0);
    uint64_t wall_us = (mono_ns() - t0) / 1000;

    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0; // 撤销
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(which, &it, nullptr);
    double ru1 = rusage_cpu_ms();
    std::printf("%-14s 墙钟 %.0f ms,CPU %.0f ms,信号 %d 次 → 每 10ms 一档,数的是%s\n", name,
                wall_us / 1000.0, ru1 - ru0, g_hits.load(),
                which == ITIMER_REAL      ? "墙钟(睡也数)"
                : which == ITIMER_VIRTUAL ? "本进程用户态(睡不数,系统态也不数)"
                                          : "用户态+系统态(睡不数)");
}

} // namespace

int main() {
    std::printf("== E2a: 同一负载,三种计时口径各自的计数 ==\n");
    run_one(ITIMER_REAL, "ITIMER_REAL", SIGALRM);
    run_one(ITIMER_VIRTUAL, "ITIMER_VIRTUAL", SIGVTALRM);
    run_one(ITIMER_PROF, "ITIMER_PROF", SIGPROF);

    std::printf("\n== E2b: 一次性(it_interval=0)与撤销 ==\n");
    g_hits.store(0);
    struct sigaction sa{};
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);
    struct itimerval it{};
    it.it_value.tv_usec = 50000; // 只设 it_value,不设 interval
    setitimer(ITIMER_REAL, &it, nullptr);
    usleep(150000);
    std::printf("一次性 50ms 后睡 150ms: 信号 %d 次(1=响一次自停,不自动续期)\n", g_hits.load());

    g_hits.store(0);
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 300000;
    setitimer(ITIMER_REAL, &it, nullptr);
    setitimer(ITIMER_REAL, nullptr, nullptr); // 立刻撤销:Linux 允许 old_value 为 NULL,值清零即撤销
    usleep(400000);
    std::printf("设 300ms 后立刻撤销,再睡 400ms: 信号 %d 次(0=撤销生效)\n", g_hits.load());

    std::printf("\n== E2c: 归属与上限 ==\n");
    std::printf("setitimer 每进程每种 which 各一笔:最多三个(REAL/VIRTUAL/PROF),粒度到微秒(内核按\n"
                "时钟源能力取整)。PROF 的差值(用户+系统 − 纯用户)常被性能剖析器拿来估系统态占比,\n"
                "gprof 采样的就是这两个信号。\n");
    return 0;
}
