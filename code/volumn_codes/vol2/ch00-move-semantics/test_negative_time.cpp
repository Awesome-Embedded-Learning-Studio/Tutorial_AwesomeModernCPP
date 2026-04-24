// test_negative_time.cpp -- 验证移动操作计时可能出现负值
// Standard: C++17
// GCC 15, -O2 -std=c++17

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>

class BigData
{
    std::vector<double> payload_;

public:
    explicit BigData(std::size_t n) : payload_(n)
    {
        std::iota(payload_.begin(), payload_.end(), 0.0);
    }

    BigData(const BigData& other) : payload_(other.payload_) {}
    BigData(BigData&& other) noexcept = default;
    BigData& operator=(const BigData&) = default;
    BigData& operator=(BigData&&) noexcept = default;
};

template<typename Func>
double measure_ms(Func&& func, int iterations)
{
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main()
{
    constexpr std::size_t kDataSize = 1000000;
    constexpr int kIterations = 100;

    std::cout << "数据大小: " << kDataSize * sizeof(double) / 1024 << " KB\n";
    std::cout << "迭代次数: " << kIterations << "\n\n";

    // 多次测量以展示测量噪声
    for (int run = 0; run < 5; ++run) {
        // 测量仅构造时间
        auto construct_time = measure_ms([&]() {
            BigData source(kDataSize);
            (void)source;
        }, kIterations);

        // 测量构造 + 移动
        auto move_time = measure_ms([&]() {
            BigData source(kDataSize);
            BigData moved = std::move(source);
            (void)moved;
        }, kIterations);

        // 计算纯移动时间
        double actual_move = move_time - construct_time;

        std::cout << "第 " << run + 1 << " 次运行:\n";
        std::cout << "  仅构造: " << construct_time << " ms\n";
        std::cout << "  构造+移动: " << move_time << " ms\n";
        std::cout << "  纯移动: " << actual_move << " ms\n";
    }

    std::cout << "\n=== 结论 ===\n";
    std::cout << "负值是正常的，这是测量噪声导致的。\n";
    std::cout << "移动操作时间极短，在高精度计时下，\n";
    std::cout << "系统调度、缓存状态等差异会导致构造+移动的总时间\n";
    std::cout << "略小于单独构造的时间。\n";

    return 0;
}
