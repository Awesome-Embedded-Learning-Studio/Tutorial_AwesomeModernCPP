---
title: Interrupt-safe coding
description: ISR Secure Coding Practices
chapter: 10
order: 5
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: advanced
reading_time_minutes: 25
prerequisites:
- 'Chapter 10.1-10.4: 原子操作与内存序'
cpp_standard:
- 11
- 14
- 17
- 20
platform: stm32f1
---
# Modern C++ for Embedded Systems — Writing Interrupt-Safe Code

## Introduction

Have you ever run into this situation: your program runs perfectly fine, but the moment you enable interrupts, it crashes intermittently? Or even more mysteriously, certain variable values inexplicably "jump," single-stepping shows everything is normal, but running at full speed triggers the issue?

If you have experienced this, congratulations—you have fallen into a classic pitfall of concurrent programming: **data races between interrupts and the main thread**.

An interrupt service routine (ISR) is like an unexpected visitor who might barge into your office at any moment. It does not make an appointment, it does not wait, and it comes in whenever it pleases. If you are processing important data (such as updating a linked list node) and are suddenly interrupted, and the ISR also needs to access that same data, the result is pure chaos.

What makes things worse is that these issues are often notoriously hard to reproduce. When you attach a debugger or add print statements, the timing changes, and the bug might "disappear"—only to resurface the day you deliver the product to the customer.

> In a nutshell: **Writing interrupt-safe code means ensuring that shared data access between interrupts and the main thread is free from data races.**

In this chapter, we will dive deep into how to write safe, efficient code within an ISR, and how to communicate correctly with the main thread.

------

## The Unique Nature of ISRs

Before diving into specific techniques, let us understand the fundamental differences between an ISR and normal code.

### Asynchronous Execution

An ISR can interrupt the execution of the main program at any time (except during certain atomic operations). This means:

```cpp
int shared_counter = 0;

// 主线程
void update_counter() {
    shared_counter++;  // 这不是原子操作！
    // 实际上是：
    // 1. 读取 shared_counter
    // 2. 加 1
    // 3. 写回 shared_counter
    // 如果在步骤1和3之间发生中断...
}

// ISR
extern "C" void TIMER_IRQHandler() {
    shared_counter++;  // 也在修改同一个变量！
}
```

If the ISR triggers exactly after the main thread reads the value but before it writes it back, the result is: an increment operation gets lost.

### Limited Stack Space

An ISR uses its own stack space (or a portion of the main stack), which is typically much smaller than the main thread's stack. This means:

- No deep recursion
- No large array allocations
- No calling functions that might use a lot of stack space

### No Blocking

This is the most critical restriction: **you cannot wait inside an ISR**. Any operation that might cause blocking is strictly forbidden:

- `std::mutex::lock()` - might block
- `new`/`malloc` - might trigger memory allocation, might block
- `condition_variable::wait()` - absolutely blocks

### Short Execution Time

The longer an ISR takes to execute, the worse the system's responsiveness becomes, and it might even cause other interrupts to be missed. Good practices include:

- Do only the absolutely necessary processing
- Leave complex processing to the main thread
- Use a queue to pass data to the main thread

------

## Absolute Prohibitions in ISRs

Based on the characteristics above, here are the things you must absolutely never do inside an ISR:

```cpp
// ❌ 禁止列表

extern "C" void BAD_IRQHandler() {
    // 1. 禁止动态内存分配
    int* p = new int;        // 可能阻塞，可能抛异常
    free(malloc(100));       // 可能阻塞

    // 2. 禁止使用互斥锁
    std::lock_guard<std::mutex> lock(mtx);  // 可能无限阻塞

    // 3. 禁止使用条件变量
    cv.wait(lock);           // 绝对阻塞

    // 4. 禁止长时间操作
    for (int i = 0; i < 1000000; ++i) {
        complex_calculation();
    }

    // 5. 禁止调用可能抛异常的函数
    some_function_that_may_throw();  // ISR中不能处理异常

    // 6. 禁止非原子地访问共享数据
    shared_var++;  // 数据竞争！
}
```

