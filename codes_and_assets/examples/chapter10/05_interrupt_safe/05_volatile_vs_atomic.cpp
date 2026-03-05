// volatile vs atomic - 演示两者的区别和正确使用场景
// 本示例揭示了 volatile 的局限性以及 atomic 的必要性

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

// ============================================================================
// 第一部分：volatile 不保证原子性
// ============================================================================

namespace volatile_not_atomic {
    // 演示 1：volatile 不保证原子操作
    volatile int counter = 0;

    void increment() {
        counter++;  // 这仍然是 读-改-写 三个步骤！
    }

    void demonstrate_race() {
        printf("\n=== 演示 1: volatile 不保证原子性 ===\n");
        printf("两个线程同时++一个 volatile int，结果会怎样？\n\n");

        counter = 0;
        constexpr int iterations = 10000;

        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                counter++;
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                counter++;
            }
        });

        t1.join();
        t2.join();

        printf("期望值: %d\n", iterations * 2);
        printf("实际值: %d\n", counter);
        printf("差异: %d (丢失的增量)\n", iterations * 2 - counter);
        printf("\n结论：volatile 只禁止编译器优化，不保证原子操作！\n");
    }
}

// ============================================================================
// 第二部分：volatile 不保证内存序
// ============================================================================

namespace volatile_not_ordered {
    // 演示 2：volatile 不保证内存顺序
    volatile int data = 0;
    volatile int flag = 0;

    void producer() {
        data = 42;
        flag = 1;  // 编译器/CPU 可能重排成先写 flag
    }

    void demonstrate_reordering() {
        printf("\n=== 演示 2: volatile 不保证内存序 ===\n");
        printf("即使 flag=1，也可能看到 data=0\n\n");

        int saw_data_correct = 0;
        int total_trials = 0;

        // 运行多次测试
        for (int trial = 0; trial < 100; ++trial) {
            data = 0;
            flag = 0;
            int local_data = 0;

            std::thread producer_thread([&]() {
                producer();
            });

            std::thread consumer_thread([&]() {
                // 短暂延迟让 producer 有机会先执行
                std::this_thread::sleep_for(std::chrono::microseconds(1));

                if (flag) {
                    local_data = data;
                }
            });

            producer_thread.join();
            consumer_thread.join();

            total_trials++;
            if (local_data == 42) {
                saw_data_correct++;
            }
        }

        printf("测试次数: %d\n", total_trials);
        printf("看到正确 data 的次数: %d\n", saw_data_correct);
        printf("正确率: %.1f%%\n", 100.0 * saw_data_correct / total_trials);
        printf("\n结论：volatile 不阻止 CPU/编译器重排内存操作！\n");
    }
}

// ============================================================================
// 第三部分：atomic 既保原子性又保内存序
// ============================================================================

namespace atomic_correct {
    // 演示 3：atomic 正确工作
    std::atomic<int> counter{0};

    void demonstrate_atomic() {
        printf("\n=== 演示 3: atomic 保证原子性 ===\n");
        printf("两个线程同时++ atomic<int>\n\n");

        counter = 0;
        constexpr int iterations = 10000;

        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });

        t1.join();
        t2.join();

        printf("期望值: %d\n", iterations * 2);
        printf("实际值: %d\n", counter.load());
        printf("结论：atomic 保证原子操作，不会丢失增量！\n");
    }

    // 演示 4：atomic 保证内存序
    std::atomic<int> data{0};
    std::atomic<bool> ready{false};

    void demonstrate_memory_order() {
        printf("\n=== 演示 4: atomic 保证内存序 ===\n");
        printf("使用 acquire-release 语义同步\n\n");

        int correct_count = 0;
        constexpr int trials = 100;

        for (int i = 0; i < trials; ++i) {
            data = 0;
            ready = false;
            int local_data = 0;

            std::thread producer([&]() {
                data.store(42, std::memory_order_relaxed);
                ready.store(true, std::memory_order_release);  // 发布
            });

            std::thread consumer([&]() {
                if (ready.load(std::memory_order_acquire)) {  // 订阅
                    local_data = data.load(std::memory_order_relaxed);
                }
            });

            producer.join();
            consumer.join();

            if (local_data == 42) {
                correct_count++;
            }
        }

        printf("测试次数: %d\n", trials);
        printf("看到正确 data 的次数: %d\n", correct_count);
        printf("正确率: %.1f%%\n", 100.0 * correct_count / trials);
        printf("结论：atomic + acquire-release 保证内存顺序！\n");
    }
}

// ============================================================================
// 第四部分：volatile 的正确用途
// ============================================================================

namespace volatile_correct_use {
    // 模拟硬件寄存器
    struct UART_Regs {
        volatile uint32_t SR;   // 状态寄存器
        volatile uint32_t DR;   // 数据寄存器
        volatile uint32_t BRR;  // 波特率寄存器
    };

    // 内存映射的 UART 寄存器
    // 使用 uintptr_t 作为中间类型以兼容 GCC 15
    // 注意：constexpr 在 GCC 15+ 中不允许整数到指针的转换
    static const UART_Regs* UART1 = reinterpret_cast<UART_Regs*>(static_cast<std::uintptr_t>(0x40011000));

