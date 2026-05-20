/*
 * 互斥量与 RAII 锁守卫：从手动加锁到 scoped_lock
 *
 * 本文件对应教程 vol5-ch02，演示 std::mutex 家族的用法与 RAII 守卫的正确姿势。
 * 我们会依次展示：
 *   1. 手动 lock/unlock（反面教材，不要这么写）
 *   2. recursive_mutex 处理递归场景
 *   3. timed_mutex 的 try_lock_for
 *   4. lock_guard —— 最简 scoped locking
 *   5. unique_lock 的延迟加锁、提前解锁与所有权转移
 *   6. C++17 scoped_lock 同时锁多个 mutex
 *   7. ThreadSafeStack 综合示例
 *
 * 编译命令（需要 C++17）：
 *   g++ -std=c++17 -pthread -O2 -Wall -Wextra \
 *       01_mutex_and_guards.cpp -o 01_mutex_and_guards
 *
 * 运行：
 *   ./01_mutex_and_guards
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stack>
#include <thread>
#include <vector>

// ============================================================================
// 辅助：线程安全的打印，避免输出交错
// ============================================================================
std::mutex g_print_mutex;

template <typename... Args> void safe_print(Args&&... args) {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    (std::cout << ... << std::forward<Args>(args)) << '\n';
}

// ============================================================================
// 1. 手动 lock/unlock —— 反面教材
// ============================================================================
void demo_manual_lock_unlock() {
    safe_print("\n=== 1. 手动 lock/unlock（反面教材）===");

    std::mutex mtx;
    int counter = 0;

    // 正确但危险：手动管理 lock/unlock
    // 如果 lock() 和 unlock() 之间抛出异常，mutex 将永远被锁住
    auto worker = [&](int id) {
        for (int i = 0; i < 3; ++i) {
            mtx.lock(); // 危险！如果下面抛异常，这把锁永远不会释放
            ++counter;
            safe_print("  [手动锁] 线程 ", id, "：counter = ", counter);
            mtx.unlock(); // 如果这行没执行到 = 死锁
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    safe_print("  最终 counter = ", counter, "（期望 6）");
    safe_print("  ⚠ 这种写法在实际项目中绝对不要使用！用 lock_guard 替代。");
}

// ============================================================================
// 2. recursive_mutex —— 允许同一线程多次加锁
// ============================================================================
void demo_recursive_mutex() {
    safe_print("\n=== 2. recursive_mutex ===");

    std::recursive_mutex rmtx;
    int value = 0;

    // recursive_mutex 允许同一线程多次 lock()，必须对应相同次数的 unlock()
    // 典型场景：递归函数内部需要加锁
    // 递归 lambda 需要借助 std::function 或显式传入自身引用
    // 这里用 std::function 来实现递归
    std::function<void(int)> recursive_work = [&](int depth) {
        std::lock_guard<std::recursive_mutex> lock(rmtx);
        ++value;
        safe_print("  递归 depth=", depth, "，value=", value);

        if (depth > 0) {
            recursive_work(depth - 1); // 重新进入时不需要担心死锁
        }
    };

    std::thread t1([&]() { recursive_work(3); });
    std::thread t2([&]() { recursive_work(2); });
    t1.join();
    t2.join();

    safe_print("  最终 value = ", value);
    safe_print("  注意：recursive_mutex 有性能开销，通常说明设计需要重构。");
}

// ============================================================================
// 3. timed_mutex —— try_lock_for / try_lock_until
// ============================================================================
void demo_timed_mutex() {
    safe_print("\n=== 3. timed_mutex ===");

    std::timed_mutex tmtx;

    // 线程 1 先持锁 500ms
    std::thread holder([&]() {
        tmtx.lock();
        safe_print("  [holder] 持有锁...");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        tmtx.unlock();
        safe_print("  [holder] 释放锁");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 线程 2 尝试在 200ms 内获取锁 —— 会超时
    std::thread waiter([&]() {
        if (tmtx.try_lock_for(std::chrono::milliseconds(200))) {
            safe_print("  [waiter] 在 200ms 内拿到了锁");
            tmtx.unlock();
        } else {
            safe_print("  [waiter] 200ms 内没拿到锁（预期行为，holder 还没释放）");
        }
    });

    holder.join();
    waiter.join();

    safe_print("  timed_mutex 适合'带超时的等待'，避免无限阻塞。");
}

// ============================================================================
// 4. lock_guard —— 最简洁的 RAII 锁
// ============================================================================
void demo_lock_guard() {
    safe_print("\n=== 4. lock_guard ===");

    std::mutex mtx;
    int counter = 0;

    auto worker = [&](int id) {
        for (int i = 0; i < 100'000; ++i) {
            // 构造时 lock()，析构时 unlock() —— 异常安全
            std::lock_guard<std::mutex> lock(mtx);
            ++counter;
        }
        safe_print("  线程 ", id, " 完成 100000 次递增");
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    std::thread t3(worker, 3);
    t1.join();
    t2.join();
    t3.join();

    safe_print("  最终 counter = ", counter, "（期望 300000）");
    safe_print("  lock_guard 是日常最常用的锁守卫，构造即加锁，析构即解锁。");
}

// ============================================================================
// 5. unique_lock —— 灵活控制锁的生命周期
// ============================================================================
void demo_unique_lock() {
    safe_print("\n=== 5. unique_lock ===");

    std::mutex mtx;

    // --- 5a. defer_lock：延迟加锁，稍后手动 lock() ---
    {
        std::unique_lock<std::mutex> lock(mtx, std::defer_lock);
        safe_print("  [defer_lock] 此时还没加锁");
        lock.lock(); // 现在才加锁
        safe_print("  [defer_lock] 已加锁");
        // 析构时自动 unlock
    }

    // --- 5b. 提前 unlock() ---
    {
        std::unique_lock<std::mutex> lock(mtx);
        safe_print("  [early unlock] 已加锁，做点临界区操作...");
        lock.unlock();
        safe_print("  [early unlock] 已提前解锁，做点非临界区操作...");
        // 析构时不会再 unlock（已经解锁了）
    }

    // --- 5c. 所有权转移 ---
    {
        std::unique_lock<std::mutex> lock1(mtx);
        safe_print("  [transfer] lock1 持有锁");

        // unique_lock 是 move-only，不能 copy
        std::unique_lock<std::mutex> lock2 = std::move(lock1);
        safe_print("  [transfer] lock2 接管了锁，lock1 现在为空");
        // lock1 此时不管理任何 mutex，lock2 在析构时释放
    }

    safe_print("  unique_lock 的灵活性适合搭配 condition_variable 使用。");
}

// ============================================================================
// 6. scoped_lock（C++17）—— 同时锁多个 mutex，避免死锁
// ============================================================================
void demo_scoped_lock() {
    safe_print("\n=== 6. scoped_lock（C++17）===");

    std::mutex mtx_a;
    std::mutex mtx_b;
    int resource_a = 0;
    int resource_b = 0;

    auto worker = [&](int id) {
        for (int i = 0; i < 50'000; ++i) {
            // scoped_lock 使用 deadlock-avoidance 算法同时获取两把锁
            // 等价于 std::lock(mtx_a, mtx_b) + 双 lock_guard
            std::scoped_lock lock(mtx_a, mtx_b);
            ++resource_a;
            ++resource_b;
        }
        safe_print("  线程 ", id, " 完成");
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    safe_print("  resource_a = ", resource_a, "，resource_b = ", resource_b);
    safe_print("  scoped_lock 是 C++17 最推荐的多 mutex 加锁方式。");
}

// ============================================================================
// 7. ThreadSafeStack —— 综合示例
// ============================================================================
template <typename T> class ThreadSafeStack {
  public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        stack_.push(std::move(value));
    }

    // 返回 optional<T>：空栈返回 std::nullopt 而不是抛异常
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stack_.empty()) {
            return std::nullopt;
        }
        T value = std::move(stack_.top());
        stack_.pop();
        return value;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stack_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stack_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::stack<T> stack_;
};

void demo_thread_safe_stack() {
    safe_print("\n=== 7. ThreadSafeStack 综合示例 ===");

    ThreadSafeStack<int> stack;

    // 多线程并发 push
    constexpr int kPushPerThread = 100;
    constexpr int kThreadCount = 4;
    std::vector<std::thread> push_threads;

    for (int t = 0; t < kThreadCount; ++t) {
        push_threads.emplace_back([&, t]() {
            for (int i = 0; i < kPushPerThread; ++i) {
                stack.push(t * kPushPerThread + i);
            }
        });
    }

    for (auto& th : push_threads) {
        th.join();
    }

    safe_print("  push 完成，stack.size() = ", stack.size(), "（期望 ",
               kPushPerThread * kThreadCount, "）");

    // 多线程并发 pop
    std::atomic<int> pop_count{0};
    std::vector<std::thread> pop_threads;

    for (int t = 0; t < kThreadCount; ++t) {
        pop_threads.emplace_back([&]() {
            int local_count = 0;
            while (true) {
                auto val = stack.try_pop();
                if (val.has_value()) {
                    ++local_count;
                } else {
                    break;
                }
            }
            pop_count += local_count;
        });
    }

    for (auto& th : pop_threads) {
        th.join();
    }

    safe_print("  pop 完成，共弹出 ", pop_count.load(), " 个元素");
    safe_print("  stack 最终为空：", stack.empty() ? "是" : "否");
}

// ============================================================================
// main
// ============================================================================
int main() {
    safe_print("互斥量与 RAII 锁守卫 —— 完整示例");

    demo_manual_lock_unlock();
    demo_recursive_mutex();
    demo_timed_mutex();
    demo_lock_guard();
    demo_unique_lock();
    demo_scoped_lock();
    demo_thread_safe_stack();

    safe_print("\n全部演示完成。");
    return 0;
}
