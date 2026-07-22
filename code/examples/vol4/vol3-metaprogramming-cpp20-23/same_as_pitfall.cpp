// same_as 的两个坑:same_as<int,const int> 为 false(cv 限定让它们成为不同类型);concept 当 is_same
// 误用 对应文章:01-concepts.md、02-constraining-templates.md 编译运行:g++ -Wall -Wextra -std=c++20
// same_as_pitfall.cpp -o sas && ./sas
#include <concepts>
#include <iostream>
#include <type_traits>

// 坑二:concept 当 is_same 用,把 T 锁死成 int
// 能跑,但这种「锁死成具体类型」通常不如直接写非模板函数 void only_int(int) 清楚
template <std::same_as<int> T> void only_int(T x) {
    std::cout << "got int: " << x << "\n";
}
// same_as 真正的用武之地是约束两个参数的关系:
// template <typename A, typename B> requires std::same_as<A, B>

int main() {
    std::cout << std::boolalpha;

    // 坑一:const 限定让 int 和 const int 成为不同类型,same_as 返回 false
    std::cout << "same_as<int, const int>:                 "
              << std::same_as<int, const int> << "\n"; // false
    // 想判断「剥掉 cv/引用后是否相同」,先用 remove_cvref_t
    std::cout << "same_as<int, remove_cvref_t<const int>>: "
              << std::same_as<int, std::remove_cvref_t<const int>> << "\n"; // true

    // 坑二演示:only_int 只收 int
    only_int(42);
    // only_int(3.14);   // 编译失败:double 不满足 same_as<int>
    return 0;
}
