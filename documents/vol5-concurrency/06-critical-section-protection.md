---
title: "临界区保护技术"
description: "临界区保护与同步技术"
chapter: 10
order: 6
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: advanced
reading_time_minutes: 30
prerequisites:
  - "Chapter 10.1-10.5: 原子操作、内存序、中断安全"
cpp_standard: [11, 14, 17, 20]
platform: host
---

# 嵌入式现代C++开发——临界区保护技术

## 引言

上一节我们讨论了中断安全的代码编写，重点是如何在ISR中正确地访问共享数据。但问题是，主线程内部也可能有多个执行上下文（比如RTOS的任务、线程）同时访问同一资源，这时光靠原子操作可能不够了。

想象一下：你正在操作一个链表，需要删除一个节点。这个过程涉及多个指针的修改——如果中途被另一个线程打断，整个链表结构可能就被破坏了。这就是**临界区问题**。

临界区（Critical Section）是指访问共享资源的那段代码，这段代码在任何时刻只能由一个执行上下文运行。保护临界区的方法有很多，从简单的关中断到复杂的锁机制，每种都有其适用场景和代价。

> 一句话总结：**临界区保护就是确保同一时刻只有一个执行上下文可以访问共享资源。**

本章我们将系统地介绍各种临界区保护技术，帮助你在不同场景下做出正确选择。

------

## 什么是临界区

首先，让我们明确什么是临界区，以及为什么需要保护。

### 临界区的定义

临界区需要满足三个条件：

1. **访问共享资源**：涉及全局变量、共享数据结构、硬件外设等
2. **不可分割**：操作过程中不能被中断或其他线程干扰
3. **有时间限制**：临界区应该尽可能短，避免影响系统响应

### 典型的临界区示例

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

### 不保护临界区的后果

如果不正确保护临界区，可能出现以下问题：

1. **数据损坏**：共享数据结构状态不一致
2. **竞态条件**：程序行为取决于执行顺序
3. **死锁**：多个线程互相等待（如果用了锁）
4. **硬件异常**：外设配置错误导致异常行为

------

## 临界区保护技术概览

不同的应用场景需要不同的保护技术。让我们先看看有哪些选择：

| 技术 | 适用场景 | 开销 | 限制 |
|------|----------|------|------|
| 原子操作 | 简单变量、标志位 | 低 | 只能用于原子类型 |
| 关中断 | ISR与主线程共享 | 低 | 影响中断响应 |
| 自旋锁 | 多核、短临界区 | 中 | 占用CPU等待 |
| 互斥锁 | 单核、长临界区 | 高 | 可能阻塞 |
| 读写锁 | 读多写少 | 中高 | 复杂度较高 |

接下来我们逐一深入讨论。

------

## 方法1：原子操作保护

对于简单的共享变量，原子操作往往是最轻量级的解决方案。

### 适用场景

- 简单的计数器、标志位
- 状态机状态转换
- 单个生产者-消费者的数据传递

### 示例：引用计数

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

### 示例：无锁栈

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

### 限制

原子操作只能用于**原子类型**（通常是整型、指针等）。对于复杂的数据结构（如链表、树），需要考虑无锁算法，这会大大增加复杂度。

------

## 方法2：关中断保护

在RTOS或裸机环境中，关中断是最直接的保护方式。通过暂时禁用中断，确保当前代码不会被打断。

### 基本原理

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

### 优缺点

**优点**：

- 实现简单，理解直观
- 开销低（几条指令）
- 可以保护任何临界区

**缺点**：

- 影响中断响应时间
- 在多核系统中无效（只禁用当前核的中断）
- 不适合保护耗时操作

### 正确使用原则

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

### RTOS 环境下的使用

在RTOS中，通常有专门的API来管理临界区：

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

## 方法3：自旋锁

自旋锁是一种"忙等待"的锁，获取锁的线程会循环检查锁是否可用，而不是进入睡眠。

### 基本实现

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

### 适用场景

**适合**：

- 多核系统
- 临界区非常短（几条指令）
- 不能阻塞的场景（如ISR）

**不适合**：

- 单核系统（自旋浪费CPU）
- 长临界区
- 可能发生优先级反转的场景

### 性能考虑

自旋锁的性能取决于：

1. **竞争程度**：竞争越严重，自旋时间越长
2. **临界区长度**：临界区越短，整体效率越高
3. **CPU 核心数**：多核时自旋锁相对更有效

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

