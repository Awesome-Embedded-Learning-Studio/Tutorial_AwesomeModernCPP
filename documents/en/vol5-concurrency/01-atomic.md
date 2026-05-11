---
title: std::atomic atomic operations
description: Detailed Explanation of C++ Atomic Operations
chapter: 10
order: 1
tags:
- cpp-modern
- host
- intermediate
difficulty: advanced
reading_time_minutes: 20
prerequisites:
- 'Chapter 9: 函数式特性'
cpp_standard:
- 11
- 14
- 17
- 20
platform: host
---
# Modern C++ for Embedded Systems—Atomic Operations (std::atomic)

## Introduction

Have you ever run into a baffling situation when writing multithreaded code: a variable is clearly modified in the main thread, but a worker thread still reads the old value? Or worse, two threads increment a counter simultaneously, it runs ten thousand times, yet the counter only shows a little over nine thousand?

Honestly, the first time I fell into this trap, I stared at the code for half a day. Single-stepping through the debugger, everything looked fine, but as soon as I ran it freely, all sorts of weird things happened. Later, I learned that this is called a data race, the number one killer in concurrent programming.

In embedded development, we frequently handle collaboration between interrupts and tasks. The traditional approach uses interrupt disabling, spinlocks, and similar mechanisms, but they all carry overhead. C++11 brought us a lighter-weight solution—`std::atomic`.

> In a nutshell: **An atomic operation is an indivisible memory access; it either executes completely or not at all, and cannot be interrupted by other threads.**

------

## Why We Need Atomic Operations

Let's start with a problematic example:

```cpp
#include <thread>
#include <iostream>

int counter = 0;

void increment() {
    for (int i = 0; i < 10000; ++i) {
        counter++;  // 看似简单，实则危险
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << '\n';
}

```

You expect an output of 20000, but in practice, you might get a random number like 12583 or 16847. Where is the problem? The `counter++` operation is actually three steps at the CPU level:

1. Read the value of counter from memory
2. Add 1 to the value
3. Write the new value back to memory

When two threads execute simultaneously, this timing can occur:

- Thread 1 reads counter=100
- Thread 2 also reads counter=100
- Thread 1 adds 1 and writes back 101
- Thread 2 adds 1 and writes back 101
- Result: two increments only increased the value by 1

This is a data race. We can easily fix it using `std::atomic`:

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};  // 现在是原子变量了

