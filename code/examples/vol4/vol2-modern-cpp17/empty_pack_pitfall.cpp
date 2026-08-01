// 配套 02-variadic-templates.md「空包的坑」
// 默认演示:if constexpr 终止的递归能处理空包(能编过)
// 加 -DEMPTY_FOLD 复现:一元 fold 对空包是 ill-formed,编译器报错
// 编译运行:g++ -std=c++17 -Wall -Wextra empty_pack_pitfall.cpp -o epp && ./epp
// 复现报错:g++ -std=c++17 -Wall -Wextra -DEMPTY_FOLD empty_pack_pitfall.cpp -o epp
#include <iostream>

namespace detail {
template <typename T, typename... Rest> constexpr auto sum_ifc_impl(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;
    } else {
        return first + sum_ifc_impl(rest...);
    }
}
} // namespace detail

// if constexpr 终止:空包走 sizeof...(args)==0 分支,根本不碰 fold,所以能过
template <typename... Args> constexpr auto sum_ifc(Args... args) {
    if constexpr (sizeof...(args) == 0) {
        return 0;
    } else {
        return detail::sum_ifc_impl(args...);
    }
}

#ifdef EMPTY_FOLD
// 一元 fold 对空包:标准规定只有 &&、||、逗号 三种运算符有默认值
// 别的运算符(包括 +)对空包是 ill-formed
template <typename... Ts> constexpr auto sum_fold(Ts... ts) {
    return (ts + ...); // 空包调用时会编译失败
}

int main() {
    return sum_fold(); // 触发空包 fold
}
#else
int main() {
    std::cout << "sum_ifc():      " << sum_ifc() << "   (空包走 if constexpr 终止分支)\n";
    std::cout << "sum_ifc(1,2,3): " << sum_ifc(1, 2, 3) << "\n";
    static_assert(sum_ifc() == 0);
    static_assert(sum_ifc(1, 2, 3) == 6);
    std::cout << "\n默认演示通过。加 -DEMPTY_FOLD 复现空包 fold 的编译报错。\n";
}
#endif
