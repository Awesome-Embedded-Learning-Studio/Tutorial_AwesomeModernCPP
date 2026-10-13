// E6: 墙上时间的人类呈现链 —— time_t → gmtime/localtime → strftime,TZ 与 /etc/localtime 的落点
// 口径: 同 E1;本机 /etc/localtime → Asia/Shanghai,TZ 环境变量未设
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

namespace {

void stamp(const char* label) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    std::time_t t = ts.tv_sec;
    std::tm gm{}, lc{};
    gmtime_r(&t, &gm);
    localtime_r(&t, &lc);
    char g[64], l[64];
    std::strftime(g, sizeof g, "%Y-%m-%d %H:%M:%S UTC", &gm);
    std::strftime(l, sizeof l, "%Y-%m-%d %H:%M:%S %Z %z", &lc);
    const char* tz = std::getenv("TZ");
    std::printf("%-14s epoch=%lld.%09ld\n"
                "               gmtime_r   → %s\n"
                "               localtime_r→ %s  (TZ=%s)\n",
                label, (long long)t, ts.tv_nsec, g, l, tz ? tz : "(未设,读 /etc/localtime)");
}

} // namespace

int main() {
    std::printf("== E6: 同一 epoch 值的两种呈现 ==\n");
    stamp("默认 TZ");

    // TZ 环境变量切换: 不改系统,只改本进程的呈现层
    setenv("TZ", "UTC0", 1);
    tzset();
    stamp("TZ=UTC0");
    setenv("TZ", "America/New_York", 1);
    tzset();
    stamp("TZ=America/New_York");
    setenv("TZ", "Asia/Tokyo", 1);
    tzset();
    stamp("TZ=Asia/Tokyo");

    // 呈现层只是换算:epoch 从头到尾没变过,变的只是解释它的规则库
    std::printf("\n要点: clock_gettime(CLOCK_REALTIME) 给的是 UTC epoch 秒;\n"
                "本地化发生在呈现层(localtime_r + tzset),TZ 换的是规则,不是时间本身。\n"
                "时区规则的来源: TZ 环境变量优先,未设则读 /etc/localtime 指向的 zoneinfo 文件。\n"
                "下一篇咱们看 C++20 chrono 怎么把这套规则收进类型系统(zoned_time)。\n");
    return 0;
}
