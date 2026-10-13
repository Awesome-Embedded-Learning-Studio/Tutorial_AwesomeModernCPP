// E3: 时区转换的硬案例 —— 歧义时刻(拨慢)与不存在时刻(拨快),中国 1986-1991 的夏令时
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra;tzdb 版本见 E2(2026c)
#include <chrono>
#include <cstdio>
#include <format>

namespace ch = std::chrono;
using namespace ch;

namespace {

void zline(const zoned_seconds& z, const char* label) {
    std::printf("%-38s %s  (对应 UTC %s)\n", label,
                std::format("{:%Y-%m-%d %H:%M:%S %Z %z}", z).c_str(),
                std::format("{:%Y-%m-%d %H:%M:%S}", z.get_sys_time()).c_str());
}

} // namespace

int main() {
    std::printf("== E3a: 常规转换 ==\n");
    auto now_sys = floor<seconds>(system_clock::now());
    zoned_time shanghai = zoned_time("Asia/Shanghai", now_sys);
    zoned_time utc = zoned_time("UTC", now_sys);
    zline(shanghai, "Asia/Shanghai 现在正点:");
    zline(utc, "UTC 同刻:");

    std::printf("\n== E3b: 拨慢之夜(ambiguous)—— America/New_York 2026-11-01 ==\n");
    // 秋季拨慢: EDT 02:00 → EST 01:00,本地 01:30 出现两次
    local_seconds ambiguous = local_days{2026y / November / 1} + 1h + 30min;
    zoned_time earliest = zoned_time("America/New_York", ambiguous, choose::earliest);
    zoned_time latest = zoned_time("America/New_York", ambiguous, choose::latest);
    zline(earliest, "01:30 choose::earliest(第 1 次,EDT):");
    zline(latest, "01:30 choose::latest(第 2 次,EST):");
    std::printf("同一句本地时间,两个 UTC 答案,差 1 小时\n");
    try {
        zoned_time boom = zoned_time("America/New_York", ambiguous); // 不带 choose
        std::printf("没抛?(%s)\n", std::format("{:%F %T}", boom).c_str());
    } catch (const std::exception& e) {
        std::printf("不带 choose 的构造直接抛: %s\n", e.what());
    }

    std::printf("\n== E3c: 拨快之夜(nonexistent)—— America/New_York 2026-03-08 ==\n");
    // 春季拨快: EST 02:00 → EDT 03:00,本地 02:30 不存在
    local_seconds ghost = local_days{2026y / March / 8} + 2h + 30min;
    zoned_time mapped = zoned_time("America/New_York", ghost, choose::earliest);
    zline(mapped, "02:30 映射出去(不存在的时刻):");
    // 回程检验:sys → local 再看一次
    std::printf("回看: 该 UTC 时刻的本地呈现是 %s → 原 02:30 回不来\n",
                std::format("{:%Y-%m-%d %H:%M:%S %Z}", mapped).c_str());
    try {
        zoned_time boom = zoned_time("America/New_York", ghost);
        std::printf("没抛?(%s)\n", std::format("{:%F %T}", boom).c_str());
    } catch (const std::exception& e) {
        std::printf("不带 choose 的构造直接抛: %s\n", e.what());
    }

    std::printf("\n== E3d: 中国也实行过夏令时(1986-1991) ==\n");
    // 1988 年的拨快边界:local 02:00 → 03:00;02:30 当年不存在
    local_seconds cn_ghost = local_days{1988y / April / 17} + 2h + 30min;
    zoned_time cn = zoned_time("Asia/Shanghai", cn_ghost, choose::earliest);
    zline(cn, "1988-04-17 02:30(拨快,不存在):");
    local_seconds cn_ok = local_days{1988y / April / 17} + 3h + 30min;
    zoned_time cn2 = zoned_time("Asia/Shanghai", cn_ok, choose::earliest);
    zline(cn2, "1988-04-17 03:30(存在,CDT +09):");
    zoned_time cn_now = zoned_time("Asia/Shanghai", local_days{1988y / July / 1} + 12h);
    zline(cn_now, "1988-07-01 12:00(夏令时中段):");
    zoned_time cn_winter = zoned_time("Asia/Shanghai", local_days{1988y / January / 15} + 12h);
    zline(cn_winter, "1988-01-15 12:00(冬令时):");
    return 0;
}
