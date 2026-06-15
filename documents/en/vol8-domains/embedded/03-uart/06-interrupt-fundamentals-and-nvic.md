---
chapter: 17
difficulty: intermediate
order: 6
platform: stm32f1
reading_time_minutes: 9
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 36: Interrupt Basics and NVIC — Letting Hardware Proactively Notify the
  CPU'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/03-uart/06-interrupt-fundamentals-and-nvic.md
  source_hash: 827336d06a96eee8d7a8570ae26c012033296a4a714f004bf117b51af745d373
  token_count: 1323
  translated_at: '2026-05-26T12:16:34.042121+00:00'
description: ''
---
# Part 36: Interrupt Basics and NVIC — Letting Hardware Notify the CPU Actively

> In the previous part, we discovered the fatal flaw of blocking receives. In this part, we start building a solution: first, we need to understand how the Cortex-M3 interrupt mechanism works.

---

## From Polling to Interrupts: A Paradigm Shift

In the final part of the button tutorial (Part 30), we briefly introduced EXTI (External Interrupt). That article covered the scenario of "pin level changes triggering an interrupt." Now we need to elevate our understanding of interrupts—because interrupt-driven UART reception is much more complex than EXTI button detection, involving data passing between the ISR and the main loop, buffer management, callback chains, and more.

Let's first review the fundamental differences between the two programming paradigms.

**Polling**: The CPU actively checks the peripheral status. "Is there data? No. Is there data? No. Is there data? Yes!" The CPU is busy-waiting—although we can fill the waiting time with other tasks (like our button state machine), the checking itself consumes CPU time.

**Interrupts**: The peripheral actively sends a signal when it needs the CPU's attention. "I have data, please process it." The CPU can focus on other tasks until the signal arrives. When the signal comes, the hardware automatically pauses the current task, jumps to a preset handler function, and returns when processing is complete.

An analogy: polling is like checking your mailbox every five minutes—you have to make the trip regardless of whether there's any mail. Interrupts are like the mail carrier ringing your doorbell—you can peacefully do other things at home when there's no mail, and the doorbell will ring when it arrives.

---

## Cortex-M3 Interrupt Hardware

The STM32F103 uses the ARM Cortex-M3 core, and its interrupt system consists of two parts: the NVIC (Nested Vectored Interrupt Controller) and the vector table.

### NVIC

The NVIC is the interrupt controller built into the Cortex-M3 core, responsible for managing the priority, enable state, and pending state of all interrupt sources. The STM32F103 has 60 maskable interrupt channels (plus 16 Cortex-M3 core exceptions), and each channel has its own independent interrupt vector.

Key features of the NVIC:

- **Nesting**: Higher-priority interrupts can preempt lower-priority interrupts. If a USART1 interrupt is being processed, a higher-priority interrupt (such as SysTick) can preempt it. After the higher-priority interrupt is handled, execution returns to continue processing the USART1 interrupt.
- **Vectoring**: Each interrupt source has its own entry function (the interrupt service routine, ISR). When an interrupt is triggered, the hardware automatically jumps to the corresponding ISR without the software needing to determine "which interrupt source triggered."
- **Automatic context save/restore**: When an interrupt is triggered, the CPU automatically pushes the current register state (r0-r3, r12, LR, PC, xPSR) onto the stack. When the ISR returns, they are automatically popped. You do not need to write manual register save/restore code.

### Vector Table

The vector table is an array of function pointers stored at the beginning of Flash (default address 0x00000000). Each interrupt source occupies a fixed position in the table—the Nth entry in the table corresponds to the ISR address of the Nth interrupt source. When interrupt number N is triggered, the CPU reads the address from the Nth entry in the table and jumps there to execute.

The interrupt number for USART1 is `USART1_IRQn` (value 37). The 37th position in the vector table stores the address of the `USART1_IRQHandler` function. This function name is not arbitrary—it must strictly correspond to its position in the vector table. The linker places it in the correct position based on the function name.

---

## How USART1 Interrupts Work

Now let's apply the general interrupt mechanism to the specific scenario of USART1.

### Trigger Condition: The RXNE Flag

In the previous part, we discussed the RXNE (Read Data Register Not Empty) flag in the SR register. When the USART1 receive shift register shifts a complete byte into the RDR, RXNE is automatically set to 1. This is the interrupt trigger condition.

However, RXNE being set to 1 does not mean the interrupt will trigger. Two additional conditions must also be met simultaneously:

1. **RXNEIE = 1**: The RXNE interrupt enable bit in the CR1 register. This bit is set by software and means "please trigger an interrupt when RXNE is set to 1."
2. **USART1 IRQ enabled in the NVIC**: The corresponding USART1_IRQn interrupt channel in the NVIC must be enabled. This is done via `HAL_NVIC_EnableIRQ(USART1_IRQn)`.

Only when all three conditions (RXNE set to 1 + RXNEIE enabled + NVIC enabled) are met simultaneously will the CPU jump to `USART1_IRQHandler`.

### What HAL_UART_Receive_IT Does

The HAL library provides a convenient function to set up interrupt-driven reception:

```c
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
```

This function does three things internally:

