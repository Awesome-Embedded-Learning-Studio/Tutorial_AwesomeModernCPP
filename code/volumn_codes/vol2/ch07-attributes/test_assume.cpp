// 验证代码：[[assume]] 的优化效果
// 验证编译器是否真的基于假设优化

#if __cplusplus >= 202302L

// 有 assume
int divide_with_assume(int a, int b) {
    [[assume(b != 0)]];
    return a / b;
}

// 无 assume
int divide_no_assume(int a, int b) {
    return a / b;
}

/*
编译命令（需要支持 C++23 的编译器）：
g++ -std=c++23 -O2 -S test_assume.cpp -o test_assume.s

测试结果（GCC 15.2.1, -O2）：
_Z18divide_with_assumeii:
    movl    %edi, %eax
    cltd
    idivl   %esi
    ret

_Z16divide_no_assumeii:
    movl    %edi, %eax
    cltd
    idivl   %esi
    ret

结论：在这个简单的除法例子中，两个函数生成的汇编完全相同。
说明对于这种简单的操作，编译器在 -O2 下已经做了足够的优化，
[[assume]] 没有带来额外的优化效果。

注意：文章中的警告是正确的 - [[assume]] 是最危险的属性。
如果假设错误，行为是未定义的。在这个简单测试中看不出
差异，但在更复杂的场景中可能会有不同的优化结果。
*/

#else

// C++23 之前的替代方案：__builtin_assume 或 __builtin_unreachable
int divide_with_builtin(int a, int b) {
    if (b == 0) __builtin_unreachable();
    return a / b;
}

int divide_normal(int a, int b) {
    return a / b;
}

/*
对于 C++17/C++20，可以使用 __builtin_unreachable() 达到类似效果。
但 [[assume]] 是 C++23 的标准化语法。
*/

#endif
