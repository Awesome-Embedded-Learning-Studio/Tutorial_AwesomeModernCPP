---
chapter: 17
difficulty: beginner
order: 3
platform: stm32f1
reading_time_minutes: 9
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 33: STM32 USART Peripheral — The Serial Engine Inside the Chip'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/03-stm32-usart-peripheral.md
  source_hash: 588d33f8c9e2c67233632e1d507128f2fdbac32de2b276ac337125e9e61a45b5
  translated_at: '2026-06-16T06:21:43.586022+00:00'
  engine: anthropic
  token_count: 1671
---
# Part 33: STM32 USART Peripheral — The Serial Engine Inside the Chip

> Following up on the previous part: We have clarified the UART protocol frame format, baud rate, and oversampling. Now it is time to look at how the STM32F103 chip implements this protocol internally.

---

## USART vs UART: What Does That Extra "S" Stand For?

You may have noticed that the STM32F103 reference manual refers to USART (Universal Synchronous/Asynchronous Receiver/Transmitter), adding an "S" for Synchronous compared to UART. This means that this STM32 peripheral can not only perform asynchronous UART communication but also operate in synchronous mode—adding a clock line (SCLK) output to provide a synchronous clock for external devices. Additionally, the USART supports SmartCard mode, IrDA (Infrared) mode, and LIN (Local Interconnect Network) mode.

However, in our tutorial, we will only use asynchronous mode (standard UART). The other modes are useful in specific application scenarios but are not essential for understanding the core mechanisms of UART communication. Therefore, although we are using the USART peripheral, we are using it as a UART.

The STM32F103C8T6 has three USART instances: USART1, USART2, and USART3. Their main difference lies in the bus they are attached to:

- **USART1** is attached to the APB2 bus. APB2 is the high-speed bus, running at 64 MHz in our code. USART1 supports the highest baud rate (up to 4.5 Mbps at 72 MHz).
- **USART2 and USART3** are attached to the APB1 bus. APB1 is the low-speed bus, running at 32 MHz in our code. Their maximum baud rates are relatively lower.

We chose USART1 for a simple reason: it is on the high-frequency bus, offering more flexibility in baud rate; moreover, the default pins for USART1 (PA9/PA10) are easy to locate on the Blue Pill headers. This is also reflected in our code—the `UartInstance` enum directly uses the base address of USART1:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
enum class UartInstance : uintptr_t {
    Usart1 = USART1_BASE,
    Usart2 = USART2_BASE,
    Usart3 = USART3_BASE,
};
```

Here we use a clever trick: the enumeration values store the base addresses of the USART peripherals in the memory map. `USART1_BASE` is defined as `0x40013800` in the STM32 header files—this is the starting address of all USART1 registers. Later, in our C++ template driver, we will see that this base address can be directly `reinterpret_cast` into a `USART_TypeDef*` pointer to access all registers.

---

## Key USART Registers

The USART peripheral on the STM32F103 has seven registers. We won't break down every bit of each register (that's the reference manual's job), but instead focus on the flags and fields most commonly used in actual programming.

### SR — Status Register

The SR register reflects the current operating status of the USART. The most important flag bits are:

- **TXE (Transmit Data Register Empty)**: The transmit data register is empty. When the previous data has been moved from the TDR (Transmit Data Register) into the shift register for transmission, TXE is set to 1, indicating "ready for the next data." Internally, `HAL_UART_Transmit()` polls waiting for this flag.
- **TC (Transmission Complete)**: Transmission complete. When the data in the shift register has been fully sent and the TDR is also empty, TC is set to 1. This is stricter than TXE—TXE only means "ready for the next data," while TC means "all data has been sent."
- **RXNE (Read Data Register Not Empty)**: The read data register is not empty. When the shift register moves received data into the RDR (Read Data Register), RXNE is set to 1, indicating "new data is available to read." This flag plays a central role in interrupt-driven reception—when RXNE is set to 1, if RXNEIE (RXNE Interrupt Enable) is enabled, the CPU will be interrupted.
- **ORE (Overrun Error)**: Overrun error. New data arrived before the previous data was read, causing the old data to be overwritten. This indicates that your code isn't reading data fast enough.

### DR — Data Register

The DR register actually consists of two separate registers—TDR (transmit) and RDR (receive)—which share the same address. When you write to DR, the data enters the TDR and triggers transmission; when you read from DR, the data comes from the RDR. Read and write operations are automatically routed to the correct internal register at the hardware level, so your code only needs to remember "write to DR to transmit, read from DR to receive."

### BRR — Baud Rate Register

BRR stores the divider value used by the USART to generate the correct baud rate. BRR consists of two parts: a 12-bit integer part (Mantissa) and a 4-bit fractional part (Fraction). For the 16x oversampling mode:

```text
BRR = fCK / BaudRate
```

Where `fCK` is the clock frequency of the bus to which the USART is attached. Since USART1 is on APB2, `fCK` = 64 MHz (in our configuration). The integer part is the integer portion of the BRR, and the fractional part is the fractional portion of the BRR multiplied by 16. This calculation is performed internally by the HAL library's `HAL_UART_Init()`. We only need to set the `BaudRate` field in `UART_InitTypeDef`, and the HAL will automatically calculate the BRR.

### CR1/CR2/CR3 —— Control Registers

Three control registers manage the USART operating modes:

**CR1** is the most important one and contains:

- **UE (USART Enable)**: The master enable switch for the USART. If not set, the USART will not function.
- **TE (Transmitter Enable)**: Transmission enable.
- **RE (Receiver Enable)**: Reception enable.
- **RXNEIE**: RXNE interrupt enable. When set, an interrupt is triggered when RXNE = 1. This is the key switch for interrupt-driven reception.
- **TXEIE**: TXE interrupt enable. Used for interrupt-driven transmission.
- **M (Word Length)**: Data bit length. 0 = 8 bits, 1 = 9 bits.
- **PCE (Parity Control Enable)**: Parity check enable.
- **PS (Parity Selection)**: Parity type. 0 = even parity, 1 = odd parity.

**CR2** mainly manages the stop bit length (STOP bit field, 00 = 1 stop bit, 10 = 2 stop bits) and clock output configuration (for synchronous mode).

**CR3** manages hardware flow control (CTSE/RTSE), DMA enable (DMAT/DMAR), and some special modes (smart card, IrDA, LIN).

---

## Clock Enabling

The USART peripheral is disabled by default—to save power. We must enable the corresponding bus clock before using it. Since USART1 is on APB2, we call:

```c
__HAL_RCC_USART1_CLK_ENABLE();
```

USART2 and USART3 are located on the APB1 bus, so we call `__HAL_RCC_USART2_CLK_ENABLE()` and `__HAL_RCC_USART3_CLK_ENABLE()` respectively.

This follows the same pattern as enabling the GPIO clock in the LED tutorial: all peripherals on the STM32 have their clocks disabled after reset, so we must manually enable them. The HAL library's `__HAL_RCC_xxx_CLK_ENABLE()` macro essentially writes a 1 to the corresponding bit in the RCC (Reset and Clock Control) register.

In our C++ code, this clock enabling is encapsulated within the `enable_clock()` private method of the `UartDriver` template, using `if constexpr` to select the correct macro at compile time:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
static inline void enable_clock() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if constexpr (INSTANCE == UartInstance::Usart2) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else if constexpr (INSTANCE == UartInstance::Usart3) {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}
```

