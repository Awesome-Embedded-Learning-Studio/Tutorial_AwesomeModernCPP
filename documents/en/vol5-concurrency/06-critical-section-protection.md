---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: Critical Section Protection and Synchronization Techniques
difficulty: advanced
order: 6
platform: host
prerequisites:
- 'Chapter 10.1-10.5: 原子操作、内存序、中断安全'
reading_time_minutes: 22
tags:
- cpp-modern
- host
- intermediate
title: Critical section protection techniques
---
# Modern C++ for Embedded Systems — Critical Section Protection Techniques

## Introduction

In the previous section, we discussed writing interrupt-safe code, focusing on how to correctly access shared data within an ISR. However, the main thread itself might also contain multiple execution contexts (such as RTOS tasks or threads) accessing the same resource simultaneously. In these cases, relying solely on atomic operations might not be enough.

Imagine this: you are manipulating a linked list and need to delete a node. This process involves modifying multiple pointers — if you are interrupted by another thread halfway through, the entire list structure could be corrupted. This is the **critical section problem**.

A critical section is a block of code that accesses a shared resource, and it must be executed by only one execution context at any given time. There are many ways to protect a critical section, ranging from simply disabling interrupts to complex locking mechanisms, each with its own use cases and trade-offs.

> In a nutshell: **Critical section protection ensures that only one execution context can access a shared resource at any given time.**

In this chapter, we will systematically introduce various critical section protection techniques to help you make the right choice in different scenarios.

------

## What is a Critical Section

First, let's clarify what a critical section is and why it needs protection.

### Definition of a Critical Section

A critical section must meet three conditions:

1. **Accesses shared resources**: Involves global variables, shared data structures, hardware peripherals, etc.
2. **Indivisible**: The operation cannot be interrupted or interfered with by other threads.
3. **Time-bounded**: The critical section should be as short as possible to avoid impacting system responsiveness.

### Typical Critical Section Examples

```cpp
// 示例1：链表操作
void remove_node(Node* node) {
    // ========== 临界区开始 ==========
    Node* prev = node->prev;
    Node* next = node->next;

    if (prev) prev->next = next;
    if (next) next->prev = prev;
    // ========== 临界区结束 ==========

    delete node;
}

// 示例2：多变量更新
void update_shared_state(int new_val1, int new_val2) {
    // ========== 临界区开始 ==========
    shared.value1 = new_val1;
    shared.value2 = new_val2;
    shared.timestamp = get_time();
    // ========== 临界区结束 ==========
}

// 示例3：硬件配置
void reconfigure_uart(uint32_t baudrate) {
    // ========== 临界区开始 ==========
    UART1->CR1 &= ~UART_CR1_UE;      // 禁用UART
    UART1->BRR = calculate_brr(baudrate);
    UART1->CR1 |= UART_CR1_UE;       // 重新启用
    // ========== 临界区结束 ==========
}
```

### Consequences of Unprotected Critical Sections

If critical sections are not properly protected, the following problems may arise:

1. **Data corruption**: Inconsistent states in shared data structures.
2. **Race conditions**: Program behavior depends on execution order.
3. **Dead lock**: Multiple threads waiting on each other (if locks are used).
4. **Hardware exceptions**: Peripheral misconfiguration leading to abnormal behavior.

------

## Overview of Critical Section Protection Techniques

Different application scenarios require different protection techniques. Let's first look at the available options:

| Technique | Use Case | Overhead | Limitations |
|-----------|----------|----------|-------------|
| Atomic operations | Simple variables, flags | Low | Only works with atomic types |
| Disabling interrupts | Shared between ISR and main thread | Low | Impacts interrupt response |
| Spinlock | Multi-core, short critical sections | Medium | Consumes CPU while waiting |
| Mutex | Single-core, long critical sections | High | May block |
| Read-write lock | Read-heavy, write-light workloads | Medium-high | Higher complexity |

We will discuss each of these in detail next.

------

## Method 1: Atomic Operation Protection

For simple shared variables, atomic operations are often the most lightweight solution.

### Use Cases

