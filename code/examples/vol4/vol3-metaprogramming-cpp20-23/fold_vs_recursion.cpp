// 配套 04-tmp-core-techniques.md「fold expressions:干掉递归样板」
// 对照 variadic 递归老办法和 C++17 fold expression 的新写法
// 编译运行:g++ -std=c++20 -Wall -Wextra fold_vs_recursion.cpp -o fvr && ./fvr
#include <iostream>

// 老办法:variadic 模板递归 + 终止函数,算任意数量参数之和
template <typename T> constexpr T sum_rec(T first) {
    return first;
}

template <typename T, typename... Rest> constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);
}

// C++17 fold expression:一行收掉上面那一坨
template <typename... Ts> constexpr auto sum_fold(Ts... ts) {
    return (ts + ...); // 一元右折叠
}

int main() {
    std::cout << "sum_rec(1,2,3,4):  " << sum_rec(1, 2, 3, 4) << "\n";
    std::cout << "sum_fold(1,2,3,4): " << sum_fold(1, 2, 3, 4) << "\n";

    static_assert(sum_fold(1, 2, 3, 4) == 10);
    static_assert(sum_fold(1, 2, 3, 4, 5, 6) == 21);

    // fold 还能就地展开,把一堆值「打印」出来(逗号折叠)
    std::cout << "逗号折叠展开: ";
    auto printer = [](auto x) { std::cout << x << " "; };
    (printer(1), printer(2.5), printer("hi"));
    std::cout << "\n";
}
