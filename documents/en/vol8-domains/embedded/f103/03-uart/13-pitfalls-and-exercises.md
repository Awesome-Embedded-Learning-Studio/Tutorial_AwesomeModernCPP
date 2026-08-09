---
chapter: 17
difficulty: intermediate
order: 13
platform: stm32f1
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 43: Common Pitfalls and Practical Exercises — Mastering UART'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/13-pitfalls-and-exercises.md
  source_hash: d046e516f26db61b29a1a6a90a1f2b688751825e935ae0d26243a351adfa91d2
  translated_at: '2026-06-16T04:12:33.638147+00:00'
  engine: anthropic
  token_count: 1135
---
# Part 43: Common Pitfalls and Practical Exercises — Mastering UART

> The final article in the UART tutorial. Pitfall avoidance + three exercises to help you truly internalize the knowledge you've learned.

---

## Common Pitfalls

### Pitfall 1: TX/RX Crossed Wiring

This is the number one issue in UART debugging, hands down.

**Symptoms**: The terminal receives nothing, or it doesn't receive the data being sent.

**Cause**: Connecting the adapter's TX to the Blue Pill's TX (PA9), and the adapter's RX to the Blue Pill's RX (PA10). TX connected to TX means both sides are transmitting and no one is listening—of course nothing is received.

**Solution**: Remember "crossover connection"—adapter TX to Blue Pill RX (PA10), adapter RX to Blue Pill TX (PA9). If you aren't sure which wire is TX or RX, swap them and try—it won't fry anything, it just won't work.

### Pitfall 2: Baud Rate Mismatch

**Symptoms**: The terminal displays garbage characters—looks like random characters.

**Cause**: The baud rate set in the code doesn't match the baud rate in the terminal software. For example, the code uses 115200, but the terminal is set to 9600. UART is an asynchronous protocol; both parties must operate at exactly the same rate, otherwise the sampling points are misaligned and the read data is completely wrong.

**Solution**: Ensure the `huart->Init.BaudRate` in the code matches the terminal software's baud rate setting exactly. It's not just the baud rate—data bits, parity bits, and stop bits must also match (standard configuration is 8N1).

### Pitfall 3: Ring Buffer Overflow

**Symptoms**: The second half of a long string is lost during transmission, or command parsing occasionally fails.

**Cause**: The ISR pushes bytes faster than the main loop pops them. Once the 128-byte buffer is full, `ring_buf.push()` returns `false`, and bytes are discarded. This happens when the PC sends a large amount of data quickly (e.g., pasting a long text), while the main loop is busy handling other things (like button debounce or sending a response).

**Solution**: Increasing the buffer size is the most direct method—change `constexpr size_t BUF_SIZE = 128;` to `256` or `512`. Additionally, ensure there are no long blocking operations in the main loop—each loop iteration should process all pending data as quickly as possible.

### Pitfall 4: Forgetting `volatile` on Ring Buffer

**Symptoms**: Seems to work normally, but occasionally loses data. Becomes more frequent when increasing the optimization level (`-O2` or `-O3`).

**Cause**: The `head` and `tail` indices of `RingBuffer` were not declared as `volatile`. When the compiler optimizes, it caches the `tail` read in the main loop into a register; subsequent loops no longer re-read from memory—so the push operation in the ISR is invisible to the main loop.

**Solution**: Ensure `head` and `tail` are declared as `volatile`. Our code already correctly uses `std::atomic<uint8_t>`—but if you write your own ring buffer, don't forget this.

### Pitfall 5: `printf` Floating Point vs `nano.specs`

**Symptoms**: `printf` outputs garbage or outputs nothing at all.

**Cause**: Our CMakeLists.txt uses the `-specs=nano.specs` linker option, which links the stripped-down C library (nano newlib). The stripped-down version does not support floating-point printf formatting—format specifiers like `%f` and `%.2f` do not work.

**Solution**: Use integers to simulate floating-point output: `printf("%d.%d", int_part, frac_part);`. Alternatively, if Flash space is sufficient, remove `-specs=nano.specs` to link the full C library (Flash usage will increase by about 10-20 KB).

### Pitfall 6: Forgetting to Restart Reception in Callback

**Symptoms**: The first byte is received, but no data is received afterwards.

**Cause**: Forgetting to call `HAL_UART_Receive_IT()` in `HAL_UART_RxCpltCallback()`. After the HAL completes a single-byte reception, it does not automatically start the next round—you must manually call `HAL_UART_Receive_IT()` to re-enable reception. If forgotten, RXNEI is not re-enabled, and the next byte arrival won't trigger an interrupt.

**Solution**: Ensure the last line in the callback is `HAL_UART_Receive_IT(...)`. This is the easiest step to miss in interrupt reception—no error, no crash, just "silent failure".

---

## Exercises

### Exercise 1: Add STATUS Command (Simple)

