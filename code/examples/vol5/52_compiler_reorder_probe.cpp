// 单线程看不见重排（as-if 规则）：两版循环的输出一模一样，
// 但机器码差得很远——放开手脚的版本可以被闭式化/向量化，
// 掐住重排的版本只能老老实实一条一条加。时间差就是编译器自由度的价格。
#include <chrono>
#include <cstdint>
#include <iostream>

std::uint64_t g_sink = 0;
constexpr long kN = 50'000'000;

__attribute__((noinline)) void free_transform() {
    // 没有屏障：编译器可以跨迭代重排、合并，甚至把整个循环换成闭式算式
    for (long i = 0; i < kN; ++i)
        g_sink += static_cast<std::uint64_t>(i);
}

__attribute__((noinline)) void fenced_transform() {
    // 每圈一道编译器屏障：跨迭代的访存合并与搬运全被掐住
    for (long i = 0; i < kN; ++i) {
        asm volatile("" ::: "memory");
        g_sink += static_cast<std::uint64_t>(i);
    }
}

template <typename F> long long time_ns(F&& f) {
    auto start = std::chrono::steady_clock::now();
    f();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

int main() {
    g_sink = 0;
    long long t_free = time_ns(free_transform);
    std::uint64_t s_free = g_sink;

    g_sink = 0;
    long long t_fence = time_ns(fenced_transform);
    std::uint64_t s_fence = g_sink;

    std::cout << "free  version: sum=" << s_free << "  " << t_free << " ns\n";
    std::cout << "fence version: sum=" << s_fence << "  " << t_fence << " ns\n";
    std::cout << "same result: " << (s_free == s_fence ? "yes" : "NO") << "\n";
    return 0;
}
