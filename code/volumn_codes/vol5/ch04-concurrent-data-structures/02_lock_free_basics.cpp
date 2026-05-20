/*
 * 无锁编程基础 —— CAS 循环、原子计数器、Treiber Stack、ABA 问题
 *
 * 本文件演示无锁编程的核心原语与经典数据结构：
 *   1. 用 CAS 循环实现 atomic fetch_max
 *   2. 用 fetch_add 实现无锁计数器
 *   3. Treiber Stack：基于 CAS 的无锁栈（push / pop）
 *   4. ABA 问题的概念演示
 *   5. 用 std::atomic<std::shared_ptr> (C++20) 缓解 ABA
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/02_lock_free_basics
 *
 * 编译器：GCC 12+ | Clang 15+（需要 C++20 std::atomic<std::shared_ptr>）
 * 平台：x86-64 Linux
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// ===========================================================================
// 1. CAS 循环实现 atomic fetch_max
// ===========================================================================

/// @brief 用 CAS 循环对 atomic<int> 执行 fetch_max 操作
///        即：将 current 值与 candidate 取较大者写回，返回旧值
///        C++26 引入了 std::atomic<T>::fetch_max，在此之前需要手动实现
static int atomic_fetch_max(std::atomic<int>& target, int candidate) {
    int old = target.load(std::memory_order_relaxed);
    while (old < candidate) {
        // compare_exchange_weak：如果 target == old，则写入 candidate
        // 否则将 target 的当前值加载到 old
        if (target.compare_exchange_weak(old,       // expected：期望值（会被更新为当前值）
                                         candidate, // desired：要写入的新值
                                         std::memory_order_acq_rel,  // 成功时的内存序
                                         std::memory_order_relaxed)) // 失败时的内存序
        {
            break; // CAS 成功，退出循环
        }
        // CAS 失败，old 已被更新为 target 的当前值，继续循环
    }
    return old;
}

static void demo_cas_fetch_max() {
    std::cout << "\n=== Demo 1: CAS 循环实现 fetch_max ===\n";

    std::atomic<int> max_val{0};
    constexpr int kThreadCount = 8;
    constexpr int kIterations = 100'000;

    // 每个线程尝试将自己的线程 ID 写入 max_val，取最大值
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&max_val, t]() {
            for (int i = 0; i < kIterations; ++i) {
                atomic_fetch_max(max_val, t * 10 + i);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    int expected_max = (kThreadCount - 1) * 10 + kIterations - 1;
    std::cout << "  最终 max_val = " << max_val.load() << "（期望 " << expected_max << "）\n";
    assert(max_val.load() == expected_max);
    std::cout << "  CAS fetch_max 验证通过\n";
}

// ===========================================================================
// 2. 无锁计数器 —— fetch_add
// ===========================================================================

static void demo_lock_free_counter() {
    std::cout << "\n=== Demo 2: 无锁计数器（fetch_add）===\n";

    std::atomic<int> counter{0};
    constexpr int kThreadCount = 8;
    constexpr int kIterations = 1'000'000;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&counter]() {
            for (int i = 0; i < kIterations; ++i) {
                // fetch_add 是原子的，无需加锁
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    int expected = kThreadCount * kIterations;
    std::cout << "  最终计数 = " << counter.load() << "（期望 " << expected << "）\n";
    std::cout << "  耗时: " << ms.count() << " ms\n";
    assert(counter.load() == expected);
    std::cout << "  无锁计数器验证通过\n";
}

// ===========================================================================
// 3. Treiber Stack —— 经典无锁栈
// ===========================================================================

/// @brief Treiber Stack 的节点
template <typename T> struct StackNode {
    T data;
    StackNode* next_;

    explicit StackNode(T val) : data(std::move(val)), next_(nullptr) {}
};

/// @brief Treiber Stack：基于 CAS 的无锁栈
/// @tparam T 元素类型
/// @note  此实现存在 ABA 问题（见 Demo 4 的讨论）
template <typename T> class TreiberStack {
  public:
    TreiberStack() = default;

    // 不处理析构中的内存回收（简化演示，真实场景需 hazard pointer 等）
    ~TreiberStack() {
        T val;
        while (try_pop(val)) {
            // 排空
        }
    }

    /// @brief 压入元素（无锁）
    void push(T value) {
        auto* new_node = new StackNode<T>(std::move(value));
        new_node->next_ = head_.load(std::memory_order_relaxed);

        // CAS 循环：尝试将 new_node 设为新的 head
        while (!head_.compare_exchange_weak(new_node->next_, // expected：当前 head
                                            new_node,        // desired：新节点
                                            std::memory_order_release, std::memory_order_relaxed)) {
            // CAS 失败，new_node->next_ 已被更新为最新 head，重试
        }
    }

    /// @brief 弹出元素（无锁）
    /// @param out 输出参数
    /// @return true 成功弹出，false 栈为空
    bool try_pop(T& out) {
        StackNode<T>* old_head = head_.load(std::memory_order_relaxed);

        while (old_head != nullptr) {
            if (head_.compare_exchange_weak(old_head,        // expected：当前 head
                                            old_head->next_, // desired：head->next
                                            std::memory_order_acquire, std::memory_order_relaxed)) {
                // CAS 成功，我们拿到了 old_head
                out = std::move(old_head->data);
                delete old_head;
                return true;
            }
            // CAS 失败，old_head 已被更新，重试
        }

        return false; // 栈空
    }

    /// @brief 检查栈是否为空（近似值，无锁环境下可能过时）
    bool empty() const { return head_.load(std::memory_order_relaxed) == nullptr; }

  private:
    std::atomic<StackNode<T>*> head_{nullptr};
};

static void demo_treiber_stack() {
    std::cout << "\n=== Demo 3: Treiber Stack（无锁栈）===\n";

    TreiberStack<int> stack;
    constexpr int kThreadCount = 4;
    constexpr int kItemsPerThread = 50'000;

    // 多线程并发 push
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreadCount; ++t) {
        producers.emplace_back([&stack, t]() {
            for (int i = 0; i < kItemsPerThread; ++i) {
                stack.push(t * kItemsPerThread + i);
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    // 统计 pop 出的元素数量
    int total_popped = 0;
    int value;
    while (stack.try_pop(value)) {
        ++total_popped;
    }

    int expected = kThreadCount * kItemsPerThread;
    std::cout << "  push 总量: " << expected << "\n";
    std::cout << "  pop 总量:  " << total_popped << "\n";
    assert(total_popped == expected);
    std::cout << "  Treiber Stack 验证通过\n";
}

// ===========================================================================
// 4. ABA 问题概念演示
// ===========================================================================

/// @brief 演示 ABA 问题的触发条件
///
/// ABA 问题的经典场景：
///   1. 线程 A 读取 head_ = 节点 X（值为 A）
///   2. 线程 A 被抢占
///   3. 线程 B pop X，push 新节点 Y（值也为 A），然后又 pop Y，
///      最终 push 一个新节点 Z，其地址恰好复用了 X 的地址
///   4. 线程 A 恢复，CAS 比较发现 head_ == X（指针值相同），CAS 成功
///      但此时 head_ 的语义已经完全不同了 -> 数据损坏
///
/// 这里我们不实际触发 ABA（太依赖调度器的时序），而是用注释解释
/// 并在 Demo 5 中展示用 std::atomic<std::shared_ptr> 解决的方案
static void demo_aba_explanation() {
    std::cout << "\n=== Demo 4: ABA 问题概念解释 ===\n";
    std::cout << "  ABA 问题描述：\n"
              << "    线程 A 读取指针值 P，被抢占。\n"
              << "    线程 B 在此期间修改了数据结构，\n"
              << "    但恰好又把指针恢复成了 P。\n"
              << "    线程 A 恢复后 CAS 成功——但语义已经错了。\n\n"
              << "  经典触发场景：Treiber Stack + 内存地址复用\n"
              << "    1. 线程 A 读 head_ = node_A，记住 node_A->next_\n"
              << "    2. 其他线程 pop node_A，delete 它\n"
              << "    3. 后续 push 中 new 恰好复用了 node_A 的地址\n"
              << "    4. 线程 A CAS 发现 head_ == node_A（地址相同），成功\n"
              << "    5. 但 head_ 现在指向的链表结构已完全不同 -> 损坏\n\n"
              << "  解决方案：\n"
              << "    - 带版本号的指针（tagged pointer）\n"
              << "    - hazard pointer / epoch-based 回收\n"
              << "    - C++20 std::atomic<std::shared_ptr>（引用计数防复用）\n";
}

// ===========================================================================
// 5. 用 std::atomic<std::shared_ptr> 缓解 ABA（C++20）
// ===========================================================================

/// @brief 基于 std::atomic<std::shared_ptr> 的无锁栈节点
///        shared_ptr 的引用计数保证节点不会被提前回收，
///        从而从根本上避免 ABA 中的地址复用问题
template <typename T> struct SharedNode {
    T data;
    std::shared_ptr<SharedNode<T>> next_;

    explicit SharedNode(T val) : data(std::move(val)), next_(nullptr) {}
};

/// @brief 使用 std::atomic<std::shared_ptr> 的无锁栈（C++20）
///        shared_ptr 的控制块在引用计数归零前不会被回收，
///        因此另一个线程持有的旧 shared_ptr 仍然指向有效节点，
///        CAS 比较时能正确检测到 head_ 已被修改
template <typename T> class SharedPtrStack {
  public:
    /// @brief 压入元素
    void push(T value) {
        auto new_node = std::make_shared<SharedNode<T>>(std::move(value));
        new_node->next_ = head_.load(std::memory_order_relaxed);

        while (!head_.compare_exchange_weak(new_node->next_, new_node, std::memory_order_release,
                                            std::memory_order_relaxed)) {
            // CAS 失败，重试
        }
    }

    /// @brief 弹出元素
    /// @param out 输出参数
    /// @return true 成功，false 栈空
    bool try_pop(T& out) {
        auto old_head = head_.load(std::memory_order_relaxed);

        while (old_head) {
            // old_head 持有 shared_ptr，引用计数 >= 1
            // 即使其他线程同时 pop 并 reset，节点也不会被回收
            if (head_.compare_exchange_weak(old_head, old_head->next_, std::memory_order_acquire,
                                            std::memory_order_relaxed)) {
                out = std::move(old_head->data);
                // old_head 离开作用域，引用计数 -1
                return true;
            }
        }

        return false;
    }

    /// @brief 栈是否为空（近似值）
    bool empty() const { return head_.load(std::memory_order_relaxed) == nullptr; }

  private:
    // C++20: std::atomic<std::shared_ptr> 是特化实现
    // 内部通常使用 spinlock 或 CAS + 版本号
    std::atomic<std::shared_ptr<SharedNode<T>>> head_{nullptr};
};

static void demo_shared_ptr_stack() {
    std::cout << "\n=== Demo 5: std::atomic<shared_ptr> 无锁栈（抗 ABA）===\n";

    SharedPtrStack<int> stack;
    constexpr int kThreadCount = 4;
    constexpr int kItemsPerThread = 50'000;

    // 多线程并发 push
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreadCount; ++t) {
        producers.emplace_back([&stack, t]() {
            for (int i = 0; i < kItemsPerThread; ++i) {
                stack.push(t * kItemsPerThread + i);
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    // 统计 pop 出的元素数量
    int total_popped = 0;
    int value;
    while (stack.try_pop(value)) {
        ++total_popped;
    }

    int expected = kThreadCount * kItemsPerThread;
    std::cout << "  push 总量: " << expected << "\n";
    std::cout << "  pop 总量:  " << total_popped << "\n";
    assert(total_popped == expected);
    std::cout << "  shared_ptr 无锁栈验证通过\n";
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    demo_cas_fetch_max();
    demo_lock_free_counter();
    demo_treiber_stack();
    demo_aba_explanation();
    demo_shared_ptr_stack();

    std::cout << "\n所有演示完成。\n";
    return 0;
}
