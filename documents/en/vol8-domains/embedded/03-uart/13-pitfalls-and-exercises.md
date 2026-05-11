---
title: 'Part 43: Common Pitfalls and Practical Exercises — Playing with UART in Creative
  Ways'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 17
order: 13
---
# Part 43: Common Pitfalls and Hands-On Exercises — Mastering UART

> The final article in the UART tutorial. Pitfall avoidance + three exercises to help you truly make the knowledge your own.

---

## Common Pitfalls

### Pitfall 1: TX/RX Crossover Wiring

This is the number one issue in UART debugging, bar none.

**Symptom**: The terminal receives nothing, or doesn't receive the data being sent.

**Cause**: Connecting the adapter's TX to the Blue Pill's TX (PA9), and the adapter's RX to the Blue Pill's RX (PA10). TX to TX means both sides are transmitting and nobody is listening — of course nothing is received.

**Fix**: Remember "crossover wiring" — adapter TX to Blue Pill RX (PA10), adapter RX to Blue Pill TX (PA9). If you aren't sure which wire is TX and which is RX, swap them and try — you won't fry anything, it just won't work.

### Pitfall 2: Baud Rate Mismatch

**Symptom**: The terminal displays garbled text — looks like random characters.

**Cause**: The baud rate set in code doesn't match the terminal software's baud rate. For example, the code uses 115200, but the terminal is set to 9600. UART is an asynchronous protocol; both sides must operate at exactly the same rate, otherwise all sampling points shift and the read data is completely wrong.

**Fix**: Confirm that the `UartConfig{.baud_rate = ...}` in your code exactly matches the terminal software's baud rate setting. It's not just the baud rate — data bits, parity bits, and stop bits must also match (standard configuration is 8N1).

### Pitfall 3: Ring Buffer Overflow

**Symptom**: The second half of a long string is lost during transmission, or command parsing occasionally fails.

**Cause**: The ISR pushes bytes faster than the main loop pops them. Once the 128-byte buffer is full, `push()` returns false and bytes are dropped. This happens when the PC rapidly sends a large amount of data (like pasting a long block of text) while the main loop is busy handling other things (like button debounce or sending a response).

**Fix**: Increasing the buffer size is the most direct approach — change `CircularBuffer<128>` to `CircularBuffer<256>` or `CircularBuffer<512>`. Additionally, ensure there are no long-blocking operations in the main loop — each loop iteration should process all pending data as quickly as possible.

### Pitfall 4: Forgetting volatile on the Ring Buffer

**Symptom**: Seems to work fine, but occasionally loses data. Becomes more frequent when increasing the optimization level (`-O2`).

**Cause**: The `CircularBuffer`'s `head_` and `tail_` are not declared as `volatile`. During compiler optimization, the `head_` read in the main loop gets cached in a register, and subsequent loops no longer re-read from memory — the ISR's push operations become invisible to the main loop.

**Fix**: Ensure `head_` and `tail_` are declared as `volatile size_t`. Our code already correctly uses `volatile` — but if you write your own ring buffer, don't forget this point.

### Pitfall 5: printf Floating-Point vs nano.specs

**Symptom**: `printf("%f", 3.14)` outputs garbled text or nothing at all.

**Cause**: Our CMakeLists.txt uses the `-specs=nano.specs` linker option, which links against the stripped-down C library (nano newlib). The stripped-down version does not support floating-point printf formatting — format specifiers like `%f` and `%g` don't work.

**Fix**: Simulate floating-point output using integers: `printf("%d.%02d", (int)(value * 100) / 100, (int)(value * 100) % 100)`. Alternatively, if Flash space is sufficient, remove `-specs=nano.specs` to link the full C library (Flash usage will increase by about 10-20 KB).

### Pitfall 6: Forgetting to Restart Reception in the Callback

**Symptom**: The first byte is received, but no further data is ever received.

**Cause**: Forgetting to call `restart_receive()` in the `HAL_UART_RxCpltCallback()`. After HAL completes a single-byte reception, it does not automatically start the next round — you must manually call `HAL_UART_Receive_IT()` to re-enable reception. If you forget, RXNEIE is not re-enabled, and the next arriving byte won't trigger an interrupt.

**Fix**: Ensure the last line in the callback is `restart_receive()`. This is the easiest step to miss in interrupt-driven reception — no errors, no crashes, just a "silent failure."

---

## Exercises

### Exercise 1: Add a STATUS Command (Easy)

Add a new command `STATUS` in `handle_command()` that returns the current LED state (ON or OFF).

Hint: You need a way to track the LED's current state. The simplest method is to use a `bool` variable, updating it each time `led.on()` or `led.off()` is called. Alternatively, you could read the actual logic level of PC13 — but note that PC13 is active-low (the Blue Pill's onboard LED is active-low).

