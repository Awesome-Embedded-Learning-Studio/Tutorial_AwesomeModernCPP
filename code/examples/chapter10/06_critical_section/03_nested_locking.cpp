// 嵌套锁定 - 演示多锁场景和死锁预防
// 本示例展示了嵌套临界区的问题和解决方案

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <climits>

// ============================================================================
// 问题演示：死锁
// ============================================================================

namespace deadlock_demo {
    std::mutex mtx_a;
    std::mutex mtx_b;
    std::atomic<int> deadlock_detected{0};

    // 线程1：先锁 A，再锁 B
    void thread1() {
        printf("  线程1: 尝试获取锁 A...\n");
        mtx_a.lock();
        printf("  线程1: 获取锁 A 成功\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        printf("  线程1: 尝试获取锁 B...\n");
        mtx_b.lock();
        printf("  线程1: 获取锁 B 成功\n");

        // 临界区操作
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        mtx_b.unlock();
        mtx_a.unlock();
        printf("  线程1: 完成\n");
    }

    // 线程2：先锁 B，再锁 A（相反顺序！）
    void thread2() {
        printf("  线程2: 尝试获取锁 B...\n");
        mtx_b.lock();
        printf("  线程2: 获取锁 B 成功\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        printf("  线程2: 尝试获取锁 A...\n");
        mtx_a.lock();
        printf("  线程2: 获取锁 A 成功\n");

        // 临界区操作
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        mtx_a.unlock();
        mtx_b.unlock();
        printf("  线程2: 完成\n");
    }

    void demonstrate() {
        printf("\n=== 问题演示：死锁 ===\n");
        printf("线程1 和 线程2 以相反的顺序获取锁\n");
        printf("可能发生死锁：\n\n");

        std::thread t1([&]() {
            thread1();
        });

        std::thread t2([&]() {
            thread2();
        });

        // 等待一段时间，看是否发生死锁
        bool completed = false;
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (t1.joinable() && t2.joinable()) {
                // 如果线程都完成了
                if (i > 1) {  // 给点时间让线程运行
                    t1.join();
                    t2.join();
                    completed = true;
                    break;
                }
            }
        }

        if (!completed) {
            printf("\n  ⚠️  检测到死锁！程序无法继续。\n");
            printf("  需要强制终止程序。\n");
            // 实际程序中这里会卡住
        } else {
            printf("\n  ✓ 这次没有发生死锁（时序问题）\n");
        }
    }
}

// ============================================================================
// 解决方案1：固定加锁顺序
// ============================================================================

namespace fixed_order_solution {
    std::mutex mtx_a;
    std::mutex mtx_b;

    // 使用地址比较确定顺序
    void safe_transfer(std::mutex& m1, std::mutex& m2) {
        // 始终按照地址大小顺序加锁
        if (&m1 < &m2) {
            m1.lock();
            m2.lock();
        } else {
            m2.lock();
            m1.lock();
        }

        // 临界区操作
        printf("    临界区操作\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 按相反顺序解锁
        if (&m1 < &m2) {
            m2.unlock();
            m1.unlock();
        } else {
            m1.unlock();
            m2.unlock();
        }
    }

    void demonstrate() {
        printf("\n=== 解决方案1：固定加锁顺序 ===\n");

        std::thread t1([&]() {
            printf("  线程1: 按地址顺序加锁\n");
            safe_transfer(mtx_a, mtx_b);
        });

        std::thread t2([&]() {
            printf("  线程2: 按地址顺序加锁\n");
            safe_transfer(mtx_b, mtx_a);  // 参数顺序不同，内部会调整
        });

        t1.join();
        t2.join();
        printf("  ✓ 完成，没有死锁！\n");
    }
}

// ============================================================================
// 解决方案2：std::lock (C++11)
// ============================================================================

namespace std_lock_solution {
    std::mutex mtx_a;
    std::mutex mtx_b;

    void safe_operation() {
        std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
        std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);

        // std::lock 会使用死锁避免算法
        std::lock(mtx_a, mtx_b);