    // 信号标志示例变量
    static volatile bool signal_received = false;

    // 正确用法 1：硬件寄存器访问
    void demonstrate_hardware_registers() {
        printf("\n=== volatile 的正确用途 1: 硬件寄存器 ===\n");

        // 使用 UART1 以抑制未使用警告
        (void)UART1;

        // 模拟读取 UART 状态寄存器
        // volatile 确保每次都从硬件读取，不被缓存或优化掉
        printf("  每次读取 UART_SR 都会访问硬件\n");
        printf("  while (UART1->SR & UART_SR_RXNE) { ... }\n");
        printf("  volatile 确保循环不会优化为只读一次\n\n");
    }

    // 正确用法 2：信号处理
    void demonstrate_signal_flag() {
        printf("\n=== volatile 的正确用途 2: 信号标志 ===\n");

        // 只有一个写入者，其他只读取
        signal_received = false;  // 使用变量以抑制警告

        printf("  信号处理器设置标志: signal_received = true\n");
        printf("  主线程轮询检查: while (!signal_received) { ... }\n");
        printf("  因为只有一个写入者，volatile 足够\n\n");
    }

    // 正确用法 3：内存映射的 I/O 操作
    void demonstrate_mmio() {
        printf("\n=== volatile 的正确用途 3: MMIO ===\n");

        // GPIO 寄存器（仅演示，实际使用时会访问）
        volatile uint32_t* GPIO_ODR = reinterpret_cast<uint32_t*>(static_cast<std::uintptr_t>(0x4001100C));
        (void)GPIO_ODR;  // 抑制未使用警告

        printf("  *GPIO_ODR |= (1 << 5);  // 设置 GPIO5\n");
        printf("  volatile 确保写入真的发生，不被优化掉\n");
        printf("  每次读取都返回硬件的实际状态\n\n");
    }
}

// ============================================================================
// 第五部分：决策树
// ============================================================================

void show_decision_tree() {
    printf("\n=== volatile vs atomic 决策树 ===\n");
    printf(R"(
                       变量会被并发修改？
                            |
                    -----------------
                   |               |
                   是              否
                   |               |
            ---------------   用普通变量
            |
     是硬件 I/O 寄存器？
            |
     -----------------------
     |                     |
     是                    否
     |                     |
用 volatile            有多个修改者？
(内存映射I/O)              |
                       -------------
                      |           |
                      是          否
                      |           |
                  用 atomic     用 volatile
                  (共享变量)   (单写入者)

)");
}

// ============================================================================
// 性能对比
// ============================================================================

namespace performance_comparison {
    constexpr int N = 1000000;

    void benchmark_volatile() {
        auto start = std::chrono::high_resolution_clock::now();

        volatile int x = 0;
        for (int i = 0; i < N; ++i) {
            x = i;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  volatile:  %ld μs\n", duration.count());
    }

    void benchmark_atomic_relaxed() {
        auto start = std::chrono::high_resolution_clock::now();

        std::atomic<int> x{0};
        for (int i = 0; i < N; ++i) {
            x.store(i, std::memory_order_relaxed);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  atomic(relaxed): %ld μs\n", duration.count());
    }

    void benchmark_atomic_seq_cst() {
        auto start = std::chrono::high_resolution_clock::now();

        std::atomic<int> x{0};
        for (int i = 0; i < N; ++i) {
            x.store(i, std::memory_order_seq_cst);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  atomic(seq_cst): %ld μs\n", duration.count());
    }

    void demonstrate() {
        printf("\n=== 性能对比 (%d 次操作) ===\n", N);

        // 预热
        benchmark_volatile();
        benchmark_atomic_relaxed();
        benchmark_atomic_seq_cst();

        printf("\n实际测试:\n");
        benchmark_volatile();
        benchmark_atomic_relaxed();
        benchmark_atomic_seq_cst();

        printf("\n注意：性能差异取决于架构\n");
        printf("      x86 上差异较小，ARM 上差异明显\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  volatile vs atomic\n");
    printf("========================================\n");

    volatile_not_atomic::demonstrate_race();
    volatile_not_ordered::demonstrate_reordering();
    atomic_correct::demonstrate_atomic();
    atomic_correct::demonstrate_memory_order();
    volatile_correct_use::demonstrate_hardware_registers();
    volatile_correct_use::demonstrate_signal_flag();
    volatile_correct_use::demonstrate_mmio();
    show_decision_tree();
    performance_comparison::demonstrate();

    printf("\n=== 总结 ===\n");
    printf("1. volatile 不保证原子性：counter++ 仍可能丢失数据\n");
    printf("2. volatile 不保证内存序：操作可能被 CPU 重排\n");
    printf("3. atomic 既保原子性又保内存序：并发安全\n");
    printf("4. volatile 的正确用途：\n");
    printf("   - 内存映射 I/O（硬件寄存器）\n");
    printf("   - 单写入者的信号标志\n");
    printf("   - DMA 缓冲区\n");
    printf("5. 选择原则：硬件寄存器用 volatile，共享变量用 atomic\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread -O2 05_volatile_vs_atomic.cpp -o 05_volatile_vs_atomic
