---
chapter: 17
difficulty: intermediate
order: 7
platform: stm32f1
reading_time_minutes: 9
tags:
- cpp-modern
- intermediate
- stm32f1
title: 第37篇：无锁环形缓冲区 —— ISR 与主循环之间的安全通道
description: ''
---
# 第37篇：无锁环形缓冲区 —— ISR 与主循环之间的安全通道

> 承接上一篇：ISR 每次收到一个字节需要传给主循环处理。这一篇设计一个专用的数据结构来完成这个任务——无锁环形缓冲区。

---

## 问题：ISR 和主循环之间的数据传递

上一篇结束时留下了一个问题：ISR 收到字节后，怎么安全地传递给主循环？

最直觉的方案可能是全局变量。ISR 往一个全局数组里写字节，主循环从数组里读。但这里有一个根本性的矛盾——ISR 和主循环是两个独立的执行流。ISR 可能在主循环读取数组的过程中被触发（反过来是不可能的，因为 ISR 打断主循环，不是主循环打断 ISR）。如果 ISR 正在往数组的某个位置写数据，而主循环恰好在读同一个位置，读到的数据可能是不完整的。

你可能想到用标志位来解决：ISR 设一个 `data_ready` 标志，主循环检查标志，有数据才读。但如果数据来得很快——115200 baud 下两个字节之间只有 87 微秒——ISR 写完第一个字节、设好标志，还没来得及写第二个字节，主循环可能已经读走了不完整的数据。

我们需要一种数据结构，让 ISR 可以持续写入、主循环持续读取，两者互不干扰，不需要锁，不需要复杂的同步机制。这个数据结构就是环形缓冲区（Circular Buffer / Ring Buffer）。

---

## 环形缓冲区的核心思想

环形缓冲区的底层是一个固定大小的数组。关键在于两个索引指针：`head` 和 `tail`。

- **`head`**：下一个写入位置。只有 ISR 可以推进 head（push 操作）。
- **`tail`**：下一个读取位置。只有主循环可以推进 tail（pop 操作）。

数据从 head 端流入，从 tail 端流出。当 head 到达数组末尾时，它绕回开头——这就是"环形"的含义。想象一个圆形的传送带：生产者（ISR）在一端放产品，消费者（主循环）在另一端取产品。两端独立工作，互不干扰。

几个关键状态：

- **空（empty）**：`head == tail`，没有任何数据。
- **满（full）**：`head` 的下一个位置等于 `tail`。注意这里不能用 `head == tail` 来判断满——因为 `head == tail` 已经被用来表示"空"了。我们留一个位置不写来区分空和满：如果 N 个位置的缓冲区，最多存 N-1 个字节。
- **数据量**：`head - tail`（处理环绕后的结果）。

这种"单生产者单消费者"（SPSC，Single-Producer Single-Consumer）的访问模式保证了一个关键性质：head 和 tail 各自只被一方修改。ISR 只修改 head，主循环只修改 tail。不存在两个执行流同时修改同一个变量的情况——因此不需要锁。

---

## 2 的幂技巧：零开销环绕

当 head 或 tail 到达数组末尾时需要绕回开头。最直觉的做法是取模：`index = index % N`。但取模运算在 ARM Cortex-M3 上需要多条指令（除法指令周期很长）。

如果 N 是 2 的幂（2, 4, 8, 16, 32, ..., 128），取模可以用位与操作替代：`index & (N - 1)`。一条 AND 指令，一个时钟周期。

为什么？因为当 N = 2^k 时，N - 1 的二进制表示是 k 个 1（比如 N=8 即 1000b，N-1=0111b）。`x & 0111b` 的效果就是只保留 x 的低 3 位——这等价于 `x % 8`。

我们的代码中 N = 128（2^7），所以 `mask(v) = v & 127`。

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/base/circular_buffer.hpp
template <size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
    // ...
    static constexpr size_t mask(size_t v) noexcept { return v & (N - 1); }
    static constexpr size_t next(size_t v) noexcept { return (v + 1) & (2 * N - 1); }
```

`static_assert` 在编译时强制检查 N 必须是 2 的幂。如果你写了 `CircularBuffer<100>`，编译直接报错。这比运行时检查好得多——你不会在烧录到板子之后才发现缓冲区大小选错了。

`next(v)` 也用了一个巧妙的设计。它不直接对 v 加 1 然后取模，而是用 `(v + 1) & (2 * N - 1)`。这意味着 head 和 tail 的实际取值范围是 0 到 2N-1，而不是 0 到 N-1。这样做的好处是 `size()` 计算更简单：`head - tail` 不需要处理环绕，因为 head 和 tail 不会相互"绕过"（它们是单调递增的，只是通过 `mask()` 映射到实际的数组下标）。

---

## CircularBuffer 模板完整讲解

让我们逐方法走读这个模板的完整实现：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/base/circular_buffer.hpp
template <size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");

  public:
    bool push(std::byte b) noexcept {
        if (full()) {
            return false;
        }
        buf_[mask(head_)] = b;
        head_              = next(head_);
        return true;
    }

    bool pop(std::byte& out) noexcept {
        if (empty()) {
            return false;
        }
        out   = buf_[mask(tail_)];
        tail_ = next(tail_);
        return true;
    }

    bool   empty() const noexcept { return head_ == tail_; }
    bool   full()  const noexcept { return next(head_) == tail_; }
    size_t size()  const noexcept {
        return head_ >= tail_ ? head_ - tail_ : N - tail_ + head_;
    }

  private:
    static constexpr size_t mask(size_t v) noexcept { return v & (N - 1); }
    static constexpr size_t next(size_t v) noexcept { return (v + 1) & (2 * N - 1); }

    std::array<std::byte, N> buf_{};
    volatile size_t head_ = 0;
    volatile size_t tail_ = 0;
};
```

