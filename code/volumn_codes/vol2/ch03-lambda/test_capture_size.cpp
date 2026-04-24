/**
 * @file test_capture_size.cpp
 * @brief 验证 lambda 闭包对象的大小
 *
 * 编译命令:
 *   g++ -std=c++20 -O0 -o test_capture_size test_capture_size.cpp
 *
 * 运行:
 *   ./test_capture_size
 */

#include <iostream>
#include <memory>
#include <string>

void test_closure_sizes() {
    int a = 0;
    double b = 0.0;
    int& ref = a;
    std::string str = "hello";
    std::unique_ptr<int> ptr = std::make_unique<int>(42);

    // 各种捕获方式的 lambda
    auto no_capture = []() {};
    auto capture_int = [a]() { return a; };
    auto capture_ref = [&a]() { return a; };
    auto capture_both = [a, &b]() { return a + b; };
    auto capture_string = [str]() { return str; };
    auto capture_ptr = [p = std::move(ptr)]() { return *p; };
    auto with_mutable = [a]() mutable { return ++a; };

    std::cout << "=== Lambda 闭包对象大小 ===\n";
    std::cout << "无捕获:            " << sizeof(no_capture) << " bytes\n";
    std::cout << "值捕获 int:        " << sizeof(capture_int) << " bytes (int 大小: " << sizeof(int) << ")\n";
    std::cout << "引用捕获 int&:     " << sizeof(capture_ref) << " bytes (指针大小: " << sizeof(int*) << ")\n";
    std::cout << "混合捕获:          " << sizeof(capture_both) << " bytes\n";
    std::cout << "值捕获 string:     " << sizeof(capture_string) << " bytes (string 大小: " << sizeof(std::string) << ")\n";
    std::cout << "初始化捕获 ptr:    " << sizeof(capture_ptr) << " bytes (unique_ptr 大小: " << sizeof(std::unique_ptr<int>) << ")\n";
    std::cout << "带 mutable:        " << sizeof(with_mutable) << " bytes\n";

    std::cout << "\n=== 验证结论 ===\n";
    std::cout << "1. 无捕获的 lambda 大小为 1 字节（C++ 不允许 0 大小对象）\n";
    std::cout << "2. 值捕获的大小等于被捕获变量的大小\n";
    std::cout << "3. 引用捕获的大小等于指针大小（64 位系统上为 8 字节）\n";
    std::cout << "4. 闭包对象没有虚函数表指针开销\n";
    std::cout << "5. mutable 关键字不影响闭包对象大小\n";
}

int main() {
    test_closure_sizes();
    return 0;
}
