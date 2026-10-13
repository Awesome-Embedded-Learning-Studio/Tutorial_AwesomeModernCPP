// E6: 周期任务调度器的漂移对照 —— 相对睡眠循环 vs 绝对到期补偿 vs timerfd 周期档
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra, 计时 CLOCK_MONOTONIC
// 任务标称 1ms 一档: 三种写法各跑 1000 档,量总时长与每档误差
// (timerfd 本体的到点精度 ch04 L02 E4 已测过 1ms 档中位 999.3µs,本篇量的是「漂移怎么累积」)
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

struct Stats {
    double total_ms;
    double median_us, max_us, min_us;
    double drift_ms; // 总时长 − 标称
};

// 每档误差分布: 记录每档实际间隔,顺带尾部总量
Stats finish(uint64_t t0, uint64_t t_last, double* intervals, int n, double nominal_ms) {
    std::qsort(intervals, n, sizeof(double), [](const void* a, const void* b) {
        double x = *(const double*)a, y = *(const double*)b;
        return x < y ? -1 : (x > y ? 1 : 0);
    });
    Stats s{};
    s.total_ms = double(t_last - t0) / 1e6;
    s.median_us = intervals[n / 2] / 1000.0;
    s.max_us = intervals[n - 1] / 1000.0;
    s.min_us = intervals[0] / 1000.0;
    s.drift_ms = s.total_ms - nominal_ms;
    return s;
}

void report(const char* name, const Stats& s, int n) {
    std::printf("%-22s 总长 %8.2f ms(标称 %d ms,漂 %+7.3f ms)\n"
                "                      档间隔: min=%.1f 中位=%.1f max=%.1f µs\n",
                name, s.total_ms, n, s.drift_ms, s.min_us, s.median_us, s.max_us);
}

constexpr int kN = 1000;
constexpr uint64_t kPeriodNs = 1000000;

} // namespace

int main() {
    static double iv[kN];

    // ---- 写法 A: 相对睡眠(每档睡满 1ms 再干活) ----
    {
        uint64_t t0 = mono_ns(), prev = t0;
        for (int i = 0; i < kN; ++i) {
            timespec req{0, kPeriodNs};
            while (clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr) == EINTR)
                ;
            uint64_t now = mono_ns();
            iv[i] = double(now - prev);
            prev = now;
        }
        Stats s = finish(t0, prev, iv, kN, kN * kPeriodNs / 1000000.0);
        std::printf("== E6a: 相对睡眠循环(sleep 1ms × 1000) ==\n");
        report("A: 相对睡眠", s, kN);
        std::printf("  漂移 = 每档的「调度+唤醒+干活」开销一路加进下一档的起点,只增不减\n");
    }

    // ---- 写法 B: 绝对到期(next += period,sleep 到绝对刻度) ----
    {
        uint64_t t0 = mono_ns(), prev = t0;
        uint64_t next = t0;
        for (int i = 0; i < kN; ++i) {
            next += kPeriodNs;
            timespec req{};
            req.tv_sec = time_t(next / 1000000000ull);
            req.tv_nsec = long(next % 1000000000ull);
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, nullptr) == EINTR)
                ;
            uint64_t now = mono_ns();
            iv[i] = double(now - prev);
            prev = now;
        }
        Stats s = finish(t0, prev, iv, kN, kN * kPeriodNs / 1000000.0);
        std::printf("\n== E6b: 绝对到期补偿(下一档 = 起点 + n×1ms) ==\n");
        report("B: 绝对到期", s, kN);
        std::printf("  晚醒的那几 µs 不再滚进下一档,下一档仍按整格刻度到点,偶尔还能补拍回来\n");
    }

    // ---- 写法 C: timerfd 周期档(poll 等可读) ----
    {
        int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
        itimerspec t{};
        t.it_value.tv_nsec = kPeriodNs;
        t.it_interval.tv_nsec = kPeriodNs;
        uint64_t t0 = mono_ns();
        timerfd_settime(tfd, 0, &t, nullptr);
        uint64_t prev = t0;
        uint64_t total_ticks = 0;
        for (int i = 0; i < kN; ++i) {
            pollfd p{tfd, POLLIN, 0};
            while (poll(&p, 1, -1) == -1 && errno == EINTR)
                ;
            uint64_t ticks = 0;
            read(tfd, &ticks, sizeof ticks);
            total_ticks += ticks;
            uint64_t now = mono_ns();
            iv[i] = double(now - prev);
            prev = now;
        }
        close(tfd);
        Stats s = finish(t0, prev, iv, kN, kN * kPeriodNs / 1000000.0);
        std::printf("\n== E6c: timerfd 周期档(1ms interval × 1000) ==\n");
        report("C: timerfd", s, kN);
        std::printf("  内核按绝对刻度摆档、错过的档折数补报(本次累计 ticks=%llu,理论 1000,\n"
                    "  多出的部分是读间隔内合并的补拍)\n",
                    (unsigned long long)total_ticks);
    }

    std::printf("\n== E6d: 怎么解读 ==\n");
    std::printf("  相对睡眠把每档的开销滚进下一档,1000 档下来漂移是「每档开销 × 1000」;\n");
    std::printf("  绝对到期和 timerfd 都锚在绝对刻度上,漂移只剩单档的唤醒抖动,不随档数增长;\n");
    std::printf("  周期任务的正确写法从来不是 sleep(period) 循环 —— 是把「第 n 档该在 n×period\n");
    std::printf("  时刻到点」交给单调钟或内核去守。\n");
    return 0;
}