        // now lock_a 和 lock_b 都持有锁的所有权
        lock_a.lock();
        lock_b.lock();

        printf("    临界区操作\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // unique_lock 析构时自动解锁
    }

    void demonstrate() {
        printf("\n=== 解决方案2：std::lock ===\n");

        std::thread t1([&]() {
            printf("  线程1: 使用 std::lock\n");
            safe_operation();
        });

        std::thread t2([&]() {
            printf("  线程2: 使用 std::lock\n");
            safe_operation();
        });

        t1.join();
        t2.join();
        printf("  ✓ 完成，没有死锁！\n");
    }
}

// ============================================================================
// 解决方案3：std::scoped_lock (C++17)
// ============================================================================

namespace scoped_lock_solution {
    std::mutex mtx_a;
    std::mutex mtx_b;
    std::mutex mtx_c;

    void safe_operation() {
        // scoped_lock 自动处理多锁，避免死锁
        std::scoped_lock lock(mtx_a, mtx_b, mtx_c);

        printf("    持有 A、B、C 三把锁\n");
        printf("    临界区操作\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 离开作用域自动解锁
    }

    void demonstrate() {
        printf("\n=== 解决方案3：std::scoped_lock (C++17) ===\n");
        printf("最简洁的多锁解决方案\n");

        std::thread t1([&]() {
            printf("  线程1: 使用 scoped_lock\n");
            safe_operation();
        });

        std::thread t2([&]() {
            printf("  线程2: 使用 scoped_lock\n");
            safe_operation();
        });

        t1.join();
        t2.join();
        printf("  ✓ 完成，没有死锁！\n");
    }
}

// ============================================================================
// 解决方案4：try_lock + 超时
// ============================================================================

namespace try_lock_solution {
    std::timed_mutex mtx_a;
    std::timed_mutex mtx_b;

    bool safe_operation_with_timeout(int timeout_ms = 100) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        // 尝试获取第一把锁
        if (!mtx_a.try_lock_until(deadline)) {
            return false;
        }

        // 确保异常安全
        std::unique_lock<std::timed_mutex> lock_a(mtx_a, std::adopt_lock);

        // 尝试获取第二把锁
        if (!mtx_b.try_lock_until(deadline)) {
            return false;
        }

        std::unique_lock<std::timed_mutex> lock_b(mtx_b, std::adopt_lock);

        // 临界区操作
        printf("    获取两把锁成功\n");
        printf("    临界区操作\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        return true;
    }

    void demonstrate() {
        printf("\n=== 解决方案4：try_lock + 超时 ===\n");

        std::thread t1([&]() {
            printf("  线程1: 尝试获取锁...\n");
            if (safe_operation_with_timeout()) {
                printf("  线程1: 操作成功\n");
            } else {
                printf("  线程1: 获取锁超时\n");
            }
        });

        std::thread t2([&]() {
            printf("  线程2: 尝试获取锁...\n");
            if (safe_operation_with_timeout()) {
                printf("  线程2: 操作成功\n");
            } else {
                printf("  线程2: 获取锁超时\n");
            }
        });

        t1.join();
        t2.join();
    }
}

// ============================================================================
// 层次锁（Hierarchical Lock）
// ============================================================================

namespace hierarchical_lock {
    // 给每个锁分配层次
    class HierarchicalMutex {
    public:
        explicit HierarchicalMutex(unsigned long value)
            : hierarchy_value_(value)
            , previous_hierarchy_value_(0)
        {}

        void lock() {
            check_for_hierarchy_violation();
            internal_mutex_.lock();
            update_hierarchy_value();
        }

        void unlock() {
            this_thread_hierarchy_value() = previous_hierarchy_value_;
            internal_mutex_.unlock();
        }

        bool try_lock() {
            check_for_hierarchy_violation();
            if (!internal_mutex_.try_lock()) {
                return false;
            }
            update_hierarchy_value();
            return true;
        }

        // 提供公共接口来初始化层次值
        static void init_thread_hierarchy(unsigned long value) {
            this_thread_hierarchy_value() = value;
        }