- Simple counters and flags
- State machine state transitions
- Single producer-consumer data passing

### Example: Reference Counting

```cpp
class RefCounted {
public:
    void ref() noexcept {
        // relaxed 足够，因为我们只关心最终值
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void unref() noexcept {
        // acq_rel 确保删除对象时能看到之前的所有操作
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    int count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }

protected:
    virtual ~RefCounted() = default;

private:
    std::atomic<int> ref_count_{0};
};
```

### Example: Lock-Free Stack

```cpp
template<typename T>
class LockFreeStack {
    struct Node {
        T data;
        Node* next;
    };

public:
    void push(T value) {
        Node* node = new Node{std::move(value), nullptr};

        // CAS 循环直到成功
        Node* old_head = head_.load(std::memory_order_acquire);
        do {
            node->next = old_head;
        } while (!head_.compare_exchange_weak(
            old_head, node,
            std::memory_order_release,
            std::memory_order_acquire));
    }

    bool pop(T& out) {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head) {
            Node* next = old_head->next;
            if (head_.compare_exchange_weak(
                old_head, next,
                std::memory_order_release,
                std::memory_order_acquire)) {
                out = std::move(old_head->data);
                delete old_head;
                return true;
            }
            // CAS 失败，old_head 已被更新为最新值，重试
        }
        return false;
    }

private:
    std::atomic<Node*> head_{nullptr};
};
```

### Limitations

Atomic operations can only be used with **atomic types** (typically integers, pointers, etc.). For complex data structures (like linked lists or trees), you need to consider lock-free algorithms, which significantly increase complexity.

------

## Method 2: Disabling Interrupts

In RTOS or bare-metal environments, disabling interrupts is the most straightforward protection method. By temporarily disabling interrupts, you ensure that the current code cannot be preempted.

### Basic Principle

```cpp
// 平台相关的中断控制函数
extern "C" {
    uint32_t __disable_irq();   // 禁用中断，返回之前状态
    void __enable_irq();        // 恢复中断
    void __restore_irq(uint32_t state);  // 恢复到指定状态
}

// 临界区保护
class InterruptGuard {
public:
    InterruptGuard() noexcept
        : state_(__disable_irq())
    {}

    ~InterruptGuard() noexcept {
        __restore_irq(state_);
    }

    // 禁止拷贝和移动
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;

private:
    uint32_t state_;
};

// 使用示例
void update_shared_variable(int new_value) {
    InterruptGuard guard;  // 进入临界区
    shared_var = new_value;
    // 离开作用域，自动恢复中断
}
```

### Pros and Cons

**Pros**:

- Simple to implement and intuitive to understand
- Low overhead (a few instructions)
- Can protect any critical section

**Cons**:

- Impacts interrupt response time
- Ineffective on multi-core systems (only disables interrupts on the current core)
- Not suitable for protecting time-consuming operations

### Correct Usage Principles

```cpp
// ✅ 好的做法：临界区尽可能短
void good_example() {
    int temp = complex_calculation();  // 在临界区外计算

    InterruptGuard guard;
    shared_var = temp;  // 只保护必要的操作
    // guard 析构，自动恢复中断
}

// ❌ 不好的做法：临界区太长
void bad_example() {
    InterruptGuard guard;
    int temp = complex_calculation();  // 浪费中断响应时间！
    shared_var = temp;
    // guard 析构
}
```

### Usage in RTOS Environments

In an RTOS, there are usually dedicated APIs for managing critical sections:

```cpp
// FreeRTOS 风格
class CriticalSection {
public:
    CriticalSection() noexcept {
        portENTER_CRITICAL();
    }

    ~CriticalSection() noexcept {
        portEXIT_CRITICAL();
    }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};

// RT-Thread 风格
class CriticalSection {
public:
    CriticalSection() noexcept
        : level_(rt_hw_interrupt_disable())
    {}

    ~CriticalSection() noexcept {
        rt_hw_interrupt_enable(level_);
    }

private:
    rt_base_t level_;
};
```

------

## Method 3: Spinlock

