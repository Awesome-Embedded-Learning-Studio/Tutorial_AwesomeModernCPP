/*
 * 验证：x86 上 atomic_thread_fence 各级别的实际生成指令
 *
 * 背景：文章声称 acquire/release/acq_rel fence 不生成 CPU 指令，
 *       seq_cst fence 生成全向屏障指令。
 *
 * 实际结果（GCC 16.1.1, -O2）：
 *   relaxed  → 无 CPU 指令（no-op）
 *   acquire  → 无 CPU 指令（x86 TSO 自动保证）
 *   release  → 无 CPU 指令（x86 TSO 自动保证）
 *   acq_rel  → 无额外 CPU 指令
 *   seq_cst  → lock orq $0, (%rsp)（等价于 mfence 的全向屏障）
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -S -o /tmp/fence_x86_instructions.s fence_x86_instructions.cpp
 *
 * 检查：
 *   grep -E "mfence|lock" /tmp/fence_x86_instructions.s
 *
 * 编译器：GCC 16.1.1
 */

#include <atomic>

std::atomic<int> flag{0};
int data = 0;

void fence_relaxed_producer() {
    data = 42;
    std::atomic_thread_fence(std::memory_order_relaxed);
    flag.store(1, std::memory_order_relaxed);
}

void fence_acquire_consumer() {
    while (flag.load(std::memory_order_relaxed) != 1) {
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    int local = data;
    (void)local;
}

void fence_release_producer() {
    data = 42;
    std::atomic_thread_fence(std::memory_order_release);
    flag.store(1, std::memory_order_relaxed);
}

void fence_seq_cst_producer() {
    data = 42;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    flag.store(1, std::memory_order_relaxed);
}