1. Stores the `pData` pointer and `Size` in the `huart` structure (HAL uses these internally to track reception progress)
2. Sets the RXNEIE bit (enabling the receive interrupt)
3. Returns `HAL_OK`

Note: this function does not block. It simply "sets up the reception conditions" and returns immediately. The actual reception happens after the interrupt is triggered—when a new byte arrives, the ISR is automatically called, the HAL code inside the ISR stores the byte into the buffer pointed to by `pData`, decrements the remaining count, and calls the `HAL_UART_RxCpltCallback()` callback once `Size` bytes have been received.

### Single-Byte Reception Strategy

Our code uses a key strategy: receiving only one byte at a time.

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/uart_irq.cpp
std::byte rx_byte{};

void restart_receive() {
    [[maybe_unused]] auto r =
        Manager::driver().receive_it(std::span<std::byte, 1>{&rx_byte, 1});
}
```

`HAL_UART_Receive_IT(&huart, &rx_byte, 1)` means: "Please set up an interrupt to receive 1 byte. Notify me when 1 byte has been received."

After receiving one byte, HAL calls `HAL_UART_RxCpltCallback()`. In the callback, we store this single byte into a ring buffer, then immediately call `restart_receive()` to set up another single-byte reception. This cycle repeats, achieving a continuous, byte-loss-free reception stream:

```text
restart_receive()
  → 等待字节...
  → 字节到达，ISR 触发
  → HAL_UART_IRQHandler()
  → HAL_UART_RxCpltCallback()
    → push(rx_byte) 到环形缓冲区
    → restart_receive()
      → 等待下一个字节...
      → （循环）
```

Why not "receive N bytes at once"? Because UART is a byte-stream protocol—you don't know when the sender will finish or how many bytes it will send. If you set "receive 10 bytes at once," and the sender stops after 3 bytes, your reception gets stuck. The single-byte strategy is the most flexible—process each byte as it arrives, avoiding any "waiting to fill up" issues.

---

## extern "C" ISR Bridging

Our project is a C++ project, but ISR function names (like `USART1_IRQHandler`) must be defined with C linkage. The reason is that the vector table stores C symbol names—the linker populates the vector table based on the undecorated function name. If the C++ compiler applies name mangling to `USART1_IRQHandler`, the linker won't be able to find the correct function.

Therefore, the ISR definition must be placed inside an `extern "C"` block:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/uart_irq.cpp
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
```

`extern "C"` ensures that these two functions appear under their original names in the symbol table, allowing the linker to correctly place them into the vector table. The code inside the functions is still C++—you can call C++ functions, use C++ types, and access members in C++ namespaces. `extern "C"` only affects linking rules, not compilation rules.

This "C linkage + C++ implementation" pattern is very common in embedded C++ projects. Any function that needs to be called from a C interface (ISRs, callbacks, system calls like `_write()`) requires an `extern "C"` wrapper.

---

## NVIC Priority Configuration

In our code, the NVIC configuration is encapsulated in the `enable_interrupt()` method:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
void enable_interrupt() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    // ...
}
```

The two parameters of `HAL_NVIC_SetPriority(USART1_IRQn, 0, 0)` are the preempt priority and the subpriority. Setting them to (0, 0) means the highest priority—the USART1 interrupt can preempt any other interrupt (except non-maskable exceptions like NMI).

In simple projects (with only USART interrupts and SysTick), setting the priority to the highest is fine. In complex projects, if multiple interrupt sources compete for CPU time, you need to carefully plan priorities. The general principle is: the interrupt with the highest real-time requirements gets the highest priority. UART reception (where delayed processing can cause data loss) usually has a higher priority than LED control (where a few milliseconds of delay is imperceptible to the human eye).

---

## The Golden Rule of Interrupt Handling

Before diving into the specific ISR implementation, remember one golden rule of embedded development:

> **ISRs must be as short as possible.**

While an ISR is executing, interrupts of the same or lower priority are masked. If your ISR takes too long to execute (for example, doing complex calculations inside the ISR, calling `printf()`, or waiting for a timeout), other interrupts may experience delayed responses or even be lost. For USART reception, if the next byte arrives while the ISR is still processing the previous byte, and RXNE hasn't been cleared yet, an ORE (Overrun Error) will be triggered—the previous byte is lost.

Our ISR implementation follows the "short ISR" principle: `USART1_IRQHandler` delegates to HAL, HAL clears the interrupt flag, reads the data, and calls the callback. Inside the callback, we only do two things—push the byte into the ring buffer (an O(1) operation), and then restart the next round of reception. The entire process completes within a few microseconds, far less than the transmission time of one byte at 115200 baud (87 microseconds).

---

## Summary

In this part, we built the theoretical foundation for interrupt-driven reception: the Cortex-M3's NVIC and vector table mechanism, the trigger conditions for the USART1 RXNE interrupt, how `HAL_UART_Receive_IT()` works, the single-byte reception strategy, the `extern "C"` bridging pattern, and the principle that ISRs must be as short as possible.

But one critical piece of the puzzle remains unsolved: how do we pass the bytes received by the ISR to the main loop? Using a global variable directly? Using an array? In the next part, we will design a data structure specifically optimized for ISR-to-main communication—a lock-free ring buffer.
