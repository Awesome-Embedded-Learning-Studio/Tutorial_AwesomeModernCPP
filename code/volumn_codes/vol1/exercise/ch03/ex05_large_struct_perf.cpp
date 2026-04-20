/**
 * @file ex05_large_struct_perf.cpp
 * @brief 练习：高效处理大型结构体
 *
 * 定义包含 1000 个 double 的 Measurement 结构体，
 * 使用 <chrono> 比较按值传递和按 const 引用传递的性能差异。
 */

#include <chrono>
#include <iostream>

// 包含 1000 个 double 的大型结构体（约 8KB）
struct Measurement {
    double data[1000];

    // 初始化所有数据
    void fill(double value) {
        for (int i = 0; i < 1000; ++i) {
            data[i] = value;
        }
    }

    // 求和
    double sum() const {
        double s = 0.0;
        for (int i = 0; i < 1000; ++i) {
            s += data[i];
        }
        return s;
    }
};

// 按值传递：每次调用拷贝整个结构体
double process_by_value(Measurement m) {
    return m.sum();
}

// 按 const 引用传递：不拷贝
double process_by_const_ref(const Measurement& m) {
    return m.sum();
}

int main() {
    constexpr int kIterations = 100000;

    Measurement m;
    m.fill(1.5);

    std::cout << "Measurement 结构体大小: " << sizeof(Measurement) << " 字节\n";
    std::cout << "迭代次数: " << kIterations << "\n\n";

    // ===== 按值传递基准测试 =====
    volatile double sink = 0.0;  // 防止编译器优化掉
    auto start_val = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        sink = process_by_value(m);
    }
    auto end_val = std::chrono::high_resolution_clock::now();
    auto duration_val = std::chrono::duration_cast<std::chrono::microseconds>(
        end_val - start_val);

    // ===== 按 const 引用传递基准测试 =====
    start_val = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        sink = process_by_const_ref(m);
    }
    end_val = std::chrono::high_resolution_clock::now();
    auto duration_ref = std::chrono::duration_cast<std::chrono::microseconds>(
        end_val - start_val);

    // 输出结果
    std::cout << "按值传递耗时:       " << duration_val.count() << " 微秒\n";
    std::cout << "按 const 引用传递:  " << duration_ref.count() << " 微秒\n";

    if (duration_val.count() > 0) {
        double ratio = static_cast<double>(duration_val.count()) /
                       static_cast<double>(duration_ref.count());
        std::cout << "比值（值/引用）:    " << ratio << "x\n";
    }

    std::cout << "\n结论：对于大型结构体，const 引用可以避免不必要的拷贝开销。\n";
    std::cout << "(void)sink = " << sink << "\n";  // 使用 sink 防止优化

    return 0;
}
