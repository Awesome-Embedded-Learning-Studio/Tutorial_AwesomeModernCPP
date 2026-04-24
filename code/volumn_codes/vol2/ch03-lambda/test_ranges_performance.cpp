// 验证 Ranges 相对于传统中间容器的性能优势
#include <ranges>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>

// 传统方法：创建中间容器（文章中的方法）
void traditional_map_filter(const std::vector<int>& data) {
    // Filter：创建第一个中间 vector
    std::vector<int> filtered;
    std::copy_if(data.begin(), data.end(), std::back_inserter(filtered),
                 [](int x) { return x % 2 == 0; });

    // Transform：创建第二个中间 vector
    std::vector<int> transformed;
    std::transform(filtered.begin(), filtered.end(), std::back_inserter(transformed),
                   [](int x) { return x * 2; });

    // 使用结果
    volatile int sum = 0;
    for (int x : transformed) sum += x;
    (void)sum;
}

// C++20 Ranges：惰性求值，无中间容器
void ranges_map_filter(const std::vector<int>& data) {
    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })
        | std::views::transform([](int x) { return x * 2; });

    // 使用结果
    volatile int sum = 0;
    for (int x : result) sum += x;
    (void)sum;
}

// 单次遍历方法：最优实现
void single_pass_optimal(const std::vector<int>& data) {
    volatile int sum = 0;
    for (int x : data) {
        if (x % 2 == 0) {
            sum += x * 2;
        }
    }
    (void)sum;
}

void benchmark() {
    std::vector<int> data(1000000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = i;

    const int iterations = 50;

    // 预热
    traditional_map_filter(data);
    ranges_map_filter(data);
    single_pass_optimal(data);

    // 基准测试传统方法
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) traditional_map_filter(data);
    auto end = std::chrono::high_resolution_clock::now();
    auto traditional_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 基准测试 Ranges
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) ranges_map_filter(data);
    end = std::chrono::high_resolution_clock::now();
    auto ranges_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 基准测试单次遍历
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) single_pass_optimal(data);
    end = std::chrono::high_resolution_clock::now();
    auto optimal_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "=== Performance Comparison (50 iterations, 1M elements) ===\n";
    std::cout << "Traditional (intermediate vectors): " << traditional_time << " us\n";
    std::cout << "Ranges (lazy evaluation):            " << ranges_time << " us\n";
    std::cout << "Single-pass optimal:                 " << optimal_time << " us\n";
    std::cout << "\nSpeedup:\n";
    std::cout << "Ranges vs Traditional: " << static_cast<double>(traditional_time) / ranges_time << "x\n";
    std::cout << "Optimal vs Traditional: " << static_cast<double>(traditional_time) / optimal_time << "x\n";

    std::cout << "\n=== Memory Allocation Analysis ===\n";
    size_t filtered_size = data.size() / 2;
    size_t traditional_memory = filtered_size * sizeof(int) * 2;  // 两个中间向量
    size_t ranges_memory = 0;  // 无中间分配
    std::cout << "Traditional: ~" << traditional_memory / 1024 << " KB for intermediate vectors\n";
    std::cout << "Ranges: " << ranges_memory << " bytes (no intermediate allocation)\n";
}

int main() {
    std::cout << "=== Testing Ranges correctness ===\n";
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })
        | std::views::transform([](int x) { return x * 2; })
        | std::views::take(3);

    std::cout << "Ranges result: ";
    for (int x : result) {
        std::cout << x << " ";   // 4 8 12
    }
    std::cout << "\n";

    std::cout << "\n";
    benchmark();

    return 0;
}
