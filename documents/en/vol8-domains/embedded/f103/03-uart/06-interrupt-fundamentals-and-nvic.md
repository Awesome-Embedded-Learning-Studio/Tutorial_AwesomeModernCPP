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
title: 'Part 36: Interrupt Basics and NVIC — Let Hardware Notify the CPU Actively'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/06-interrupt-fundamentals-and-nvic.md
  source_hash: 0d2335906460335741e58e4646a3cd45c5b9d8205759ab760e7acafb9cc9331e
  translated_at: '2026-06-16T04:11:52.407076+00:00'
  engine: anthropic
  token_count: 1327
---
# Part 36: Interrupt Basics and NVIC — Let Hardware Notify the CPU Actively

> In the previous post, we discovered the fatal flaw of blocking reception. In this post, we start building a solution: first, let's clarify how the interrupt mechanism in Cortex-M3 works.

---

## From Polling to Interrupts: A Paradigm Shift

In the final part of the button tutorial (Part 30), we briefly introduced EXTI (External Interrupts). That article covered the scenario where "pin level changes trigger an interrupt." Now, we need to level up our understanding of interrupts—because UART interrupt-driven reception is much more complex than EXTI button detection. It involves data passing between the ISR and the main loop, buffer management, callback chains, and more.

First, let's review the essential difference between the two programming paradigms.

**Polling**: The CPU actively checks the peripheral status. "Is there data? No. Is there data? No. Is there data? Yes!" The CPU is busy waiting—although we can fill the waiting time by doing other tasks (like our button state machine), the check itself consumes CPU time.

**Interrupt**: The peripheral actively sends a signal when it needs CPU attention. "I have data, please handle it." The CPU can focus on other tasks before the signal arrives. When the signal arrives, the hardware automatically pauses the current task, jumps to a preset handler, and returns after processing.

Here is an analogy. Polling is like checking your mailbox every 5 minutes—whether there is mail or not, you have to make the trip. Interrupts are like the mailman ringing the doorbell—when there is no mail, you can focus on doing other things at home; when the mail arrives, the bell rings.

---

## Cortex-M3 Interrupt Hardware

The STM32F103 uses an ARM Cortex-M3 core. Its interrupt system consists of two parts: the NVIC (Nested Vectored Interrupt Controller) and the vector table.

### NVIC

The NVIC is the interrupt controller built into the Cortex-M3 core. It manages the priority, enabling, and pending status of all interrupt sources. The STM32F103 has 60 maskable interrupt channels (plus 16 Cortex-M3 core exceptions), and each channel has its own interrupt vector.

Key features of the NVIC:

- **Nesting**: High-priority interrupts can preempt low-priority interrupts. If a USART1 interrupt is being processed, a higher-priority interrupt (such as SysTick) can preempt it. After the high-priority interrupt is handled, execution returns to continue processing the USART1 interrupt.
- **Vectored**: Each interrupt source has its own entry function (Interrupt Service Routine, ISR). When an interrupt triggers, the hardware automatically jumps to the corresponding ISR; no software judgment is needed to determine "which interrupt source triggered."
- **Automatic Context Save/Restore**: When an interrupt triggers, the CPU automatically pushes the current register state (r0-r3, r12, LR, PC, xPSR) onto the stack. When the ISR returns, it automatically pops them. You do not need to write code to save/restore registers manually.

### Vector Table

The vector table is an array of function pointers stored at the beginning of Flash (default address 0x00000000). Each interrupt source occupies a fixed position in the table—the Nth entry in the table corresponds to the ISR address of the Nth interrupt source. When the Nth interrupt triggers, the CPU reads the address of the Nth entry from the table and jumps there to execute.

USART1's interrupt number is `USART1_IRQn` (value 37). The 37th position in the vector table stores the address of the `USART1_IRQHandler` function. This function name is not arbitrary—it must strictly correspond to the position in the vector table. The linker places it in the correct location based on the function name.

---

## How USART1 Interrupts Work

Now, let's apply the general interrupt mechanism to the specific scenario of USART1.

### Trigger Condition: The RXNE Flag

The previous post mentioned the RXNE (Read Data Register Not Empty) flag in the SR register. When the USART1 receive shift register moves a complete byte into the RDR, RXNE is automatically set to 1. This is the trigger condition for the interrupt.

However, RXNE being set to 1 does not mean the interrupt will trigger. Two other conditions must also be met simultaneously:

1. **RXNEIE = 1**: The RXNE interrupt enable bit in the CR1 register. This bit is set by software, indicating "please trigger an interrupt when RXNE is set to 1."
2. **NVIC USART1 IRQ Enabled**: The corresponding USART1_IRQn interrupt channel in the NVIC must be enabled. This is done via `HAL_NVIC_SetPriority`.

Only when all three conditions (RXNE set + RXNEIE enabled + NVIC enabled) are met simultaneously will the CPU jump to `USART1_IRQHandler`.

### What `HAL_UART_Receive_IT` Does

The HAL library provides a convenient function to set up interrupt reception:

