// E6: 打点日志小实战 —— 一条日志同时带 epoch 纳秒、闰秒安全的 utc 呈现、本地时区呈现
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra
// 分层: steady_clock 只管计时差;system_clock 给 epoch;utc_clock 给闰秒安全的时间轴;
//       zoned_time 给本地呈现;format 给版式。四层各司其职,不互相顶替。
#include <chrono>
#include <cstdio>
#include <format>
#include <thread>

namespace ch = std::chrono;
using namespace ch;

namespace {

struct LogLine {
    ch::nanoseconds since_start;
    ch::system_clock::time_point wall;
    const char* event;
};

} // namespace

int main() {
    const auto t0 = ch::steady_clock::now();

    std::printf("%-10s %-24s %-30s %s\n", "elapsed", "epoch(ns)", "UTC(闰秒安全)",
                "本地(Asia/Shanghai)");
    const char* events[] = {"connect", "auth ok", "query begin", "query end", "close"};
    for (const char* ev : events) {
        std::this_thread::sleep_for(ch::milliseconds(50) + ch::microseconds(ev[0] * 5));
        ch::nanoseconds elapsed = ch::steady_clock::now() - t0;
        auto wall = ch::system_clock::now();
        auto utc_tp = clock_cast<ch::utc_clock>(wall);
        zoned_seconds local = zoned_seconds("Asia/Shanghai", floor<seconds>(wall));
        std::printf("%-10s %-24s %-30s %s\n", std::format("{:%T}", elapsed).c_str(),
                    std::format("{}", wall.time_since_epoch().count()).c_str(),
                    std::format("{:%Y-%m-%d %H:%M:%S}", utc_tp).c_str(),
                    std::format("{:%Y-%m-%d %H:%M:%S %Z}", local).c_str());
        (void)ev;
    }

    std::printf("\n读法:\n"
                "  elapsed 一列来自 steady_clock(单调,测量专用,受篇 1 E3 的结论保护);\n"
                "  epoch 一列来自 system_clock(可换算、可持久化、可跨机器对时);\n"
                "  UTC 一列来自 utc_clock(时间轴含闰秒,2038/2100 都不用管,呈现给审计最稳);\n"
                "  本地一列来自 zoned_time(规则来自 tzdb,给值班的人看)。\n"
                "四列是同一瞬间的四个读法,换算全部类型安全,没有一个手写除法。\n");
    return 0;
}
