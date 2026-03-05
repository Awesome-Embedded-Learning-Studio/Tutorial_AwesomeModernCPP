// 死锁预防 - 演示死锁场景和预防策略
// 本示例展示了各种死锁预防技术和工具

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include <functional>
#include <tuple>

// ============================================================================
// 死锁的四个必要条件（Coffman 条件）
// ============================================================================

namespace coffman_conditions {
    void explain() {
        printf("\n=== 死锁的四个必要条件 (Coffman 条件) ===\n\n");

        printf("1. 互斥 (Mutual Exclusion)\n");
        printf("   - 资源不能被多个进程同时使用\n\n");

        printf("2. 持有并等待 (Hold and Wait)\n");
        printf("   - 进程持有至少一个资源，等待获取其他资源\n\n");

        printf("3. 不可抢占 (No Preemption)\n");
        printf("   - 资源不能被强制从持有者夺走\n\n");

        printf("4. 循环等待 (Circular Wait)\n");
        printf("   - 存在进程的环形等待链 P1->P2->...->Pn->P1\n\n");

        printf("打破这四个条件中的任何一个，就能预防死锁！\n");
    }
}

// ============================================================================
// 死锁检测工具
// ============================================================================

namespace deadlock_detector {
    // 简单的死锁检测器（模拟）
    class DeadlockDetector {
    public:
        void record_lock_attempt(void* mutex, const char* task_name) {
            printf("[检测器] %s 尝试获取锁 %p\n", task_name, mutex);
        }

        void record_lock_acquired(void* mutex, const char* task_name) {
            printf("[检测器] %s 获取锁 %p 成功\n", task_name, mutex);
        }

        void record_lock_released(void* mutex, const char* task_name) {
            printf("[检测器] %s 释放锁 %p\n", task_name, mutex);
        }

        void check_for_cycle() {
            // 实际实现会分析等待图
            printf("[检测器] 检查循环等待...\n");
        }
    };

    static DeadlockDetector& get_instance() {
        static DeadlockDetector instance;
        return instance;
    }
}

// ============================================================================
// 可检测的互斥量
// ============================================================================

namespace detectable_mutex {
    class DetectableMutex {
    public:
        void lock(const char* owner) {
            deadlock_detector::get_instance().record_lock_attempt(this, owner);
            mtx_.lock();
            owner_ = owner;
            deadlock_detector::get_instance().record_lock_acquired(this, owner);
        }

        void unlock() {
            deadlock_detector::get_instance().record_lock_released(this, owner_);
            mtx_.unlock();
        }

    private:
        std::mutex mtx_;
        const char* owner_ = "unknown";
    };
}

// ============================================================================
// 预防策略1：锁顺序规范
// ============================================================================

namespace lock_ordering {
    using detectable_mutex::DetectableMutex;

    // 全局锁顺序表
    enum class LockId {
        None,  // 添加 None 值
        Database,
        Network,
        FileSystem,
        Logger,
        // ... 更多资源
    };

    // 验证锁顺序
    class LockOrderChecker {
    public:
        static LockOrderChecker& instance() {
            static LockOrderChecker checker;
            return checker;
        }

        void acquire(LockId id) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (current_lock_ != LockId::None && id <= current_lock_) {
                printf("[警告] 锁顺序违反！当前: %d, 尝试: %d\n",
                    static_cast<int>(current_lock_), static_cast<int>(id));
            }
            current_lock_ = id;
        }

        void release() {
            std::lock_guard<std::mutex> lock(mtx_);
            current_lock_ = LockId::None;
        }

