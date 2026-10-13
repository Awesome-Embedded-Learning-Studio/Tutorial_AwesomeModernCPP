// E2: 闰秒进了类型系统 —— utc_clock 时间轴上真实存在的 23:59:60 与 get_leap_second_info
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra
#include <chrono>
#include <cstdio>
#include <format>

namespace ch = std::chrono;

int main() {
    using ch::sys_days;
    using ch::utc_clock;
    using namespace ch;

    // 2016-12-31 23:59:59 UTC,再走 1 秒是什么?
    auto t235959 = clock_cast<utc_clock>(sys_days{2016y / December / 31} + 23h + 59min + 59s);
    auto leap = t235959 + 1s; // utc_clock 的加法:这一秒是闰秒
    auto after = leap + 1s;   // 越过闰秒

    std::printf("== E2a: 闰秒那一夜,utc_clock 时间轴 ==\n");
    std::printf("sys 时间轴(系统刻度): 2016-12-31 23:59:59 的下一个秒是 2017-01-01 00:00:00,\n"
                "                    闰秒在这把尺子上没有位置,只能靠标注。\n");
    std::printf("utc_clock: %s\n", std::format("{:%Y-%m-%d %H:%M:%S}", t235959).c_str());
    std::printf("utc_clock +1s = %s   ← 23:59:60 真的在轴上\n",
                std::format("{:%Y-%m-%d %H:%M:%S}", leap).c_str());
    std::printf("utc_clock +2s = %s\n", std::format("{:%Y-%m-%d %H:%M:%S}", after).c_str());
    std::printf("闰秒所在秒的亚秒位: %s\n", std::format("{:%Y-%m-%d %H:%M:%S %S}", leap).c_str());

    // get_leap_second_info: 某时刻是否闰秒、自纪元起已数过几个闰秒
    std::printf("\n== E2b: get_leap_second_info ==\n");
    auto a = get_leap_second_info(t235959); // 闰秒前一秒
    auto b = get_leap_second_info(leap);    // 闰秒本体
    auto c = get_leap_second_info(clock_cast<utc_clock>(sys_days{2017y / January / 2}));
    auto d = get_leap_second_info(clock_cast<utc_clock>(sys_days{2015y / January / 2}));
    std::printf("2016-12-31 23:59:59 → is_leap_second=%d, 已经历闰秒=%lld\n", (int)a.is_leap_second,
                (long long)a.elapsed.count());
    std::printf("2016-12-31 23:59:60 → is_leap_second=%d, 已经历闰秒=%lld(含当次)\n",
                (int)b.is_leap_second, (long long)b.elapsed.count());
    std::printf("2017-01-02          → is_leap_second=%d, 已经历闰秒=%lld\n", (int)c.is_leap_second,
                (long long)c.elapsed.count());
    std::printf("2015-01-02          → is_leap_second=%d, 已经历闰秒=%lld\n", (int)d.is_leap_second,
                (long long)d.elapsed.count());

    // tzdb 的闰秒表:数据从哪来
    std::printf("\n== E2c: tzdb 的闰秒表 ==\n");
    const auto& db = get_tzdb();
    std::printf("tzdb 版本 %s,leap_seconds 条数=%zu\n", db.version.c_str(), db.leap_seconds.size());
    std::printf("首条 %s,末条 %s\n",
                std::format("{:%Y-%m-%d}", db.leap_seconds.front().date()).c_str(),
                std::format("{:%Y-%m-%d}", db.leap_seconds.back().date()).c_str());
    std::printf("(条数 27 对表 E1 的 utc−system=+27 s;表是 OS 的 tzdata 解析来的,来源实验在 E4)\n");
    return 0;
}
