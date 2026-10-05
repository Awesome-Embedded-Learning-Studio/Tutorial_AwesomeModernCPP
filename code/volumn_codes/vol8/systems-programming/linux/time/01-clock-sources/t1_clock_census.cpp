// E1: 时钟族普查 —— clock_getres 与首次读数,TAI/BOOTTIME 偏移与 COARSE 的档位
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra, 计时一律 CLOCK_MONOTONIC
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/timex.h>
#include <unistd.h>

namespace {

uint64_t ns(const timespec& ts) {
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

void one_line(clockid_t id, const char* name, const char* note) {
    timespec res{}, now{};
    if (clock_getres(id, &res) != 0) {
        std::printf("%-22s getres 失败: %s\n", name, std::strerror(errno));
        return;
    }
    clock_gettime(id, &now);
    std::printf("%-22s getres=%lld.%09lld  now=%lld.%09lld  %s\n", name, (long long)res.tv_sec,
                (long long)res.tv_nsec, (long long)now.tv_sec, (long long)now.tv_nsec, note);
}

} // namespace

int main() {
    std::printf("== E1: clock_getres 与时钟族首次读数 ==\n");
    std::printf("CLK_TCK(sysconf)=%ld  (COARSE 分辨率反推出的 tick 档位见下两行)\n",
                sysconf(_SC_CLK_TCK));

    one_line(CLOCK_REALTIME, "CLOCK_REALTIME", "墙上时间,可被 settimeofday/adjtime 改");
    one_line(CLOCK_REALTIME_COARSE, "CLOCK_REALTIME_COARSE", "墙上时间的粗读档,分辨率=1/HZ");
    one_line(CLOCK_MONOTONIC, "CLOCK_MONOTONIC", "单调钟,不受 settimeofday 步进影响");
    one_line(CLOCK_MONOTONIC_COARSE, "CLOCK_MONOTONIC_COARSE", "单调钟的粗读档,分辨率=1/HZ");
    one_line(CLOCK_MONOTONIC_RAW, "CLOCK_MONOTONIC_RAW", "原始硬件计数,不受 NTP 频率调整影响");
    one_line(CLOCK_BOOTTIME, "CLOCK_BOOTTIME", "单调钟,挂起期间也在走");
    one_line(CLOCK_TAI, "CLOCK_TAI", "国际原子时刻度,闰秒只往一个方向加");
    one_line(CLOCK_PROCESS_CPUTIME_ID, "CLOCK_PROCESS_CPUTIME_ID", "进程 CPU 时间(用户+系统)");
    one_line(CLOCK_THREAD_CPUTIME_ID, "CLOCK_THREAD_CPUTIME_ID", "线程 CPU 时间(用户+系统)");

    // 同一瞬间背靠背读三组差值: TAI-REALTIME / BOOTTIME-MONOTONIC / RAW-MONOTONIC
    timespec rt{}, tai{}, mono{}, boot{}, raw{};
    clock_gettime(CLOCK_REALTIME, &rt);
    clock_gettime(CLOCK_TAI, &tai);
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_BOOTTIME, &boot);
    clock_gettime(CLOCK_MONOTONIC_RAW, &raw);

    std::printf("\n== 同期差值(背靠背读,残差来自读取顺序的先后) ==\n");
    std::printf("TAI - REALTIME       = %lld ns (原子时与 UTC 的整秒差,即闰秒累计数)\n",
                (long long)(ns(tai) - ns(rt)));
    std::printf("BOOTTIME - MONOTONIC = %lld ns (开机以来挂起的累计时长)\n",
                (long long)(ns(boot) - ns(mono)));
    std::printf("RAW - MONOTONIC      = %lld ns (0 附近=本机没有 NTP 频率纪律在拉,见 E3)\n",
                (long long)(ns(raw) - ns(mono)));

    // /proc/uptime 是 BOOTTIME 口径(含挂起),拿它对表
    FILE* f = std::fopen("/proc/uptime", "r");
    if (f) {
        double up = 0, idle = 0;
        if (std::fscanf(f, "%lf %lf", &up, &idle) == 2) {
            std::printf("/proc/uptime         = %.3f s 对 BOOTTIME %.6f s,差 %.3f s\n"
                        "                       (挂起总时长;为 0 即本次开机以来没挂起过)\n",
                        up, double(boot.tv_sec) + double(boot.tv_nsec) / 1e9,
                        up - (double(boot.tv_sec) + double(boot.tv_nsec) / 1e9));
        }
        std::fclose(f);
    }

    // adjtimex 只读,看内核时钟的同步状态(不需要特权)
    timex tx{};
    std::printf("\n== adjtimex 只读: 内核时钟纪律状态 ==\n");
    if (adjtimex(&tx) >= 0) {
        std::printf("status=0x%x  freq=%ld ppm  maxerror=%ld us  esterror=%ld us\n", tx.status,
                    tx.freq, tx.maxerror, tx.esterror);
        std::printf("STA_UNSYNC(0x40) 置位? %s —— 置位说明内核没有活跃的时间纪律源在调频\n",
                    (tx.status & STA_UNSYNC) ? "是" : "否");
    }
    return 0;
}
