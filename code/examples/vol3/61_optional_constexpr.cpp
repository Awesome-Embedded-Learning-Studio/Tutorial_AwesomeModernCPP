// Standard: C++20
// C++20 起 optional 的绝大多数操作（构造/emplace/reset/value_or/operator*）都是 constexpr，
// 能在编译期求值、塞进 static_assert——模板元编程/编译期查表/consteval 里
// 「可能没值的盒子」也能用，不用再手搓 union。
#include <iostream>
#include <optional>

constexpr int compute() {
    std::optional<int> o;
    o.emplace(7);
    int v = *o;
    o.reset();
    return v + 35; // 42
}

int main() {
    static_assert(compute() == 42); // 编译期就定下来
    constexpr std::optional<int> empty;
    static_assert(empty.value_or(99) == 99); // value_or 也 constexpr

    std::cout << "constexpr compute() = " << compute() << '\n';
    std::cout << "empty.value_or(99) = " << empty.value_or(99) << '\n';
    std::cout << "C++20 constexpr optional: OK\n";
    return 0;
}