    private:
        LockOrderChecker() = default;
        LockId current_lock_ = LockId::None;
        std::mutex mtx_;
    };

    // RAII 锁，带顺序检查
    class OrderedLock {
    public:
        OrderedLock(LockId id, const char* name)
            : id_(id), name_(name)
        {
            LockOrderChecker::instance().acquire(id_);
            mtx_.lock(name_);
        }

        ~OrderedLock() {
            mtx_.unlock();
            LockOrderChecker::instance().release();
        }

    private:
        LockId id_;
        const char* name_;
        DetectableMutex mtx_;
    };

    void demonstrate() {
        printf("\n=== 预防策略1：锁顺序规范 ===\n");
        printf("为所有锁定义全局顺序，强制按顺序获取\n\n");

        DetectableMutex db_mutex;
        DetectableMutex net_mutex;

        // 正确的顺序
        printf("正确示例：按顺序获取锁\n");
        std::thread t1([&]() {
            OrderedLock db(LockId::Database, "任务1-DB");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            OrderedLock net(LockId::Network, "任务1-NET");
            printf("  任务1: 处理数据库和网络\n");
        });

        std::thread t2([&]() {
            OrderedLock db(LockId::Database, "任务2-DB");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            OrderedLock net(LockId::Network, "任务2-NET");
            printf("  任务2: 处理数据库和网络\n");
        });

        t1.join();
        t2.join();

        printf("\n错误的顺序（会被检测到）：\n");
        // 注意：这里的演示不会真的获取锁，只是展示检查
        printf("  如果先获取 Network 再获取 Database，会被检测为违反顺序\n");
    }
}

// ============================================================================
// 预防策略2：超时机制
// ============================================================================

namespace timeout_strategy {
    std::timed_mutex mtx_a;
    std::timed_mutex mtx_b;

    bool safe_operation_with_timeout(int timeout_ms = 100) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        // 尝试获取第一把锁
        if (!mtx_a.try_lock_until(deadline)) {
            printf("  [超时] 获取锁 A 超时\n");
            return false;
        }

        // 确保异常安全
        std::unique_lock<std::timed_mutex> lock_a(mtx_a, std::adopt_lock);
        printf("  [成功] 获取锁 A\n");

        // 尝试获取第二把锁
        if (!mtx_b.try_lock_until(deadline)) {
            printf("  [超时] 获取锁 B 超时，释放锁 A\n");
            return false;
        }

        std::unique_lock<std::timed_mutex> lock_b(mtx_b, std::adopt_lock);
        printf("  [成功] 获取锁 B\n");

        // 临界区操作
        printf("  执行临界区操作\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        return true;
    }

    void demonstrate() {
        printf("\n=== 预防策略2：超时机制 ===\n");
        printf("使用 try_lock_for/try_lock_until 避免无限等待\n\n");

        std::thread t1([&]() {
            printf("线程1: 尝试获取锁...\n");
            if (safe_operation_with_timeout()) {
                printf("线程1: 操作完成\n");
            } else {
                printf("线程1: 操作超时\n");
            }
        });

        std::thread t2([&]() {
            printf("线程2: 尝试获取锁...\n");
            if (safe_operation_with_timeout()) {
                printf("线程2: 操作完成\n");
            } else {
                printf("线程2: 操作超时\n");
            }
        });

        t1.join();
        t2.join();
    }
}

// ============================================================================
// 预防策略3：try_lock 回退
// ============================================================================

namespace backoff_strategy {
    std::mutex mtx_a;
    std::mutex mtx_b;

    void try_lock_with_backoff(int max_retries = 3) {
        int attempt = 0;
        while (attempt < max_retries) {
            // 尝试同时锁定两个互斥量
            int lock_result = std::try_lock(mtx_a, mtx_b);

            if (lock_result == -1) {
                // 成功获取所有锁
                printf("  尝试 %d: 成功获取所有锁\n", attempt + 1);

                // 临界区操作
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                mtx_a.unlock();
                mtx_b.unlock();
                return;
            }

            // 失败：释放已获取的锁
            printf("  尝试 %d: 获取锁失败，回退\n", attempt + 1);
            if (lock_result == 0) mtx_a.unlock();
            if (lock_result == 1) mtx_b.unlock();

            // 指数退避
            auto backoff_time = std::chrono::milliseconds(10 << attempt);
            std::this_thread::sleep_for(backoff_time);
            attempt++;
        }

        printf("  达到最大重试次数\n");
    }

    void demonstrate() {
        printf("\n=== 预防策略3：try_lock 回退 ===\n");
        printf("使用 try_lock + 指数退避避免活锁\n\n");

        std::thread t1([&]() {
            printf("线程1: ");
            try_lock_with_backoff();
        });

        std::thread t2([&]() {
            printf("线程2: ");
            try_lock_with_backoff();
        });

        t1.join();
        t2.join();
    }
}

// ============================================================================
// 预防策略4：死锁检测与恢复
// ============================================================================

