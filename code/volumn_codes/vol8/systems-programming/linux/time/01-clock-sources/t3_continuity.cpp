// E3: 连续性 —— MONOTONIC 单调不回退扫描;REALTIME 对 MONOTONIC 的偏移监测;RAW 的分歧率
// 口径: 同 E1;本机无 root,settimeofday 步进做不了,改为被动监测 + adjtimex 只读佐证
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {

uint64_t ns(const timespec& ts) {
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}
int64_t sub(const timespec& a, const timespec& b) {
    return int64_t(ns(a)) - int64_t(ns(b)); // 都是纳秒量级且同号,int64 装得下
}

} // namespace

int main() {
    // 3a: 200 万次背靠背读,MONOTONIC 是否出现过一次回退
    constexpr int kScan = 2000000;
    timespec prev{}, cur{};
    clock_gettime(CLOCK_MONOTONIC, &prev);
    long long backwards = 0;
    for (int i = 0; i < kScan; ++i) {
        clock_gettime(CLOCK_MONOTONIC, &cur);
        if (ns(cur) < ns(prev))
            ++backwards;
        prev = cur;
    }
    std::printf("== E3a: MONOTONIC 回退扫描 ==\n");
    std::printf("%d 次连读,回退次数=%lld(settimeofday 步进不进 MONOTONIC,这正是它当计时钟的资格)\n",
                kScan, backwards);

    // 3b: 10 秒窗口,1kHz 采样 REALTIME/MONOTONIC/RAW 的两两偏移
    std::printf("\n== E3b: 10 s 窗口偏移监测(1 kHz 采样) ==\n");
    constexpr int kSamples = 10000;
    double rt_off_min = 1e300, rt_off_max = -1e300;
    double raw_off_first = 0, raw_off_last = 0;
    double first_ms = 0, last_ms = 0;
    int rt_jumps = 0;
    double prev_rt_off = 0;
    for (int i = 0; i < kSamples; ++i) {
        if (i)
            usleep(900); // 约 1ms 一采(900us 睡 + 读取开销)
        timespec rt{}, mono{}, raw{};
        clock_gettime(CLOCK_REALTIME, &rt);
        clock_gettime(CLOCK_MONOTONIC, &mono);
        clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
        double rt_off = double(sub(rt, mono));
        double raw_off = double(sub(raw, mono));
        double mono_ms = double(ns(mono)) / 1e6;
        if (i == 0) {
            first_ms = mono_ms;
            raw_off_first = raw_off;
            prev_rt_off = rt_off;
        }
        last_ms = mono_ms;
        raw_off_last = raw_off;
        if (rt_off < rt_off_min)
            rt_off_min = rt_off;
        if (rt_off > rt_off_max)
            rt_off_max = rt_off;
        if (i) {
            double step = rt_off - prev_rt_off;
            if (step > 100000.0 || step < -100000.0)
                ++rt_jumps; // 偏移瞬移超过 ±100us 记一次
            prev_rt_off = rt_off;
        }
    }
    std::printf("REALTIME-MONOTONIC 偏移: min=%.1f us  max=%.1f us  峰谷差=%.1f us\n",
                rt_off_min / 1000.0, rt_off_max / 1000.0, (rt_off_max - rt_off_min) / 1000.0);
    std::printf("偏移瞬移(单步变化超 ±100us)次数=%d —— 0 次=窗口内没有发生墙上时间步进\n",
                rt_jumps);
    double window = last_ms - first_ms;
    double raw_drift = raw_off_last - raw_off_first;
    std::printf("窗口实长 %.1f ms;RAW-MONOTONIC 从 %.3f ms 走到 %.3f ms,变化 %.1f us\n"
                "  → 当下分歧率 = %.3f ppm(RAW 不受 NTP 调频,分歧率就是内核频率修正的力度)\n",
                window, raw_off_first / 1e6, raw_off_last / 1e6, raw_drift / 1000.0,
                raw_drift / (window * 1000.0));

    // 3c: 开机以来的累计分歧,与 E1 的读数对表
    timespec raw{}, mono{};
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
    std::printf("\n== E3c: 开机以来的累计分歧 ==\n");
    std::printf("MONOTONIC=%.1f s,RAW-MONOTONIC=%.3f ms → 平均分歧率 %.3f ppm\n",
                double(ns(mono)) / 1e9, double(sub(raw, mono)) / 1e6,
                double(sub(raw, mono)) / (double(ns(mono)) / 1000.0));
    return 0;
}
