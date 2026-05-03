// std::invoke 与统一调用协议
// 来源：OnceCallback 前置知识（二）(pre-02)
// 编译：g++ -std=c++17 -Wall -Wextra 08_invoke.cpp -o 08_invoke

#include <functional>
#include <iostream>
#include <string>
#include <type_traits>

// --- 普通函数 ---

int add(int a, int b) { return a + b; }

// --- 仿函数 ---

struct Adder {
    int operator()(int a, int b) { return a + b; }
};

// --- 成员函数与数据成员 ---

struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

struct Point {
    double x, y;
};

int main() {
    std::cout << "=== 普通函数指针 ===\n";
    {
        int (*fp)(int, int) = &add;
        int result = fp(3, 4);
        std::cout << "  fp(3, 4) = " << result << "\n";
    }

    std::cout << "\n=== Lambda / 仿函数 ===\n";
    {
        auto lam = [](int a, int b) { return a + b; };
        std::cout << "  lambda(3, 4) = " << lam(3, 4) << "\n";

        Adder fn;
        std::cout << "  functor(3, 4) = " << fn(3, 4) << "\n";
    }

    std::cout << "\n=== 成员函数指针 ===\n";
    {
        Calculator calc;
        int (Calculator::*pmf)(int, int) = &Calculator::multiply;
        int result = (calc.*pmf)(3, 4);
        std::cout << "  (calc.*multiply)(3, 4) = " << result << "\n";
    }

    std::cout << "\n=== 数据成员指针 ===\n";
    {
        Point p{1.0, 2.0};
        double Point::*pmx = &Point::x;
        double val = p.*pmx;
        std::cout << "  p.*&Point::x = " << val << "\n";
    }

    std::cout << "\n=== std::invoke 统一调用 ===\n";
    {
        // 成员函数 + 对象引用
        Calculator calc;
        int r1 = std::invoke(&Calculator::multiply, calc, 3, 4);
        std::cout << "  invoke(member_func, ref, 3, 4) = " << r1 << "\n";

        // 成员函数 + 对象指针
        int r2 = std::invoke(&Calculator::multiply, &calc, 3, 4);
        std::cout << "  invoke(member_func, ptr, 3, 4) = " << r2 << "\n";

        // 数据成员
        Point p{5.0, 6.0};
        double r3 = std::invoke(&Point::x, p);
        std::cout << "  invoke(&Point::x, p) = " << r3 << "\n";

        // lambda
        int r4 = std::invoke([](int a, int b) { return a + b; }, 3, 4);
        std::cout << "  invoke(lambda, 3, 4) = " << r4 << "\n";
    }

    std::cout << "\n=== std::invoke_result_t 编译期推导 ===\n";
    {
        using R1 = std::invoke_result_t<decltype(add), int, int>;
        static_assert(std::is_same_v<R1, int>);
        std::cout << "  invoke_result_t<typeof(add), int, int> == int\n";

        auto lam = [](double x) { return std::to_string(x); };
        using R2 = std::invoke_result_t<decltype(lam), double>;
        static_assert(std::is_same_v<R2, std::string>);
        std::cout << "  invoke_result_t<typeof(lambda), double> == std::string\n";
    }

    return 0;
}
