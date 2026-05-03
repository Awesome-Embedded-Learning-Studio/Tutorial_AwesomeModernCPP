// Lambda 基础：捕获模式与泛型 lambda
// 来源：OnceCallback 前置知识速查 (pre-00)
// 编译：g++ -std=c++17 -Wall -Wextra 04_lambda_basics.cpp -o 04_lambda_basics

#include <memory>
#include <iostream>
#include <string>

int main() {
    std::cout << "=== Lambda 捕获模式 ===\n";
    {
        int x = 10;

        auto f1 = [x]() { return x; };                   // 值捕获
        auto f2 = [&x]() { return x; };                   // 引用捕获
        auto f3 = [p = std::make_unique<int>(42)]() {     // 初始化捕获 (C++14)
            return *p;
        };

        std::cout << "  value capture: " << f1() << "\n";
        std::cout << "  ref capture: " << f2() << "\n";
        std::cout << "  init capture: " << f3() << "\n";
    }

    std::cout << "\n=== 泛型 lambda (C++14) ===\n";
    {
        auto generic = [](auto x, auto y) { return x + y; };
        std::cout << "  generic(1, 2) = " << generic(1, 2) << "\n";
        std::cout << "  generic(1.5, 2.5) = " << generic(1.5, 2.5) << "\n";
        std::cout << "  generic(std::string(\"hello\"), std::string(\" world\")) = "
                  << generic(std::string("hello"), std::string(" world")) << "\n";
    }

    std::cout << "\n=== [[nodiscard]] 属性 (C++17) ===\n";
    {
        struct Checker {
            [[nodiscard]] bool is_valid() const noexcept { return true; }
        };
        Checker c;
        // c.is_valid();  // 编译器警告：返回值被忽略
        bool ok = c.is_valid();
        std::cout << "  is_valid() = " << ok << "\n";
    }

    return 0;
}
