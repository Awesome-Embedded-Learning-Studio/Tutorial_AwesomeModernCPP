---
title: 'Part 35: `printf` Redirection and Blocking Receive — Making the Chip Speak
  with `printf`, and Listen Too'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 17
order: 5
translation:
  source: documents/vol8-domains/embedded/03-uart/05-printf-redirect-and-blocking-receive.md
  source_hash: bfc84034896ca641ead87b2adc1105cac3d20bae8a305e95f3f2c0a7b13b2f2d
  translated_at: '2026-05-26T12:15:52.556018+00:00'
  engine: anthropic
  token_count: 1226
---
# Part 35: printf Retargeting and Blocking Receive — Making the Chip Speak with printf, and Learning to Listen

> In the previous part, we made the chip speak its first words. In this part, we do two things: redirect `printf` output directly to the serial port, and then try blocking receive — after which you will understand exactly why blocking receive does not work.

---

## printf Retargeting: The Principle

If you have used `printf` in an embedded project, you might have noticed that by default, it outputs nothing. This is because `printf` itself does not know where the data should go — it only formats the string and hands the formatted result to a low-level I/O function. On a PC, this low-level function writes data to the terminal; on bare-metal STM32, you need to provide this low-level function yourself.

newlib (the C standard library implementation used by the ARM toolchain) provides a set of retargetable system calls. Among them, `_write` is responsible for writing `len` bytes pointed to by `ptr` to the file descriptor `fd`. When `printf` is called, the formatted string ultimately goes out through `_write`. If we override `_write` to send data to the UART, all `printf` output automatically goes to the serial port.

This mechanism is called "retargeting" — redirecting standard I/O to a custom hardware interface.

---

## Line-by-Line Walkthrough of printf_redirect.cpp

Here is the complete implementation in our code, only 11 lines:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/printf_redirect.cpp
#include "device/uart/uart_manager.hpp"

