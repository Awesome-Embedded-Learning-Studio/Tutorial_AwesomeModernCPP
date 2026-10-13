// E4: tzdb 从哪来 —— 首次加载的耗时、读了 OS 的哪些文件(strace 见 t4_tzdb_strace.txt)、坏名字的下场
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra;tzdata 包版本见输出
#include <chrono>
#include <cstdio>
#include <string>

namespace ch = std::chrono;

namespace {

ch::steady_clock::duration mono_now() {
    // 拿来计时的还是单调钟(篇 1 的结论):steady_clock 落在 CLOCK_MONOTONIC 上(见 E1)
    return ch::steady_clock::now().time_since_epoch();
}

} // namespace

int main() {
    std::printf("== E4a: 首次 get_tzdb() 的代价 ==\n");
    auto t0 = mono_now();
    const auto& db = ch::get_tzdb();
    auto t1 = mono_now();
    std::printf("首次 get_tzdb(): %.3f ms(解析时区数据库,之后常驻)\n",
                ch::duration<double, std::milli>(t1 - t0).count());
    auto t2 = mono_now();
    const auto& db2 = ch::get_tzdb();
    auto t3 = mono_now();
    std::printf("再次 get_tzdb(): %.6f ms(同一份引用,指针是 %s)\n",
                ch::duration<double, std::milli>(t3 - t2).count(),
                &db == &db2 ? "同一个" : "不同!");

    std::printf("\n== E4b: 库里有什么 ==\n");
    std::printf("版本串: %s(来自 OS 的 tzdata)\n", db.version.c_str());
    std::printf("zones=%zu  links=%zu  leap_seconds=%zu\n", db.zones.size(), db.links.size(),
                db.leap_seconds.size());

    std::printf("\n== E4c: 查名与 current_zone ==\n");
    auto t4 = mono_now();
    const ch::time_zone* z = db.locate_zone("Asia/Shanghai");
    auto t5 = mono_now();
    std::printf("locate_zone(\"Asia/Shanghai\"): %.3f us → %s\n",
                ch::duration<double, std::micro>(t5 - t4).count(), std::string(z->name()).c_str());
    std::printf("current_zone(): %s(读 TZ 环境变量,未设则 /etc/localtime,机制同篇 1 E6)\n",
                std::string(ch::current_zone()->name()).c_str());

    try {
        db.locate_zone("Mars/Olympus_Mons");
    } catch (const std::exception& e) {
        std::printf("locate_zone(\"Mars/Olympus_Mons\"): 抛 %s\n", e.what());
    }

    std::printf("\n== E4d: 数据从 OS 哪里读 ==\n");
    std::printf("strace 的记录在 t4_tzdb_strace.txt:openat 落在 /usr/share/zoneinfo/tzdata.zi 与\n"
                "leapseconds 两处 —— 标准库不是自带时区数据,而是解析发行版的 tzdata 文件。\n"
                "(这也是跨机器部署要确认 tzdata 包版本的实证依据)\n");
    return 0;
}
