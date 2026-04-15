---
title: "中断安全的代码编写"
description: "ISR安全编程实践"
chapter: 10
order: 5
tags:
  - 中断
  - ISR
  - 并发
difficulty: advanced
reading_time_minutes: 25
prerequisites:
  - "Chapter 10.1-10.4: 原子操作与内存序"
cpp_standard: [11, 14, 17, 20]
---

# 嵌入式现代C++开发——中断安全的代码编写

## 引言

你有没有遇到过这种情况：程序跑得好好的，一打开中断就时不时崩溃？或者更诡异的是，某些变量的值莫名其妙地"跳变"，单步调试一切正常，全速运行就出问题？

如果你有这种经历，恭喜你，你踩中了并发编程的经典大坑——**中断与主线程之间的数据竞争**。

中断服务程序（ISR）就像一个随时可能闯入你办公室的紧急访客。它不预约、不等候，想什么时候进来就什么时候进来。如果你正在处理某些重要数据（比如更新一个链表节点），突然被中断打断，而ISR也要访问这些数据，那场面就非常混乱了。

更糟糕的是，这类问题往往难以复现。当你加上调试器、加上打印语句时，时序就变了，bug可能就"消失"了——直到产品交付给客户的那天。

> 一句话总结：**中断安全的代码编写，就是确保中断与主线程之间的共享数据访问不会发生数据竞争。**

本章我们将深入探讨如何在ISR中编写安全、高效的代码，以及如何与主线程正确通信。

------

## ISR 的特殊性

在深入具体技术之前，我们先理解ISR和普通代码有什么本质区别。

### 异步执行

ISR可以在任何时候打断主程序的执行（除了少数原子操作期间）。这意味着：

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

如果ISR恰好在主线程读取之后、写回之前触发，结果就是：一次加一操作丢失了。

### 栈空间有限

ISR使用的是它自己的栈空间（或者主栈的一部分），通常比主线程栈小得多。这意味着：

- 不能进行深度递归
- 不能分配大数组
- 不能调用可能使用大量栈的函数

### 不能阻塞

这是最关键的限制：**ISR中不能等待**。任何可能导致阻塞的操作都是禁止的：

- `std::mutex::lock()` - 可能阻塞
- `new`/`malloc` - 可能触发内存分配，可能阻塞
- `condition_variable::wait()` - 绝对阻塞

### 执行时间要短

ISR执行时间越长，系统响应性越差，甚至可能丢失其他中断。好的实践是：

- 只做最必要的处理
- 复杂处理留给主线程
- 使用队列将数据传递给主线程

------

## ISR 中的绝对禁区

基于上述特性，以下是ISR中绝对不能做的事情：

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

> **关键理解**：ISR的执行环境是"受限的"，你必须假设任何可能导致阻塞或异常的操作都是致命的。

------

## 原子操作在 ISR 中的应用

既然不能用锁，那ISR中怎么安全地访问共享数据呢？答案是：**原子操作**。

### 基础：检查 is_lock_free()

使用原子操作之前，首先要确认它在你的平台上是无锁实现的：

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

**为什么这很重要？** 某些平台上的原子操作可能内部用锁实现。如果在ISR中调用这样的操作，可能导致死锁。

### 经典模式：ISR 写，主线程读

最常见的模式是ISR设置标志，主线程轮询处理：

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

**内存序的选择**：

- ISR中用 `release`：确保data的写入在ready=true之前完成
- 主线程用 `acquire`：确保读取data时能看到完整的写入

### 经典模式：原子计数器

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

**为什么用 relaxed？** 对于简单的计数器，我们只关心最终值，不关心操作顺序。relaxed 性能最好。

### 经典模式：多个相关变量的同步

当需要同步多个变量时，需要更仔细的内存序设计：

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

**关键点**：用单个原子变量（ready）作为"发布开关"，确保其他变量的可见性。

------

## 内存屏障

有时候，仅仅用原子变量还不够，我们需要显式地控制内存访问顺序。这就是内存屏障的作用。

### 什么是内存屏障

内存屏障（Memory Barrier）是一种强制约束CPU和编译器内存操作顺序的指令。它告诉编译器和CPU："在这个屏障之前的内存操作必须完成后，才能执行屏障之后的操作"。

### std::atomic_thread_fence

C++提供了 `std::atomic_thread_fence` 函数用于创建内存屏障：

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

### 何时需要显式屏障

大多数情况下，使用带内存序参数的原子操作就够了。但以下场景可能需要显式屏障：

**场景1：保护非原子数据**

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

**场景2：多个标志的同步**

```cpp
// ISR 中
void interrupt_handler() {
    buffer[index] = new_data;
    std::atomic_thread_fence(std::memory_order_release);
    data_valid.store(true, std::memory_order_relaxed);
    index = (index + 1) % BUFFER_SIZE;
}
```