Goal: Type "STATUS" in the terminal, and the chip returns "LED is ON" or "LED is OFF". Understand how to extend the existing command processing framework.

### Exercise 2: ECHO Mode Toggle (Medium)

Implement an ECHO mode: when enabled, every received byte is immediately sent back as-is. Add "ECHO ON" and "ECHO OFF" commands to toggle the mode.

Hint: In the UART receive section of the main loop, add an `bool echo_mode = false` flag. When `echo_mode` is true, immediately `send_string()` each popped byte back. Note: the echo should happen before line parsing — after a byte is popped, echo it first, then append it to the line buffer.

Goal: After typing "ECHO ON", every character you type in the terminal is echoed back (you can see what you're typing). After typing "ECHO OFF", echoing stops. Understand how to add real-time response logic within the interrupt-receive + main-loop-consume framework.

### Exercise 3: Interrupt-Driven Transmit + Transmit Ring Buffer (Challenge)

In our code, reception is interrupt-driven, but transmission is still blocking. This exercise requires you to implement interrupt-driven transmission.

Hint: You need to:

1. A transmit-direction ring buffer (`CircularBuffer<256> tx_ring`)
2. In the main loop, push data to `tx_ring` instead of directly calling `HAL_UART_Transmit` when you need to send data
3. Start interrupt-driven transmission: `HAL_UART_Transmit_IT(&huart, &byte, 1)`
4. In the `HAL_UART_TxCpltCallback()`, check if `tx_ring` still has data — if yes, keep sending; if no, stop
5. Pay attention to TXEIE (transmit interrupt enable) management — only enable it when there is pending data, and disable it when done

The challenge of this exercise lies in the fact that transmission is "started on demand" — unlike reception, which runs continuously. You need to handle edge cases like "how to stop the interrupt when the ring buffer is empty" and "how to kick off transmission for the first byte."

Goal: Understand the symmetry between interrupt-driven transmission and reception, and master the complete interrupt-driven UART architecture with dual ring buffers.

---

## UART Tutorial Recap

We've completed 13 articles. Let's review our learning path:

**Phase 1: Motivation (Part 31)**

- Derived the communication need from LED (output) and Button (input)
- What UART is and why we chose it
- Final result preview and hardware preparation

**Phase 2: Hardware Fundamentals (Parts 32-33)**

- UART protocol deep dive: start bit, data bits, parity bit, stop bits, baud rate, oversampling
- STM32 USART peripheral: three instances, key registers, GPIO alternate functions, NVIC preview

**Phase 3: HAL + Blocking I/O (Parts 34-35)**

- HAL initialization and blocking transmission
- printf redirection, the fatal problem with blocking reception

**Phase 4: Interrupt-Driven (Parts 36-38)**

- Cortex-M3 interrupt mechanism and NVIC
- Lock-free SPSC ring buffer
- UART IRQ handling and callback chain

**Phase 5: C++ Abstractions (Parts 39-42)**

- `std::expected` error handling
- UART driver template: zero-size abstraction, `if constexpr`, `static inline`
- Concepts constraints + UartManager
- Command processor and complete code walkthrough

**Phase 6: Summary (Part 43)**

- Six common pitfalls and three progressive exercises

Summary of C++ features used:

- `std::expected<T, E>` (C++23) — type-safe error handling
- `std::span` (C++20) — safe contiguous memory view
- `std::string_view` (C++17) — zero-copy string view
- `consteval` (C++20) — compile-time baud rate validation
- Concepts (C++20) — constraining callback signatures
- `static inline` members (C++17) — template singletons
- `if constexpr` (C++17) — compile-time hardware dispatch
- `enum class : uintptr_t` — base address encoding
- `volatile` — ISR visibility guarantees
- `extern "C"` — ISR and printf bridging
- `[[maybe_unused]]` (C++17) — suppressing unused parameter warnings
- designated initializer (C++20) — `UartConfig{.baud_rate = 115200}`

Every feature solved a real problem in the specific context of a UART driver. From error handling to type constraints, from compile-time dispatch to ISR bridging — modern C++ in the embedded domain isn't "just for show"; it genuinely makes code safer, more maintainable, and more efficient.

This concludes the UART tutorial. We covered everything from protocol principles to interrupt-driven design, from C-style HAL calls to C++23 templates and Concepts. Your STM32 can now not only blink LEDs and read buttons on its own, but also communicate bidirectionally with a PC — this is a qualitative leap. Moving forward, whether you build SPI sensor drivers, read EEPROMs via I2C, or put together a complete embedded web server, UART communication will remain your foundational tool for debugging and verification.
