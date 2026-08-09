---
chapter: 17
difficulty: beginner
order: 5
platform: stm32f1
reading_time_minutes: 8
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 35: printf Redirection and Blocking Receive — Making the Chip Speak via
  printf, and Learning to Listen'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/05-printf-redirect-and-blocking-receive.md
  source_hash: 3ea45b20a32cd5ec816b8290e0f0154998fc5478d49ba1c6924b305eecc0a0ef
  translated_at: '2026-06-16T04:11:50.594570+00:00'
  engine: anthropic
  token_count: 1230
---
# Part 35: printf Redirection and Blocking Receive — Making the Chip Speak with printf, and Learning to Listen

> The previous part made the chip utter its first words. In this part, we will do two things: make `printf` output directly to the serial port, and then attempt blocking receive—after which you will understand why blocking receive doesn't work.

---

## printf Redirection: The Principle

If you have used `printf` in an embedded project, you may have noticed that, by default, it outputs nothing. This is because `printf` itself doesn't know where the data should go—it is only responsible for formatting the string, and then handing the formatted result to the underlying I/O functions. On a PC, this underlying function writes data to the terminal; on bare-metal STM32, you need to provide this underlying function yourself.

newlib (the C standard library implementation used by the ARM toolchain) provides a set of system calls that can be redirected. Among them, `_write` is responsible for writing `len` bytes pointed to by `ptr` to the file descriptor `fd`. When `printf` is called, the formatted string is ultimately output through `_write`. If we override `_write` to make it send data to the UART, then all `printf` output will automatically go to the serial port.

This mechanism is called "retargeting"—redirecting standard I/O to custom hardware interfaces.

---

## Line-by-Line Explanation of printf_redirect.cpp

This is our complete implementation in the code, only 11 lines:

```cpp
#include <unistd.h>
#include <uart.hpp>

extern "C" {
    int _write(int fd, const char *ptr, int len) {
        (void)fd; // Suppress unused parameter warning
        auto huart = UART::get_handle<1>();
        HAL_UART_Transmit(huart, (uint8_t *)ptr, len, HAL_MAX_DELAY);
        return len;
    }
}
```

Let's break it down line by line:

### `extern "C"` Block

The function signature of `_write` must appear to the linker as a C function. This is because newlib uses C linkage to find this symbol—it expects the name of `_write` in the symbol table to be exactly `_write`, not something like `_Z6_write...` after the C++ compiler mangles it. `extern "C"` tells the C++ compiler: "Use C linkage rules for this function, do not perform name mangling."

### `int _write(int fd, const char *ptr, int len)`

Three parameters: `fd` is the file descriptor (1 = stdout, 2 = stderr), `ptr` points to the data to be sent, and `len` is the data length. We don't need to distinguish the `fd` parameter—whether it is stdout or stderr, it is sent to the same UART.

The `(void)fd;` attribute tells the compiler, "I know `fd` is not being used, don't warn me." This is a C++17 attribute that expresses intent more clearly than old-style comments like `// unused`.

### `auto huart = UART::get_handle<1>();`

Get the HAL handle pointer for USART1. `get_handle<1>` is a static method that returns a `UART_HandleTypeDef*`—this is the parameter required by all UART functions in the HAL library. We obtain the handle through `get_handle` instead of using a global variable `huart1`. The benefit of this is that the lifetime and access permissions of the handle are entirely managed by the C++ type system, with no global state leakage.

### `HAL_UART_Transmit(...)`

Blocking transmission. Exactly the same as discussed in the previous part—it sends `len` bytes one by one and returns only after completion. Because `HAL_MAX_DELAY` is used, it will never time out.

### `return len;`

Return the number of bytes actually written. Tell the C library "all data was successfully written." If -1 or 0 is returned, the C library might assume an error occurred.

---

## The Power of printf

With this redirection, any `printf` call in your code will automatically output to the serial port:

```cpp
printf("System started!\r\n");
printf("ADC Value: %d\r\n", adc_result);
printf("Status: %s\r\n", error ? "FAIL" : "OK");
```

This is much more convenient than manually splicing strings and then calling `HAL_UART_Transmit`. Especially for formatted output—format specifiers like `%d`, `%x`, and `%s` allow you to directly output numbers, hexadecimal values, and strings without writing `itoa` and string concatenation yourself.

