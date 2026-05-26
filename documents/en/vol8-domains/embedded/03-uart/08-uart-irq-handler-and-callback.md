---
title: 'Part 38: UART IRQ Handling and Callbacks — The Complete Puzzle of Interrupt
  Reception'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 17
order: 8
translation:
  source: documents/vol8-domains/embedded/03-uart/08-uart-irq-handler-and-callback.md
  source_hash: c0a86e9a6d69c3552d18c50ac4d5ac39eaa64a3903c3e233252fcb21b203d2a7
  translated_at: '2026-05-26T12:16:38.783352+00:00'
  engine: anthropic
  token_count: 1360
---
# Part 38: UART IRQ Handling and Callbacks — The Complete Picture of Interrupt-Driven Reception

> The NVIC (Nested Vectored Interrupt Controller), ring buffer, and single-byte reception strategy—the previous three parts prepared all the pieces. This part assembles them into a complete interrupt-driven reception pipeline.

---

## uart_irq.cpp: Everything in This Part Comes Down to One File

The core of this part is `uart_irq.cpp`. It is only 42 lines long, but it serves as the central hub of the entire interrupt-driven reception system. Let us break down every line from start to finish.

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

## Anonymous Namespace: Encapsulating Implementation Details

The `namespace { ... }` at the beginning of the file is an anonymous namespace. In C++, all symbols within an anonymous namespace have internal linkage—they are visible only within the current translation unit (.cpp file) and do not leak into the global scope.

The `rx_byte`, `rx_ring`, and `Manager` type aliases, along with the `restart_receive()` function, are all placed inside the anonymous namespace. Why? Because they are implementation details and should not be directly accessed by other files.

`rx_byte` is the buffer used by the HAL (Hardware Abstraction Layer) to receive a single byte. If external code accidentally modifies it, the ISR (interrupt service routine) will read incorrect data. `rx_ring` is the ring buffer instance. If external code directly calls `push()`, it violates the SPSC (Single-Producer Single-Consumer) pattern (only the ISR should push). `restart_receive()` should also not be called arbitrarily from the outside—it is used only within the ISR callback.

Through the anonymous namespace, these symbols are given unique internal names after compilation, and the linker will not expose them to other translation units. This is the standard C++ approach to replacing C's `static` keyword—the functionality is equivalent, but the semantics are much clearer.

---

## Three Public Interfaces

Outside the anonymous namespace, there are three functions, which represent the entire public interface provided by `uart_irq.cpp`:

### uart_rx_buffer() — Exposing a Read-Only Reference to the Ring Buffer

```cpp
base::CircularBuffer<128>& uart_rx_buffer() { return rx_ring; }
```

`main.cpp` needs to pop bytes from the ring buffer, but it should not directly access `rx_ring` (because `rx_ring` is inside the anonymous namespace and completely invisible to the outside). `uart_rx_buffer()` returns a reference—the main loop uses this reference to call `pop()` and read data.

Why use a function instead of a `extern` global variable? Two reasons. First, a function provides better encapsulation—if we need to add thread-safety checks or track access counts in the future, we only need to modify the function implementation. Second, returning a reference rather than a pointer results in more natural syntax (`rx.pop(b)` vs `rx->pop(b)`), and a reference cannot be null.

### uart_start_receive() — Starting the Reception Pipeline

```cpp
void uart_start_receive() { restart_receive(); }
```

Called once in `main()`, this starts the first round of single-byte reception. This name is clearer than `restart_receive()`—external code does not care about the concept of "restarting"; it only knows to "please start receiving." Internally, it calls the same `restart_receive()`, but it exposes different semantics to the outside.

### USART1_IRQHandler and HAL_UART_RxCpltCallback — ISR Entry and Callback

These two functions are defined inside an `extern "C"` block, and the previous part already explained why C linkage is necessary here.

---

## The Complete Callback Chain

When a byte arrives at USART1, the path from hardware interrupt trigger to the byte entering the ring buffer goes through the following call chain:

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

The entire process, from the byte arriving and triggering the interrupt to the ISR returning, takes about 1-2 microseconds on a 72 MHz Cortex-M3. Compared to the 87-microsecond byte interval, the ISR has ample time to complete processing—there is no risk of losing bytes.

---

## The Receive-Process-Restart Loop

This callback chain forms a self-looping structure. Expressed in pseudocode:

```text
初始化时：
  uart_start_receive() → HAL_UART_Receive_IT(&rx_byte, 1) → 等待

每个字节到达时：
  ISR → HAL_UART_IRQHandler → RxCpltCallback
    → push(rx_byte)           // 字节入队
    → restart_receive()       // 重新等待下一个字节
```

The key point is that `restart_receive()` is called within the callback. Every time a byte is received and processed, the next round of reception is immediately set up. This keeps the pipeline between the ISR and the main loop in a perpetual "ready" state—the next byte can arrive at any time, and the ISR is always ready to handle it.

What happens if we forget to call `restart_receive()` in the callback? We will only receive the first byte. After that, RXNEIE is not re-enabled, so subsequent bytes will not trigger interrupts when they arrive, and the bytes are lost. This error will not throw an exception or crash the system—it simply results in "receiving one byte and then never receiving anything again." This is one of the most common bugs in UART interrupt-driven reception.

---

## How main.cpp Consumes Data

In the main loop, consuming data is very straightforward:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
auto& rx = uart_rx_buffer();
std::byte b{};
while (rx.pop(b)) {
    char c = static_cast<char>(b);
    // 处理字符 c...
}
```

`rx.pop(b)` pops a byte from the ring buffer. If the buffer is not empty, it returns true and stores the byte in `b`; if the buffer is empty, it returns false. The `while (rx.pop(b))` loop keeps popping bytes until the buffer is cleared.

During each main loop iteration, we pop all available bytes at once, and then process them. The ISR might continue to push new bytes while the main loop is executing, but these bytes will safely wait in the ring buffer until they are popped during the next main loop iteration.

This push-pop pattern is the practical application of the SPSC (Single-Producer Single-Consumer) pattern discussed in the previous part: the ISR is the producer (push), the main loop is the consumer (pop), and the ring buffer is the queue between them.

---

## The Callback Registration Mechanism in UartDriver

In addition to handling bytes directly in `HAL_UART_RxCpltCallback`, `UartDriver` also provides a more flexible callback registration mechanism:

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

This mechanism allows users to register custom receive/transmit complete callbacks. When `on_rx_complete()` is called, it passes the received data (in the form of `std::span`) to the user-registered callback function.

In the current code, we do not actually use this callback mechanism—`uart_irq.cpp` handles bytes directly in the HAL callback. However, this mechanism leaves an interface open for future expansion. For example, we could register a callback to trigger event processing when a complete line is received, without needing to poll the ring buffer in the main loop.

---

## Summary

This part finishes assembling all the pieces for interrupt-driven reception. From `USART1_IRQHandler` to `HAL_UART_RxCpltCallback` to `rx_ring.push()` to `restart_receive()`, a complete reception pipeline is formed. The ISR completes byte enqueuing and reception restarting in a few microseconds, while the main loop consumes data from the ring buffer at its own pace. The two communicate safely through a lock-free ring buffer, without blocking or interfering with each other.

The three parts of Phase Four (Interrupt-Driven) end here. Starting from the next part, we enter Phase Five—C++ Abstraction. We will begin with error handling: how `std::expected` provides type-safe error handling in embedded environments where exceptions are disabled.