namespace detection_recovery {
    // 资源分配图
    class ResourceAllocationGraph {
    public:
        struct Node {
            enum Type { Process, Resource };
            Type type;
            int id;
        };

        void add_edge(int from, int to) {
            std::lock_guard<std::mutex> lock(mtx_);
            printf("[图] 添加边: %d -> %d\n", from, to);
            // 实际实现会维护图的邻接表
        }

        void remove_edge(int from, int to) {
            std::lock_guard<std::mutex> lock(mtx_);
            printf("[图] 移除边: %d -> %d\n", from, to);
        }

        bool has_cycle() {
            // 实际实现会使用DFS检测环
            printf("[图] 检测循环...\n");
            return false;
        }

    private:
        std::mutex mtx_;
    };

    ResourceAllocationGraph rag;

    void demonstrate() {
        printf("\n=== 预防策略4：死锁检测与恢复 ===\n");
        printf("定期检查资源分配图，检测到死锁后恢复\n\n");

        printf("1. 维护资源分配图\n");
        printf("2. 定期运行循环检测算法\n");
        printf("3. 检测到死锁时，选择牺牲者回滚\n");
        printf("4. 牺牲者通常是完成进度最少的进程\n\n");

        printf("示例：银行家算法\n");
        printf("- 每次分配前检查是否安全\n");
        printf("- 如果不安全，推迟分配\n");
    }
}

// ============================================================================
// 预防策略5：资源分层
// ============================================================================

namespace resource_hierarchy {
    // 资源层次
    enum class Layer {
        None = 0,
        Application = 1,    // 最高层
        Service = 2,
        Driver = 3,         // 中层
        Hardware = 4,       // 最底层
    };

    // 分层锁
    class LayeredLock {
    public:
        explicit LayeredLock(Layer layer)
            : layer_(layer)
        {}

        void lock(const char* owner) {
            // 检查层次规则
            if (current_layer_ != Layer::None && layer_ <= current_layer_) {
                printf("[错误] 违反层次规则！当前: %d, 尝试: %d\n",
                    static_cast<int>(current_layer_), static_cast<int>(layer_));
            }
            mtx_.lock();
            current_layer_ = layer_;
            owner_ = owner;
            printf("[层次锁] %s 获取层次 %d\n", owner, static_cast<int>(layer_));
        }

        void unlock() {
            printf("[层次锁] %s 释放层次 %d\n", owner_, static_cast<int>(layer_));
            current_layer_ = Layer::None;
            mtx_.unlock();
        }

    private:
        Layer layer_;
        std::mutex mtx_;
        static Layer current_layer_;
        const char* owner_;
    };

    Layer LayeredLock::current_layer_ = Layer::None;

    void demonstrate() {
        printf("\n=== 预防策略5：资源分层 ===\n");
        printf("将资源分层，只允许从高层到低层的顺序获取锁\n\n");

        LayeredLock app_lock(Layer::Application);
        LayeredLock driver_lock(Layer::Driver);
        LayeredLock hw_lock(Layer::Hardware);

        printf("正确的层次顺序：\n");
        app_lock.lock("任务");
        driver_lock.lock("任务");
        hw_lock.lock("任务");
        hw_lock.unlock();
        driver_lock.unlock();
        app_lock.unlock();

        printf("\n如果违反层次顺序会被检测到\n");
    }
}

// ============================================================================
// 综合示例：转账系统
// ============================================================================

namespace transfer_system {
    struct Account {
        int id;
        int balance;
        mutable std::mutex mtx;  // mutable 允许在 const 上下文中使用

        Account() = default;
        Account(int i, int b) : id(i), balance(b) {}
    };

