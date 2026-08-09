---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: ISR Safe Programming Practices
difficulty: advanced
order: 5
platform: stm32f1
prerequisites:
- 'Chapter 10.1-10.4: 原子操作与内存序'
reading_time_minutes: 15
tags:
- cpp-modern
- intermediate
- stm32f1
title: Writing Interrupt-Safe Code
translation:
  source: documents/vol8-domains/embedded/05-interrupt-safe-coding.md
  source_hash: 3053a264d4083ea1d70154afe9502ee9b6c20525ae50b511b578368ea3bc685f
  translated_at: '2026-06-16T04:12:48.498571+00:00'
  engine: anthropic
  token_count: 3479
---
# Modern C++ for Embedded Systems — Writing Interrupt-Safe Code

## Introduction

Have you ever encountered a situation where your program runs fine, but crashes intermittently once interrupts are enabled? Or even more strangely, do variable values mysteriously "jump," where single-stepping works perfectly, but full-speed execution fails?

If you have experienced this, congratulations: you have hit the classic pitfall of concurrent programming—**data races between interrupts and the main thread**.

An interrupt service routine (ISR) is like an emergency visitor who might barge into your office at any moment. It doesn't make an appointment, doesn't wait, and comes in whenever it pleases. If you are processing important data (like updating a linked list node) and are suddenly interrupted by an ISR that also needs to access that data, the situation becomes chaotic.

To make matters worse, such problems are often hard to reproduce. When you attach a debugger or add print statements, the timing changes, and the bug might "disappear"—until the day the product is delivered to the customer.

> In a nutshell: **Writing interrupt-safe code means ensuring that shared data access between interrupts and the main thread does not result in data races.**

In this chapter, we will dive deep into how to write safe and efficient code in ISRs, and how to communicate correctly with the main thread.

------

## The Unique Nature of ISRs

Before diving into specific techniques, let's understand the fundamental differences between ISRs and normal code.

### Asynchronous Execution

An ISR can interrupt the execution of the main program at any time (except during a few atomic operations). This means:

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

If the ISR triggers exactly after the main thread reads but before it writes back, the result is: one increment operation is lost.

### Limited Stack Space

ISRs use their own stack space (or a portion of the main stack), which is usually much smaller than the main thread stack. This means:

- No deep recursion
- No allocating large arrays
- No calling functions that might use a lot of stack

### No Blocking

This is the most critical limitation: **You cannot wait in an ISR.** Any operation that might cause blocking is forbidden:

- `std::mutex::lock()` - May block
- `new`/`malloc` - May trigger memory allocation, may block
- `condition_variable::wait()` - Absolutely blocking

### Keep Execution Time Short

The longer an ISR runs, the worse the system's responsiveness, potentially even causing other interrupts to be lost. Good practice dictates:

- Do only the bare minimum processing
- Leave complex processing to the main thread
- Use queues to pass data to the main thread

------

## Absolute Taboos in ISRs

Based on these characteristics, here are things you must absolutely never do in an ISR:

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

> **Key Takeaway**: The execution environment of an ISR is "constrained." You must assume that any operation that might cause blocking or exceptions is fatal.

------

## Application of Atomic Operations in ISRs

Since we cannot use locks, how do we safely access shared data in an ISR? The answer is: **atomic operations**.

### Basics: Checking is_lock_free()

Before using atomic operations, first confirm that they are implemented lock-free on your platform:

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

**Why is this important?** Atomic operations on some platforms might be implemented using locks internally. If you call such an operation in an ISR, it could lead to a deadlock.

### Classic Pattern: ISR Writes, Main Thread Reads

The most common pattern is the ISR setting a flag and the main thread polling to handle it:

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

**Choice of Memory Order**:

- In ISR: Use `release` to ensure the write to `data` completes before `ready=true`
- In main thread: Use `acquire` to ensure we see the complete write when reading `data`

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

**Why relaxed?** For a simple counter, we only care about the final value, not the order of operations. `relaxed` offers the best performance.

### Classic Pattern: Synchronizing Multiple Related Variables

When you need to synchronize multiple variables, you need a more careful design of memory ordering:

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

**Key Point**: Use a single atomic variable (`ready`) as a "publish switch" to ensure the visibility of other variables.

------

## Memory Barriers

Sometimes, just using atomic variables isn't enough; we need to explicitly control the order of memory accesses. This is where memory barriers come in.

### What is a Memory Barrier

A memory barrier is an instruction that forces a constraint on the order of memory operations by the CPU and compiler. It tells the compiler and CPU: "Memory operations before this barrier must complete before any operations after the barrier can execute."

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