However, there is one caveat: our CMakeLists.txt uses the `-specs=nano.specs` linker option. This option uses a reduced version of the C library to save Flash space, at the cost of **not supporting floating point `printf`**. This means that `printf("%f", 3.14)` will not output the correct result. If you need to output floating point numbers, either simulate with integers (e.g., output 314 as an integer), or switch to the full `newlib` implementation (remove `-specs=nano.specs`, but Flash usage will increase significantly).

---

## Blocking Receive: HAL_UART_Receive

With sending sorted, let's look at receiving. The HAL library provides a blocking receive function symmetric to `HAL_UART_Transmit`:

```cpp
uint8_t rx_data;
HAL_UART_Receive(&huart1, &rx_data, 1, HAL_MAX_DELAY);
printf("Received: %c\r\n", rx_data);
```

`HAL_UART_Receive` waits to receive the specified number of bytes. The code above waits to receive 1 byte, and prints it after receipt. If it times out (never with `HAL_MAX_DELAY`), it returns `HAL_TIMEOUT`.

Sounds reasonable, right? But let's put this receive into a complete main loop and see what happens:

```cpp
while (true) {
    uint8_t rx_data;
    // This line blocks forever!
    HAL_UART_Receive(&huart1, &rx_data, 1, HAL_MAX_DELAY);
    printf("Received: %c\r\n", rx_data);

    // Button polling, LED blinking...
    // None of this code runs until data arrives!
    button.poll();
    led.toggle();
    osDelay(100);
}
```

The problem is obvious: `HAL_UART_Receive` will **block forever** until a byte is received. If the PC side doesn't send any data, none of the code after this line will execute. Button polling stops, the LED stops flashing, the whole system "freezes," waiting for a byte that may never come.

This is the same essential problem as `HAL_GPIO_ReadPin` blocking the system in the button tutorial—your main loop is stuck on a call that might not return for a long time. In the button tutorial, the solution was non-blocking debouncing (using `millis()` for timestamp management). For UART receive, the solution is—interrupts.

You might think: "Can't I just set the timeout shorter? Like 100 milliseconds."

```cpp
// Receive with 100ms timeout
if (HAL_OK == HAL_UART_Receive(&huart1, &rx_data, 1, 100)) {
    printf("Received: %c\r\n", rx_data);
}
```

This does allow the main loop to keep running, but it introduces new problems. A 100-millisecond timeout means your button polling interval becomes, at worst, 100 milliseconds—which might be too slow for fast button presses. Also, every call to `HAL_UART_Receive` reconfigures the receive registers, and frequent configuration/timeout/reconfiguration wastes CPU time. This is not an elegant solution.

The correct solution is to let the hardware actively notify the CPU when data arrives, rather than the CPU actively waiting. This is interrupt-driven receive—the core theme of this series.

---

## From Blocking to Interrupt: The Essence of the Problem

Let's take a step back and see the essence of the problem clearly.

Blocking transmission isn't actually a big issue. You actively send data, you decide how fast, and when you're done, you continue with other tasks. The blocking time is predictable—at 115200 baud, one byte is 87 microseconds, and sending a 100-byte log takes just 8.7 milliseconds. This is perfectly acceptable in debugging scenarios.

Blocking receive is completely different. Receiving is a passive behavior—you don't know when data will come, it might be in the next millisecond, or it might not come for ten minutes. If you choose to wait (block), the system can't do anything during the wait. If you choose not to wait (timeout), there is a conflict between checking frequency and system response—checking too frequently wastes CPU, checking too slowly misses data.

The universal solution to this problem is: make the act of receiving change from "main loop actively asking" to "hardware actively notifying". When data arrives, the hardware generates an interrupt signal, the CPU pauses the current task to handle this byte, and then returns. The main loop doesn't need to wait, doesn't need to poll, and doesn't need to trade off between "timely response" and "not wasting CPU."

This is what the next three parts will cover. Part 36 discusses the Cortex-M3 interrupt mechanism and NVIC configuration. Part 37 designs a lock-free ring buffer to safely connect the ISR and the main loop. Part 38 strings together the complete callback chain.

---

## Summary

In this part, we did two things. printf redirection allows us to use the familiar `printf` for formatted output to the serial port, greatly improving the debugging experience. Blocking receive showed us the fatal problem of "waiting for data"—the main loop freezes. The existence of this problem is not a bug, but a fundamental limitation of blocking I/O.

In the next part, we enter the core stage of this series: interrupts. First, we clarify how the Cortex-M3 interrupt hardware works, and then we gradually build a complete interrupt-driven receive system.
