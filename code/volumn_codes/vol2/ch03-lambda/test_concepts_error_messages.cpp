/**
 * @file test_concepts_error_messages.cpp
 * @brief 验证 Concepts 与 SFINAE 的错误信息质量差异
 *
 * 编译环境: g++ (GCC) 15.2.1, -std=c++20
 *
 * 运行方式:
 * 1. 正常编译: g++ -std=c++20 test_concepts_error_messages.cpp -o test
 * 2. 触发错误: 取消注释 main() 中的错误调用，重新编译查看错误信息
 */

#include <concepts>
#include <iostream>
#include <type_traits>

/**
 * @brief 使用 Concepts 约束的 lambda
 *
 * 当传入不满足 std::integral 的类型时，编译器会清晰地提示：
 * "constraints not satisfied" 和 "integral<T> evaluated to false"
 */
auto int_only_concepts = []<std::integral T>(T a, T b) {
    return a + b;
};

/**
 * @brief 使用 static_assert 的传统方式
 *
 * 当传入不满足条件的类型时，编译器会提示：
 * "static_assert failed" 和自定义消息
 */
template<typename T>
auto int_only_static_assert(T a, T b) -> decltype(a + b) {
    static_assert(std::is_integral_v<T>, "T must be an integral type");
    return a + b;
};

/**
 * @brief 使用 SFINAE 的传统方式（enable_if）
 *
 * 当传入不满足条件的类型时，编译器会输出：
 * - "no matching function for call to ..."
 * - 大量的模板实例化堆栈
 * - 难以定位真正的问题
 */
template<typename T,
         std::enable_if_t<std::is_integral_v<T>, int> = 0>
auto int_only_sfinae(T a, T b) {
    return a + b;
};

/**
 * @brief 展示 Concepts 的优势
 *
 * Concepts 不仅提供更好的错误信息，还支持：
 * 1. 概念组合（requires 子句）
 * 2. 部分约束（某些重载满足概念，某些不满足）
 * 3. 更好的重载决议
 */
void demo_concepts_advantages() {
    std::cout << "=== Concepts Error Messages Demo ===\n\n";

    // 正常调用
    std::cout << "int_only_concepts(1, 2) = " << int_only_concepts(1, 2) << "\n";
    std::cout << "int_only_static_assert(1, 2) = " << int_only_static_assert(1, 2) << "\n";
    std::cout << "int_only_sfinae(1, 2) = " << int_only_sfinae(1, 2) << "\n";

    std::cout << "\n=== Try uncommenting the following to see error messages ===\n";
    std::cout << "// int_only_concepts(1.0, 2.0);   // Concepts error: clear\n";
    std::cout << "// int_only_static_assert(1.0, 2.0);   // static_assert error: custom message\n";
    std::cout << "// int_only_sfinae(1.0, 2.0);   // SFINAE error: verbose template stack\n";
}

/**
 * @brief 实际场景：只接受支持流输出的类型
 */
template<typename T>
concept Streamable = requires(T t, std::ostream& os) {
    { os << t } -> std::same_as<std::ostream&>;
};

auto print_streamable = []<Streamable T>(const T& obj) {
    std::cout << "Object: " << obj << "\n";
};

void demo_streamable() {
    print_streamable(42);        // OK: int supports <<
    print_streamable(3.14);      // OK: double supports <<

    // print_streamable(std::vector<int>{});  // Error: vector doesn't support <<
}

int main() {
    demo_concepts_advantages();
    std::cout << "\n";
    demo_streamable();

    // 取消注释以下行来查看不同的错误信息：
    // int_only_concepts(1.0, 2.0);
    // int_only_static_assert(1.0, 2.0);
    // int_only_sfinae(1.0, 2.0);

    return 0;
}

/**
 * @section 错误信息对比
 *
 * **Concepts 错误信息**（清晰）:
 * ```
 * error: no match for call to '(<lambda(T, T)>) (double, double)'
 * note: constraints not satisfied
 * note: the expression 'is_integral_v<double>' evaluated to false
 * ```
 *
 * **static_assert 错误信息**（中等）:
 * ```
 * error: static_assert failed
 * note: "T must be an integral type"
 * ```
 *
 * **SFINAE 错误信息**（冗长）:
 * ```
 * error: no matching function for call to 'int_only_sfinae'
 * note: candidate: template<class T, typename std::enable_if<...>::type> auto int_only_sfinae(T, T)
 * note: template argument deduction/substitution failed [with T = double]
 * [数十行模板实例化堆栈...]
 * ```
 */
