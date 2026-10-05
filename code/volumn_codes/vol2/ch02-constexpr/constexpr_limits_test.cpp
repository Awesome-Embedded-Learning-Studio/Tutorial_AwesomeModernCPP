/**
 * @file constexpr_limits_test.cpp
 * @brief 验证 constexpr 求值限制的测试程序（配合 01-constexpr-basics.md）
 *
 * 编译环境：g++ 16.2.1（本地）与 Compiler Explorer g152（GCC 15.2）实测行为一致，-std=c++17
 *
 * 实测口径（单独编译 linear_recursive(N)，前面不带任何垫背求值）：
 *   N=511 通过；N=512 报错：
 *   error: 'constexpr' evaluation depth exceeds maximum of 512
 *         (use '-fconstexpr-depth=' to increase the maximum)
 *
 * 注意：本程序是"由浅到深"的顺序求值。GCC 按函数缓存已算过的层，
 * 缓存命中的层不再计入新深度，所以 512/520/600 在这里全部通过。
 * Clang 没有这层缓存，本程序在 Clang 下 kDepth512 处就地报错——这本身就是两家实现的差异。
 * 要亲眼看 GCC 的 512 报错，去在线 demo（code/examples/vol2/51_constexpr_depth_limit.cpp）
 * 把 kDepth 改成 512 单独触发。
 */

#include <iostream>

// 线性递归（每次减 1）：递归深度就等于参数本身
constexpr int linear_recursive(int n) {
    return n <= 0 ? 0 : 1 + linear_recursive(n - 1);
}

// 斐波那契递归：调用树很大，但深度只有 n
constexpr int fib_recursive(int n) {
    return n <= 1 ? n : fib_recursive(n - 1) + fib_recursive(n - 2);
}

int main() {
    std::cout << "Sequential evaluation (shallow to deep):\n\n";

    // 由浅到深顺序求值：全部通过——已算过的浅层在缓存里，不计入新深度
    constexpr int kDepth100 = linear_recursive(100);
    constexpr int kDepth256 = linear_recursive(256);
    constexpr int kDepth512 = linear_recursive(512);
    constexpr int kDepth520 = linear_recursive(520);
    constexpr int kDepth600 = linear_recursive(600);

    std::cout << "Depth 100: " << kDepth100 << " (OK)\n";
    std::cout << "Depth 256: " << kDepth256 << " (OK)\n";
    std::cout << "Depth 512: " << kDepth512 << " (OK, cache hit)\n";
    std::cout << "Depth 520: " << kDepth520 << " (OK, cache hit)\n";
    std::cout << "Depth 600: " << kDepth600 << " (OK, cache hit)\n";

    // 斐波那契：深度浅，但朴素递归的总运算量是指数级的
    constexpr int kFib20 = fib_recursive(20);
    constexpr int kFib30 = fib_recursive(30);

    std::cout << "\nFibonacci tests:\n";
    std::cout << "Fib(20): " << kFib20 << " (OK)\n";
    std::cout << "Fib(30): " << kFib30 << " (OK)\n";

    // fib(50) 撞不上深度限制，但会先撞总步数限制（-fconstexpr-ops-limit，默认 33554432）：
    // constexpr int kFib50 = fib_recursive(50);
    //   error: constexpr evaluation operation count exceeds limit of 33554432

    std::cout << "\n结论：\n";
    std::cout << "- GCC 的 constexpr 递归深度默认上限是 512（-fconstexpr-depth）\n";
    std::cout << "- 本程序 512/520/600 都能过：GCC 按函数缓存求值结果，\n";
    std::cout << "  顺序测试里已算过的浅层不计入新深度\n";
    std::cout << "- 测真实上限要单独编译，别拿顺序测试的结果当数\n";

    return 0;
}
