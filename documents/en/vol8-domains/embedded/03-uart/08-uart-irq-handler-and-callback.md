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
title: 'Part 38: UART IRQ Handling and Callbacks — The Complete Picture of Interrupt
  Reception'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/08-uart-irq-handler-and-callback.md
  source_hash: 52a22a780a46b108a0cd09cfd7fbd99bde038ecc8f8d1c271246fe015bb091b4
  translated_at: '2026-06-16T04:12:07.243295+00:00'
  engine: anthropic
  token_count: 1364
---
# Part 38: UART IRQ Handling and Callbacks — The Final Piece of the Interrupt Reception Puzzle

> NVIC, ring buffer, single-byte reception strategy — the previous three parts prepared all the components. This part assembles them into a complete interrupt-driven reception pipeline.

---

## uart_irq.cpp: Everything About This One File

The core of this part is `uart_irq.cpp`. It is only 42 lines long, but it serves as the central hub for the entire interrupt-driven reception system. Let's dissect every line from start to finish.

```cpp
// uart_irq.cpp
```

---

## Anonymous Namespace: Encapsulating Implementation Details

The `namespace {` at the beginning of the file is an anonymous namespace. In C++, all symbols within an anonymous namespace have internal linkage — they are visible only within the current translation unit (the .cpp file) and do not leak into the global scope.

The `RxBuffer`, `RingBuffer`, `UartDriver` type aliases and the `restart_receive` function are all placed inside the anonymous namespace. Why? Because they are implementation details and should not be directly accessed by other files.

`rx_buffer` is the buffer used by the HAL to receive a single byte. If external code accidentally modifies it, the ISR will read incorrect data. `rx_ring_buffer` is the ring buffer instance. If external code directly calls `push`, it violates the SPSC (Single Producer Single Consumer) pattern (only the ISR should push). `restart_receive` should also not be arbitrarily called by external code — it is used only within the ISR callback.

Through the anonymous namespace, these symbols are assigned unique internal names after compilation, and the linker will not expose them to other translation units. This is the standard C++ idiom for replacing C's `static` keyword — the functionality is equivalent, but the semantics are clearer.

---

## Three Public Interfaces

Outside the anonymous namespace, there are three functions, which constitute the entire interface provided by `uart_irq.cpp`:

### uart_rx_buffer() — Exposing a Read-Only Reference to the Ring Buffer

```cpp
const RingBuffer& uart_rx_buffer() {
    return rx_ring_buffer;
}
```

`main.cpp` needs to pop bytes from the ring buffer, but it should not access `rx_ring_buffer` directly (since `rx_ring_buffer` is in the anonymous namespace, it is invisible to the outside). `uart_rx_buffer` returns a reference — the main loop uses this reference to call `pop` to read data.

Why a function instead of a `static` global variable? Two reasons. First, functions provide better encapsulation — if we need to add thread-safety checks or count accesses later, we only change the function implementation. Second, returning a reference rather than a pointer results in more natural syntax (`buf.pop()` vs `buf->pop()`), and a reference can never be null.

### uart_start_receive() — Starting the Reception Pipeline

```cpp
void uart_start_receive() {
    restart_receive();
}
```

We call this once in `setup()` to start the first round of single-byte reception. This name is clearer than `restart_receive` — external code doesn't care about the concept of "restarting"; it just knows "please start receiving". Internally it calls the same `restart_receive`, but it exposes different semantics to the outside.

### USART1_IRQHandler and HAL_UART_RxCpltCallback — ISR Entry and Callback

These two functions are defined in an `extern "C"` block, as explained in the previous part.

---

## The Complete Callback Chain

When a byte arrives at USART1, the path from the hardware interrupt triggering to the byte entering the ring buffer goes through the following call chain:

```text
Hardware Interrupt
  └─> USART1_IRQHandler (C linkage, defined in startup)
      └─> HAL_UART_IRQHandler (HAL library)
          └─> UART_RxEventCallback (HAL weak definition)
              └─> HAL_UARTEx_RxEventCallback (HAL weak definition)
                  └─> HAL_UART_RxCpltCallback (Our override in uart_irq.cpp)
                      └─> restart_receive (Internal helper)
```

The entire process, from the byte arriving and triggering the interrupt to the ISR returning, takes about 1-2 microseconds on a 72 MHz Cortex-M3. Compared to the 87-microsecond byte interval, the ISR has ample time to complete processing — there is no risk of losing bytes.

---

## The Receive-Process-Restart Loop

This callback chain forms a self-looping structure. Expressed in pseudocode:

```text
loop forever:
    wait for interrupt
    byte = read from UART data register
    ring_buffer.push(byte)
    restart_receive()  // Re-arm the HAL for the next byte
```

The key point is that `restart_receive` is called within the callback. Every time a byte is received and processed, the next round of reception is set up immediately. This keeps the pipeline between the ISR and the main loop in a "ready" state — the next byte can arrive at any time, and the ISR is ready to handle it.

What happens if we forget to call `restart_receive` in the callback? You will only receive the first byte. After that, RXNEIE is not re-enabled, so subsequent bytes will not trigger interrupts, and data will be lost. This error won't report a failure or crash — it just "stops receiving after the first byte." This is one of the most common bugs in UART interrupt reception.

---

## How main.cpp Consumes Data

In the main loop, consuming data is very simple:

```cpp
// main.cpp
void loop() {
    std::byte data;
    while (uart_rx_buffer().pop(data)) {
        // Process data
    }
}
```

`pop` removes a byte from the ring buffer. If the buffer is not empty, it returns true and stores the byte in `data`; if the buffer is empty, it returns false. The `while` loop keeps popping bytes until the buffer is cleared.

During each iteration of the main loop, we pop all available bytes at once, then process them. The ISR may continue to push new bytes while the main loop is executing, but these bytes will wait safely in the ring buffer until the next iteration of the main loop.

This push-pop pattern is the application of the SPSC (Single Producer Single Consumer) mode discussed in the previous part: the ISR is the producer (push), the main loop is the consumer (pop), and the ring buffer is the queue between them.

---

## Callback Registration Mechanism in UartDriver

Besides handling bytes directly in `uart_irq.cpp`, `UartDriver` provides a more flexible callback registration mechanism:

```cpp
// uart_driver.hpp
using RxCallback = std::function<void(const std::span<std::byte>&)>;
void register_rx_callback(RxCallback callback);
```

This mechanism allows users to register custom receive/transmit completion callbacks. When `on_rx_complete` is called, it passes the received data (as a `std::span<std::byte>`) to the user-registered callback function.

In the current code, we don't use this callback mechanism — `uart_irq.cpp` handles bytes directly in the HAL callback. However, this mechanism leaves an interface for future expansion. For example, you could register a callback to trigger event handling when a complete line is received, without needing to poll the ring buffer in the main loop.

---

## Summary

This part completes the assembly of all components for interrupt-driven reception. From `USART1_IRQHandler` to `HAL_UART_RxCpltCallback` to `restart_receive` to `rx_ring_buffer`, a complete reception pipeline is formed. The ISR completes byte queuing and reception restart within a few microseconds, while the main loop consumes data from the ring buffer at its own pace. The two communicate safely via a lock-free ring buffer, without blocking or interfering with each other.

The three parts of Phase Four (Interrupt-Driven) end here. Starting with the next part, we enter Phase Five — C++ Abstraction. We begin with error handling: how `std::expected` provides type-safe error handling in embedded environments where exceptions are disabled.
