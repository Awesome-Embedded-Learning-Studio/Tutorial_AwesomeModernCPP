---
chapter: 17
difficulty: intermediate
order: 8
platform: stm32f1
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: 第38篇：UART IRQ 处理与回调 —— 中断接收的完整拼图
description: ''
---
# 第38篇：UART IRQ 处理与回调 —— 中断接收的完整拼图

> NVIC、环形缓冲区、单字节接收策略——前面三篇把所有零件都准备好了。这一篇把它们组装成一套完整的中断接收流水线。

---

## uart_irq.cpp：整篇都在讲这一个文件

这一篇的核心是 `uart_irq.cpp`。它只有 42 行，但它是整个中断驱动接收系统的中枢。让我们从头到尾拆解每一行。

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/uart_irq.cpp
#include "base/circular_buffer.hpp"
#include "device/uart/uart_manager.hpp"

#include <cstddef>

namespace {

std::byte rx_byte{};

base::CircularBuffer<128> rx_ring;

using Manager = device::uart::UartManager<device::uart::UartInstance::Usart1>;

void restart_receive() {
    [[maybe_unused]] auto r =
        Manager::driver().receive_it(std::span<std::byte, 1>{&rx_byte, 1});
}

} // namespace

base::CircularBuffer<128>& uart_rx_buffer() { return rx_ring; }

extern "C" {

void USART1_IRQHandler(void) {
    HAL_UART_IRQHandler(Manager::handle());
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance == USART1) {
        rx_ring.push(rx_byte);
        restart_receive();
    }
}

} // extern "C"

void uart_start_receive() { restart_receive(); }
```

---

## 匿名命名空间：封装实现细节

文件开头的 `namespace { ... }` 是一个匿名命名空间。在 C++ 中，匿名命名空间内的所有符号具有内部链接——它们只在当前翻译单元（.cpp 文件）内可见，不会泄漏到全局作用域。

`rx_byte`、`rx_ring`、`Manager` 类型别名和 `restart_receive()` 函数都被放在匿名命名空间中。为什么？因为它们是实现细节，不应该被其他文件直接访问。

`rx_byte` 是 HAL 用来接收单字节的缓冲区。如果外部代码意外修改了它，ISR 就会读到错误的数据。`rx_ring` 是环形缓冲区实例。如果外部代码直接调用 `push()`，就会违反 SPSC 模式（只有 ISR 应该 push）。`restart_receive()` 也不应该被外部随意调用——它只在 ISR 回调中使用。

通过匿名命名空间，这些符号在编译后被赋予了唯一的内部名称，链接器不会把它们暴露给其他翻译单元。这是 C++ 中替代 C 的 `static` 关键字的标准做法——功能等价，但语义更清晰。

---

## 三个公开接口

匿名命名空间外有三个函数，是 `uart_irq.cpp` 对外提供的全部接口：

### uart_rx_buffer() —— 暴露环形缓冲区的只读引用

```cpp
base::CircularBuffer<128>& uart_rx_buffer() { return rx_ring; }
```

`main.cpp` 需要从环形缓冲区 pop 字节，但它不应该直接访问 `rx_ring`（因为 `rx_ring` 在匿名命名空间中，外部根本看不到）。`uart_rx_buffer()` 返回一个引用——主循环通过这个引用调用 `pop()` 读取数据。

为什么是函数而不是 `extern` 全局变量？两个原因。第一，函数提供了更好的封装——如果将来需要加线程安全检查或统计访问次数，改函数实现就行。第二，返回引用而不是指针，语法更自然（`rx.pop(b)` vs `rx->pop(b)`），而且引用不可能为 null。

### uart_start_receive() —— 启动接收流水线

```cpp
void uart_start_receive() { restart_receive(); }
```

在 `main()` 中调用一次，启动第一轮单字节接收。这个名字比 `restart_receive()` 更清晰——外部代码不关心"重启"的概念，它只知道"请开始接收"。内部调用的是同一个 `restart_receive()`，但对外暴露了不同的语义。

### USART1_IRQHandler 和 HAL_UART_RxCpltCallback —— ISR 入口和回调

这两个函数在 `extern "C"` 块中定义，上一篇已经解释过为什么需要 C 链接。

---

## 完整的回调链

当一个字节到达 USART1 时，从硬件中断触发到字节进入环形缓冲区，经过以下调用链：

```text
物理层：字节到达 PA10 (RX)
  → USART 接收移位寄存器逐 bit 移入
  → 完整字节移入 RDR，RXNE 标志置 1
  → RXNEIE 已使能，NVIC 已使能 → CPU 暂停当前任务
  → 保存上下文（自动压栈 r0-r3, r12, LR, PC, xPSR）
  → 从向量表读取 USART1_IRQHandler 地址
  → 跳转到 USART1_IRQHandler