## 方法4：互斥锁

互斥锁（Mutex）是最常用的同步原语之一。与自旋锁不同，获取不到锁的线程会进入睡眠，让出CPU。

### 基本用法（回顾 Chapter 10.4）

```cpp
#include <mutex>

std::mutex mtx;
int shared_data = 0;

void update_data() {
    std::lock_guard<std::mutex> lock(mtx);
    shared_data++;
}
```

### 适用场景

**适合**：

- 单核系统
- 长临界区
- 可以阻塞等待的场景

**不适合**：

- ISR 中（绝对禁止）
- 实时性要求极高的场景

### 配合条件变量

互斥锁常与条件变量配合使用，实现复杂的同步逻辑：

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

### 读写锁（shared_mutex）

对于读多写少的数据，读写锁可以提高并发性能：

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

**注意**：`std::shared_mutex` 在C++17引入。在某些嵌入式平台上可能不支持或实现很重。

------

## 方法5：优先级天花板协议

在RTOS环境中，使用互斥锁可能引发**优先级反转**问题。优先级天花板协议是一种解决方案。

### 优先级反转问题

```text
高优先级任务  等待资源
                  ↓
            低优先级任务 (持有资源)
                  ↓
            中优先级任务 (抢占低优先级任务)
```

结果是高优先级任务被中优先级任务间接阻塞，表现像是优先级降低了。

### 优先级天花板协议

基本思想：当一个任务获取锁时，它的优先级临时提升到所有可能使用该锁的任务中的最高优先级。

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

### 实际RTOS的支持

许多RTOS直接支持优先级天花板协议：

- **FreeRTOS**: 创建互斥量时指定优先级继承属性
- **RT-Thread**: 互斥量支持优先级继承
- **Zephyr**: 优先级继承是默认选项

```cpp
// FreeRTOS 示例
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
// 默认支持优先级继承
```

------

## 嵌套临界区

当一个临界区内需要访问另一个被不同锁保护的资源时，就产生了嵌套临界区的问题。

### 问题示例

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

如果线程1拿到A，线程2拿到B，然后互相等待对方，就发生了**死锁**。

### 解决方案1：固定加锁顺序

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

### 解决方案2：使用 scoped_lock（C++17）

```cpp
void safe_function() {
    // scoped_lock 会自动避免死锁
    std::scoped_lock lock(mtx_a, mtx_b);
    // 临界区...
    // 离开作用域自动解锁
}
```

`std::scoped_lock` 使用类似 `std::lock()` 的算法，确保所有锁以一致的顺序获取，避免死锁。

------

## 性能与最佳实践

### 原则1：临界区尽可能短

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

### 原则2：避免在临界区内调用外部代码

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

### 原则3：优先使用高层抽象

```cpp
// ✅ 使用 RAII
std::lock_guard<std::mutex> lock(mtx);

// ❌ 避免手动管理
mtx.lock();
// ... 如果抛异常，unlock 不会执行！
mtx.unlock();
```

### 原则4：选择合适的同步机制

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

## 常见陷阱

### 陷阱1：忘记锁的粒度

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

### 陷阱2：锁的生命周期管理

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

### 陷阱3：在持有锁时重新获取同一把锁

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

### 陷阱4：多锁场景的顺序不一致

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

## 小结

临界区保护是并发编程的核心技能。让我们回顾一下关键点：

1. **原子操作**：最轻量，适合简单变量
2. **关中断**：适合ISR与主线程共享，短临界区
3. **自旋锁**：适合多核、短临界区
4. **互斥锁**：适合单核、长临界区，但ISR中不能用
5. **读写锁**：读多写少场景的优化
6. **优先级天花板**：RTOS环境中防止优先级反转
7. **嵌套临界区**：使用 `scoped_lock` 避免死锁

**实践建议**：

- 根据场景选择合适的同步机制
- 临界区尽可能短
- 优先使用RAII封装
- 避免在临界区内调用外部代码
- 多锁场景注意加锁顺序
- 在RTOS中优先考虑优先级继承/天花板

**下一步建议**：

- 学习具体RTOS的同步API
- 实践中分析死锁案例
- 研究更高级的无锁数据结构
- 了解内存屏障在临界区中的作用

本章和前面几章构成了现代C++并发编程的基础。掌握这些技术，你就能在嵌入式系统中写出安全、高效的并发代码了。
