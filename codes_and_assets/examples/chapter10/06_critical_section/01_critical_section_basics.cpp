// 临界区基础 - 演示为什么需要临界区保护
// 本示例展示了数据竞争问题以及各种保护方法

#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

// ============================================================================
// 问题演示：没有保护的临界区
// ============================================================================

namespace unprotected {
    // 共享的银行账户
    struct Account {
        int balance;
        int transaction_count;
    };

    Account shared_account = {1000, 0};

    // 转账操作（不是原子的！）
    void transfer(int amount) {
        // ========== 临界区开始（但没保护）==========
        int old_balance = shared_account.balance;
        int new_balance = old_balance + amount;
        shared_account.balance = new_balance;
        shared_account.transaction_count++;
        // ========== 临界区结束 ==========
    }

    void demonstrate_race_condition() {
        printf("\n=== 问题演示：没有临界区保护 ===\n");
        printf("初始余额: %d\n", shared_account.balance);

        constexpr int transactions_per_thread = 1000;
        constexpr int amount = 1;

        std::thread t1([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(amount);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(amount);
            }
        });

        t1.join();
        t2.join();

        printf("最终余额: %d\n", shared_account.balance);
        printf("交易次数: %d\n", shared_account.transaction_count);
        printf("期望余额: %d\n", 1000 + 2 * transactions_per_thread);
        printf("差异: %d\n", shared_account.balance - (1000 + 2 * transactions_per_thread));
        printf("\n结论：余额不正确！发生了数据竞争。\n");
    }
}

// ============================================================================
// 解决方案 1：原子操作
// ============================================================================

namespace with_atomic {
    struct Account {
        std::atomic<int> balance;
        std::atomic<int> transaction_count;
    };

    Account shared_account{1000, 0};

    void transfer(int amount) {
        // 原子操作保证安全
        shared_account.balance.fetch_add(amount, std::memory_order_relaxed);
        shared_account.transaction_count.fetch_add(1, std::memory_order_relaxed);
    }

