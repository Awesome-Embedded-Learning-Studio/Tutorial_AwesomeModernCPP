// E5: 日历的类型系统 —— 合法性校验、最后一个周几、weekday、互转与间隔天数、hh_mm_ss
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra
// 与 vol3《chrono:duration、时钟与 C++20 日历》分工: 那篇讲 duration 分数运算与 steady 选型,
// 本篇守 POSIX/OS 落点这条线,日历只验证类型系统里跟"时间轴对表"相关的行为。
#include <chrono>
#include <cstdio>
#include <format>

namespace ch = std::chrono;
using namespace ch;

int main() {
    std::printf("== E5a: 日期合法性是类型自己的事 ==\n");
    year_month_day bad1 = 2026y / February / 30d;
    year_month_day bad2 = 2025y / February / 29d;
    year_month_day ok1 = 2028y / February / 29d;
    year_month_day bad3 = 2026y / March / 32d;
    std::printf(
        "2026-02-30 .ok()=%d  2025-02-29 .ok()=%d  2028-02-29 .ok()=%d  2026-03-32 .ok()=%d\n",
        (int)bad1.ok(), (int)bad2.ok(), (int)ok1.ok(), (int)bad3.ok());
    // 年月合法、日子越界时转 sys_days 是定义良好的折算(标准: sys_days(y/m/1d) + (day-1d))
    sys_days folded = sys_days{2026y / February / 30d};
    std::printf("2026-02-30 转 sys_days = %s(折算: 2026-02-01 + 29 天)\n",
                std::format("{:%Y-%m-%d}", year_month_day{folded}).c_str());
    std::printf(
        "不合法的值还能存(ok() 只是标记),转 sys_days 不报错也不跳变,年月也无效时结果未指明,\n"
        "合法性要在转入时间轴之前自己把关\n");

    std::printf("\n== E5b: 「最后一个周几」是编译期表达 ==\n");
    auto last_sunday = sys_days{2026y / October / Sunday[last]};
    std::printf("2026 年 10 月的最后一个周日 = %s\n",
                std::format("{:%Y-%m-%d(%A)}", last_sunday).c_str());
    auto first_mon = year_month_weekday{2026y / October / Monday[1]};
    std::printf("2026 年 10 月的第 1 个周一  = %s\n",
                std::format("{:%Y-%m-%d}", sys_days{first_mon}).c_str());

    std::printf("\n== E5c: 互转与对表 ==\n");
    sys_days today = 2026y / October / 4d;
    std::printf("2026-10-04 的 weekday = %s(与 date 命令输出对表)\n",
                std::format("{:%A}", weekday{today}).c_str());
    year_month_day back{today};
    std::printf("sys_days ↔ year_month_day 往返: %s\n", std::format("{:%Y-%m-%d}", back).c_str());
    auto days_since_2000 = sys_days{2026y / October / 4d} - sys_days{2000y / January / 1d};
    std::printf("2000-01-01 → 2026-10-04 共 %lld 天(类型是 days,不是自己除 86400)\n",
                (long long)days_since_2000.count());
    // 今天(周日)到年底还有多少个周日
    int sundays = 0;
    for (sys_days d = today; d <= sys_days{2026y / December / 31d}; d += days{1})
        if (weekday{d} == Sunday)
            ++sundays;
    std::printf("2026-10-04 → 年底之间的周日数 = %d(日历运算直接落在循环里)\n", sundays);

    std::printf("\n== E5d: hh_mm_ss 把 duration 摆成钟面 ==\n");
    hh_mm_ss hms{9025ms + 234us};
    std::printf("9025ms+234us → %02d:%02d:%02d.%06d(to_dur 回去还是同一个 duration)\n",
                (int)hms.hours().count(), (int)hms.minutes().count(), (int)hms.seconds().count(),
                (int)hms.subseconds().count());
    hh_mm_ss neg{-5min};
    std::printf("负 duration -5min → is_negative=%d,钟面 %02d:%02d:%02d(符号单列,域不掺假)\n",
                (int)neg.is_negative(), (int)neg.hours().count(), (int)neg.minutes().count(),
                (int)neg.seconds().count());
    return 0;
}