A spinlock is a "busy-wait" lock where the thread trying to acquire the lock loops continuously to check if it is available, rather than going to sleep.

### Basic Implementation

```cpp
class SpinLock {
public:
    SpinLock() noexcept : flag_(false) {}

    void lock() noexcept {
        // 一直尝试获取锁
        while (!try_lock()) {
            // 可以加个 CPU 指令降低功耗
            #if defined(__x86_64__)
            _mm_pause();  // x86 PAUSE 指令
            #elif defined(__ARM_ARCH)
            __yield();    // ARM YIELD 指令
            #endif
        }
    }

    bool try_lock() noexcept {
        // test_and_set 是原子操作
        return !flag_.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_;
};

// RAII 封装
class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock) noexcept
        : lock_(lock)
    {
        lock_.lock();
    }

    ~SpinLockGuard() noexcept {
        lock_.unlock();
    }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

private:
    SpinLock& lock_;
};
```

### Use Cases

**Suitable for**:

- Multi-core systems
- Very short critical sections (a few instructions)
- Scenarios where blocking is not allowed (e.g., inside an ISR)

**Not suitable for**:

- Single-core systems (spinning wastes CPU cycles)
- Long critical sections
- Scenarios prone to priority inversion

### Performance Considerations

Spinlock performance depends on:

1. **Contention level**: Higher contention leads to longer spin times.
2. **Critical section length**: Shorter critical sections yield higher overall efficiency.
3. **Number of CPU cores**: Spinlocks are relatively more effective on multi-core systems.

```cpp
// 性能优化示例
class OptimizedSpinLock {
public:
    void lock() noexcept {
        // 先尝试快速获取
        if (!flag_.test_and_set(std::memory_order_acquire)) {
            return;
        }

        // 失败后再自旋等待
        constexpr int spin_limit = 100;
        int spin_count = 0;

        while (flag_.test_and_set(std::memory_order_acquire)) {
            if (++spin_count < spin_limit) {
                // 短暂自旋
                #if defined(__x86_64__)
                _mm_pause();
                #elif defined(__ARM_ARCH)
                __yield();
                #endif
            } else {
                // 长时间等待，让出CPU
                std::this_thread::yield();
                spin_count = 0;
            }
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
```

------

## Method 4: Mutex

A mutex is one of the most commonly used synchronization primitives. Unlike a spinlock, a thread that fails to acquire the mutex goes to sleep and yields the CPU.

### Basic Usage (Reviewing Chapter 10.4)

```cpp
#include <mutex>

std::mutex mtx;
int shared_data = 0;

void update_data() {
    std::lock_guard<std::mutex> lock(mtx);
    shared_data++;
}
```

### Use Cases

**Suitable for**:

- Single-core systems
- Long critical sections
- Scenarios where blocking is acceptable

**Not suitable for**:

- Inside an ISR (strictly prohibited)
- Scenarios with extreme real-time requirements

### Pairing with Condition Variables

Mutexes are often paired with condition variables to implement complex synchronization logic:

```cpp
template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push(value);
        cv_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T value = queue_.front();
        queue_.pop();
        return value;
    }

    bool try_pop(T& value, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty(); })) {
            value = queue_.front();
            queue_.pop();
            return true;
        }
        return false;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};
```

### Read-Write Locks (shared_mutex)

For data that is read frequently but written to infrequently, read-write locks can improve concurrent performance:

```cpp
#include <shared_mutex>

class ThreadSafeCache {
public:
    // 写操作：独占访问
    void update(const std::string& key, int value) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        cache_[key] = value;
    }

    // 读操作：共享访问
    std::optional<int> lookup(const std::string& key) const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

private:
    mutable std::shared_mutex mtx_;
    std::unordered_map<std::string, int> cache_;
};
```

**Note**: `std::shared_mutex` was introduced in C++17. It may not be supported or may have a heavy implementation on certain embedded platforms.

------

## Method 5: Priority Ceiling Protocol

In RTOS environments, using a mutex can trigger the **priority inversion** problem. The priority ceiling protocol is one solution.

### The Priority Inversion Problem

