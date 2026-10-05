// E1: chrono 时钟怎么落在 OS 时钟上 —— system/steady/high_res 与 clock_gettime 对拍,utc/tai/gps
// 的纪元 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra 说明: 标准里没有
// steady_system_clock 这个名字(C++20 新添的是 utc/tai/gps),
//       g++ 16.2.1 无对应特性测试宏、名字编不过(均实测);本篇聚焦 C++20 已落地的三把。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <type_traits>

namespace ch = std::chrono;

int main() {
    std::printf("== E1a: 三个老熟人是谁的别名 ==\n");
    std::printf("is_same<high_resolution_clock, system_clock> : %s\n",
                std::is_same_v<ch::high_resolution_clock, ch::system_clock> ? "是" : "否");
    std::printf("is_same<high_resolution_clock, steady_clock> : %s\n",
                std::is_same_v<ch::high_resolution_clock, ch::steady_clock> ? "是" : "否");
    std::printf("system_clock::is_steady=%d  steady_clock::is_steady=%d\n",
                ch::system_clock::is_steady, ch::steady_clock::is_steady);

    // 与 POSIX 时钟对拍: 同一瞬间背靠背读,差值在读取延迟量级即"同源"
    std::printf("\n== E1b: chrono::now() 对拍 clock_gettime ==\n");
    {
        auto a = ch::system_clock::now().time_since_epoch().count(); // 纳秒
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        auto b = ch::system_clock::duration(ch::seconds(ts.tv_sec) + ch::nanoseconds(ts.tv_nsec))
                     .count();
        std::printf("system_clock − CLOCK_REALTIME = %lld ns → %s\n", (long long)(a - b),
                    std::llabs(a - b) < 100000 ? "同源(都读墙上钟)" : "不同源?");
    }
    {
        auto a = ch::steady_clock::now().time_since_epoch().count();
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        // steady_clock::period 在 libstdc++ 是 nano
        auto b = ch::steady_clock::duration(ch::seconds(ts.tv_sec) + ch::nanoseconds(ts.tv_nsec))
                     .count();
        std::printf("steady_clock  − CLOCK_MONOTONIC  = %lld ns → %s\n", (long long)(a - b),
                    std::llabs(a - b) < 100000 ? "同源(都读单调钟)" : "不同源?");
    }

    std::printf("\n== E1c: 同一瞬间,四把尺子 ==\n");
    auto u = ch::system_clock::now();
    auto utc = ch::clock_cast<ch::utc_clock>(u);
    auto tai = ch::clock_cast<ch::tai_clock>(u);
    auto gps = ch::clock_cast<ch::gps_clock>(u);
    double sys = ch::duration<double>(u.time_since_epoch()).count();
    double utc_c = ch::duration<double>(utc.time_since_epoch()).count();
    double tai_c = ch::duration<double>(tai.time_since_epoch()).count();
    double gps_c = ch::duration<double>(gps.time_since_epoch()).count();
    std::printf("system_clock 纪元 1970-01-01 UTC:  %.3f s(UTC 刻度,不数闰秒)\n", sys);
    std::printf("utc_clock    纪元 1970-01-01 UTC:  %.3f s(UTC 刻度,1972 以来 27 次闰秒都数进去)\n",
                utc_c);
    std::printf("tai_clock    纪元 1958-01-01 TAI:  %.3f s\n", tai_c);
    std::printf("gps_clock    纪元 1980-01-06 GPS:  %.3f s\n", gps_c);
    std::printf("\n把纪元差扣掉之后,剩下的就是物理偏移:\n");
    std::printf("utc − system                     = %+.0f s → 1972 以来插入的闰秒数\n",
                utc_c - sys);
    std::printf("tai − system − 378,691,200(纪元差)= %+.0f s → TAI−UTC 当下值\n",
                tai_c - sys - 378691200.0);
    std::printf("gps − system + 315,964,800(纪元差) = %+.0f s → GPS−UTC 当下值(GPS 领先)\n",
                gps_c - sys + 315964800.0);
    std::printf(
        "(TAI−UTC=37 对表篇 1 E1 的 CLOCK_TAI−CLOCK_REALTIME=37 s:内核与库读同一份闰秒数据)\n");
    return 0;
}