软件层：
USART1_IRQHandler()
  → HAL_UART_IRQHandler(Manager::handle())
    → 检查 RXNE 标志（确认是接收中断）
    → 读取 DR 寄存器，数据存入 rx_byte
    → RXNE 标志自动清除（读 DR 时硬件自动清零）
    → 递减接收计数（1 → 0，接收完成）
    → 调用 HAL_UART_RxCpltCallback(huart)

HAL_UART_RxCpltCallback()
  → 检查 huart->Instance == USART1（确认是 USART1 的回调）
  → rx_ring.push(rx_byte)（字节进入环形缓冲区）
  → restart_receive()（设置下一轮单字节接收）
    → HAL_UART_Receive_IT(&huart, &rx_byte, 1)
    → 重新使能 RXNEIE

  → ISR 返回（硬件自动出栈，恢复被中断的代码）
```

整个过程从字节到达中断触发到 ISR 返回，在 72 MHz 的 Cortex-M3 上大约需要 1-2 微秒。相比 87 微秒的字节间隔，ISR 有充裕的时间完成处理——不会有字节丢失的风险。

---

## 接收-处理-重启循环

这个回调链构成了一个自循环的结构。用伪代码表示：

```text
初始化时：
  uart_start_receive() → HAL_UART_Receive_IT(&rx_byte, 1) → 等待

每个字节到达时：
  ISR → HAL_UART_IRQHandler → RxCpltCallback
    → push(rx_byte)           // 字节入队
    → restart_receive()       // 重新等待下一个字节
```

关键点在于 `restart_receive()` 在回调中调用。每次收到一个字节并处理完后，立即设置下一轮接收。这样 ISR 和主循环之间的流水线永远保持"就绪"状态——下一个字节随时可以到达，ISR 随时可以处理。

如果回调中忘记调用 `restart_receive()`，会发生什么？你只会收到第一个字节。之后 RXNEIE 没有被重新使能，后续字节到达时不会触发中断，字节就丢了。这个错误不会报错、不会崩溃——只是"收了一个字节后再也收不到了"。这是 UART 中断接收中最常见的 bug 之一。

---

## main.cpp 如何消费数据

在主循环中，数据的消费非常简单：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
auto& rx = uart_rx_buffer();
std::byte b{};
while (rx.pop(b)) {
    char c = static_cast<char>(b);
    // 处理字符 c...
}
```

`rx.pop(b)` 从环形缓冲区取出一个字节。如果缓冲区非空，返回 true 并把字节存入 `b`；如果缓冲区为空，返回 false。`while (rx.pop(b))` 循环会一直弹出字节，直到缓冲区清空。

每次主循环迭代时，先一次性弹出所有可用字节，然后处理。ISR 在主循环执行期间可能继续 push 新字节，但这些字节会安全地待在环形缓冲区里，等下一轮主循环时被弹出。

这个 push-pop 模式就是上一篇讲的 SPSC（单生产者单消费者）模式在实际代码中的应用：ISR 是生产者（push），主循环是消费者（pop），环形缓冲区是两者之间的队列。

---

## UartDriver 中的回调注册机制

除了直接在 `HAL_UART_RxCpltCallback` 中处理字节，`UartDriver` 还提供了一个更灵活的回调注册机制：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
using RxCallback = void (*)(std::span<const std::byte>);
using TxCallback = void (*)();

void set_rx_callback(RxCallback cb) { rx_callback_ = cb; }
void set_tx_callback(TxCallback cb) { tx_callback_ = cb; }

void on_rx_complete(std::span<const std::byte> data) {
    if (rx_callback_) { rx_callback_(data); }
}

void on_tx_complete() {
    if (tx_callback_) { tx_callback_(); }
}
```

这个机制允许用户注册自定义的接收/发送完成回调。当 `on_rx_complete()` 被调用时，它会把接收到的数据（以 `std::span` 形式）传给用户注册的回调函数。

在当前代码中，我们并没有使用这个回调机制——`uart_irq.cpp` 直接在 HAL 回调中处理字节。但这个机制为将来的扩展留了接口。比如，你可以注册一个回调来在接收到完整行时触发事件处理，而不需要在主循环中轮询环形缓冲区。

---

## 小结

这一篇把中断驱动接收的所有零件组装完毕。从 `USART1_IRQHandler` 到 `HAL_UART_RxCpltCallback` 到 `rx_ring.push()` 到 `restart_receive()`，构成了一条完整的接收流水线。ISR 在几个微秒内完成字节入队和重启接收，主循环按自己的节奏从环形缓冲区消费数据。两者通过无锁环形缓冲区安全通信，互不阻塞，互不干扰。

阶段四（中断驱动）的三篇到此结束。从下一篇开始，我们进入阶段五——C++ 抽象。先从错误处理开始：`std::expected` 如何在禁用异常的嵌入式环境中提供类型安全的错误处理。