Add a new command `STATUS` in `CmdParser` that returns the current LED state (ON or OFF).

Hint: You need a way to track the current LED state. The simplest method is to use a `bool` variable, updating it whenever `LedOn()` or `LedOff()` is called. Alternatively, you can read the actual level of PC13—but note that PC13 is low-active (the Blue Pill's onboard LED is active-low).

Goal: Enter "STATUS" in the terminal, and the chip returns "LED is ON" or "LED is OFF". Understand how to extend the existing command handling framework.

### Exercise 2: ECHO Mode Toggle (Medium)

Implement an ECHO mode: when enabled, every received byte is immediately sent back as-is. Add "ECHO ON" and "ECHO OFF" commands to toggle the mode.

Hint: In the UART reception section of the main loop, add a `bool echo_mode` flag. When `echo_mode` is true, immediately `HAL_UART_Transmit()` the byte back after popping it. Note: echo should happen before line parsing—echo the byte first after popping, then concatenate it into the line buffer.

Goal: After entering "ECHO ON", every character you type in the terminal will be echoed (you can see what you typed). After entering "ECHO OFF", echoing stops. Understand how to add real-time response logic within the interrupt reception + main loop consumption framework.

### Exercise 3: Interrupt Transmission + Transmit Ring Buffer (Challenge)

In our code, reception is interrupt-driven, but transmission is still blocking. This exercise requires you to implement interrupt-driven transmission.

Hint: You need:

1. A ring buffer for the transmission direction (`tx_buf`)
2. In the main loop, when data needs to be sent, push to `tx_buf` instead of directly calling `HAL_UART_Transmit()`
3. Start interrupt transmission: `HAL_UART_Transmit_IT()`
4. In `HAL_UART_TxCpltCallback()`, check if `tx_buf` still has data—if yes, continue sending; if no, stop
5. Pay attention to the management of TXEIE (Transmit Interrupt Enable)—only enable when there is data to send, disable when done

The challenge in this exercise lies in: transmission is "started on demand"—unlike reception, which runs continuously. You need to handle edge cases like "how to stop the interrupt when the ring buffer is empty" and "how to start transmission for the first byte".

Goal: Understand the symmetry between interrupt transmission and interrupt reception, and master the complete interrupt-driven UART architecture with dual ring buffers.

---

## UART Tutorial Review

We've covered 13 articles. Let's review our learning path:

**Phase 1: Motivation (Part 31)**

- Derived communication requirements from LED (output) and Button (input)
- What is UART and why choose it
- Final effect preview and hardware preparation

**Phase 2: Hardware Fundamentals (Parts 32-33)**

- UART protocol deep dive: start bit, data bit, parity bit, stop bit, baud rate, oversampling
- STM32 USART peripheral: three instances, key registers, GPIO alternate functions, NVIC preview

**Phase 3: HAL + Blocking I/O (Parts 34-35)**

- HAL initialization and blocking transmission
- `printf` redirection, the fatal problem with blocking reception

**Phase 4: Interrupt Driven (Parts 36-38)**

- Cortex-M3 interrupt mechanism and NVIC
- Lock-free SPSC ring buffer
- UART IRQ handling and callback chain

**Phase 5: C++ Abstraction (Parts 39-42)**

- `std::expected` error handling
- UART driver template: zero-size abstraction, `std::span`, `std::string_view`
- Concepts constraints + UartManager
- Command processor and full code walkthrough

**Phase 6: Summary (Part 43)**

- 6 common pitfalls and 3 progressive exercises

Summary of C++ features used:

- `std::expected` (C++23) — Type-safe error handling
- `std::span` (C++20) — Safe view of contiguous memory
- `std::string_view` (C++17) — Zero-copy string view
- `consteval` (C++20) — Compile-time baud rate validation
- Concepts (C++20) — Constrain callback signatures
- `inline` static members (C++17) — Template singleton
- `if constexpr` (C++17) — Compile-time hardware dispatch
- `std::array` — Base address encoding
- `volatile` / `atomic` — ISR visibility guarantees
- `__attribute__((used))` — ISR and printf bridging
- `[[maybe_unused]]` (C++17) — Suppress unused parameter warnings
- Designated initializers (C++20) — `GPIO_InitTypeDef`

Every feature solved a practical problem in the specific context of the UART driver. From error handling to type constraints, from compile-time dispatch to ISR bridging—modern C++ in the embedded field is not "flashy moves without substance"; it genuinely makes code safer, more maintainable, and more efficient.

At this point, the UART tutorial is finished. We've covered everything from protocol principles to interrupt-driven implementation, and from C-style HAL calls to C++23 templates and Concepts. Your STM32 can now not only light up LEDs and read buttons on its own, but also communicate bidirectionally with a PC—this is a qualitative leap. Next, whether you go on to drive sensors with SPI, read EEPROMs with I2C, or build a complete embedded web server, UART communication will be your foundational tool for debugging and verification.
