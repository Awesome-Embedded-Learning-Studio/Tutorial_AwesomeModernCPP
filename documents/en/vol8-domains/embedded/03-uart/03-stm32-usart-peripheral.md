---
title: 'Part 33: STM32 USART Peripheral — The Serial Engine Inside the Chip'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 17
order: 3
translation:
  source: documents/vol8-domains/embedded/03-uart/03-stm32-usart-peripheral.md
  source_hash: 04a873e68c1606743dd942721a7cea8eb0979698946fbce9259c1f1d13847968
  translated_at: '2026-05-26T12:15:16.059561+00:00'
  engine: anthropic
  token_count: 1667
---
# Part 33: STM32 USART Peripheral — The Serial Engine Inside the Chip

> Following up on the previous article: we clarified the UART protocol's frame format, baud rate, and oversampling. Now it's time to look at how the STM32F103 chip implements this protocol internally.

---

## USART vs UART: What Does the Extra S Stand For

You might have noticed that the STM32F103 reference manual uses USART (Universal Synchronous/Asynchronous Receiver/Transmitter), which has an extra "S" compared to UART — Synchronous. This means the STM32 peripheral can not only handle asynchronous UART communication, but also operate in synchronous mode — outputting an extra clock line (SCLK) to provide a synchronous clock for external devices. Additionally, USART supports SmartCard mode, IrDA (Infrared) mode, and LIN (Local Interconnect Network) mode.

However, in our tutorial, we only use asynchronous mode (i.e., standard UART). The other modes are useful in specific application scenarios, but they aren't necessary for understanding the core mechanisms of UART communication. So although we are using the USART peripheral, we treat it as a UART.

The STM32F103C8T6 has three USART instances: USART1, USART2, and USART3. Their main difference lies in the bus they are connected to:

- **USART1** is on the APB2 (Advanced Peripheral Bus) bus. APB2 is the high-speed bus, running at 64 MHz in our code. USART1 supports the highest maximum baud rate (up to 4.5 Mbps at 72 MHz).
- **USART2 and USART3** are on the APB1 bus. APB1 is the low-speed bus, running at 32 MHz in our code. Their maximum baud rates are relatively lower.

