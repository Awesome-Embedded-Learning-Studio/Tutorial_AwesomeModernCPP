// 验证不可变数据风格和 NRVO 优化
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>

// 不可变风格：返回新容器，不修改原始数据
std::vector<int> sorted_copy(const std::vector<int>& input) {
    std::vector<int> result = input;        // 复制
    std::sort(result.begin(), result.end()); // 排序副本
    return result;                           // NRVO 优化掉额外复制
}

// 可变风格：直接修改原始数据
void sort_in_place(std::vector<int>& data) {
    std::sort(data.begin(), data.end());
}

void benchmark() {
    std::vector<int> data(1000000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<int>(i * 2654435761u % 1000000);

    const int iterations = 100;

    // 预热
    auto copy1 = sorted_copy(data);
    auto copy2 = data;
    sort_in_place(copy2);

    // 基准测试不可变风格
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto sorted = sorted_copy(data);
        // 防止编译器优化掉调用
        volatile int x = sorted[0];
        (void)x;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto immutable_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // 基准测试可变风格
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto copy = data;
        sort_in_place(copy);
        volatile int x = copy[0];
        (void)x;
    }
    end = std::chrono::high_resolution_clock::now();
    auto mutable_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "=== Performance Comparison ===\n";
    std::cout << "Immutable (sorted_copy): " << immutable_time << " us\n";
    std::cout << "Mutable (sort_in_place): " << mutable_time << " us\n";
    std::cout << "Overhead: " << static_cast<double>(immutable_time) / mutable_time << "x\n";

    std::cout << "\n=== Analysis ===\n";
    std::cout << "不可变风格的主要开销是初始复制，NRVO 优化了返回值的复制。\n";
    std::cout << "在现代 C++ 中，移动语义也进一步减少了开销。\n";
}

int main() {
    std::cout << "=== Testing immutability ===\n";
    std::vector<int> original = {3, 1, 4, 1, 5, 9, 2, 6};

    auto sorted = sorted_copy(original);

    std::cout << "Original: ";
    for (int x : original) std::cout << x << " ";
    std::cout << "\n";

    std::cout << "Sorted: ";
    for (int x : sorted) std::cout << x << " ";
    std::cout << "\n";

    // 验证原始数据未被修改
    std::cout << "Original after sorted_copy: ";
    for (int x : original) std::cout << x << " ";
    std::cout << "\n";

    std::cout << "\n";
    benchmark();

    return 0;
}
