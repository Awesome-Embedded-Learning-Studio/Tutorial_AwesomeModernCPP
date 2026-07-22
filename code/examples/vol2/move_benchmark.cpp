// move_benchmark.cpp -- 拷贝 vs 移动性能对比（分离构造开销）
// Standard: C++17
// 对应文档：vol2-modern-features/ch00-move-semantics/05-move-in-practice.md
//
// 关键设计：把"构造"这一固定开销单独测出来作为 baseline，再用
// (构造+拷贝) - 构造 和 (构造+移动) - 构造 得到纯粹的拷贝/移动耗时，
// 避免构造开销稀释掉移动操作本身"接近零"的事实。
//
// 注意：绝对耗时是机器相关的，但"纯移动 ≈ 0、纯拷贝 >> 0"的结论稳定。
#include <chrono>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

class BigData {
    std::vector<double> payload_;

  public:
    explicit BigData(std::size_t n) : payload_(n) {
        std::iota(payload_.begin(), payload_.end(), 0.0);
    }

    BigData(const BigData& other) : payload_(other.payload_) {}
    BigData(BigData&& other) noexcept = default;
    BigData& operator=(const BigData&) = default;
    BigData& operator=(BigData&&) noexcept = default;
};

/// @brief 测量函数执行时间的辅助模板
template <typename Func> double measure_ms(Func&& func, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        func();
    }
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    constexpr std::size_t kDataSize = 1000000; // 100 万个 double，约 8MB
    constexpr int kIterations = 100;

    std::cout << "数据大小: " << kDataSize * sizeof(double) / 1024 << " KB\n";
    std::cout << "迭代次数: " << kIterations << "\n\n";

    // 测试 0：仅构造（baseline）
    auto construct_time = measure_ms(
        [&]() {
            BigData source(kDataSize);
            (void)source;
        },
        kIterations);

    std::cout << "仅构造（baseline）: " << construct_time << " ms\n";

    // 测试 1：构造 + 拷贝
    auto copy_time = measure_ms(
        [&]() {
            BigData source(kDataSize);
            BigData copy = source; // 拷贝构造
            (void)copy;
        },
        kIterations);

    std::cout << "构造 + 拷贝:        " << copy_time << " ms\n";

    // 测试 2：构造 + 移动
    auto move_time = measure_ms(
        [&]() {
            BigData source(kDataSize);
            BigData moved = std::move(source); // 移动构造
            (void)moved;
        },
        kIterations);

    std::cout << "构造 + 移动:        " << move_time << " ms\n\n";

    // 分离出纯粹的拷贝/移动耗时
    double actual_copy = copy_time - construct_time;
    double actual_move = move_time - construct_time;

    std::cout << "=== 分离后的实际耗时 ===\n";
    std::cout << "纯拷贝: " << actual_copy << " ms\n";
    std::cout << "纯移动: " << actual_move << " ms\n";

    if (actual_move > 0.01) {
        std::cout << "加速比: " << actual_copy / actual_move << "x\n";
    } else {
        std::cout << "移动耗时在测量噪声范围内（接近零）\n";
    }

    return 0;
}