    void demonstrate() {
        printf("\n=== 解决方案 1：原子操作 ===\n");

        shared_account.balance = 1000;
        shared_account.transaction_count = 0;

        constexpr int transactions_per_thread = 1000;

        std::thread t1([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        t1.join();
        t2.join();

        printf("最终余额: %d\n", shared_account.balance.load());
        printf("交易次数: %d\n", shared_account.transaction_count.load());
        printf("结论：原子操作保证了正确性！\n");
    }
}

// ============================================================================
// 解决方案 2：互斥锁
// ============================================================================

namespace with_mutex {
    struct Account {
        int balance;
        int transaction_count;
        std::mutex mtx;  // 保护这个账户的锁
    };

    Account shared_account{1000, 0};

    void transfer(int amount) {
        std::lock_guard<std::mutex> lock(shared_account.mtx);
        // ========== 临界区开始 ==========
        shared_account.balance += amount;
        shared_account.transaction_count++;
        // ========== 临界区结束 ==========
    }

    void demonstrate() {
        printf("\n=== 解决方案 2：互斥锁 ===\n");

        shared_account.balance = 1000;
        shared_account.transaction_count = 0;

        constexpr int transactions_per_thread = 1000;

        std::thread t1([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        t1.join();
        t2.join();

        printf("最终余额: %d\n", shared_account.balance);
        printf("交易次数: %d\n", shared_account.transaction_count);
        printf("结论：互斥锁保护了临界区！\n");
    }
}

// ============================================================================
// 解决方案 3：关中断（模拟）
// ============================================================================

namespace with_interrupt_disable {
    // 模拟中断控制
    std::atomic<bool> interrupts_enabled{true};
    std::atomic<int> interrupt_disable_count{0};

    inline uint32_t disable_interrupts() {
        interrupts_enabled.store(false, std::memory_order_release);
        return interrupt_disable_count.fetch_add(1, std::memory_order_acq_rel);
    }

    inline void restore_interrupts(uint32_t) {
        if (interrupt_disable_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            interrupts_enabled.store(true, std::memory_order_release);
        }
    }

    // RAII 中断守卫
    class InterruptGuard {
    public:
        InterruptGuard() noexcept : count_(disable_interrupts()) {}
        ~InterruptGuard() noexcept { restore_interrupts(count_); }
        InterruptGuard(const InterruptGuard&) = delete;
        InterruptGuard& operator=(const InterruptGuard&) = delete;
    private:
        uint32_t count_;
    };

    struct Account {
        int balance;
        int transaction_count;
    };

    Account shared_account{1000, 0};

    void transfer(int amount) {
        InterruptGuard guard;  // 进入临界区
        shared_account.balance += amount;
        shared_account.transaction_count++;
        // guard 析构，自动恢复中断
    }

    void demonstrate() {
        printf("\n=== 解决方案 3：关中断保护 ===\n");

        shared_account.balance = 1000;
        shared_account.transaction_count = 0;

        constexpr int transactions_per_thread = 1000;

        std::thread t1([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        t1.join();
        t2.join();

        printf("最终余额: %d\n", shared_account.balance);
        printf("交易次数: %d\n", shared_account.transaction_count);
        printf("结论：关中断保护了临界区（模拟）！\n");
    }
}

// ============================================================================
// 解决方案 4：自旋锁
// ============================================================================

namespace with_spinlock {
    class SpinLock {
    public:
        SpinLock() noexcept : flag_(false) {}

        void lock() noexcept {
            while (!try_lock()) {
                // 短暂自旋
                #if defined(__x86_64__)
                __asm__ volatile("pause" ::: "memory");
                #elif defined(__aarch64__) || defined(__ARM_ARCH)
                __asm__ volatile("yield" ::: "memory");
                #else
                std::this_thread::yield();
                #endif
            }
        }

        bool try_lock() noexcept {
            return !flag_.test_and_set(std::memory_order_acquire);
        }

        void unlock() noexcept {
            flag_.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    };

    template<typename Lock>
    class LockGuard {
    public:
        explicit LockGuard(Lock& lock) noexcept : lock_(lock) {
            lock_.lock();
        }
        ~LockGuard() noexcept {
            lock_.unlock();
        }
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
    private:
        Lock& lock_;
    };

    struct Account {
        int balance;
        int transaction_count;
        SpinLock spinlock;
    };

    Account shared_account{1000, 0};

    void transfer(int amount) {
        LockGuard<SpinLock> lock(shared_account.spinlock);
        shared_account.balance += amount;
        shared_account.transaction_count++;
    }

    void demonstrate() {
        printf("\n=== 解决方案 4：自旋锁 ===\n");

        shared_account.balance = 1000;
        shared_account.transaction_count = 0;

        constexpr int transactions_per_thread = 1000;

        std::thread t1([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < transactions_per_thread; ++i) {
                transfer(1);
            }
        });

        t1.join();
        t2.join();

        printf("最终余额: %d\n", shared_account.balance);
        printf("交易次数: %d\n", shared_account.transaction_count);
        printf("结论：自旋锁保护了临界区！\n");
    }
}

// ============================================================================
// 性能对比
// ============================================================================

namespace performance_comparison {
    constexpr int iterations = 100000;

    void benchmark_unprotected() {
        int counter = 0;
        auto start = std::chrono::high_resolution_clock::now();

        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                counter++;  // 数据竞争！
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                counter++;
            }
        });

        t1.join();
        t2.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  无保护: %ld μs (结果: %d, 不正确!)\n", duration.count(), counter);
    }

    void benchmark_atomic() {
        std::atomic<int> counter{0};
        auto start = std::chrono::high_resolution_clock::now();

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

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  原子操作: %ld μs (结果: %d)\n", duration.count(), counter.load());
    }

    void benchmark_mutex() {
        int counter = 0;
        std::mutex mtx;
        auto start = std::chrono::high_resolution_clock::now();

        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                std::lock_guard<std::mutex> lock(mtx);
                counter++;
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                std::lock_guard<std::mutex> lock(mtx);
                counter++;
            }
        });

        t1.join();
        t2.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  互斥锁: %ld μs (结果: %d)\n", duration.count(), counter);
    }

    void benchmark_spinlock() {
        int counter = 0;
        with_spinlock::SpinLock spinlock;
        auto start = std::chrono::high_resolution_clock::now();

        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                with_spinlock::LockGuard<with_spinlock::SpinLock> lock(spinlock);
                counter++;
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                with_spinlock::LockGuard<with_spinlock::SpinLock> lock(spinlock);
                counter++;
            }
        });

        t1.join();
        t2.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        printf("  自旋锁: %ld μs (结果: %d)\n", duration.count(), counter);
    }

    void demonstrate() {
        printf("\n=== 性能对比 (%d 次操作) ===\n", iterations);

        // 预热
        benchmark_unprotected();
        benchmark_atomic();
        benchmark_mutex();
        benchmark_spinlock();

        printf("\n实际测试:\n");
        benchmark_atomic();
        benchmark_mutex();
        benchmark_spinlock();
        benchmark_unprotected();  // 最后运行，展示数据竞争

        printf("\n注意：性能因平台而异\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  临界区基础演示\n");
    printf("========================================\n");

    unprotected::demonstrate_race_condition();
    with_atomic::demonstrate();
    with_mutex::demonstrate();
    with_interrupt_disable::demonstrate();
    with_spinlock::demonstrate();
    performance_comparison::demonstrate();

    printf("\n=== 总结 ===\n");
    printf("1. 没有保护的临界区会产生数据竞争\n");
    printf("2. 原子操作：适合简单变量，性能最好\n");
    printf("3. 互斥锁：适合复杂临界区，可能阻塞\n");
    printf("4. 关中断：适合ISR-主线程共享，影响响应\n");
    printf("5. 自旋锁：适合多核短临界区，占用CPU\n");
    printf("6. 根据场景选择合适的保护机制\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 01_critical_section_basics.cpp -o 01_critical_section_basics
