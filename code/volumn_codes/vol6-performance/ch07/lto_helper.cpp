// lto_helper.cpp — vol6 ch07-02 LTO 跨翻译单元内联演示(配合 lto_main.cpp)
// helper 函数在另一个 TU,无 LTO 时编译器看不见实现,不能内联;有 LTO 能。
int helper(int x) {
    // 一个能被内联后进一步优化的函数(内联后常量传播/消除)
    int s = 0;
    for (int i = 0; i < 10; ++i)
        s += x * i;
    return s + 1;
}
