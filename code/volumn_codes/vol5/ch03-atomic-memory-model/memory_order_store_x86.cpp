/*
 * 验证：x86 上 seq_cst store 生成的指令
 *
 * 背景：文章声称 seq_cst store 在 x86 上使用 MFENCE 或 LOCK XCHG。
 *
 * 实际结果（GCC 16.1.1, -O2）：
 *   relaxed  → movl（普通 store）
 *   release  → movl（x86 TSO 自动保证 release 语义）
 *   seq_cst  → xchgl（隐式 LOCK，全向屏障）
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -S -o /tmp/memory_order_store_x86.s memory_order_store_x86.cpp
 *
 * 检查：
 *   grep -E "store_|movl|xchg|mfence" /tmp/memory_order_store_x86.s
 *
 * 编译器：GCC 16.1.1
 */

#include <atomic>

std::atomic<int> x{0};

void store_relaxed(int v) {
    x.store(v, std::memory_order_relaxed);
}

void store_release(int v) {
    x.store(v, std::memory_order_release);
}

void store_seq_cst(int v) {
    x.store(v, std::memory_order_seq_cst);
}
