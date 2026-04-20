// passing.cpp —— 演示值传递、引用传递和 const 引用传递

#include <iostream>
#include <string>
#include <chrono>

/// @brief 交换两个整数的值
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

struct BigData {
    int payload[4096];  // 16 KB
};

/// @brief 值传递版本：每次调用拷贝整个 BigData
long sum_by_value(BigData data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief const 引用版本：零拷贝
long sum_by_const_ref(const BigData& data)
{
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

/// @brief 拼接问候语，const 引用避免字符串拷贝
std::string build_greeting(const std::string& name)
{
    return "Hello, " + name + "! Welcome to Modern C++.";
}

int main()
{
    // swap 演示
    int a = 10;
    int b = 20;
    std::cout << "交换前: a = " << a << ", b = " << b << std::endl;
    swap_values(a, b);
    std::cout << "交换后: a = " << a << ", b = " << b << std::endl;

    // 性能对比
    BigData data{};
    for (int i = 0; i < 4096; ++i) {
        data.payload[i] = i;
    }

    constexpr int kIterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    long result_value = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_value = sum_by_value(data);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms_value = std::chrono::duration_cast<std::chrono::milliseconds>(
                        end - start)
                        .count();

    start = std::chrono::high_resolution_clock::now();
    long result_ref = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_ref = sum_by_const_ref(data);
    }
    end = std::chrono::high_resolution_clock::now();
    auto ms_ref = std::chrono::duration_cast<std::chrono::milliseconds>(
                      end - start)
                      .count();

    std::cout << "\n--- 性能对比 (" << kIterations << " 次调用) ---"
              << std::endl;
    std::cout << "值传递: " << result_value
              << ", 耗时: " << ms_value << " ms" << std::endl;
    std::cout << "const引用: " << result_ref
              << ", 耗时: " << ms_ref << " ms" << std::endl;

    // 字符串处理
    std::string name = "Charlie";
    std::cout << build_greeting(name) << std::endl;
    std::cout << build_greeting(std::string("World")) << std::endl;

    return 0;
}