Our reason for choosing USART1 is simple: it's on the high-frequency bus, offering more baud rate flexibility; moreover, the default pins for USART1 (PA9/PA10) are easy to find on the Blue Pill header pins. This is also reflected in our code — the `UartInstance` enum directly uses the base address of USART1:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
enum class UartInstance : uintptr_t {
    Usart1 = USART1_BASE,
    Usart2 = USART2_BASE,
    Usart3 = USART3_BASE,
};
```

Here we use a clever approach: the enum value directly stores the base address of the USART peripheral in the memory map. `USART1_BASE` is defined in the STM32 header file as `0x40013800` — this is the starting address of all USART1 registers. Later, in our C++ template driver, we will see that this base address can be directly `reinterpret_cast` into a `USART_TypeDef*` pointer to access all registers.

---

## Key USART Registers

The STM32F103 USART peripheral has seven registers. We won't break down every bit of each register (that's the reference manual's job); instead, we'll focus on the flags and fields most commonly used in actual programming.

### SR — Status Register

The SR register reflects the current operating state of the USART. The most important flag bits are:

- **TXE (Transmit Data Register Empty)**: The transmit data register is empty. When the previous data is moved from the TDR (Transmit Data Register) into the shift register for transmission, TXE is set to 1, indicating "the next data can be written." Internally, `HAL_UART_Transmit()` polls and waits for this flag.
- **TC (Transmission Complete)**: Transmission is complete. When all data in the shift register has been sent and the TDR is also empty, TC is set to 1. This is stricter than TXE — TXE only means "the next data can be written," while TC means "all data has been sent."
- **RXNE (Read Data Register Not Empty)**: The receive data register is not empty. When the shift register moves the received data into the RDR (Receive Data Register), RXNE is set to 1, indicating "there is new data to read." This flag plays a central role in interrupt-driven reception — when RXNE is set to 1 and RXNEIE (RXNE interrupt enable) is turned on, the CPU will be interrupted.
- **ORE (Overrun Error)**: Overrun error. A new data byte arrived before the previous one was read, causing the old data to be overwritten. This indicates that your code is not reading data fast enough.

### DR — Data Register

The DR register actually consists of two independent registers — TDR (transmit) and RDR (receive) — which share the same address. When you write data to DR, the data enters the TDR and triggers transmission; when you read data from DR, the data comes from the RDR. Read and write operations are automatically routed to the correct internal register at the hardware level. Your code only needs to remember one rule: "write to DR to transmit, read from DR to receive."

### BRR — Baud Rate Register

BRR stores the division value that the USART uses to generate the correct baud rate. BRR consists of two parts: a 12-bit integer part (Mantissa) and a 4-bit fractional part (Fraction). For 16x oversampling mode:

```text
BRR = fCK / BaudRate
```

Where `fCK` is the clock frequency of the bus to which the USART is attached. USART1 is on APB2, so `fCK` = 64 MHz (in our configuration). The integer part corresponds to the integer bits of BRR, and the fractional part corresponds to the fractional bits of BRR multiplied by 16. This calculation is handled internally by the HAL library's `HAL_UART_Init()`. You only need to set the `BaudRate` field in `UART_InitTypeDef`, and HAL will automatically calculate BRR.

### CR1/CR2/CR3 — Control Registers

Three control registers manage the USART's operating modes:

**CR1** is the most important one, containing:

- **UE (USART Enable)**: The master enable switch for the USART. If not set, the USART does not operate.
- **TE (Transmitter Enable)**: Transmit enable.
- **RE (Receiver Enable)**: Receive enable.
- **RXNEIE**: RXNE interrupt enable. When set, an interrupt is triggered when RXNE = 1. This is the key switch for interrupt-driven reception.
- **TXEIE**: TXE interrupt enable. Used for interrupt-driven transmission.
- **M (Word Length)**: Data bit length. 0 = 8 bits, 1 = 9 bits.
- **PCE (Parity Control Enable)**: Parity enable.
- **PS (Parity Selection)**: Parity type. 0 = even parity, 1 = odd parity.

**CR2** mainly manages the stop bit length (STOP bit field, 00 = 1 stop bit, 10 = 2 stop bits) and clock output configuration (used in synchronous mode).

**CR3** manages hardware flow control (CTSE/RTSE), DMA enable (DMAT/DMAR), and some special modes (SmartCard, IrDA, LIN).

---

## Clock Enable

The USART peripheral is not enabled by default — to save power. Before using it, we must turn on the corresponding bus clock. Since USART1 is on APB2, we call:

```c
__HAL_RCC_USART1_CLK_ENABLE();
```

USART2 and USART3 are on APB1, so we call `__HAL_RCC_USART2_CLK_ENABLE()` and `__HAL_RCC_USART3_CLK_ENABLE()` respectively.

This follows the same pattern as enabling the GPIO clock in the LED tutorial: all STM32 peripherals have their clocks turned off after reset, and you need to turn them on manually. The HAL library's `__HAL_RCC_xxx_CLK_ENABLE()` macro essentially writes a 1 to the corresponding bit in the RCC (Reset and Clock Control) register.

In our C++ code, this clock enable is encapsulated in the `enable_clock()` private method of the `UartDriver` template, using `if constexpr` to select the correct macro at compile time:

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

`if constexpr` determines which branch to take at compile time — the最终 compiled code only contains the corresponding macro call, with no runtime conditional branching overhead.

---

## GPIO Alternate Function: The Special Identity of PA9 and PA10

In the LED tutorial, GPIO was configured as push-pull output (`GPIO_MODE_OUTPUT_PP`) or input (`GPIO_MODE_INPUT`). However, the USART1 TX pin PA9 needs to be configured as **alternate function push-pull output** (`GPIO_MODE_AF_PP`), a mode we haven't seen before.

Why do we need an alternate function? Because PA9 is not an ordinary GPIO pin — when the USART1 transmitter is enabled, the USART peripheral directly controls the level output of PA9, rather than the GPIO's ODR (Output Data Register). In other words, the output control of PA9 is transferred from the GPIO module to the USART module. `GPIO_MODE_AF_PP` tells the GPIO controller: "The output of this pin is managed by a peripheral (AF = Alternate Function), so don't interfere."

As the USART1 RX pin, PA10 is configured as input mode with a pull-up resistor (`GPIO_MODE_INPUT` + `GPIO_PULLUP`). This is the same as the input configuration in the button tutorial — the pull-up resistor ensures the RX line stays high during idle time, which is consistent with the UART protocol's idle state.

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

This code first enables the GPIOA clock (both PA9 and PA10 are on GPIOA), then configures PA9 as alternate function push-pull output and PA10 as pull-up input. `gpio.Speed = GPIO_SPEED_FREQ_HIGH` sets the output slew rate of PA9 — high-speed mode ensures that signal edges are steep enough at 115200 baud.

Note that this function is declared as `noexcept`. In C++ driver design, GPIO initialization should not throw exceptions (our project disables exceptions entirely). Later, in Part 41 when we cover Concepts, you'll see that the `UartGpioInitializer` Concept uses `std::is_nothrow_invocable_v` to enforce this at compile time.

---

## NVIC Connection Preview

USART1 has its own interrupt vector, `USART1_IRQn`. When the USART1 RXNE flag is set (a new byte has been received) and RXNEIE is enabled, and if the USART1 interrupt in the NVIC (Nested Vectored Interrupt Controller) is also enabled, the CPU will pause the current task and jump to the `USART1_IRQHandler` function for execution.

The NVIC configuration is in the `enable_interrupt()` method of `uart_driver.hpp`:

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

Two steps: set the priority (preemption priority 0, sub-priority 0 — the highest priority), then enable the IRQ. This follows the same pattern as the NVIC configuration for the EXTI (External Interrupt) interrupt in the button tutorial.

The complete interrupt workflow — from hardware trigger to the byte entering the ring buffer — will be dissected in detail in Parts 36 through 38. For now, you just need to know that USART1 has its own interrupt channel, and once the NVIC and RXNEIE are configured, receiving a single byte will trigger an interrupt.

---

## Summary

In this article, we clarified the hardware architecture of the STM32 USART peripheral: the differences between the three USART instances, the roles of the key registers (SR/DR/BRR/CR1/CR2/CR3), how to configure GPIO alternate function pins, and a preview of the NVIC interrupt connection. This knowledge forms the foundation for writing code in the next article — knowing how the hardware works means you'll understand exactly what each step in your code is doing.

In the next article, we'll officially get to work. The HAL library's UART initialization flow, blocking transmission, and seeing the chip say "Hello" in the terminal for the first time — these are what we'll cover in Part 34.