    private:
        std::mutex internal_mutex_;
        const unsigned long hierarchy_value_;
        unsigned long previous_hierarchy_value_;

        static thread_local unsigned long this_thread_hierarchy_value_;

        void check_for_hierarchy_violation() {
            if (this_thread_hierarchy_value() <= hierarchy_value_) {
                printf("    ⚠️  层次违反！当前层次: %lu, 请求层次: %lu\n",
                    this_thread_hierarchy_value(), hierarchy_value_);
            }
        }

        void update_hierarchy_value() {
            previous_hierarchy_value_ = this_thread_hierarchy_value();
            this_thread_hierarchy_value() = hierarchy_value_;
        }

        static unsigned long& this_thread_hierarchy_value() {
            static thread_local unsigned long value = ULONG_MAX;
            return value;
        }
    };

    // 层次定义：数值越小，层次越高
    HierarchicalMutex high_level_mutex(1000);
    HierarchicalMutex mid_level_mutex(5000);
    HierarchicalMutex low_level_mutex(10000);

    void demonstrate() {
        printf("\n=== 层次锁 ===\n");
        printf("锁按层次组织，只允许从高到低获取\n");

        std::thread t1([&]() {
            printf("  线程1: 正确的层次顺序\n");
            HierarchicalMutex::init_thread_hierarchy(0);
            high_level_mutex.lock();
            printf("    获取高层次锁\n");
            mid_level_mutex.lock();
            printf("    获取中层次锁\n");
            low_level_mutex.lock();
            printf("    获取低层次锁\n");

            low_level_mutex.unlock();
            mid_level_mutex.unlock();
            high_level_mutex.unlock();
            printf("  线程1: 完成\n");
        });

        std::thread t2([&]() {
            printf("  线程2: 错误的层次顺序（会被警告）\n");
            HierarchicalMutex::init_thread_hierarchy(0);
            low_level_mutex.lock();
            printf("    获取低层次锁\n");
            mid_level_mutex.lock();
            printf("    ⚠️  这里应该检测到层次违反\n");

            mid_level_mutex.unlock();
            low_level_mutex.unlock();
        });

        t1.join();
        t2.join();
    }
}

// ============================================================================
// 实用示例：银行转账
// ============================================================================

namespace bank_transfer {
    struct Account {
        int id;              // 必须在初始化列表顺序中排第一
        int balance;
        std::mutex mtx;

        Account(int i, int b) : id(i), balance(b) {}
    };

    // ❌ 不安全：可能死锁
    void unsafe_transfer(Account& from, Account& to, int amount) {
        from.mtx.lock();
        to.mtx.lock();

        from.balance -= amount;
        to.balance += amount;

        to.mtx.unlock();
        from.mtx.unlock();
    }

    // ✅ 安全：使用 std::lock
    void safe_transfer(Account& from, Account& to, int amount) {
        std::unique_lock<std::mutex> lock_from(from.mtx, std::defer_lock);
        std::unique_lock<std::mutex> lock_to(to.mtx, std::defer_lock);

        std::lock(from.mtx, to.mtx);
        lock_from.lock();
        lock_to.lock();

        from.balance -= amount;
        to.balance += amount;

        printf("    转账: 账户%d -> 账户%d, 金额: %d\n",
            from.id, to.id, amount);
    }

    // ✅ 最安全：使用 scoped_lock (C++17)
    void safest_transfer(Account& from, Account& to, int amount) {
        std::scoped_lock lock(from.mtx, to.mtx);

        from.balance -= amount;
        to.balance += amount;

        printf("    转账: 账户%d -> 账户%d, 金额: %d\n",
            from.id, to.id, amount);
    }