extern "C" {

int _write(int fd [[maybe_unused]], char* ptr, int len) {
    auto* huart = device::uart::UartManager<device::uart::UartInstance::Usart1>::handle();
    HAL_UART_Transmit(huart, reinterpret_cast<uint8_t*>(ptr), len, HAL_MAX_DELAY);
    return len;
}

} // extern "C"
```

Line-by-line breakdown:

### `extern "C"` Block

The function signature of `_write` must appear as a C function in the linker's eyes. This is because newlib uses C linkage to look up this symbol — it expects `_write` to have the exact name `_write` in the symbol table, not something mangled by the C++ compiler like `__Z5_writePvii`. `extern "C"` tells the C++ compiler: "Use C linkage rules for this function, do not perform name mangling."

### `int _write(int fd, char* ptr, int len)`

Three parameters: `fd` is the file descriptor (1 = stdout, 2 = stderr), `ptr` points to the data to be sent, and `len` is the data length. We do not need to differentiate the `fd` parameter — whether it is stdout or stderr, everything goes to the same UART.

The `[[maybe_unused]]` attribute tells the compiler "I know `fd` is not being used, do not warn." This is a C++17 attribute that expresses the intent much more clearly than the old-style `(void)fd;` approach.

### `auto* huart = UartManager<UartInstance::Usart1>::handle()`

We get the HAL handle pointer for USART1. `handle()` is a static method that returns `UART_HandleTypeDef*` — the parameter required by all UART functions in the HAL library. We obtain the handle through `handle()` rather than using the global variable `huart1`. The benefit of this approach is that the handle's lifetime and access permissions are entirely managed by the C++ type system, with zero global state leakage.

### `HAL_UART_Transmit(huart, ...)`

Blocking send. This is exactly the same as what we discussed in the previous part — it sends out `len` bytes one by one and only returns when finished. Because we use `HAL_MAX_DELAY`, it will never time out.

### `return len`

Return the number of bytes actually written. This tells the C library "all data was successfully written." If you return -1 or 0, the C library might assume an error occurred.

---

## The Power of printf

With this retargeting in place, any `printf` call in your code will automatically output to the serial port:

```c
printf("System initialized at %lu Hz\r\n", SystemCoreClock);
printf("Button pressed! Count: %d\r\n", count);
printf("Temperature: %d.%d C\r\n", temp / 10, temp % 10);
```

This is far more convenient than manually concatenating strings and calling `HAL_UART_Transmit`. This is especially true for formatted output — format specifiers like `%d`, `%x`, and `%s` let you directly output numbers, hexadecimal values, and strings without writing your own `itoa` and string concatenation routines.

However, there is one thing to note: our CMakeLists.txt uses the `-specs=nano.specs` linker option. This option uses a stripped-down C library to save Flash space, but the tradeoff is that **it does not support floating-point `printf`**. In other words, `printf("%f", 3.14)` will not output the correct result. If you need to output floating-point numbers, you can either simulate it with integers (`printf("%d.%02d", 3, 14)`), or switch to the full `newlib` implementation (remove `-specs=nano.specs`, but Flash usage will increase significantly).

---

## Blocking Receive: HAL_UART_Receive

With sending sorted out, let us look at receiving. The HAL library provides a blocking receive function that is symmetric to `HAL_UART_Transmit`:

```c
uint8_t byte;
HAL_StatusTypeDef result = HAL_UART_Receive(&huart1, &byte, 1, HAL_MAX_DELAY);
if (result == HAL_OK) {
    printf("Received: 0x%02X\r\n", byte);
}
```

`HAL_UART_Receive` waits to receive the specified number of bytes. The code above waits to receive one byte, and prints it after it arrives. If it times out (which will never happen with `HAL_MAX_DELAY`), it returns `HAL_TIMEOUT`.

Sounds reasonable, right? But let us put this receive into a complete main loop and see what happens:

```c
while (1) {
    uint8_t byte;
    HAL_UART_Receive(&huart1, &byte, 1, HAL_MAX_DELAY);
    // 处理接收到的字节...
    process_byte(byte);

    // 检查按钮
    button_poll();  // <-- 这行永远不会执行，直到收到下一个字节！

    // 闪烁 LED
    led_toggle();   // <-- 同上
}
```

The problem is obvious: `HAL_UART_Receive` will **block forever** until it receives a byte. If the PC side does not send any data, none of the code after this line will execute. Button polling stops, the LED stops blinking, and the entire system "freezes," waiting for a byte that might never come.

This is the same fundamental issue as `HAL_Delay` blocking the system in the button tutorial — your main loop gets stuck on a call that might not return for a long time. In the button tutorial, the solution was non-blocking debounce (using timestamps managed by `HAL_GetTick`). For UART receive, the solution is — interrupts.

You might think: "Could I just set a shorter timeout? Like 100 milliseconds."

```c
while (1) {
    uint8_t byte;
    HAL_StatusTypeDef result = HAL_UART_Receive(&huart1, &byte, 1, 100);
    if (result == HAL_OK) {
        process_byte(byte);
    }
    // 即使没收到数据，100ms 后也会返回
    button_poll();
    led_toggle();
}
```

This does let the main loop continue running, but it introduces new problems. A 100-millisecond timeout means your button polling interval becomes 100 milliseconds in the worst case — which might be too slow for fast button presses. Furthermore, every call to `HAL_UART_Receive` reconfigures the receive registers, and frequent configure/timeout/reconfigure cycles waste CPU time. This is not an elegant solution.

The correct approach is to let the hardware proactively notify the CPU when data arrives, rather than having the CPU actively wait. This is interrupt-driven receive — the core theme of this series.

---

## From Blocking to Interrupt: The Essence of the Problem

Let us take a step back and see the essence of the problem clearly.

Blocking send is actually not a big issue. You actively send data, you decide how fast to send it, and you move on to other things once it is done. The blocking time is predictable — at 115200 baud, one byte takes 87 microseconds, and sending a 100-byte log message takes only 8.7 milliseconds. This is perfectly acceptable in debugging scenarios.

Blocking receive is completely different. Receiving is a passive behavior — you do not know when the data will arrive. It might come in the next millisecond, or it might not come for ten minutes. If you choose to wait (block), the system can do nothing during the wait. If you choose not to wait (timeout), there is a contradiction between the check frequency and system responsiveness — checking too frequently wastes CPU, and checking too slowly causes you to miss data.

The general solution to this problem is to change receiving from "the main loop actively asking" to "the hardware proactively notifying." When data arrives, the hardware generates an interrupt signal, the CPU pauses its current task to handle this byte, and then returns to what it was doing. The main loop does not need to wait, does not need to poll, and does not need to trade off between "timely response" and "not wasting CPU."

This is what the next three parts will cover. Part 36 discusses the Cortex-M3 interrupt mechanism and NVIC configuration. Part 37 designs a lock-free ring buffer to safely connect the ISR and the main loop. Part 38 strings the complete callback chain together.

---

## Summary

In this part, we did two things. printf retargeting allows us to use the familiar `printf` for formatted output to the serial port, significantly improving the debugging experience. Blocking receive let us see with our own eyes the fatal problem of "waiting for data" — the main loop freezes. The existence of this problem is not a bug, but a fundamental limitation of blocking I/O.

In the next part, we enter the core phase of this series: interrupts. We will first clarify how the Cortex-M3 interrupt hardware works, and then gradually build a complete interrupt-driven receive system.
