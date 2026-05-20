/*
 * 死锁问题与解决策略
 *
 * 本文件对应教程 vol5-ch02，演示多线程编程中最令人头疼的问题之一：死锁。
 * 我们会逐步展示：
 *   1. 经典 AB-BA 死锁（用超时避免永久挂起）
 *   2. 按地址排序加锁
 *   3. std::lock + adopt_lock 实现无死锁多锁获取
 *   4. std::try_lock + 退避策略
 *   5. HierarchicalMutex -- 运行时强制层级约束
 *   6. 层级互斥量在实际场景中的使用
 *
 * 编译命令（需要 C++17）：
 *   g++ -std=c++17 -pthread -O2 -Wall -Wextra \
 *       02_deadlock_strategies.cpp -o 02_deadlock_strategies
 *
 * 运行：
 *   ./02_deadlock_strategies
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

// ============================================================================
// 辅助：线程安全打印
// ============================================================================
std::mutex g_print_mutex;

template <typename... Args> void safe_print(Args&&... args) {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    (std::cout << ... << std::forward<Args>(args)) << '\n';
}

// ============================================================================
// 1. 经典 AB-BA 死锁（用超时避免永久挂起）
//
// 注意：这里使用 std::timed_mutex 而非 std::mutex，
// 因为 std::mutex 没有 try_lock_for()，只有 std::timed_mutex 有。
// ============================================================================
void demo_classic_deadlock() {
    safe_print("\n=== 1. 经典 AB-BA 死锁（超时保护）===");

    std::timed_mutex mtx_a;
    std::timed_mutex mtx_b;

    // 用 atomic flag 检测是否真的发生了死锁
    std::atomic<bool> deadlock_detected{false};

    auto worker_ab = [&](int id) {
        // 线程 A：先锁 mtx_a，再锁 mtx_b
        if (!mtx_a.try_lock_for(std::chrono::milliseconds(100))) {
            safe_print("  线程 ", id, "：获取 mtx_a 超时");
            deadlock_detected = true;
            return;
        }
        safe_print("  线程 ", id, "：持有 mtx_a，尝试获取 mtx_b...");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (!mtx_b.try_lock_for(std::chrono::milliseconds(200))) {
            safe_print("  线程 ", id, "：获取 mtx_b 超时 - 可能死锁！");
            deadlock_detected = true;
            mtx_a.unlock();
            return;
        }

        safe_print("  线程 ", id, "：成功持有两把锁");
        mtx_b.unlock();
        mtx_a.unlock();
    };

    auto worker_ba = [&](int id) {
        // 线程 B：先锁 mtx_b，再锁 mtx_a - 相反顺序
        if (!mtx_b.try_lock_for(std::chrono::milliseconds(100))) {
            safe_print("  线程 ", id, "：获取 mtx_b 超时");
            deadlock_detected = true;
            return;
        }
        safe_print("  线程 ", id, "：持有 mtx_b，尝试获取 mtx_a...");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (!mtx_a.try_lock_for(std::chrono::milliseconds(200))) {
            safe_print("  线程 ", id, "：获取 mtx_a 超时 - 可能死锁！");
            deadlock_detected = true;
            mtx_b.unlock();
            return;
        }

        safe_print("  线程 ", id, "：成功持有两把锁");
        mtx_a.unlock();
        mtx_b.unlock();
    };

    // 通过 sleep_for 的时机控制，让两个线程几乎同时各持一把锁
    std::thread t1(worker_ab, 1);
    std::thread t2(worker_ba, 2);
    t1.join();
    t2.join();

    if (deadlock_detected.load()) {
        safe_print("  检测到潜在的死锁情况！这就是经典 AB-BA 问题。");
    } else {
        safe_print("  本次运行没有死锁（时序相关，不一定每次都能复现）。");
    }

    safe_print("  教训：永远以相同的顺序获取多把锁。");
}

// ============================================================================
// 2. 按地址排序加锁 -- 避免死锁的通用方法
// ============================================================================
void demo_lock_ordering() {
    safe_print("\n=== 2. 按地址排序加锁 ===");

    std::mutex mtx_a;
    std::mutex mtx_b;

    // 按 mutex 的内存地址排序，始终先锁地址小的
    auto lock_two = [](std::mutex& first, std::mutex& second) {
        std::mutex* lo = &first;
        std::mutex* hi = &second;
        if (lo > hi) {
            std::swap(lo, hi);
        }
        lo->lock();
        hi->lock();
    };

    auto unlock_two = [](std::mutex& first, std::mutex& second) {
        first.unlock();
        second.unlock();
    };

    std::atomic<int> success_count{0};

    auto worker = [&](int id) {
        for (int i = 0; i < 1000; ++i) {
            // 无论传入顺序如何，实际加锁顺序总是地址升序
            if (id % 2 == 0) {
                lock_two(mtx_a, mtx_b);
            } else {
                lock_two(mtx_b, mtx_a); // 反序传入，但内部会纠正
            }
            ++success_count;
            unlock_two(mtx_a, mtx_b);
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    safe_print("  成功操作次数：", success_count.load(), "（期望 2000）");
    safe_print("  按地址排序是一种通用的无死锁加锁策略，简单有效。");
}

// ============================================================================
// 3. std::lock + adopt_lock -- 标准库的 deadlock-avoidance 算法
// ============================================================================
void demo_std_lock() {
    safe_print("\n=== 3. std::lock + adopt_lock ===");

    std::mutex mtx_a;
    std::mutex mtx_b;
    int resource_a = 0;
    int resource_b = 0;

    auto worker = [&](int id) {
        for (int i = 0; i < 50'000; ++i) {
            // std::lock 使用内部 deadlock-avoidance 算法
            // （反复 try_lock 所有 mutex）一次性获取全部锁
            std::lock(mtx_a, mtx_b);

            // 互斥量已锁定，用 adopt_lock 告知 lock_guard 不要再 lock()
            std::lock_guard<std::mutex> guard_a(mtx_a, std::adopt_lock);
            std::lock_guard<std::mutex> guard_b(mtx_b, std::adopt_lock);

            ++resource_a;
            ++resource_b;
        }
        safe_print("  线程 ", id, " 完成 50000 次操作");
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    safe_print("  resource_a = ", resource_a, "，resource_b = ", resource_b);
    safe_print("  std::lock 是 C++11 提供的标准死锁避免工具。");
    safe_print("  C++17 的 scoped_lock 内部也使用相同算法，是更好的替代。");
}

// ============================================================================
// 4. std::try_lock + 退避策略
// ============================================================================
void demo_try_lock_backoff() {
    safe_print("\n=== 4. std::try_lock + 退避策略 ===");

    std::mutex mtx_a;
    std::mutex mtx_b;
    std::atomic<int> success_count{0};

    auto worker = [&](int id) {
        for (int attempt = 0; attempt < 1000; ++attempt) {
            // std::try_lock 尝试同时锁定所有互斥量
            // 返回 -1 表示全部成功，否则返回第一个失败的索引
            int result = std::try_lock(mtx_a, mtx_b);

            if (result == -1) {
                // 全部锁定成功
                ++success_count;
                mtx_b.unlock();
                mtx_a.unlock();
            } else {
                // 锁定失败，退避一段时间后重试
                // 退避时间可以随重试次数增长（指数退避）
                auto backoff = std::chrono::microseconds(1 << std::min(attempt, 10));
                std::this_thread::sleep_for(backoff);
                --attempt; // 重试这次迭代
            }
        }
        safe_print("  线程 ", id, " 退出");
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    safe_print("  成功操作次数：", success_count.load());
    safe_print("  try_lock + backoff 是一种悲观但安全的策略，适合高竞争场景。");
}

// ============================================================================
// 5. HierarchicalMutex -- 运行时强制层级约束
//
// 核心思想：给每把互斥量分配一个层级值，规定只能从低层级向高层级加锁。
// 如果违反层级规则（高层级 -> 低层级），立即报告错误。
// 这是《C++ Concurrency in Action》推荐的实践。
// ============================================================================

class HierarchicalMutex {
  public:
    explicit HierarchicalMutex(unsigned long hierarchy_value)
        : hierarchy_value_(hierarchy_value), previous_hierarchy_value_(0) {}

    void lock() {
        check_for_hierarchy_violation();
        internal_mutex_.lock();
        update_hierarchy_value();
    }

    void unlock() {
        // 恢复前一个线程的层级值
        this_thread_hierarchy_value_ = previous_hierarchy_value_;
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

  private:
    void check_for_hierarchy_violation() const {
        if (this_thread_hierarchy_value_ <= hierarchy_value_) {
            // 当前线程的层级不高于这把锁的层级 = 违反规则
            safe_print("  !!! 层级违反：当前线程层级 ", this_thread_hierarchy_value_,
                       " 试图锁定层级 ", hierarchy_value_, " 的 mutex");
            std::terminate(); // 实际项目中可以 throw
        }
    }

    void update_hierarchy_value() {
        // 保存当前线程的层级值，然后更新为这把锁的层级
        previous_hierarchy_value_ = this_thread_hierarchy_value_;
        this_thread_hierarchy_value_ = hierarchy_value_;
    }

  private:
    std::mutex internal_mutex_;
    unsigned long const hierarchy_value_;
    unsigned long previous_hierarchy_value_;

    // 每个线程独立维护"当前所在的最低层级"
    // 初始化为 ULONG_MAX（最高层级），确保第一次加锁不受限
    static thread_local unsigned long this_thread_hierarchy_value_;
};

// thread_local 定义：初始值为最大值，表示"还没有加过任何层级锁"
thread_local unsigned long HierarchicalMutex::this_thread_hierarchy_value_{
    std::numeric_limits<unsigned long>::max()};

// ============================================================================
// 6. 层级互斥量使用示例
//
// 注意：嵌套 lambda 存在前向引用问题，所以我们使用独立的自由函数
// 配合引用捕获来组织 high/mid/low 三层操作。
// ============================================================================
void demo_hierarchical_mutex() {
    safe_print("\n=== 5. HierarchicalMutex ===");
    safe_print("  （层级约束：高编号 = 高层级，只能从高向低加锁）");

    // 定义层级：编号越小，层级越低
    // 规则：只能从高层级（大编号）向低层级（小编号）加锁
    HierarchicalMutex high_level_mutex(10000); // 高层操作
    HierarchicalMutex mid_level_mutex(5000);   // 中层操作
    HierarchicalMutex low_level_mutex(100);    // 低层操作

    int high_data = 0;
    int mid_data = 0;
    int low_data = 0;

    // 使用函数局部结构体避免 lambda 前向引用问题
    struct Operations {
        HierarchicalMutex& high_mtx;
        HierarchicalMutex& mid_mtx;
        HierarchicalMutex& low_mtx;
        int& high_data;
        int& mid_data;
        int& low_data;

        void low_level_operation() {
            std::lock_guard<HierarchicalMutex> low_lock(low_mtx);
            ++low_data;
        }

        void mid_level_operation() {
            std::lock_guard<HierarchicalMutex> mid_lock(mid_mtx);
            ++mid_data;
            // 中层操作内部调用低层操作（合法：5000 -> 100）
            low_level_operation();
        }

        void high_level_operation() {
            std::lock_guard<HierarchicalMutex> high_lock(high_mtx);
            ++high_data;
            // 高层操作内部调用中层操作（合法：10000 -> 5000）
            mid_level_operation();
        }
    };

    Operations ops{high_level_mutex, mid_level_mutex, low_level_mutex,
                   high_data,        mid_data,        low_data};

    // 正常使用：高层 -> 中层 -> 低层
    std::thread t1([&]() {
        for (int i = 0; i < 100; ++i) {
            ops.high_level_operation();
        }
        safe_print("  线程 1 完成 100 次高层操作");
    });

    std::thread t2([&]() {
        for (int i = 0; i < 100; ++i) {
            ops.high_level_operation();
        }
        safe_print("  线程 2 完成 100 次高层操作");
    });

    t1.join();
    t2.join();

    safe_print("  high_data = ", high_data, "，mid_data = ", mid_data, "，low_data = ", low_data);
    safe_print("  所有操作遵循 high->mid->low 层级，无死锁。");

    // 演示违反层级的行为（注释掉，避免 terminate）
    safe_print("  如果尝试从 low_level 加锁后再锁 high_level，");
    safe_print("  HierarchicalMutex 会在运行时检测到并终止程序。");
    safe_print("  这是运行时强制'锁排序'的强大手段。");

    // 取消注释以下代码会触发层级违反检测：
    // {
    //     std::lock_guard<HierarchicalMutex> low_lock(low_level_mutex);
    //     // 此时线程层级 = 100
    //     // 试图锁 10000 层级的 mutex = 违反！
    //     std::lock_guard<HierarchicalMutex> high_lock(high_level_mutex);
    // }
}

// ============================================================================
// main
// ============================================================================
int main() {
    safe_print("死锁问题与解决策略 -- 完整示例");

    demo_classic_deadlock();
    demo_lock_ordering();
    demo_std_lock();
    demo_try_lock_backoff();
    demo_hierarchical_mutex();

    safe_print("\n全部演示完成。");
    return 0;
}
