// Test: auto 推导规则验证
// 验证 auto 丢弃引用和顶层 const 的行为

#include <iostream>
#include <type_traits>

int& get_ref() {
    static int x = 42;
    return x;
}

void test_auto_discards_reference() {
    auto a = get_ref();      // 应该推导为 int（丢弃引用）
    auto& b = get_ref();     // 应该推导为 int&（保留引用）

    static_assert(std::is_same_v<decltype(a), int>, "auto 应该丢弃引用");
    static_assert(std::is_same_v<decltype(b), int&>, "auto& 应该保留引用");

    // 验证：修改 a 不会影响原值
    a = 100;
    std::cout << "get_ref() after modifying a: " << get_ref() << " (should be 42)\n";

    // 验证：修改 b 会影响原值
    b = 200;
    std::cout << "get_ref() after modifying b: " << get_ref() << " (should be 200)\n";
}

void test_auto_discards_top_const() {
    const int ci = 42;
    auto a = ci;      // 应该推导为 int（丢弃顶层 const）

    static_assert(std::is_same_v<decltype(a), int>, "auto 应该丢弃顶层 const");
    a = 100;          // 应该能编译（a 不是 const）
    std::cout << "auto 丢弃顶层 const: a = " << a << "\n";
}

void test_auto_preserves_bottom_const() {
    const int* p = nullptr;   // 底层 const
    auto q = p;               // 应该保留底层 const

    static_assert(std::is_same_v<decltype(q), const int*>, "auto 应该保留底层 const");
    std::cout << "auto 保留底层 const\n";
}

int main() {
    test_auto_discards_reference();
    test_auto_discards_top_const();
    test_auto_preserves_bottom_const();
    return 0;
}
