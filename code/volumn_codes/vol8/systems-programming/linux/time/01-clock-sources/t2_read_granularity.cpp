// E2: 分辨率不等于精度 —— getres 报 1ns,背靠背连读的间隔分布才是真读取粒度
// 口径: 同 E1;统计 1,000,000 次背靠背读的相邻间隔
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {

constexpr int kN = 1000000;

// 对一个 clockid 连读 kN 次,统计相邻差值
void granularity(clockid_t id, const char* name) {
    static timespec buf[kN];
    for (int i = 0; i < kN; ++i)
        clock_gettime(id, &buf[i]);

    // deltas
    static uint64_t d[kN - 1];
    int n = kN - 1;
    uint64_t zeros = 0;
    for (int i = 0; i < n; ++i) {
        uint64_t a = uint64_t(buf[i + 1].tv_sec) * 1000000000ull + uint64_t(buf[i + 1].tv_nsec);
        uint64_t b = uint64_t(buf[i].tv_sec) * 1000000000ull + uint64_t(buf[i].tv_nsec);
        d[i] = a - b;
        if (d[i] == 0)
            ++zeros;
    }
    std::qsort(d, n, sizeof(uint64_t), [](const void* x, const void* y) {
        uint64_t a = *(const uint64_t*)x, b2 = *(const uint64_t*)y;
        return a < b2 ? -1 : (a > b2 ? 1 : 0);
    });
    auto pick = [&](double q) { return d[(size_t)(q * (n - 1))]; };
    std::printf("%-22s 零差=%5.1f%%  最小非零=%llu ns  中位=%llu ns  p99=%llu ns  max=%llu ns\n",
                name, 100.0 * zeros / n, (unsigned long long)(zeros == (uint64_t)n ? 0 : d[zeros]),
                (unsigned long long)pick(0.5), (unsigned long long)pick(0.99),
                (unsigned long long)d[n - 1]);
}

// COARSE 的值是台阶式的: 数一数 kN 次读里出现了几个不同的值
void coarse_steps(clockid_t id, const char* name) {
    static timespec buf[kN];
    for (int i = 0; i < kN; ++i)
        clock_gettime(id, &buf[i]);
    int distinct = 1;
    for (int i = 1; i < kN; ++i)
        if (buf[i].tv_sec != buf[i - 1].tv_sec || buf[i].tv_nsec != buf[i - 1].tv_nsec)
            ++distinct;
    // 台阶之间的间隔
    uint64_t first_step = 0, last_step = 0;
    int steps_seen = 0;
    for (int i = 1; i < kN; ++i) {
        if (buf[i].tv_sec != buf[i - 1].tv_sec || buf[i].tv_nsec != buf[i - 1].tv_nsec) {
            uint64_t a = uint64_t(buf[i].tv_sec) * 1000000000ull + uint64_t(buf[i].tv_nsec);
            if (steps_seen == 0)
                first_step = a;
            last_step = a;
            ++steps_seen;
        }
    }
    uint64_t span_ms = 0;
    if (steps_seen >= 2)
        span_ms = (last_step - first_step) / 1000000ull;
    std::printf("%-22s %d 次读里只有 %d 个不同值;相邻台阶平均间隔 %llu ms(getres 见 E1)\n", name,
                kN, distinct,
                (unsigned long long)(steps_seen >= 2 ? span_ms / (steps_seen - 1) : 0));
}

uint64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

// 每次读取的代价(纳秒/次)
void cost(clockid_t id, const char* name) {
    timespec ts{};
    auto t0 = now_ns();
    for (int i = 0; i < kN; ++i)
        clock_gettime(id, &ts);
    auto t1 = now_ns();
    std::printf("%-22s %llu 次读总耗 %llu ns → %.1f ns/次\n", name, (unsigned long long)kN,
                (unsigned long long)(t1 - t0), double(t1 - t0) / kN);
}

} // namespace

int main() {
    std::printf("== E2a: 背靠背连读的相邻间隔分布(每次读 kN=%d) ==\n", kN);
    std::printf("getres 都报 1ns(E1),下面的分布才是真读取粒度:\n");
    granularity(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
    granularity(CLOCK_REALTIME, "CLOCK_REALTIME");
    granularity(CLOCK_MONOTONIC_RAW, "CLOCK_MONOTONIC_RAW");
    granularity(CLOCK_THREAD_CPUTIME_ID, "CLOCK_THREAD_CPUTIME_ID");

    std::printf("\n== E2b: COARSE 的值是台阶 ==\n");
    coarse_steps(CLOCK_MONOTONIC_COARSE, "CLOCK_MONOTONIC_COARSE");

    std::printf("\n== E2c: 每次读取的代价(整段计时/kN) ==\n");
    cost(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
    cost(CLOCK_MONOTONIC_RAW, "CLOCK_MONOTONIC_RAW");
    cost(CLOCK_MONOTONIC_COARSE, "CLOCK_MONOTONIC_COARSE");
    return 0;
}