```text
高优先级任务  等待资源
                  ↓
            低优先级任务 (持有资源)
                  ↓
            中优先级任务 (抢占低优先级任务)
```

The result is that the high-priority task is indirectly blocked by the medium-priority task, making it appear as if its priority has been lowered.

### Priority Ceiling Protocol

Basic idea: When a task acquires a lock, its priority is temporarily elevated to the highest priority among all tasks that might ever use that lock.

```cpp
// 伪代码示例（依赖RTOS支持）
class PriorityCeilingMutex {
public:
    explicit PriorityCeilingMutex(int ceiling_priority)
        : ceiling_priority_(ceiling_priority)
    {}

    void lock() {
        // 提升当前任务优先级
        original_priority_ = get_current_priority();
        set_current_priority(ceiling_priority_);

        // 尝试获取锁
        internal_lock_.lock();
    }

    void unlock() {
        internal_lock_.unlock();

        // 恢复原优先级
        set_current_priority(original_priority_);
    }

private:
    int ceiling_priority_;
    int original_priority_;
    std::mutex internal_lock_;
};
```

### Support in Real-World RTOS

Many RTOSes directly support the priority ceiling protocol:

- **FreeRTOS**: Specify the priority inheritance attribute when creating a mutex
- **RT-Thread**: Mutexes support priority inheritance
- **Zephyr**: Priority inheritance is the default option

```cpp
// FreeRTOS 示例
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
// 默认支持优先级继承
```

------

## Nested Critical Sections

When a critical section needs to access another resource protected by a different lock, we encounter the nested critical section problem.

### Problem Example

```cpp
std::mutex mtx_a;
std::mutex mtx_b;

// 线程1
void thread1() {
    std::lock_guard<std::mutex> lock_a(mtx_a);
    // ...
    std::lock_guard<std::mutex> lock_b(mtx_b);
}

// 线程2
void thread2() {
    std::lock_guard<std::mutex> lock_b(mtx_b);
    // ...
    std::lock_guard<std::mutex> lock_a(mtx_a);
}
```

If Thread 1 acquires Lock A and Thread 2 acquires Lock B, and then they wait on each other, a **dead lock** occurs.

### Solution 1: Fixed Locking Order

```cpp
class Account {
public:
    static void transfer(Account& from, Account& to, int amount) {
        // 始终按照地址大小排序，确保顺序一致
        if (&from < &to) {
            from.lock();
            to.lock();
        } else {
            to.lock();
            from.lock();
        }

        from.balance -= amount;
        to.balance += amount;

        to.unlock();
        from.unlock();
    }

private:
    std::mutex mtx;
    int balance;

    void lock() { mtx.lock(); }
    void unlock() { mtx.unlock(); }
};
```

### Solution 2: Using scoped_lock (C++17)

```cpp
void safe_function() {
    // scoped_lock 会自动避免死锁
    std::scoped_lock lock(mtx_a, mtx_b);
    // 临界区...
    // 离开作用域自动解锁
}
```

`std::scoped_lock` uses an algorithm similar to `std::lock()` to ensure all locks are acquired in a consistent order, avoiding dead locks.

------

## Performance and Best Practices

### Principle 1: Keep Critical Sections as Short as Possible

```cpp
// ❌ 不好
void bad() {
    std::lock_guard<std::mutex> lock(mtx);
    int result = expensive_computation();  // 不需要锁！
    shared_data = result;
    more_work();  // 不需要锁！
}

// ✅ 好
void good() {
    int result = expensive_computation();  // 锁外计算
    {
        std::lock_guard<std::mutex> lock(mtx);
        shared_data = result;  // 只锁必要的操作
    }
    more_work();  // 锁外执行
}
```

### Principle 2: Avoid Calling External Code Inside a Critical Section

```cpp
// ❌ 危险
void dangerous() {
    std::lock_guard<std::mutex> lock(mtx);
    user_callback();  // 不知道回调会做什么
}

// ✅ 安全
void safe() {
    auto result = user_callback();  // 先调用
    std::lock_guard<std::mutex> lock(mtx);
    shared_data = result;
}
```

