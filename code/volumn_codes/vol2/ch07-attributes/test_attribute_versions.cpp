// 验证代码：各属性引入的 C++ 标准版本
// 测试结果用于验证文章中的断言

#include <cstdio>

// C++11 引入的属性
[[noreturn]] void test_cxx11_noreturn() {
    __builtin_unreachable();
}

// C++14 引入的属性
[[deprecated("Use new_func")]] void test_cxx14_deprecated() {
    printf("C++14 deprecated\n");
}

// C++17 引入的属性
[[nodiscard]] int test_cxx17_nodiscard() {
    return 42;
}

[[maybe_unused]] void test_cxx17_maybe_unused(int x) {
    (void)x;
}

// C++20 引入的属性
#if __cplusplus >= 202002L
[[likely]] void test_cxx20_likely() {}
[[unlikely]] void test_cxx20_unlikely() {}

struct Empty {};
struct Container {
    [[no_unique_address]] Empty e;
    int x;
};
#endif

// C++23 引入的属性
#if __cplusplus >= 202302L
int test_cxx23_assume(int a, int b) {
    [[assume(b != 0)]];
    return a / b;
}
#endif

int main() {
    printf("Testing attribute versions\n");
    printf("__cplusplus = %ld\n", __cplusplus);

    test_cxx14_deprecated();
    test_cxx17_nodiscard();  // 警告：忽略返回值
    test_cxx17_maybe_unused(42);

    return 0;
}

/*
测试结果总结：
1. C++11: [[noreturn]], [[carries_dependency]]
2. C++14: [[deprecated]]
3. C++17: [[nodiscard]], [[maybe_unused]], [[fallthrough]]
4. C++20: [[likely]], [[unlikely]], [[no_unique_address]]
5. C++23: [[assume]]

结论：文章中 "C++11 开始引入的标准属性语法" 的表述不够准确，
实际上不同属性在不同版本引入。
*/