```cpp
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size);
```

This function does three things internally:

1. Stores the `pData` pointer and `Size` in the `huart` structure (HAL uses these internally to track reception progress).
2. Sets the RXNEIE bit (enables the receive interrupt).
3. Returns `HAL_OK`.

Note: This function does not block. It simply "sets up the reception conditions" and returns immediately. The actual reception happens after the interrupt triggers—when a new byte arrives, the ISR is called automatically. The HAL code inside the ISR stores the byte into the buffer pointed to by `pData`, decrements the remaining count, and calls the `RxHALCallback` callback after receiving `Size` bytes.

### Single-Byte Reception Strategy

Our code uses a key strategy: receiving only 1 byte at a time.

```cpp
HAL_UART_Receive_IT(&huart1, &single_byte_buf, 1);
```

`HAL_UART_Receive_IT(..., ..., 1)` means: "Please set up an interrupt to receive 1 byte. Notify me when 1 byte is received."

After 1 byte is received, HAL calls `HAL_UART_RxCpltCallback`. In the callback, we store that 1 byte into a ring buffer, and then immediately call `HAL_UART_Receive_IT` again to set up another single-byte reception. This cycle repeats, achieving a continuous, lossless reception stream:

```cpp
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        rx_queue.push(single_byte_buf); // Push byte to ring buffer
        HAL_UART_Receive_IT(&huart1, &single_byte_buf, 1); // Restart next reception
    }
}
```

Why not "receive N bytes at once"? Because UART is a byte-stream protocol—you don't know when the sender will finish or how many bytes it will send. If you set "receive 10 bytes at once," and the sender stops after 3 bytes, your reception gets stuck. The single-byte strategy is the most flexible—process one byte as soon as it arrives, avoiding any "wait until full" issues.

---

## extern "C" ISR Bridging

Our project is a C++ project, but ISR function names (like `USART1_IRQHandler`) must be defined with C linkage. The reason is that the vector table stores C symbol names—the linker populates the vector table based on unmangled function names. If the C++ compiler performs name mangling on `USART1_IRQHandler`, the linker won't find the correct function.

Therefore, the ISR definition must be placed in an `extern "C"` block:

```cpp
extern "C" {
    void USART1_IRQHandler() {
        // C++ code here
    }
}
```

`extern "C"` ensures that these two functions appear in the symbol table with their original names, allowing the linker to correctly place them in the vector table. The code inside the functions is still C++—you can call C++ functions, use C++ types, and access members in C++ namespaces. `extern "C"` only affects linking rules, not compilation rules.

This "C linkage + C++ implementation" pattern is very common in embedded C++ projects. Any function that needs to be called by a C interface (ISRs, callbacks, system calls like `SysTick_Handler`) needs `extern "C"` wrapping.

---

## NVIC Priority Configuration

In our code, NVIC configuration is encapsulated in the `Configure_NVIC` method:

```cpp
void Configure_NVIC() {
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}
```

The two parameters of `SetPriority` are preemption priority and sub-priority. Setting them to (0, 0) means the highest priority—USART1 interrupts can preempt almost any other interrupt (except non-maskable exceptions like NMI).

In simple projects (with only USART interrupts and SysTick), setting the priority to the highest is fine. In complex projects, if multiple interrupt sources compete for CPU time, you need to plan priorities carefully. A general rule is: the interrupt with the highest real-time requirements gets the highest priority. UART reception (data loss may occur if not handled in time) usually has a higher priority than LED control (the human eye can't perceive a delay of a few milliseconds).

---

## The Golden Rule of Interrupt Handling

Before diving into the specific ISR implementation, let's remember a golden rule of embedded development:

> **ISRs must be as short as possible.**

During ISR execution, interrupts of the same and lower priorities are masked. If your ISR takes too long to execute (e.g., doing complex calculations in the ISR, calling `HAL_Delay`, waiting for timeouts), other interrupts may be delayed or even lost. For USART reception, if the next byte arrives while the ISR is still processing the previous byte, and RXNE hasn't been cleared, an ORE (Overrun Error) will be triggered—the previous byte is lost.

Our ISR implementation follows the "short ISR" principle: `USART1_IRQHandler` delegates to HAL, HAL clears the interrupt flag, reads the data, and calls the callback. The callback does only two things—push the byte to the ring buffer (an O(1) operation) and restart the next round of reception. The entire process completes within a few microseconds, far less than the transmission time of one byte at 115200 baud (87 microseconds).

---

## Summary

This post built the theoretical foundation for interrupt-driven reception: the NVIC and vector table mechanisms of Cortex-M3, the trigger conditions for USART1 RXNE interrupts, how `HAL_UART_Receive_IT` works, the single-byte reception strategy, the `extern "C"` bridging pattern, and the principle that ISRs must be as short as possible.

But there is still a key piece of the puzzle missing: how do we pass the bytes received by the ISR to the main loop? Directly using global variables? Using an array? In the next post, we will design a data structure specifically optimized for ISR-to-main communication—a lock-free ring buffer.