> **Key understanding**: The execution environment of an ISR is "constrained." You must assume that any operation that might cause blocking or exceptions is fatal.

------

## Applying Atomic Operations in ISRs

Since we cannot use locks, how do we safely access shared data in an ISR? The answer is: **atomic operations**.

### The Basics: Checking is_lock_free()

Before using atomic operations, first confirm that they are implemented in a lock-free manner on your platform:

```cpp
std::atomic<int> flag{0};

// 编译期检查
static_assert(std::atomic<int>::is_always_lock_free,
              "atomic<int> must be lock-free for ISR use!");

// 运行时检查
extern "C" void init_interrupts() {
    if (!flag.is_lock_free()) {
        // 处理错误：不能用在中断里
        handle_error();
    }
}
```

**Why does this matter?** On certain platforms, atomic operations might be implemented using internal locks. If you call such an operation inside an ISR, it could lead to a dead lock.

### Classic Pattern: ISR Writes, Main Thread Reads

The most common pattern is having the ISR set a flag while the main thread polls and processes it:

```cpp
class DataReadyFlag {
public:
    // ISR 中调用：设置标志
    void set() noexcept {
        ready.store(true, std::memory_order_release);
        data = 42;  // 简单赋值，假设是原子操作或单字节
    }

    // 主线程中调用：检查并获取数据
    bool get(int& out_data) noexcept {
        if (ready.load(std::memory_order_acquire)) {
            out_data = data;
            ready.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> ready{false};
    int data;  // 注意：这里假设int的读写是原子的
};
```

**Choosing the memory order**:

- In the ISR, use `release`: ensures the write to `data` completes before `ready` is set to `true`
- In the main thread, use `acquire`: ensures that when reading `data`, the complete write is visible

### Classic Pattern: Atomic Counter

```cpp
class InterruptCounter {
public:
    // ISR 中调用：递增计数
    void increment() noexcept {
        count.fetch_add(1, std::memory_order_relaxed);
    }

    // 主线程：获取并重置
    int get_and_reset() noexcept {
        return count.exchange(0, std::memory_order_relaxed);
    }

private:
    std::atomic<int> count{0};
};
```

**Why use relaxed?** For a simple counter, we only care about the final value, not the order of operations. `relaxed` offers the best performance.

### Classic Pattern: Synchronizing Multiple Related Variables

When you need to synchronize multiple variables, you need a more careful memory order design:

```cpp
class TimestampedValue {
public:
    // ISR 中调用：更新值和时间戳
    void update(int new_value, uint32_t new_timestamp) noexcept {
        // 先写数据
        value = new_value;
        timestamp = new_timestamp;
        // 最后用 release 发布
        ready.store(true, std::memory_order_release);
    }

    // 主线程：读取数据
    bool get(int& out_value, uint32_t& out_timestamp) noexcept {
        if (ready.load(std::memory_order_acquire)) {
            out_value = value;
            out_timestamp = timestamp;
            ready.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> ready{false};
    int value;
    uint32_t timestamp;
};
```

**Key point**: Use a single atomic variable (`ready`) as a "publish switch" to ensure the visibility of other variables.

------

## Memory Barriers

Sometimes, using atomic variables alone is not enough, and we need to explicitly control the order of memory accesses. This is where memory barriers come in.

### What Is a Memory Barrier?

A memory barrier is an instruction that forcibly constrains the order of memory operations performed by the CPU and the compiler. It tells the compiler and the CPU: "Memory operations before this barrier must complete before any operations after the barrier can execute."

### std::atomic_thread_fence

C++ provides the `std::atomic_thread_fence` function for creating memory barriers:

```cpp
#include <atomic>

// 发布屏障：确保之前的写入都完成
std::atomic_thread_fence(std::memory_order_release);
shared_data = 42;

// 获取屏障：确保之后的读取能看到之前的写入
std::atomic_thread_fence(std::memory_order_acquire);
if (shared_data == 42) {
    // ...
}
```

### When You Need Explicit Barriers

In most cases, using atomic operations with memory order parameters is sufficient. However, the following scenarios might require explicit barriers:

