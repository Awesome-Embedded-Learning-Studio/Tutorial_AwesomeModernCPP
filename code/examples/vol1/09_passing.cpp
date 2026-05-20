// 09_passing.cpp
// 参数传递方式对比：值传递 vs const 引用传递

#include <chrono>
#include <iostream>
#include <string>

void swap_values(int& a, int& b) {
    int temp = a;
    a = b;
    b = temp;
}

struct BigData {
    int payload[4096]; // 16 KB
};

long sum_by_value(BigData data) {
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

long sum_by_const_ref(const BigData& data) {
    long total = 0;
    for (int i = 0; i < 4096; ++i) {
        total += data.payload[i];
    }
    return total;
}

std::string build_greeting(const std::string& name) {
    return "Hello, " + name + "! Welcome to Modern C++.";
}

int main() {
    int a = 10;
    int b = 20;
    std::cout << "交换前: a = " << a << ", b = " << b << std::endl;
    swap_values(a, b);
    std::cout << "交换后: a = " << a << ", b = " << b << std::endl;

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
    auto ms_value = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    start = std::chrono::high_resolution_clock::now();
    long result_ref = 0;
    for (int i = 0; i < kIterations; ++i) {
        result_ref = sum_by_const_ref(data);
    }
    end = std::chrono::high_resolution_clock::now();
    auto ms_ref = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\n--- 性能对比 (" << kIterations << " 次调用) ---" << std::endl;
    std::cout << "值传递:   结果=" << result_value << ", 耗时: " << ms_value << " ms" << std::endl;
    std::cout << "const引用: 结果=" << result_ref << ", 耗时: " << ms_ref << " ms" << std::endl;

    std::string name = "Charlie";
    std::cout << build_greeting(name) << std::endl;

    return 0;
}
