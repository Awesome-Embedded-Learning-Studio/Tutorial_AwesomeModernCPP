// Concepts 与 requires 约束
// 来源：OnceCallback 前置知识（四）(pre-04)
// 编译：g++ -std=c++20 -Wall -Wextra 09_concepts_requires.cpp -o 09_concepts_requires

#include <concepts>
#include <type_traits>
#include <iostream>
#include <string>

// --- 自定义 concept ---

template<typename T>
concept Integral = std::is_integral_v<T>;

template<typename T>
    requires Integral<T>
void foo(T x) {
    std::cout << "  foo(" << x << "): T is integral\n";
}

// --- not_the_same_t concept ---

template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

// --- 模板构造函数劫持演示 ---

struct Wrapper {
    Wrapper() = default;

    template<typename T>
        requires (!std::same_as<std::decay_t<T>, Wrapper>)
    Wrapper(T&&) {
        std::cout << "  template constructor called\n";
    }

    Wrapper(Wrapper&&) noexcept {
        std::cout << "  move constructor called\n";
    }
};

int main() {
    std::cout << "=== 标准库 concepts ===\n";
    {
        static_assert(std::invocable<int(*)(int), int>);
        std::cout << "  int(*)(int) is invocable with int: OK\n";

        static_assert(std::same_as<int, int>);
        std::cout << "  same_as<int, int>: OK\n";

        static_assert(std::convertible_to<int, double>);
        std::cout << "  convertible_to<int, double>: OK\n";
    }

    std::cout << "\n=== 自定义 concept Integral ===\n";
    {
        foo(42);      // OK：int 是整数
        // foo(3.14);  // 编译错误：double 不满足 Integral
    }

    std::cout << "\n=== not_the_same_t concept ===\n";
    {
        static_assert(not_the_same_t<int, std::string>);
        static_assert(!not_the_same_t<int, int>);
        static_assert(!not_the_same_t<const int&, int>);  // decay_t 去掉引用和 const
        std::cout << "  not_the_same_t<int, string>: true\n";
        std::cout << "  not_the_same_t<int, int>: false\n";
        std::cout << "  not_the_same_t<const int&, int>: false (after decay)\n";
    }

    std::cout << "\n=== 模板构造函数 vs 移动构造函数 ===\n";
    {
        Wrapper a;
        std::cout << "  moving Wrapper:\n";
        Wrapper b = std::move(a);  // 应该调用移动构造函数，而非模板构造函数

        std::cout << "  constructing from int:\n";
        Wrapper c(42);  // 应该调用模板构造函数
    }

    return 0;
}
