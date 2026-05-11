# UART Serial Communication

> From serial protocol principles to interrupt-driven reception, building an STM32 UART driver and command processor with modern C++23.

## Phase 1: Motivation

- [Part 31: From Buttons to Serial](01-motivation-and-overview.md) — Why UART is the cornerstone of embedded communication

## Phase 2: Hardware Fundamentals

- [Part 32: UART Protocol in Detail](02-uart-protocol-basics.md) — How to synchronize without a clock line
- [Part 33: STM32 USART Peripheral](03-stm32-usart-peripheral.md) — The serial engine inside the chip

## Phase 3: HAL + Blocking I/O

- [Part 34: HAL Initialization and Transmission](04-hal-uart-init-and-send.md) — Making the chip speak
- [Part 35: printf Redirection and Blocking Reception](05-printf-redirect-and-blocking-receive.md) — Making the chip speak with printf, and teaching it to listen

## Phase 4: Interrupt-Driven

- [Part 36: Interrupt Fundamentals and NVIC](06-interrupt-fundamentals-and-nvic.md) — Letting hardware proactively notify the CPU
- [Part 37: Lock-Free Ring Buffer](07-circular-buffer-lock-free-spsc.md) — A safe channel between the ISR and the main loop
- [Part 38: UART IRQ Handling and Callbacks](08-uart-irq-handler-and-callback.md) — The complete puzzle of interrupt reception

## Phase 5: C++ Abstractions

- [Part 39: std::expected Error Handling](09-cpp-expected-and-error-handling.md) — A better choice than exceptions in embedded systems
- [Part 40: UART Driver Template](10-cpp-uart-driver-template.md) — Zero-size abstractions and compile-time dispatch
- [Part 41: Concepts + UartManager](11-cpp-concepts-and-uart-manager.md) — Type-safe assembly
- [Part 42: Command Processor and Full Walkthrough](12-command-processor-and-main-walkthrough.md) — From serial input to LED control

## Phase 6: Summary

- [Part 43: Common Pitfalls and Exercises](13-pitfalls-and-exercises.md) — Mastering UART with flair
