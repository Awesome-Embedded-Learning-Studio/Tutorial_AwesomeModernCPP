/*
 * 验证：x86 上 compare_exchange_weak 和 compare_exchange_strong
 *       生成相同的汇编指令
 *
 * 背景：文章声称 x86 上 weak 和 strong 生成的代码完全相同，
 *       因为 x86 使用硬件 CMPXCHG 指令，不存在虚假失败。
 *
 * 预期结果：两个函数生成相同的 lock cmpxchgl 指令
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -S -o /tmp/atomic_cas_weak_strong_asm.s atomic_cas_weak_strong_asm.cpp
 *
 * 检查：
 *   grep -A3 "test_weak\|test_strong" /tmp/atomic_cas_weak_strong_asm.s
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange
 *
 * 编译器：GCC 16.1.1
 */

#include <atomic>

std::atomic<int> x{0};

bool test_weak(int& expected, int desired) {
    return x.compare_exchange_weak(expected, desired);
}

bool test_strong(int& expected, int desired) {
    return x.compare_exchange_strong(expected, desired);
}