**Scenario 1: Protecting Non-Atomic Data**

```cpp
class NonAtomicDataWithFence {
public:
    // ISR 中调用
    void update(const Data& new_data) noexcept {
        data = new_data;
        // 发布屏障：确保data写入完成后，再设置标志
        std::atomic_thread_fence(std::memory_order_release);
        ready.store(true, std::memory_order_relaxed);
    }

    // 主线程
    bool get(Data& out) noexcept {
        if (ready.load(std::memory_order_relaxed)) {
            // 获取屏障：确保读取data之前，ready标志已经被看到
            std::atomic_thread_fence(std::memory_order_acquire);
            out = data;
            ready.store(false, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> ready{false};
    Data data;  // 非原子类型！
};
```

**Scenario 2: Synchronizing Multiple Flags**

```cpp
// ISR 中
void interrupt_handler() {
    buffer[index] = new_data;
    std::atomic_thread_fence(std::memory_order_release);
    data_valid.store(true, std::memory_order_relaxed);
    index = (index + 1) % BUFFER_SIZE;
}
```

### Compiler Barriers vs. CPU Memory Barriers

There are also lighter-weight "compiler barriers" that only prevent compiler reordering and do not generate CPU instructions:

```cpp
// GNU C/C++ 的编译器屏障
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

// 使用
int x = 1;
COMPILER_BARRIER();
int y = 2;  // 编译器不会把y的赋值优化到x之前
```

However, for most C++ code, using `std::atomic_thread_fence` or atomic operations with memory orders is sufficient.

------

## Communication Patterns Between Interrupts and the Main Thread

Communication between the ISR and the main thread is a core pattern in embedded systems. Let us look at a few common implementation approaches.

### Pattern 1: Single-Producer Single-Consumer (SPSC) Queue

This is the most commonly used and most reliable pattern. The ISR acts as the producer, and the main thread acts as the consumer (or vice versa):

```cpp
template<typename T, size_t Size>
class SPSCQueue {
public:
    bool push(const T& item) noexcept {
        const size_t current_write = write_idx.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) % Size;

        // 检查队列是否满
        if (next_write == read_idx.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }

        buffer[current_write] = item;
        // release 确保数据写入完成后，再更新索引
        write_idx.store(next_write, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const size_t current_read = read_idx.load(std::memory_order_relaxed);

        // 检查队列是否空
        if (current_read == write_idx.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }

        item = buffer[current_read];
        const size_t next_read = (current_read + 1) % Size;
        // release 确保更新索引
        read_idx.store(next_read, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Size> buffer;
    std::atomic<size_t> read_idx{0};
    std::atomic<size_t> write_idx{0};
};

// 使用示例
SPSCQueue<uint8_t, 256> uart_rx_queue;

// UART 接收中断
extern "C" void USART1_IRQHandler() {
    if (USART1->SR & USART_SR_RXNE) {
        uint8_t data = USART1->DR;
        uart_rx_queue.push(data);  // ISR中不能阻塞，满了就丢弃
    }
}

// 主循环
void main_loop() {
    uint8_t data;
    while (uart_rx_queue.pop(data)) {
        process_data(data);
    }
}
```

**Key design points**:

1. Single-producer single-consumer means no complex synchronization is needed
2. The ISR cannot block, so if the queue is full, data is dropped (or a larger queue is used)
3. Correct memory orders ensure data visibility

### Pattern 2: Double Buffering

For larger data blocks, double buffering is an efficient choice:

