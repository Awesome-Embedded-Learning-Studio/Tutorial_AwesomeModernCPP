// 00_os_threads_and_cost.cpp
// 《OS 线程与开销》配套基准：把"起一个什么都不做的线程"和"直接调用同一个
// 函数"放在同一台机器上掐表，单次线程创建的成本就量出来了。
//
// 编译：
//   g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread 00_os_threads_and_cost.cpp
//
// 提醒：输出数字随机器、负载与内核版本浮动，看量级就好，别背具体值。

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

// "空任务"：只做一次加法。返回值会被累加进 sink，
// 编译器也就没法把整个调用优化没了。
long long noop_task(long long seed) noexcept {
    return seed + 1;
}

struct TimingResult {
    double total_ms;
    long long sink; // 校验用：两种版本必须累加出同一个数
};

TimingResult run_threads(int count) {
    const auto start = std::chrono::steady_clock::now();
    long long sink = 0;
    for (int i = 0; i < count; ++i) {
        // 每轮起一条新线程，做完立刻 join：量的是"创建 + 收尾"一整个来回
        std::thread t([i, &sink] { sink += noop_task(i); });
        t.join();
    }
    const auto stop = std::chrono::steady_clock::now();
    return {std::chrono::duration<double, std::milli>(stop - start).count(), sink};
}

TimingResult run_calls(int count) {
    const auto start = std::chrono::steady_clock::now();
    long long sink = 0;
    for (int i = 0; i < count; ++i) {
        sink += noop_task(i);
    }
    const auto stop = std::chrono::steady_clock::now();
    return {std::chrono::duration<double, std::milli>(stop - start).count(), sink};
}

} // namespace

int main(int argc, char* argv[]) {
    const int count = (argc > 1) ? std::max(1, std::atoi(argv[1])) : 1000;

    std::cout << "count = " << count << "（数字随机器与负载浮动，看量级就好）\n";

    const TimingResult threads = run_threads(count);
    const TimingResult calls = run_calls(count);

    std::cout << "threads : " << threads.total_ms << " ms 总耗时，"
              << "单次 " << threads.total_ms * 1000.0 / count << " us\n";
    std::cout << "calls   : " << calls.total_ms << " ms 总耗时，"
              << "单次 " << calls.total_ms * 1000.0 * 1000.0 / count << " ns\n";

    if (threads.sink != calls.sink) {
        std::cerr << "sink 校验失败：" << threads.sink << " != " << calls.sink << '\n';
        return 1;
    }
    std::cout << "sink 校验一致：" << threads.sink << '\n';
    return 0;
}
