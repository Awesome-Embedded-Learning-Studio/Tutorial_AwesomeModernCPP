// 验证代码：[[noreturn]] 的优化效果
// 验证编译器是否真的优化了不可达代码

[[noreturn]] void fatal_error() {
    __builtin_unreachable();
}

int check_value(int x) {
    if (x < 0) {
        fatal_error();
        // 这里的代码应该被优化掉
        return -1;  // 不可达
    }
    return x * 2;
}

// 对比版本（无 noreturn）
void normal_error() {
    __builtin_unreachable();
}

int check_value_no_hint(int x) {
    if (x < 0) {
        normal_error();
        return -1;  // 可能不会被优化掉
    }
    return x * 2;
}

/*
编译命令：
g++ -std=c++17 -O2 -S test_noreturn.cpp -o test_noreturn.s

测试结果（GCC 15.2.1, -O2）：
check_value:        leal    (%rdi,%rdi), %eax
                    ret
check_value_no_hint: leal    (%rdi,%rdi), %eax
                    ret

两个函数生成的汇编完全相同，编译器都优化掉了不可达代码。
说明在这个简单例子中，即使没有 [[noreturn]]，编译器也能推断。

结论：文章中关于 [[noreturn]] 优化效果的描述在概念上是正确的，
但现代编译器的优化能力很强，即使没有提示也能做类似推断。
*/
