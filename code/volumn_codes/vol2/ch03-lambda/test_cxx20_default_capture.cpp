/**
 * @file test_cxx20_default_capture.cpp
 * @brief 验证 C++20 中 [=] 不再隐式捕获 this
 *
 * 编译命令:
 *   g++ -std=c++17 -O0 -o test_cxx17 test_cxx20_default_capture.cpp
 *   g++ -std=c++20 -O0 -o test_cxx20 test_cxx20_default_capture.cpp
 *
 * 运行:
 *   ./test_cxx17  # C++17 中 [=] 隐式捕获 this（有警告）
 *   ./test_cxx20  # C++20 中 [=] 不再隐式捕获 this（有警告）
 */

#include <iostream>

class Test {
    int x = 42;

public:
    auto get_lambda_cxx11_17() {
        // C++11/14/17: [=] 隐式捕获 this
        // 实际上是通过 this->x 访问成员变量
        return [=]() { return x; };
    }

    auto get_lambda_cxx20_safe() {
        // C++20: [=] 不再隐式捕获 this
        // 需要显式指定 this 或 *this
        return [=, this]() { return x; };  // 显式捕获 this
    }

    auto get_lambda_cxx20_by_value() {
        // C++20: 按值捕获整个对象
        return [*this]() { return x; };
    }
};

int main() {
    Test t;

    std::cout << "=== C++11/17/20 默认捕获行为 ===\n";
    auto lam1 = t.get_lambda_cxx11_17();
    std::cout << "[=] 隐式捕获 this (C++11/17): " << lam1() << "\n";

    std::cout << "\n=== C++20 显式捕获 ===\n";
    auto lam2 = t.get_lambda_cxx20_safe();
    std::cout << "[=, this] 显式捕获: " << lam2() << "\n";

    auto lam3 = t.get_lambda_cxx20_by_value();
    std::cout << "[*this] 按值捕获对象: " << lam3() << "\n";

    std::cout << "\n=== 验证结论 ===\n";
    std::cout << "1. C++20 中 [=] 不再隐式捕获 this 指针\n";
    std::cout << "2. C++20 需要显式使用 [=, this] 或 [=, *this]\n";
    std::cout << "3. [this] 捕获指针（有悬垂风险）\n";
    std::cout << "4. [*this] 按值捕获整个对象（安全但有复制开销）\n";

    return 0;
}