    void demonstrate() {
        printf("\n=== 实用示例：银行转账 ===\n");

        Account acc1(1, 1000);
        Account acc2(2, 500);
        Account acc3(3, 300);

        printf("初始余额: 账户1=%d, 账户2=%d, 账户3=%d\n",
            acc1.balance, acc2.balance, acc3.balance);

        std::thread t1([&]() {
            safest_transfer(acc1, acc2, 100);
        });

        std::thread t2([&]() {
            safest_transfer(acc2, acc3, 50);
        });

        std::thread t3([&]() {
            safest_transfer(acc3, acc1, 30);
        });

        t1.join();
        t2.join();
        t3.join();

        printf("最终余额: 账户1=%d, 账户2=%d, 账户3=%d\n",
            acc1.balance, acc2.balance, acc3.balance);
        printf("总余额: %d (应该等于初始 1800)\n",
            acc1.balance + acc2.balance + acc3.balance);
    }
}

// ============================================================================
// 读写锁与嵌套
// ============================================================================

namespace read_write_lock {
    // 简单的读写锁实现
    class RWLock {
    public:
        void read_lock() {
            std::unique_lock<std::mutex> lock(mtx_);
            while (writer_count_ > 0) {
                reader_cv_.wait(lock);
            }
            reader_count_++;
        }

        void read_unlock() {
            std::unique_lock<std::mutex> lock(mtx_);
            reader_count_--;
            if (reader_count_ == 0) {
                writer_cv_.notify_one();
            }
        }

        void write_lock() {
            std::unique_lock<std::mutex> lock(mtx_);
            writer_count_++;
            while (reader_count_ > 0) {
                writer_cv_.wait(lock);
            }
        }

        void write_unlock() {
            std::unique_lock<std::mutex> lock(mtx_);
            writer_count_--;
            reader_cv_.notify_all();
            writer_cv_.notify_all();
        }

    private:
        std::mutex mtx_;
        std::condition_variable reader_cv_;
        std::condition_variable writer_cv_;
        int reader_count_ = 0;
        int writer_count_ = 0;
    };

    class ReadGuard {
    public:
        explicit ReadGuard(RWLock& lock) : lock_(lock) {
            lock_.read_lock();
        }
        ~ReadGuard() {
            lock_.read_unlock();
        }
    private:
        RWLock& lock_;
    };

    class WriteGuard {
    public:
        explicit WriteGuard(RWLock& lock) : lock_(lock) {
            lock_.write_lock();
        }
        ~WriteGuard() {
            lock_.write_unlock();
        }
    private:
        RWLock& lock_;
    };

    void demonstrate() {
        printf("\n=== 读写锁示例 ===\n");

        RWLock rwlock;
        int shared_data = 0;

        std::thread writer([&]() {
            for (int i = 0; i < 3; ++i) {
                {
                    WriteGuard lock(rwlock);
                    shared_data = i * 100;
                    printf("  写者: 更新数据为 %d\n", shared_data);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

        std::thread reader1([&]() {
            for (int i = 0; i < 5; ++i) {
                {
                    ReadGuard lock(rwlock);
                    printf("  读者1: 读取数据 %d\n", shared_data);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        });

        std::thread reader2([&]() {
            for (int i = 0; i < 5; ++i) {
                {
                    ReadGuard lock(rwlock);
                    printf("  读者2: 读取数据 %d\n", shared_data);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        });

        writer.join();
        reader1.join();
        reader2.join();
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  嵌套锁定演示\n");
    printf("========================================\n");

    // 死锁演示可能卡住，注释掉或小心运行
    // deadlock_demo::demonstrate();

    fixed_order_solution::demonstrate();
    std_lock_solution::demonstrate();
    scoped_lock_solution::demonstrate();
    try_lock_solution::demonstrate();
    hierarchical_lock::demonstrate();
    bank_transfer::demonstrate();
    read_write_lock::demonstrate();

    printf("\n=== 总结 ===\n");
    printf("1. 死锁原因：多个线程以不同顺序获取多把锁\n");
    printf("2. 解决方案1：固定加锁顺序（按地址或层次）\n");
    printf("3. 解决方案2：使用 std::lock (C++11)\n");
    printf("4. 解决方案3：使用 scoped_lock (C++17)，最推荐\n");
    printf("5. 解决方案4：try_lock + 超时，避免无限等待\n");
    printf("6. 层次锁：编译期/运行时检测死锁风险\n");
    printf("7. 读写锁：读多写少场景优化\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 03_nested_locking.cpp -o 03_nested_locking