```cpp
template<typename T>
class DoubleBuffer {
public:
    // 写入者（ISR）获取写入缓冲区
    T* acquire_write_buffer() noexcept {
        return &buffers[write_index];
    }

    // 写入完成，交换缓冲区
    void commit_write() noexcept {
        std::atomic_thread_fence(std::memory_order_release);
        size_t old = write_index;
        write_index = read_index;
        read_index = old;
        swapped.store(true, std::memory_order_release);
    }

    // 读取者（主线程）检查并获取数据
    const T* try_get_read_buffer() noexcept {
        if (swapped.load(std::memory_order_acquire)) {
            swapped.store(false, std::memory_order_relaxed);
            return &buffers[read_index];
        }
        return nullptr;
    }

private:
    std::array<T, 2> buffers;
    size_t write_index = 0;
    size_t read_index = 1;
    std::atomic<bool> swapped{false};
};

// 使用示例
DoubleBuffer<SensorData> sensor_buffer;

// 定时器中断
extern "C" void TIM_IRQHandler() {
    auto* buf = sensor_buffer.acquire_write_buffer();
    buf->temperature = read_temperature();
    buf->pressure = read_pressure();
    buf->timestamp = get_timestamp();
    sensor_buffer.commit_write();
}

// 主循环
void main_loop() {
    if (const auto* data = sensor_buffer.try_get_read_buffer()) {
        display_data(*data);
        log_to_storage(*data);
    }
}
```

**Advantages of double buffering**:

- Reading and writing are completely lock-free
- The ISR only needs simple assignment
- The main thread gets a complete snapshot of the data

### Pattern 3: Ring Buffer

For streaming data (such as audio or serial communication), a ring buffer is highly practical:

```cpp
template<typename T, size_t Capacity>
class RingBuffer {
public:
    bool push(const T& item) noexcept {
        const size_t next_head = (head + 1) % Capacity;

        // 检查是否满
        if (next_head == tail) {
            return false;
        }

        buffer[head] = item;
        head = next_head;
        return true;
    }

    bool pop(T& item) noexcept {
        // 检查是否空
        if (head == tail) {
            return false;
        }

        item = buffer[tail];
        tail = (tail + 1) % Capacity;
        return true;
    }

    size_t size() const noexcept {
        if (head >= tail) {
            return head - tail;
        }
        return Capacity - tail + head;
    }

    bool empty() const noexcept {
        return head == tail;
    }

    bool full() const noexcept {
        return ((head + 1) % Capacity) == tail;
    }

private:
    std::array<T, Capacity> buffer;
    size_t head = 0;  // 写位置
    size_t tail = 0;  // 读位置
};

// 注意：这个简单版本没有原子保护
// 如果在多线程/中断环境使用，需要加原子操作
```

A **thread-safe ring buffer** requires more careful design; refer to the accompanying example code for details.

------

## The Pitfalls of volatile

Many embedded developers (including the author in the past) have misconceptions about `volatile`. Let us clear things up.

### volatile Does Not Guarantee Atomicity

```cpp
volatile int counter = 0;

// 中断
extern "C" void TIM_IRQHandler() {
    counter++;  // ❌ 不是原子操作！
    // 仍然是：读-改-写三个步骤
}

// 主线程
void update() {
    counter++;  // ❌ 数据竞争
}
```

`volatile` merely tells the compiler "do not optimize away accesses to this variable," but it does not guarantee that the operations are atomic.

### volatile Does Not Guarantee Memory Ordering

```cpp
volatile int flag = 0;
int data = 0;

// 线程1（或中断）
data = 42;
flag = 1;  // 编译器可能重排成 flag = 1; data = 42;

// 线程2
if (flag) {
    use(data);  // 可能读到 data = 0！
}
```

`volatile` does not prevent the CPU from reordering memory operations. To guarantee ordering, you must use atomic operations combined with the appropriate memory order.

### The Correct Use of volatile

So when should we actually use `volatile`?

**Use Case 1: Memory-Mapped I/O**

```cpp
// 硬件寄存器必须用 volatile
volatile uint32_t* const UART_DR = (volatile uint32_t*)0x40011004;

// 写数据
*UART_DR = byte;  // 必须真的写进去，不能被优化掉

// 读状态
while (*UART_DR & 0x80) {  // 每次都必须从硬件读取
    // 等待...
}
```

**Use Case 2: Non-Shared Variables in Signal Handlers**

```cpp
volatile bool keep_running = true;

extern "C" void SIGINT_Handler() {
    keep_running = false;  // 只有信号处理器修改
}

int main() {
    while (keep_running) {  // 主线程只读
        do_work();
    }
}
```

**Principle**: If a variable is modified by only one execution context and other contexts only read it, using `volatile` is sufficient. If there are multiple modifiers, you must use `atomic`.