### push() —— ISR 调用

`push()` 由 ISR 调用（生产者端）。流程是：检查缓冲区是否已满 → 如果不满，把字节写入 `mask(head_)` 位置 → 推进 head → 返回 true。如果满了，返回 false（数据丢失）。

`push()` 是 `noexcept` 的——ISR 中不能抛出异常（我们的项目本身就禁用了异常）。整个操作是 O(1)：一次比较、一次数组写入、一次加法和一次 AND。

### pop() —— 主循环调用

`pop()` 由主循环调用（消费者端）。流程是：检查是否为空 → 如果不空，从 `mask(tail_)` 位置读取字节 → 推进 tail → 返回 true。如果空了，返回 false。

同样是 O(1) 的 `noexcept` 操作。

### empty() 和 full()

- `empty()`：`head_ == tail_`。简单直接——如果 head 和 tail 相等，说明没有数据。
- `full()`：`next(head_) == tail_`。如果 head 的下一个位置就是 tail，说明写一个新字节就会覆盖还没读走的数据——所以满了。

### size()

当前缓冲区中的数据量。当 `head_ >= tail_` 时（没发生环绕），直接 `head_ - tail_`。当 `head_ < tail_` 时（head 绕过了 tail），数据量是 head 前面的部分加上 tail 后面的部分。

不过，由于我们使用了 `next()` 的设计（head 和 tail 的范围是 0 到 2N-1），实际上 `head_ - tail_` 在大多数情况下就足够了——但为了防御性编程，代码还是处理了两种情况。

---

## volatile 的作用

你可能注意到了 `head_` 和 `tail_` 被声明为 `volatile`：

```cpp
volatile size_t head_ = 0;
volatile size_t tail_ = 0;
```

为什么需要 `volatile`？因为编译器的优化器不知道 ISR 的存在。

考虑主循环中的 `pop()` 函数。编译器看到 `pop()` 被反复调用，可能会做这样的优化：第一次从内存读取 `head_`，然后缓存到寄存器中——后续调用直接用寄存器中的值，不再从内存读取。编译器的逻辑是："这个函数里没有任何代码修改 `head_`，所以值不会变，不需要重复读取。"

但编译器错了。`head_` 是被 ISR 中的 `push()` 修改的——而编译器看不到 ISR 的调用上下文。如果编译器缓存了 `head_` 的值，主循环就永远看不到 ISR push 进来的新数据。

`volatile` 关键字告诉编译器："这个变量可能被编译器看不到的方式修改，每次读取都必须从内存重新加载，不能缓存到寄存器。" 这样，主循环每次调用 `pop()` 时都会从内存重新读取 `head_`，确保能看到 ISR 的修改。

⚠️ `volatile` 不保证原子性——它只保证"每次都从内存读取"。如果一个操作需要多个步骤（比如 read-modify-write），`volatile` 本身不能保证这些步骤不被打断。但在我们的 SPSC 模式中，`push()` 只修改 head、`pop()` 只修改 tail，各自都是单步的赋值操作，所以不存在原子性问题。ARM Cortex-M3 的 32 位对齐读写本身就是原子的（在单核上），配合 SPSC 模式就足够安全了。

### 为什么不用 mutex？

`std::mutex` 需要操作系统支持（RTOS 或 C++ 线程库）。我们的 bare-metal STM32 上没有这些。而且 ISR 中不能阻塞——如果 ISR 尝试获取一个被主循环持有的 mutex，ISR 就会卡住（因为主循环正被 ISR 打断着，不可能释放 mutex），系统直接死锁。

无锁 SPSC 是 bare-metal 系统中 ISR-to-main 通信的标准方案。它不需要操作系统支持，不需要动态内存分配，不需要阻塞——ISR 中 push 一个字节是确定的、O(1) 的、不会失败的（除非缓冲区满了）。

---

## N = 128 够不够？

我们选择的缓冲区大小是 128 字节。这个数字是怎么来的？

115200 baud 下，每秒最多接收 11520 个字节（10 bits/byte）。每个字节间隔 87 微秒。如果主循环能在 87 微秒内处理完一个字节（读取 + 判断 + 拼入行缓冲），128 字节的缓冲区绰绰有余——缓冲区只会同时存几个字节。

但如果主循环在做耗时操作（比如处理一个复杂命令时），可能有几十个字节在缓冲区里排队。128 字节大约能缓冲 1.1 毫秒的数据。对于绝大多数交互场景（人打字、终端发送命令），1.1 毫秒的缓冲已经足够了。

如果真的不够，改模板参数就行——`CircularBuffer<256>` 或 `CircularBuffer<512>`。只要还是 2 的幂，编译时的 `static_assert` 就会通过，性能不会有任何变化。

---

## 小结

这一篇我们设计并实现了 ISR 和主循环之间的数据桥梁：无锁环形缓冲区。核心设计包括：SPSC 模式（单写单读，不需要锁）、2 的幂大小（位与替代取模，零开销）、`volatile` 保证跨执行流可见性、`static_assert` 编译时约束缓冲区大小。

下一篇我们把所有东西串起来：ISR 的回调链从 `USART1_IRQHandler` 到 `HAL_UART_RxCpltCallback` 到环形缓冲区的 push 和 restart，形成一个完整的"中断产生字节→缓冲区暂存→主循环消费"的流水线。
