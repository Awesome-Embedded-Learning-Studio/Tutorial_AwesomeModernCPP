// Test: decltype 括号规则验证
// 验证 decltype(x) 和 decltype((x)) 的区别

#include <iostream>
#include <type_traits>

int x = 42;

void test_decltype_parens_difference() {
    decltype(x) a = 100;      // 应该是 int
    decltype((x)) b = x;      // 应该是 int&

    static_assert(std::is_same_v<decltype(a), int>, "decltype(x) 应该是 int");
    static_assert(std::is_same_v<decltype(b), int&>, "decltype((x)) 应该是 int&");

    std::cout << "decltype(x) is int: " << std::is_same_v<decltype(x), int> << "\n";
    std::cout << "decltype((x)) is int&: " << std::is_same_v<decltype((x)), int&> << "\n";

    // 验证：b 是引用，修改 b 会影响 x
    b = 200;
    std::cout << "x after modifying b: " << x << " (should be 200)\n";
}

int main() {
    test_decltype_parens_difference();
    return 0;
}
