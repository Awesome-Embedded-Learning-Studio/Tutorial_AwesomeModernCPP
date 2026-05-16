/*
 * 验证：多参数 concept 的正确使用方式
 *
 * 背景：文章原稿中 CanAdd auto a 试图将双参数 concept 用于单参数占位符约束，
 *       这在 C++20 中是编译错误——constrained auto 只对单参数 concept 有效。
 *       双参数 concept 必须用显式 requires 子句。
 *
 * 预期结果：编译通过，输出 "a + b = 30" 和 "a + b = 13.14"
 *
 * 编译命令：
 *   g++ -std=c++20 -Wall -Wextra -o /tmp/08-03-concept-param-demo 08-03-concept-param-demo.cpp
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/language/constraints
 *
 * 编译器：GCC 16.1.1
 */

#include <iostream>
#include <concepts>
#include <type_traits>

// 单参数 concept：接受一个类型参数
template<typename T>
concept HasSize = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

// 双参数 concept：表达跨类型约束
template<typename T, typename U>
concept CanAdd = requires(T a, U b) {
    a + b;
};

// 值参数 concept：sizeof 在编译期求值，始终合法
template<typename T, std::size_t N>
concept IsLargeType = (sizeof(T) >= N);

void test_single_param(HasSize auto& container) {
    std::cout << "size = " << container.size() << "\n";
}

// 双参数 concept 必须用显式 requires 子句
template<typename T, typename U>
    requires CanAdd<T, U>
void test_cross_add(T a, U b) {
    auto result = a + b;
    std::cout << "a + b = " << result << "\n";
}

int main() {
    std::string s = "hello";
    test_single_param(s);

    test_cross_add(10, 20);
    test_cross_add(10, 3.14);

    static_assert(IsLargeType<double, 4>);
    static_assert(!IsLargeType<char, 4>);

    return 0;
}