### 编译器屏障 vs CPU 内存屏障

还有更轻量的"编译器屏障"，只阻止编译器重排，不生成CPU指令：

```cpp
// GNU C/C++ 的编译器屏障
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

// 使用
int x = 1;
COMPILER_BARRIER();
int y = 2;  // 编译器不会把y的赋值优化到x之前
```

但对于大多数C++代码，使用 `std::atomic_thread_fence` 或带内存序的原子操作就够了。

------

## 中断与主线程通信模式

ISR和主线程之间的通信是嵌入式系统的核心模式。让我们看看几种常见的实现方式。

### 模式1：单生产者单消费者（SPSC）队列

这是最常用也最可靠的模式。ISR是生产者，主线程是消费者（或反过来）：

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

**关键设计点**：

1. 单生产者单消费者，无需复杂的同步
2. ISR中不能阻塞，满了就丢弃（或使用更大的队列）
3. 正确的内存序确保数据可见性

### 模式2：双缓冲技术

对于较大的数据块，双缓冲是一个高效的选择：

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

**双缓冲的优势**：

- 读写完全无锁
- ISR中只需简单赋值
- 主线程获取到的是完整的数据快照

### 模式3：环形缓冲区（Ring Buffer）

对于流式数据（如音频、串口），环形缓冲区非常实用：

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

**线程安全的环形缓冲区**需要更细心的设计，参见配套示例代码。

------

## volatile 的陷阱

很多嵌入式开发者（包括当年的笔者）对 `volatile` 有误解。让我们澄清一下。

### volatile 不保证原子性

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

`volatile` 只是告诉编译器"不要优化掉对这个变量的访问"，但它不保证操作的原子性。

### volatile 不保证内存序

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

`volatile` 不阻止CPU重排内存操作。要保证顺序，必须用原子操作+适当的内存序。

### volatile 的正确用途

那 `volatile` 到底什么时候用呢？

**用途1：内存映射I/O**

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

**用途2：信号处理程序中的非共享变量**

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

**原则**：如果变量只被一个执行上下文修改，其他上下文只读取，用 `volatile` 足够。如果有多个修改者，必须用 `atomic`。

### volatile vs atomic：选择决策树

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

## 常见的陷阱与调试

即使理解了上述概念，实践中还是容易踩坑。让我们看看几个常见问题。

### 陷阱1：误以为单字节赋值是原子的

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

**问题**：虽然单个字节的赋值可能是原子的，但"先写flags，再写counter"这两个操作之间没有同步保证。

**解决**：用一个原子变量作为同步点，或者把整个结构体用原子包装。

### 陷阱2：忽略编译期优化

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

**解决**：硬件寄存器必须声明为 `volatile`：

```cpp
struct UART_Regs {
    volatile uint32_t SR;
    volatile uint32_t DR;
    // ...
};

// 编译器不会优化掉对 volatile 的访问
```

### 陷阱3：在ISR中调用不可重入函数

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

**常见的不可重入函数**：

- `malloc`/`free`
- `printf`/`sprintf`
- 大部分C标准库函数

### 调试技巧

1. **使用硬件调试器**：设置数据观察点（Data Watchpoint），当变量被修改时暂停

2. **静态分析工具**：

   ```bash
   # 使用 ThreadSanitizer 检测数据竞争（需要修改代码模拟）
   g++ -fsanitize=thread -g your_code.cpp
   ```

3. **代码审查**：仔细检查所有ISR和主线程共享的变量

4. **单元测试**：模拟中断时序，测试各种边界情况

------

## 小结

中断安全的代码编写是嵌入式系统开发的核心技能。让我们回顾一下关键点：

1. **ISR 的限制**：不能阻塞、不能分配内存、执行时间要短
2. **原子操作是关键**：使用 `std::atomic` 确保共享变量的原子访问
3. **内存序很重要**：正确选择 `relaxed`、`acquire`、`release`
4. **通信模式**：SPSC队列、双缓冲、环形缓冲区是ISR-主线程通信的常用模式
5. **volatile 不是万能的**：硬件寄存器用 volatile，共享变量用 atomic
6. **注意陷阱**：单字节赋值不保证整体一致性、避免不可重入函数

**实践建议**：

- 在ISR中做最少的工作：设置标志、收集数据、放入队列
- 复杂处理留给主线程
- 用静态断言确保原子操作是无锁的
- 仔细审查所有ISR和主线程共享的数据
- 编写测试模拟各种中断时序

下一节，我们将深入探讨**临界区保护技术**，学习如何用多种方法保护临界区，以及如何避免死锁和优先级反转等高级话题。
