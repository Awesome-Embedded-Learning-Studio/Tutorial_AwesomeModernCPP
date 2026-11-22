// 4 亿整数求和:串行对双线程,观察多核加速比与划分开销
#include <chrono>
#include <cstdint>
#include <print>
#include <thread>

using namespace std::chrono;

long long sum_range(long long from, long long to) {
    long long s = 0;
    for (long long i = from; i < to; ++i)
        s += i % 1000;
    return s;
}

int main() {
    const long long n = 400000000LL;

    auto t0 = steady_clock::now();
    long long s1 = sum_range(0, n);
    auto t1 = steady_clock::now();
    std::println("serial    : {} ms, sum {}", duration_cast<milliseconds>(t1 - t0).count(), s1);

    long long sa = 0, sb = 0;
    t0 = steady_clock::now();
    std::thread ta([&] { sa = sum_range(0, n / 2); });
    std::thread tb([&] { sb = sum_range(n / 2, n); });
    ta.join(); // join 等待该线程结束
    tb.join();
    t1 = steady_clock::now();
    std::println("two threads: {} ms, sum {}", duration_cast<milliseconds>(t1 - t0).count(),
                 sa + sb);
}