### volatile vs. atomic: A Decision Tree

```text
                       变量会被并发修改？
                            |
                    ----------------
                   |                |
                   是               否
                   |                |
            --------------   用普通变量
            |
      需要硬件I/O语义？
            |
     -------------------
     |                   |
     是                  否
     |                   |
用 volatile         用 std::atomic
（内存映射寄存器）  （共享变量）
```

------

## Common Pitfalls and Debugging

Even with a solid understanding of the concepts above, it is still easy to stumble in practice. Let us look at a few common issues.

### Pitfall 1: Assuming Single-Byte Assignment Is Atomic

```cpp
struct {
    uint8_t flags;
    uint8_t counter;
    uint8_t status;
} shared_state;

// ISR 中
shared_state.flags = 0xFF;
shared_state.counter = 10;

// 主线程
if (shared_state.flags == 0xFF) {
    use(shared_state.counter);  // 可能读到部分更新的状态！
}
```

**The problem**: Although assigning a single byte might be atomic, there is no synchronization guarantee between the two operations of "writing `flags` first, then writing `counter`."

**The solution**: Use a single atomic variable as a synchronization point, or wrap the entire struct with an atomic type.

### Pitfall 2: Ignoring Compile-Time Optimization

```cpp
// 看起来没问题...
extern "C" void UART_IRQHandler() {
    uint8_t status = UART->SR;
    if (status & UART_SR_RXNE) {
        uint8_t data = UART->DR;
        rx_buffer[head++] = data;
    }
    // ❌ 问题：如果编译器认为status之后没被使用，
    //    可能优化掉整个变量！
}
```

**The solution**: Hardware registers must be declared as `volatile`:

```cpp
struct UART_Regs {
    volatile uint32_t SR;
    volatile uint32_t DR;
    // ...
};

// 编译器不会优化掉对 volatile 的访问
```

### Pitfall 3: Calling Non-Reentrant Functions in an ISR

```cpp
// ❌ 危险：printf 可能使用静态缓冲区
extern "C" void TIM_IRQHandler() {
    printf("Timer tick!\n");  // 如果主线程也在打印...
}

// ✅ 正确：使用专门的日志缓冲区
extern "C" void TIM_IRQHandler() {
    log_buffer.push('T');  // 无锁队列
}
```

**Common non-reentrant functions**:

- `malloc`/`free`
- `printf`/`sprintf`
- Most C standard library functions

### Debugging Techniques

1. **Use a hardware debugger**: Set up data watchpoints to pause execution when a variable is modified

2. **Static analysis tools**:

   ```bash
   # 使用 ThreadSanitizer 检测数据竞争（需要修改代码模拟）
   g++ -fsanitize=thread -g your_code.cpp
   ```

3. **Code review**: Carefully inspect all variables shared between ISRs and the main thread

4. **Unit testing**: Simulate interrupt timing and test various edge cases

------

## Summary

Writing interrupt-safe code is a core skill in embedded systems development. Let us review the key points:

1. **ISR restrictions**: No blocking, no memory allocation, short execution time
2. **Atomic operations are key**: Use `std::atomic` to ensure atomic access to shared variables
3. **Memory order matters**: Correctly choose `relaxed`, `acquire`, and `release`
4. **Communication patterns**: SPSC queues, double buffering, and ring buffers are common patterns for ISR-to-main-thread communication
5. **volatile is not a silver bullet**: Use `volatile` for hardware registers, and `atomic` for shared variables
6. **Watch out for pitfalls**: Single-byte assignment does not guarantee overall consistency; avoid non-reentrant functions

**Practical advice**:

- Do the absolute minimum in the ISR: set flags, collect data, and push to a queue
- Leave complex processing to the main thread
- Use static assertions to ensure atomic operations are lock-free
- Carefully audit all data shared between ISRs and the main thread
- Write tests to simulate various interrupt timings

In the next section, we will dive deep into **critical section protection techniques**, learning how to protect critical sections using multiple methods, and how to avoid advanced topics like dead locks and priority inversion.