void increment() {
    for (int i = 0; i < 10000; ++i) {
        counter++;  // 原子地自增
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << '\n';
    // 保证输出：counter = 20000
}

```

------

## Basic Usage of std::atomic

`std::atomic` is a class template that can atomically wrap integers, pointers, and even user-defined types. However, user-defined types have requirements: they must be trivially copyable and cannot have things like virtual functions.

### Defining Atomic Variables

```cpp
#include <atomic>

// 基本整型
std::atomic<int> ai;
std::atomic<unsigned long> aul;

// 带初始值
std::atomic<int> counter{0};
std::atomic<bool> flag{false};

// 指针类型
struct Node { int value; };
std::atomic<Node*> node_ptr;

// C++20起支持浮点
std::atomic<double> atomic_double{0.0};

```

### store and load: The Most Basic Read and Write

We cannot read and write atomic variables directly using `=` and normal value retrieval; we must use dedicated methods:

```cpp
std::atomic<int> value{0};

// 存储：写入值
value.store(42);                    // 默认内存序
value.store(42, std::memory_order_relaxed);  // 指定内存序

// 加载：读取值
int x = value.load();               // 默认内存序
int y = value.load(std::memory_order_relaxed);

// 更简洁的方式：隐式转换
int z = value;  // 等同于 value.load()
value = 100;    // 等同于 value.store(100)

```

### exchange: Swap and Return the Old Value

`exchange` is a very handy operation that writes in a new value while taking out the old value:

```cpp
std::atomic<int> flag{0};

int old = flag.exchange(1);
// 现在 flag = 1，old = 0

// 嵌入式场景：状态机切换
enum class DeviceState { Idle, Busy, Error };
std::atomic<DeviceState> state{DeviceState::Idle};

// 尝试进入Busy状态
DeviceState old_state = state.exchange(DeviceState::Busy);
if (old_state != DeviceState::Idle) {
    // 之前不是Idle，说明有人已经在用了
    // 处理冲突...
}

```

### compare_exchange_weak/strong: CAS Operations

This is the "nuclear weapon" of atomic operations—Compare-And-Swap. The cornerstone of lock-free algorithms.

```cpp
std::atomic<int> value{10};

int expected = 10;
int desired = 20;

// 如果value等于expected，就把value设为desired
// 成功：返回true，value变成20
// 失败：返回false，expected被更新为当前值
bool succeeded = value.compare_exchange_strong(expected, desired);

```

It looks a bit convoluted, but it is key to implementing lock-free data structures:

```cpp
// 无锁栈的push操作
struct Node {
    int data;
    Node* next;
};

std::atomic<Node*> head{nullptr};

void push(int value) {
    Node* new_node = new Node{value, nullptr};

    Node* old_head = head.load();
    do {
        new_node->next = old_head;
        // 如果head还是old_head，就把它设为new_node
        // 如果head已经被别人改了，old_head会被更新为最新值，循环重试
    } while (!head.compare_exchange_weak(old_head, new_node));
}

```

**The difference between weak and strong**:

- `compare_exchange_strong`: Guarantees that success means success and failure means failure, but it may "spuriously fail" and require a few extra loops
- `compare_exchange_weak`: May "spuriously fail" (returning false even if the values are equal), but is usually more efficient inside a loop

**Embedded advice**: Use `weak` inside loops, and `strong` in non-loop scenarios.

### fetch_add/fetch_sub: Atomic Arithmetic Operations

For integer atomic variables, there is a series of fetch operations:

```cpp
std::atomic<int> counter{0};

// 加法：返回旧值
int old1 = counter.fetch_add(5);   // counter = 5，old1 = 0

// 减法：返回旧值
int old2 = counter.fetch_sub(2);   // counter = 3，old2 = 5

// 位运算
counter.fetch_or(0xFF);    // 按位或
counter.fetch_and(0xF0);   // 按位与
counter.fetch_xor(0x0F);   // 按位异或

// 前置/后置运算符
counter++;      // 等同于 counter.fetch_add(1) + 1
++counter;      // 等同于 counter.fetch_add(1) + 1
counter--;      // 等同于 counter.fetch_sub(1) - 1
--counter;      // 等同于 counter.fetch_sub(1) + 1

counter += 10;  // 等同于 counter.fetch_add(10) + 10
counter -= 5;   // 等同于 counter.fetch_sub(5) - 5

```

C++26 also adds `fetch_max` and `fetch_min`:

```cpp
std::atomic<int> max_value{100};
max_value.fetch_max(150);  // max_value变成150
max_value.fetch_min(80);   // max_value变成80

```

------

## is_lock_free: Checking If It Is Truly Lock-Free

This is a crucial point that is easily overlooked. Operations on `std::atomic` might need to be implemented using locks on certain platforms, which means it is not "truly lock-free."

```cpp
std::atomic<int> ai;
std::atomic<long long> all;

std::cout << "atomic<int> is lock free: "
          << ai.is_lock_free() << '\n';
std::cout << "atomic<long long> is lock free: "
          << all.is_lock_free() << '\n';

// 编译期检查
static_assert(std::atomic<int>::is_always_lock_free,
              "int must be lock-free on this platform!");

```

**Why are some types not lock-free?**

- Certain architectures lack atomic instructions for the corresponding width (for example, 64-bit atomic operations on a 32-bit ARM)
- The compiler decides to implement it using locks (but this is transparent to the programmer)

**Embedded advice**:

- On performance-critical paths, be sure to check `is_lock_free()`
- If you need to guarantee lock-free behavior, consider using `std::atomic_ref` (C++20) or implementing your own CAS loop

------

## Practical Embedded Scenarios

### Scenario 1: Sharing a Flag Between an Interrupt and the Main Thread

```cpp
class UARTRXDriver {
public:
    UARTRXDriver() : data_ready{false}, byte_value{0} {}

    // 中断服务程序（ISR）中调用
    void irq_handler() {
        byte_value = UART_DATA_REG;  // 假设读取硬件寄存器
        data_ready.store(true, std::memory_order_release);
    }

    // 主循环中调用
    bool get_byte(uint8_t& byte) {
        if (data_ready.load(std::memory_order_acquire)) {
            byte = byte_value;
            data_ready.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> data_ready;
    std::atomic<uint8_t> byte_value;
};

```

### Scenario 2: Lock-Free Queue Producer-Consumer

```cpp
template<typename T, size_t Size>
class SPSCQueue {
    // 单生产者单消费者，无需锁
public:
    SPSCQueue() : read_idx(0), write_idx(0) {}

    bool push(const T& item) {
        const size_t current_write = write_idx.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) % Size;

        if (next_write == read_idx.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }

        buffer[current_write] = item;
        write_idx.store(next_write, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t current_read = read_idx.load(std::memory_order_relaxed);

        if (current_read == write_idx.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }

        item = buffer[current_read];
        const size_t next_read = (current_read + 1) % Size;
        read_idx.store(next_read, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Size> buffer;
    std::atomic<size_t> read_idx;
    std::atomic<size_t> write_idx;
};

// 使用
SPSCQueue<int, 1024> uart_rx_queue;

// UART中断中
void uart_irq_handler() {
    uint8_t byte = UART_DR;
    uart_rx_queue.push(byte);
}

// 主线程中
void main_loop() {
    int data;
    while (uart_rx_queue.pop(data)) {
        process_byte(data);
    }
}

```

### Scenario 3: Reference Counting (Similar to shared_ptr)

```cpp
class RefCounted {
public:
    void ref() {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    void unref() {
        // fetch_sub返回旧值，如果减后为0则删除
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    int get_count() const {
        return ref_count.load(std::memory_order_relaxed);
    }

protected:
    virtual ~RefCounted() = default;

private:
    std::atomic<int> ref_count{0};
};

```

------

## Common Pitfalls

### Pitfall 1: Assuming Atomic Variables Solve All Races

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程1
x.store(1);
y.store(2);

// 线程2
int a = y.load();
int b = x.load();

// 你以为 a=2 一定意味着 b=1？
// 错！没有合适的内存序保证，可能是 a=2, b=0

```

We will dive into this issue in detail in the next chapter when we discuss memory order.

### Pitfall 2: Forgetting to Consider the Scope of Atomicity

```cpp
std::atomic<int> array[10];  // 10个原子int

// 这不是原子的！
array[0].store(array[1].load());  // 两次独立的原子操作

```

If you need to operate on multiple atomic variables "simultaneously," you still need to use locks or other synchronization mechanisms.

### Pitfall 3: Using atomic on Non-Trivial Types

```cpp
// ❌ 编译错误：string不是平凡可复制类型
std::atomic<std::string> str;

// ✅ 用指针或者shared_ptr
std::atomic<std::shared_ptr<std::string>> str_ptr;
// 或者C++20的atomic_ref配合外部锁

```

### Pitfall 4: Ignoring Alignment Requirements on Embedded Platforms

Atomic operations on certain platforms require alignment, for example, on ARMv6:

```cpp
// 可能不对齐，导致总线错误
char buffer[sizeof(std::atomic<int>) * 2];
auto p = new (&buffer[1]) std::atomic<int>;  // 危险！

// ✅ 使用alignas
alignas(std::atomic<int>) char buffer[sizeof(std::atomic<int>) * 2];

```

------

## C++20 New Features: atomic_ref and atomic_wait

### atomic_ref: Performing Atomic Operations on Existing Variables

In the past, if you had a normal variable and wanted to perform atomic operations on it, you had to create an atomic wrapper for it. C++20 provides `atomic_ref`, which allows atomic access to any existing variable:

```cpp
int regular_int = 0;

std::atomic_ref<int> atomic_ref_int(regular_int);

atomic_ref_int.store(42);
int x = atomic_ref_int.load();

// 注意：atomic_ref的生命周期不能超过原变量！

```

**Embedded scenario**: Hardware register mapping

```cpp
// 假设这是硬件映射的内存地址
volatile uint32_t* hw_reg = reinterpret_cast<uint32_t*>(0x40000000);

// 可以用atomic_ref来原子访问
std::atomic_ref<uint32_t> atomic_reg(*const_cast<uint32_t*>(hw_reg));

atomic_reg.fetch_or(0x01);

```

### atomic_wait/notify: Efficient Waiting Mechanism

C++20 introduced a waiting/notification mechanism similar to condition variables but more efficient:

```cpp
std::atomic<int> signal{0};

// 等待线程
void waiter() {
    int expected = 0;
    // 等待signal变成非0，比自旋等待更省CPU
    signal.wait(expected);
    // 醒来后处理...
}

// 通知线程
void notifier() {
    signal.store(1);
    signal.notify_one();  // 唤醒一个等待者
    // signal.notify_all();  // 或者唤醒全部
}

```

**Embedded scenario**: Efficient task synchronization

```cpp
class TaskSignal {
public:
    void wait() {
        int expected = 0;
        // 这里的实现可能用WFE指令（ARM）之类的低功耗等待
        flag.wait(expected);
    }

    void signal() {
        flag.store(1, std::memory_order_release);
        flag.notify_one();
    }

    void reset() {
        flag.store(0, std::memory_order_release);
    }

private:
    std::atomic<int> flag{0};
};

```

------

## Performance Considerations

The overhead of atomic operations mainly comes from three aspects:

1. **Preventing compiler optimization**: The compiler cannot optimize away or reorder atomic operations
2. **CPU instruction overhead**: Certain atomic operations require special instructions (such as the lock prefix on x86)
3. **Cache coherency traffic**: Synchronizing cache lines between multiple cores

Because of this, we might adopt the following strategies:

- Use `relaxed` instead of stronger memory orders whenever possible (detailed in the next chapter)
- Use local variables instead of shared atomic variables when possible
- Consider using thread-local storage (`thread_local`) to reduce the frequency of atomic operations
- For high-frequency counters, consider using per-thread counting and periodically aggregating the results

```cpp
// 高频计数优化示例
thread_local int local_counter = 0;
std::atomic<int> global_counter{0};

void increment() {
    local_counter++;  // 无原子开销
    if (local_counter >= 1000) {
        global_counter.fetch_add(local_counter, std::memory_order_relaxed);
        local_counter = 0;
    }
}

```

------

## Summary

`std::atomic` is the cornerstone of modern C++ concurrent programming, and mastering it is of great significance:

1. **Basic operations**: `store`, `load`, `exchange`, `compare_exchange`, `fetch_*`
2. **Checking lock-free guarantees**: `is_lock_free()` tells you whether it is truly lock-free
3. **Embedded applications**: Interrupt flags, lock-free queues, reference counting
4. **C++20 new features**: `atomic_ref` and `atomic_wait` make atomic operations more flexible

But there is a crucial question we haven't explored in depth yet—**memory order**. Why is `relaxed` sometimes enough, and when must we use `acquire-release`? This is exactly what the next chapter will cover, and it is also where many concurrency bugs hide.
