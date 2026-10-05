// GCC 对 constexpr 递归深度的默认限制是 512(-fconstexpr-depth 的默认值)
// 单独编译时:linear_recursive(511) 能过,512 就报错
#include <iostream>

constexpr int linear_recursive(int n) {
    return n <= 0 ? 0 : 1 + linear_recursive(n - 1);
}

// C++14 起可以写成循环:递归深度恒为 1,600 层的量也轻松通过
constexpr int linear_loop(int n) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
        ++total;
    }
    return total;
}

int main() {
    // 试试把 511 改成 512:点「运行」,结果区直接给出编译器的报错原文
    constexpr int kDepth = 511;

    // 进阶:解注释下面这行浅层求值,再把 kDepth 改成 600 ——
    // GCC 按函数缓存已算过的层,新的展开只有 500 层,又能过了
    // constexpr int kWarm = linear_recursive(100);

    constexpr int kRecursion = linear_recursive(kDepth);
    std::cout << "Recursion depth " << kDepth << ": " << kRecursion << " (OK)\n";

    constexpr int kLoop600 = linear_loop(600);
    std::cout << "Loop 600: " << kLoop600 << " (OK)\n";
    return 0;
}
