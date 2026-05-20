// 09_lambda_capture.cpp
// Lambda 捕获：值捕获、引用捕获、mutable、初始化捕获与闭包大小

#include <iostream>
#include <memory>

int main() {
    std::cout << "=== Lambda 捕获机制演示 ===\n\n";

    std::cout << "1. 值捕获 vs 引用捕获:\n";
    int threshold = 100;
    int counter = 0;
    auto by_value = [threshold]() { return threshold; };
    auto by_ref = [&counter]() { return ++counter; };
    threshold = 200;
    std::cout << "  值捕获结果: " << by_value() << " (仍为100)\n";
    std::cout << "  引用捕获结果: " << by_ref() << " (counter=1)\n\n";

    std::cout << "2. mutable 闭包状态:\n";
    auto make_counter = [count = 0]() mutable { return ++count; };
    std::cout << "  调用1: " << make_counter() << "\n";
    std::cout << "  调用2: " << make_counter() << "\n";
    std::cout << "  调用3: " << make_counter() << "\n\n";

    std::cout << "3. 初始化捕获（移动 unique_ptr）:\n";
    auto ptr = std::make_unique<int>(42);
    auto handler = [p = std::move(ptr)]() { return *p; };
    std::cout << "  lambda 持有的值: " << handler() << "\n";
    std::cout << "  原 ptr 是否为空: " << (ptr == nullptr ? "是" : "否") << "\n\n";

    std::cout << "4. 闭包对象大小:\n";
    int a = 0;
    double b = 0.0;
    auto no_capture = []() {};
    auto capture_int = [a]() { return a; };
    auto capture_ref = [&a]() { return a; };
    auto capture_both = [a, &b]() { return a + b; };
    std::cout << "  无捕获:     " << sizeof(no_capture) << " bytes\n";
    std::cout << "  值捕获int: " << sizeof(capture_int) << " bytes\n";
    std::cout << "  引用捕获:  " << sizeof(capture_ref) << " bytes\n";
    std::cout << "  混合捕获:  " << sizeof(capture_both) << " bytes\n";

    return 0;
}
