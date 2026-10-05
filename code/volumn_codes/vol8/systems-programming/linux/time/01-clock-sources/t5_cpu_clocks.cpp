// E5: 进程与线程的 CPU 时钟 —— 谁 burn CPU 谁长个儿,顺带对表 tick 口径的 /proc 与 getrusage
// 口径: 同 E1;-pthread
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

namespace {

uint64_t ns(clockid_t id) {
    timespec ts{};
    clock_gettime(id, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

std::atomic<unsigned long long> g_sink{0};

} // namespace

int main() {
    std::printf("== E5: CPU 时间记在谁的时钟上 ==\n");
    std::printf("安排: 主线程睡 300ms,工作线程忙转 300ms;进程 CPU 应≈工作线程的量\n\n");

    // /proc/self/stat 的第 14/15 字段是 utime/stime,单位 tick(1/CLK_TCK 秒)
    auto proc_ticks = [] {
        FILE* f = std::fopen("/proc/self/stat", "r");
        if (!f)
            return -1.0;
        char buf[4096];
        size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        std::fclose(f);
        char* p = std::strrchr(buf, ')'); // 跳过 comm 字段(里面可能带空格)
        unsigned long ut = 0, st = 0;
        // p+2 起是第 3 字段 state;utime/stime 是第 14/15,前面要跳过 11 个
        if (p)
            std::sscanf(p + 2, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu", &ut, &st);
        return double(ut + st) / sysconf(_SC_CLK_TCK);
    };

    double stat_before = proc_ticks();
    struct rusage ru_before{};
    getrusage(RUSAGE_SELF, &ru_before);
    uint64_t proc0 = ns(CLOCK_PROCESS_CPUTIME_ID);
    uint64_t main0 = ns(CLOCK_THREAD_CPUTIME_ID);

    std::atomic<bool> stop{false};
    std::thread worker([&] {
        while (!stop.load(std::memory_order_relaxed))
            g_sink.fetch_add(1, std::memory_order_relaxed); // 纯用户态忙转
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    uint64_t main1 = ns(CLOCK_THREAD_CPUTIME_ID);
    stop.store(true);
    worker.join();

    uint64_t proc1 = ns(CLOCK_PROCESS_CPUTIME_ID);
    double stat_after = proc_ticks();
    struct rusage ru_after{};
    getrusage(RUSAGE_SELF, &ru_after);

    double ru_cpu = double(ru_after.ru_utime.tv_sec - ru_before.ru_utime.tv_sec) +
                    double(ru_after.ru_stime.tv_sec - ru_before.ru_stime.tv_sec) +
                    (double(ru_after.ru_utime.tv_usec - ru_before.ru_utime.tv_usec) +
                     double(ru_after.ru_stime.tv_usec - ru_before.ru_stime.tv_usec)) /
                        1e6;

    std::printf("主线程睡满 300ms,它自己的 CPU 时钟只长了 %llu ns(睡觉不增 CPU 时间)\n",
                (unsigned long long)(main1 - main0));
    std::printf("工作线程纯忙转,进程 CPU 时钟长了 %llu ns ≈ %.1f ms\n",
                (unsigned long long)(proc1 - proc0), double(proc1 - proc0) / 1e6);
    std::printf("对表: getrusage(RUSAGE_SELF) 记 %.1f ms;/proc/self/stat(tick 口径)记 %.0f ms(1 "
                "tick=1/%ld s)\n",
                ru_cpu * 1000.0, (stat_after - stat_before) * 1000.0, sysconf(_SC_CLK_TCK));

    // 主线程再单独忙转一段,看线程钟与进程钟的差
    uint64_t p0 = ns(CLOCK_PROCESS_CPUTIME_ID), m0 = ns(CLOCK_THREAD_CPUTIME_ID);
    for (int i = 0; i < 50000000; ++i)
        g_sink.fetch_add(1, std::memory_order_relaxed);
    uint64_t p1 = ns(CLOCK_PROCESS_CPUTIME_ID), m1 = ns(CLOCK_THREAD_CPUTIME_ID);
    std::printf("\n主线程也忙转一段: 线程钟 +%.1f ms,进程钟 +%.1f ms(此时两者同源同速)\n",
                double(m1 - m0) / 1e6, double(p1 - p0) / 1e6);
    std::printf(
        "用途对表: 限制 CPU 配额(CPULIMIT 类工具)、线程级性能剖析,读的就是这两个 clockid\n");
    return 0;
}