    class Bank {
    public:
        Bank() {
            // 使用 piecewise_construct 和 forward_as_tuple
            // 因为 std::mutex 不可复制
            accounts_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(1),
                             std::forward_as_tuple(1, 1000));
            accounts_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(2),
                             std::forward_as_tuple(2, 500));
            accounts_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(3),
                             std::forward_as_tuple(3, 300));
        }

        // 不安全的转账
        bool unsafe_transfer(int from_id, int to_id, int amount) {
            printf("[不安全] 账户%d -> 账户%d: %d\n", from_id, to_id, amount);

            auto& from = accounts_.at(from_id);
            auto& to = accounts_.at(to_id);

            // 可能死锁！
            from.mtx.lock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            to.mtx.lock();

            if (from.balance < amount) {
                from.mtx.unlock();
                to.mtx.unlock();
                printf("[不安全] 余额不足！\n");
                return false;
            }

            from.balance -= amount;
            to.balance += amount;

            from.mtx.unlock();
            to.mtx.unlock();
            printf("[不安全] 转账完成\n");
            return true;
        }

        // 安全的转账：使用 scoped_lock
        bool safe_transfer(int from_id, int to_id, int amount) {
            printf("[安全] 账户%d -> 账户%d: %d\n", from_id, to_id, amount);

            auto& from = accounts_.at(from_id);
            auto& to = accounts_.at(to_id);

            // scoped_lock 自动避免死锁
            std::scoped_lock lock(from.mtx, to.mtx);

            if (from.balance < amount) {
                printf("[安全] 余额不足！\n");
                return false;
            }

            from.balance -= amount;
            to.balance += amount;
            printf("[安全] 转账完成\n");
            return true;
        }

        void print_balances() {
            printf("[银行] 账户余额: ");
            for (const auto& [id, acc] : accounts_) {
                printf("#%d=%d ", id, acc.balance);
            }
            printf("\n");
        }

    private:
        std::unordered_map<int, Account> accounts_;
    };

    void demonstrate() {
        printf("\n=== 综合示例：转账系统 ===\n");

        Bank bank;
        bank.print_balances();

        printf("\n使用 scoped_lock 的安全转账：\n");

        std::thread t1([&]() {
            bank.safe_transfer(1, 2, 100);
        });

        std::thread t2([&]() {
            bank.safe_transfer(2, 3, 50);
        });

        std::thread t3([&]() {
            bank.safe_transfer(3, 1, 30);
        });

        t1.join();
        t2.join();
        t3.join();

        bank.print_balances();
    }
}

// ============================================================================
// 死锁预防检查清单
// ============================================================================

namespace checklist {
    void print() {
        printf("\n=== 死锁预防检查清单 ===\n\n");

        printf("设计阶段：\n");
        printf("  [ ] 定义清晰的锁获取顺序\n");
        printf("  [ ] 识别所有可能的共享资源\n");
        printf("  [ ] 考虑使用无锁数据结构\n");
        printf("  [ ] 设计资源层次结构\n\n");

        printf("编码阶段：\n");
        printf("  [ ] 使用 RAII 管理锁\n");
        printf("  [ ] 使用 scoped_lock 处理多锁\n");
        printf("  [ ] 避免嵌套锁\n");
        printf("  [ ] 临界区尽可能短\n");
        printf("  [ ] 不在持有锁时调用外部代码\n\n");

        printf("测试阶段：\n");
        printf("  [ ] 压力测试高并发场景\n");
        printf("  [ ] 使用死锁检测工具\n");
        printf("  [ ] 代码审查锁的使用\n");
        printf("  [ ] 静态分析工具检查\n\n");

        printf("运行时：\n");
        printf("  [ ] 启用死锁检测（如果平台支持）\n");
        printf("  [ ] 监控锁的等待时间\n");
        printf("  [ ] 设置合理的超时\n");
        printf("  [ ] 有死锁恢复机制\n\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  死锁预防演示\n");
    printf("========================================\n");

    coffman_conditions::explain();
    lock_ordering::demonstrate();
    timeout_strategy::demonstrate();
    backoff_strategy::demonstrate();
    detection_recovery::demonstrate();
    resource_hierarchy::demonstrate();
    transfer_system::demonstrate();
    checklist::print();

    printf("\n=== 总结 ===\n");
    printf("1. 死锁需要四个条件同时满足\n");
    printf("2. 预防策略：\n");
    printf("   - 固定锁获取顺序\n");
    printf("   - 使用超时机制\n");
    printf("   - try_lock + 回退\n");
    printf("   - 资源分层\n");
    printf("   - 死锁检测与恢复\n");
    printf("3. 实践中：\n");
    printf("   - 优先使用 scoped_lock\n");
    printf("   - 避免嵌套锁\n");
    printf("   - 临界区尽可能短\n");
    printf("   - 考虑无锁替代方案\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 05_deadlock_prevention.cpp -o 05_deadlock_prevention