`if constexpr` determines which branch to take at compile time—the resulting code contains only the corresponding macro call, avoiding the overhead of runtime conditional checks.

---

## GPIO Alternate Functions: The Special Identities of PA9 and PA10

In the LED tutorial, GPIOs were configured as push-pull outputs (`GPIO_MODE_OUTPUT_PP`) or inputs (`GPIO_MODE_INPUT`). However, the USART1 TX pin PA9 needs to be configured as an **alternate function push-pull output** (`GPIO_MODE_AF_PP`), a mode we haven't seen before.

Why do we need an alternate function? Because PA9 is not a standard GPIO pin—when the USART1 transmitter is enabled, the USART peripheral directly controls the output level of PA9, rather than the GPIO ODR (Output Data Register). In other words, output control of PA9 is transferred from the GPIO module to the USART module. `GPIO_MODE_AF_PP` tells the GPIO controller: "This pin's output is managed by the peripheral (AF = Alternate Function), so stand down."

PA10, acting as the USART1 RX pin, is configured as an input mode with a pull-up resistor (`GPIO_MODE_INPUT` + `GPIO_PULLUP`). This is identical to the input configuration in the button tutorial—the pull-up resistor ensures the RX line remains high when idle, matching the UART protocol's idle state.

In our `main.cpp`, the GPIO initialization is encapsulated in a separate function:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
static void usart1_gpio_init() noexcept {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}
```

This code first enables the clock for GPIOA (since both PA9 and PA10 are on GPIOA), and then configures PA9 as multiplexed push-pull output and PA10 as input with a pull-up resistor. `gpio.Speed = GPIO_SPEED_FREQ_HIGH` sets the output toggle speed for PA9—high-speed mode ensures that signal edges are steep enough for 115200 baud.

Note that this function is declared as `noexcept`. In C++ driver design, GPIO initialization should not throw exceptions (our project disables exceptions anyway). Later, in Article 41 when we cover Concepts, you will see that the `UartGpioInitializer` Concept enforces this at compile time using `std::is_nothrow_invocable_v`.

---

## NVIC Connection Preview

USART1 has its own interrupt vector, `USART1_IRQn`. When the USART1 RXNE flag is set (a new byte has been received) and RXNEIE is enabled, if the USART1 interrupt in the NVIC is also enabled, the CPU will pause the current task and jump to the `USART1_IRQHandler` function to execute.

The NVIC configuration is in the `enable_interrupt()` method in `uart_driver.hpp`:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
void enable_interrupt() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if constexpr (INSTANCE == UartInstance::Usart2) {
        HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    } else if constexpr (INSTANCE == UartInstance::Usart3) {
        HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    }
}
```

Two steps: set the priority (preemption priority 0, subpriority 0—the highest priority), then enable the IRQ. This follows the same pattern as the NVIC configuration for the EXTI interrupt in the button tutorial.

We will break down the complete interrupt workflow—from hardware trigger to the byte entering the ring buffer—in detail in Chapters 36 through 38. For now, you just need to know that USART1 has its own interrupt channel. Once the NVIC and RXNEIE are configured, an interrupt is triggered every time a byte is received.

---

## Summary

In this chapter, we clarified the hardware architecture of the STM32 USART peripheral: the differences between the three USART instances, the functions of key registers (SR/DR/BRR/CR1/CR2/CR3), how to configure GPIO alternate function pins, and a preview of the NVIC interrupt connection. This knowledge serves as the foundation for the next chapter—understanding how the hardware works allows us to understand what each step accomplishes when writing code.

In the next chapter, we will get to work. The HAL library UART initialization process, blocking transmission, and seeing the chip say "Hello" in the terminal for the first time—these are the topics for Chapter 34.