### When Explicit Barriers are Needed

In most cases, using atomic operations with memory order arguments is sufficient. However, the following scenarios might require explicit barriers:

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

### Compiler Barrier vs CPU Memory Barrier

There are also lighter "compiler barriers," which only prevent compiler reordering and do not generate CPU instructions:

```cpp
// GNU C/C++ 的编译器屏障
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

// 使用
int x = 1;
COMPILER_BARRIER();
int y = 2;  // 编译器不会把y的赋值优化到x之前
```

But for most C++ code, using `std::atomic_thread_fence` or atomic operations with memory ordering is sufficient.

------

## Communication Patterns Between Interrupts and Main Thread

Communication between the ISR and the main thread is a core pattern in embedded systems. Let's look at a few common implementations.

### Pattern 1: Single Producer Single Consumer (SPSC) Queue

This is the most common and reliable pattern. The ISR is the producer, and the main thread is the consumer (or vice versa):

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

**Key Design Points**:

1. Single producer single consumer, no complex synchronization needed
2. Cannot block in the ISR; if full, discard (or use a larger queue)
3. Correct memory order ensures data visibility

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

**Advantages of Double Buffering**:

- Completely lock-free read/write
- Only simple assignment needed in the ISR
- The main thread gets a complete snapshot of the data

### Pattern 3: Ring Buffer

For streaming data (like audio, serial), a ring buffer is very practical:

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

A **thread-safe ring buffer** requires more careful design; refer to the accompanying example code.

------

## The volatile Trap

Many embedded developers (including the author in the past) have misconceptions about `volatile`. Let's clarify.

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

`volatile` only tells the compiler "do not optimize away accesses to this variable," but it does not guarantee the atomicity of the operation.

### volatile Does Not Guarantee Memory Order

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

`volatile` does not prevent the CPU from reordering memory operations. To guarantee ordering, you must use atomic operations + appropriate memory ordering.

### Correct Use of volatile

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

**Principle**: If a variable is modified by only one execution context and others only read it, `volatile` is sufficient. If there are multiple modifiers, `atomic` must be used.

### volatile vs atomic: Decision Tree

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

Even with an understanding of the concepts above, it's easy to stumble in practice. Let's look at a few common issues.

### Pitfall 1: Assuming Single-Byte Assignment is Atomic

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

**Problem**: Although assigning a single byte might be atomic, there is no synchronization guarantee between the "write flags, then write counter" operations.

**Solution**: Use a single atomic variable as a synchronization point, or wrap the entire struct in an atomic.

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

**Solution**: Hardware registers must be declared as `volatile`:

```cpp
struct UART_Regs {
    volatile uint32_t SR;
    volatile uint32_t DR;
    // ...
};

// 编译器不会优化掉对 volatile 的访问
```

### Pitfall 3: Calling Non-Reentrant Functions in ISRs

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

**Common Non-Reentrant Functions**:

- `malloc`/`free`
- `printf`/`sprintf`
- Most C standard library functions

### Debugging Tips

1. **Use a Hardware Debugger**: Set data watchpoints to pause execution when a variable is modified.

2. **Static Analysis Tools**:

   ```bash
   # 使用 ThreadSanitizer 检测数据竞争（需要修改代码模拟）
   g++ -fsanitize=thread -g your_code.cpp
   ```

3. **Code Review**: Carefully check all variables shared between ISRs and the main thread.

4. **Unit Testing**: Simulate interrupt timing and test various boundary conditions.

------

## Summary

Writing interrupt-safe code is a core skill in embedded system development. Let's review the key points:

1. **ISR Limitations**: No blocking, no memory allocation, keep execution time short.
2. **Atomic Operations are Key**: Use `std::atomic` to ensure atomic access to shared variables.
3. **Memory Order Matters**: Correctly choose `relaxed`, `acquire`, and `release`.
4. **Communication Patterns**: SPSC queues, double buffering, and ring buffers are common patterns for ISR-main thread communication.
5. **volatile is Not a Panacea**: Use `volatile` for hardware registers, `atomic` for shared variables.
6. **Watch Out for Traps**: Single-byte assignment doesn't guarantee overall consistency; avoid non-reentrant functions.

**Practical Advice**:

- Do the minimum work in the ISR: set flags, collect data, put in queues.
- Leave complex processing to the main thread.
- Use static assertions to ensure atomic operations are lock-free.
- Carefully review all data shared between ISRs and the main thread.
- Write tests to simulate various interrupt timings.

In the next section, we will dive deep into **critical section protection techniques**, learning how to protect critical sections using various methods, and how to avoid advanced topics like deadlocks and priority inversion.
