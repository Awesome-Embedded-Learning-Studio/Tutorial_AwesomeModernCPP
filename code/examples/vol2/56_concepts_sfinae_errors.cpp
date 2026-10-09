// 三种「只允许整数」的约束写法,传错类型时的报错质量天差地别。
// 默认能编译通过;把 main 里被注释的错误调用逐个放开、点「运行」,
// 结果区会给出编译器报错原文——三种写法各放一次,放在一起比。
#include <concepts>
#include <iostream>
#include <type_traits>

// 写法一:C++20 concepts 约束的模板 lambda
// 传错类型,编译器直说「约束不满足」,还指明是哪个概念挂了
auto int_only_concepts = []<std::integral T>(T a, T b) { return a + b; };

// 写法二:函数体内的 static_assert
// 报错带您自己写的消息,但要等实例化到函数体内才爆
template <typename T> auto int_only_assert(T a, T b) -> decltype(a + b) {
    static_assert(std::is_integral_v<T>, "T must be an integral type");
    return a + b;
}

// 写法三:SFINAE(enable_if)
// 传错类型,报错是「没有匹配的函数」外加一串候选与模板堆栈
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
auto int_only_sfinae(T a, T b) {
    return a + b;
}

int main() {
    std::cout << int_only_concepts(1, 2) << "\n";
    std::cout << int_only_assert(3, 4) << "\n";
    std::cout << int_only_sfinae(5, 6) << "\n";

    // 逐个放开(一次放一个,报完再换下一个),对比三种报错:
    // std::cout << int_only_concepts(1.5, 2.5) << "\n";  // constraints not satisfied
    // std::cout << int_only_assert(1.5, 2.5) << "\n";    // static_assert failed
    // std::cout << int_only_sfinae(1.5, 2.5) << "\n";    // no matching function
    return 0;
}
