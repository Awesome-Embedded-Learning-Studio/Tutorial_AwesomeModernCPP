// 配套 08-templates-and-exception-safety.md「条件 noexcept:把底层会不会抛如实传出去」
// 对比「没标 noexcept」和「条件 noexcept」在调用方眼里的 noexcept 性
// 编译运行:g++ -std=c++20 -Wall -Wextra noexcept_propagation.cpp -o nep && ./nep
#include <iostream>
#include <utility>

struct NoThrowMove {
    NoThrowMove() = default;
    NoThrowMove(NoThrowMove&&) noexcept {} // 显式 noexcept
    NoThrowMove(const NoThrowMove&) = default;
    NoThrowMove& operator=(NoThrowMove&&) noexcept { return *this; }
};

struct ThrowMove {
    ThrowMove() = default;
    ThrowMove(ThrowMove&&) {} // 没标,隐式可能抛
    ThrowMove(const ThrowMove&) = default;
    ThrowMove& operator=(ThrowMove&&) { return *this; }
};

// 没标 noexcept:调用方只能保守地认为它可能抛
template <typename T> void uncond_op(T& x) {
    T tmp(std::move(x));
    x = std::move(tmp);
}

// 条件 noexcept:继承「T 的移动构造是否 noexcept」
template <typename T> void cond_op(T& x) noexcept(noexcept(T(std::move(x)))) {
    T tmp(std::move(x));
    x = std::move(tmp);
}

int main() {
    std::cout << std::boolalpha;
    std::cout << "uncond_op<NoThrowMove> noexcept: "
              << noexcept(uncond_op(std::declval<NoThrowMove&>())) << "\n";
    std::cout << "cond_op<NoThrowMove> noexcept:   "
              << noexcept(cond_op(std::declval<NoThrowMove&>())) << "\n";
    std::cout << "cond_op<ThrowMove> noexcept:     "
              << noexcept(cond_op(std::declval<ThrowMove&>())) << "\n";
}
