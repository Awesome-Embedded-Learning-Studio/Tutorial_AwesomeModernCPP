// 同一个 lambda,直传给算法(auto)和先装进 std::function 再传,速度差一截:
// 类型擦除挡住了内联。值捕获的复制开销则会被 -O3 整个消掉,和无捕获几乎同速。
// 绝对数字随机器浮动,倍数才是重点。-O3 已在下方「运行」里配好。
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    constexpr size_t kDataSize = 10'000'000;
    std::vector<int> data(kDataSize);
    std::iota(data.begin(), data.end(), 0);
    const int threshold = 5'000'000;

    // 测试 1:auto 直传(可内联) vs 先装进 std::function(间接调用)
    auto start = std::chrono::high_resolution_clock::now();
    auto count1 =
        std::count_if(data.begin(), data.end(), [threshold](int x) { return x > threshold; });
    auto mid = std::chrono::high_resolution_clock::now();

    std::function<bool(int)> pred = [threshold](int x) { return x > threshold; };
    auto count2 = std::count_if(data.begin(), data.end(), pred);
    auto end = std::chrono::high_resolution_clock::now();

    auto us = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
    };

    std::cout << "data: " << kDataSize << " ints\n";
    std::cout << "auto lambda:    " << us(start, mid) << " us (count=" << count1 << ")\n";
    std::cout << "std::function:  " << us(mid, end) << " us (count=" << count2 << ")\n";
    std::cout << "ratio:          " << static_cast<double>(us(mid, end)) / us(start, mid) << "x\n";

    // 测试 2:值捕获 vs 无捕获,各跑一亿次
    constexpr int kIterations = 100'000'000;

    volatile int result1 = 0;
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        auto lam = [threshold = 100](int x) { return x > threshold; };
        result1 += lam(i);
    }
    mid = std::chrono::high_resolution_clock::now();

    volatile int result2 = 0;
    end = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        auto lam = [](int x) { return x > 100; };
        result2 += lam(i);
    }
    auto last = std::chrono::high_resolution_clock::now();

    auto ms = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    };

    std::cout << "\niterations: " << kIterations << "\n";
    std::cout << "value capture: " << ms(start, mid) << " ms\n";
    std::cout << "no capture:    " << ms(end, last) << " ms\n";
    return 0;
}
