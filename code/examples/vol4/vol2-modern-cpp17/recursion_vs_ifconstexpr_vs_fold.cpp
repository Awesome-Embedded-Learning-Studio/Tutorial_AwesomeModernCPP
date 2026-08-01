// 配套 02-variadic-templates.md「三种展开写法对照」
// 同一个求和需求的三种写法:模板递归+终止重载 / if constexpr 终止 / fold,结果一致
// 编译运行:g++ -std=c++17 -Wall -Wextra recursion_vs_ifconstexpr_vs_fold.cpp -o rcf && ./rcf
#include <iostream>

// 写法一:老办法——主模板递归 + 终止重载(C++11 风格)
// 两个重载:单参数的终止版,多参数的递归版;编译器按参数个数挑选
template <typename T> constexpr T sum_rec(T first) {
    return first; // 终止:只剩一个参数,直接返回
}

template <typename T, typename... Rest> constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...); // 剥掉第一个,对剩下的递归
}

// 写法二:if constexpr 终止,一个函数体搞定(C++17)
// 需要拆首尾:helper 处理「至少一个参数」的情况,外层处理空包
namespace detail {
template <typename T, typename... Rest> constexpr auto sum_ifc_impl(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first; // 编译期分支:剩下零个就终止
    } else {
        return first + sum_ifc_impl(rest...); // 否则继续递归
    }
}
} // namespace detail

template <typename... Args> constexpr auto sum_ifc(Args... args) {
    if constexpr (sizeof...(args) == 0) {
        return 0; // 空包:if constexpr 跳过,不触发任何 fold
    } else {
        return detail::sum_ifc_impl(args...);
    }
}

// 写法三:fold(C++17),一行收掉
template <typename... Ts> constexpr auto sum_fold(Ts... ts) {
    return (ts + ...); // 一元右折叠
}

int main() {
    std::cout << "sum_rec(1,2,3,4,5):  " << sum_rec(1, 2, 3, 4, 5) << "\n";
    std::cout << "sum_ifc(1,2,3,4,5):  " << sum_ifc(1, 2, 3, 4, 5) << "\n";
    std::cout << "sum_fold(1,2,3,4,5): " << sum_fold(1, 2, 3, 4, 5) << "\n";

    std::cout << "\n空包处理:\n";
    std::cout << "  sum_ifc(): " << sum_ifc() << "   (if constexpr 终止,空包返回 0)\n";
    // sum_fold() 和 sum_rec() 对空包都不好使:fold 会编译失败(见 empty_pack_pitfall.cpp)

    // 编译期断言:三种写法结果一致
    static_assert(sum_rec(1, 2, 3, 4, 5) == 15);
    static_assert(sum_ifc(1, 2, 3, 4, 5) == 15);
    static_assert(sum_fold(1, 2, 3, 4, 5) == 15);
    std::cout << "\nstatic_assert 全过:三种写法结果一致\n";
}