### Principle 3: Prefer Higher-Level Abstractions

```cpp
// ✅ 使用 RAII
std::lock_guard<std::mutex> lock(mtx);

// ❌ 避免手动管理
mtx.lock();
// ... 如果抛异常，unlock 不会执行！
mtx.unlock();
```

### Principle 4: Choose the Appropriate Synchronization Mechanism

```text
                   需要保护的是？
                        |
        -------------------------------
        |              |              |
     简单变量        复杂结构        ISR-主线程共享
        |              |              |
    原子操作          |          关中断 + 原子变量
                      |
             在单核还是多核？
                   |
         -----------------
         |               |
        单核            多核
         |               |
      互斥锁          自旋锁
         |               |
      读写锁(读多写少)  读写锁
```

------

## Common Pitfalls

### Pitfall 1: Forgetting Lock Granularity

```cpp
class BadExample {
    std::mutex mtx;
    std::vector<int> data;

public:
    // ❌ 每个操作都加锁，效率低
    void push(int val) {
        std::lock_guard<std::mutex> lock(mtx);
        data.push_back(val);
    }

    int size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return data.size();
    }

    // ✅ 批量操作只加一次锁
    void push_multiple(const std::vector<int>& vals) {
        std::lock_guard<std::mutex> lock(mtx);
        for (int v : vals) {
            data.push_back(v);
        }
    }
};
```

### Pitfall 2: Managing Lock Lifecycles

```cpp
// ❌ 临时对象立即析构
void bad_function() {
    std::lock_guard<std::mutex>(mtx);  // 创建临时对象！
    shared_data++;  // 没有保护
}

// ✅ 给变量命名
void good_function() {
    std::lock_guard<std::mutex> lock(mtx);
    shared_data++;
}
```

### Pitfall 3: Reacquiring the Same Lock While Already Holding It

```cpp
std::recursive_mutex rmtx;

void recursive_function(int n) {
    std::lock_guard<std::recursive_mutex> lock(rmtx);
    if (n > 0) {
        recursive_function(n - 1);  // 递归，需要递归锁
    }
}

// 但递归锁通常是设计不良的信号，应该重构
```

### Pitfall 4: Inconsistent Locking Order in Multi-Lock Scenarios

```cpp
// ❌ 可能死锁
void dangerous() {
    std::lock(mtx_a, mtx_b);  // 顺序 A, B
}

void also_dangerous() {
    std::lock(mtx_b, mtx_a);  // 顺序 B, A
}

// ✅ 始终一致的顺序
void safe() {
    std::scoped_lock lock(mtx_a, mtx_b);  // 自动处理顺序
}
```

------

## Summary

Critical section protection is a core skill in concurrent programming. Let's review the key points:

1. **Atomic operations**: The most lightweight approach, suitable for simple variables.
2. **Disabling interrupts**: Suitable for sharing between ISRs and the main thread, with short critical sections.
3. **Spinlocks**: Suitable for multi-core systems and short critical sections.
4. **Mutexes**: Suitable for single-core systems and long critical sections, but strictly prohibited in ISRs.
5. **Read-write locks**: An optimization for read-heavy, write-light scenarios.
6. **Priority ceiling**: Prevents priority inversion in RTOS environments.
7. **Nested critical sections**: Use `scoped_lock` to avoid dead locks.

**Practical advice**:

- Choose the appropriate synchronization mechanism based on the scenario.
- Keep critical sections as short as possible.
- Prefer RAII wrappers.
- Avoid calling external code inside critical sections.
- Pay attention to locking order in multi-lock scenarios.
- In an RTOS, prioritize priority inheritance/ceiling protocols.

**Next steps**:

- Learn the synchronization APIs of specific RTOSes.
- Analyze dead lock cases in practice.
- Study advanced lock-free data structures.
- Understand the role of memory barriers in critical sections.

This chapter, along with the previous few, forms the foundation of modern C++ concurrent programming. Mastering these techniques will enable you to write safe, efficient concurrent code in embedded systems.
