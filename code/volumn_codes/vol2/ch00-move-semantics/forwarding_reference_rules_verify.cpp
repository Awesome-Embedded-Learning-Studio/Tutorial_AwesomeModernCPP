/// @file forwarding_reference_rules_verify.cpp
/// @brief 验证文章中关于万能引用/转发引用的关键技术规则
///
/// 验证项目：
///   1. const T&& 不是万能引用（拒绝左值）
///   2. std::vector<T>&& 不是万能引用（拒绝左值）
///   3. auto&& 是万能引用（根据实参推导值类别）
///   4. 引用折叠规则：T& && → T&, T&& && → T&&
///   5. std::forward 的自定义实现与标准库行为一致
///   6. 字符串字面量在万能引用中的推导类型
///
/// Standard: C++17
/// Compiler: g++ 13+, -std=c++17 -Wall -Wextra

#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================
// 验证 1: const T&& 不是万能引用
// ============================================================

/// @brief 接受 const T&& —— 这是 const 右值引用，不是万能引用
/// 它只能接受右值，不能接受左值
template<typename T>
void accept_const_rref(const T&&)
{
    std::cout << "  accept_const_rref: const T&& accepted (rvalue only)\n";
}

/// @brief 接受 T&& —— 这是万能引用
/// 能同时接受左值和右值
template<typename T>
void accept_universal(T&&)
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  accept_universal: T&& → lvalue ref (universal)\n";
    } else {
        std::cout << "  accept_universal: T&& → rvalue ref (universal)\n";
    }
}

// ============================================================
// 验证 2: std::vector<T>&& 不是万能引用
// ============================================================

template<typename T>
void accept_vector_rref(std::vector<T>&&)
{
    std::cout << "  accept_vector_rref: std::vector<T>&& accepted (rvalue only)\n";
}

// ============================================================
// 验证 3: auto&& 是万能引用
// ============================================================

void test_auto_universal()
{
    std::string name = "lvalue";

    // auto&& 绑定左值 → 左值引用
    auto&& ref1 = name;
    std::cout << "  auto&& with lvalue: "
              << (std::is_lvalue_reference_v<decltype(ref1)> ? "lvalue ref" : "rvalue ref")
              << "\n";

    // auto&& 绑定右值 → 右值引用
    auto&& ref2 = std::string("rvalue");
    std::cout << "  auto&& with rvalue: "
              << (std::is_lvalue_reference_v<decltype(ref2)> ? "lvalue ref" : "rvalue ref")
              << "\n";
}

// ============================================================
// 验证 4: 引用折叠规则
// ============================================================

void test_ref_collapsing()
{
    using T1 = int&;
    using T2 = int&&;

    // T1& → int& (already lvalue ref)
    static_assert(std::is_same_v<T1&, int&>, "T1& should be int&");
    // T1&& → int& (lvalue ref && → lvalue ref, collapsed)
    static_assert(std::is_same_v<T1&&, int&>, "T1&& should be int& (collapsed)");
    // T2& → int& (rvalue ref & → lvalue ref, collapsed)
    static_assert(std::is_same_v<T2&, int&>, "T2& should be int& (collapsed)");
    // T2&& → int&& (rvalue ref && → rvalue ref, no collapse needed)
    static_assert(std::is_same_v<T2&&, int&&>, "T2&& should be int&&");

    std::cout << "  All reference collapsing static_asserts passed.\n";
}

// ============================================================
// 验证 5: 自定义 my_forward 与 std::forward 行为一致
// ============================================================

template<typename T>
constexpr T&& my_forward(std::remove_reference_t<T>& t) noexcept
{
    return static_cast<T&&>(t);
}

template<typename T>
constexpr T&& my_forward(std::remove_reference_t<T>&& t) noexcept
{
    static_assert(!std::is_lvalue_reference_v<T>,
                  "Cannot forward an rvalue as an lvalue");
    return static_cast<T&&>(t);
}

void target(std::string& x) { std::cout << "  target(lvalue): " << x << "\n"; }
void target(std::string&& x) { std::cout << "  target(rvalue): " << x << "\n"; }

template<typename T>
void compare_forward(T&& x)
{
    // 使用 std::forward
    std::cout << "  std::forward → ";
    target(std::forward<T>(x));

    // 使用 my_forward
    std::cout << "  my_forward   → ";
    target(my_forward<T>(x));
}

// ============================================================
// 验证 6: 字符串字面量在万能引用中的推导类型
// ============================================================

template<typename T>
void show_literal_deduction(T&&)
{
    if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
        constexpr std::size_t N = std::extent_v<std::remove_reference_t<T>>;
        std::cout << "  T is lvalue ref to array of size " << N
                  << " (const char (&)[" << N << "])\n";
    } else if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  T is lvalue ref (not array)\n";
    } else {
        std::cout << "  T is non-ref type\n";
    }
}

// ============================================================
// main
// ============================================================

int main()
{
    std::cout << "========================================\n"
              << "验证 1: const T&& 不是万能引用\n"
              << "========================================\n";
    accept_universal(std::string("rvalue"));  // OK
    accept_const_rref(std::string("rvalue")); // OK
    std::string lv = "lvalue";
    accept_universal(lv);                     // OK: 万能引用接受左值
    // accept_const_rref(lv);                 // 编译错误: const T&& 拒绝左值
    std::cout << "  (accept_const_rref(lv) 已注释，编译不通过)\n\n";

    std::cout << "========================================\n"
              << "验证 2: std::vector<T>&& 不是万能引用\n"
              << "========================================\n";
    accept_vector_rref(std::vector<int>{1, 2, 3});
    std::vector<int> vec = {4, 5, 6};
    // accept_vector_rref(vec);               // 编译错误: 不是万能引用
    std::cout << "  (accept_vector_rref(vec) 已注释，编译不通过)\n\n";

    std::cout << "========================================\n"
              << "验证 3: auto&& 是万能引用\n"
              << "========================================\n";
    test_auto_universal();
    std::cout << "\n";

    std::cout << "========================================\n"
              << "验证 4: 引用折叠规则\n"
              << "========================================\n";
    test_ref_collapsing();
    std::cout << "\n";

    std::cout << "========================================\n"
              << "验证 5: my_forward vs std::forward\n"
              << "========================================\n";
    std::string s1 = "hello";
    std::cout << "左值测试:\n";
    compare_forward(s1);
    std::cout << "右值测试:\n";
    compare_forward(std::string("world"));
    std::cout << "\n";

    std::cout << "========================================\n"
              << "验证 6: 字符串字面量推导类型\n"
              << "========================================\n";
    std::cout << "传入 \"first\" (6 chars including NUL):\n";
    show_literal_deduction("first");
    std::cout << "传入 100 (int literal):\n";
    show_literal_deduction(100);
    std::cout << "\n";

    std::cout << "=== 全部验证通过 ===\n";
    return 0;
}
