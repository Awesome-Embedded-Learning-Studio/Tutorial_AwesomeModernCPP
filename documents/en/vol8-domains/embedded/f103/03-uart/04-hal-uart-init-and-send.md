---
chapter: 17
difficulty: beginner
order: 4
platform: stm32f1
reading_time_minutes: 8
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 34: HAL UART Initialization and Transmission — Making the Chip Speak'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/04-hal-uart-init-and-send.md
  source_hash: a19e27a0f1f0e91835c3d24cce1ea518b356e7150f67a8ad5238f189841d1b4f
  translated_at: '2026-06-16T04:11:51.097378+00:00'
  engine: anthropic
  token_count: 1371
---
# Part 34: HAL UART Initialization and Transmission — Making the Chip Speak

> We've covered the hardware principles for three parts; now we can finally write some code. The goal for this part is simple: make the chip send its first words to your computer via UART.

---

## Our Goal

Before writing code, let's clarify what we aim to achieve. The final result is this: after flashing the code, open a terminal emulator on your PC (baud rate 115200, 8N1), and you will see "Hello UART!" appear. It's just that simple. But this implies that the entire UART transmission chain—GPIO configuration, USART clock enabling, HAL initialization, and blocking transmission—is fully connected.

This part only covers transmission, not reception. The reason is simple: transmission is much easier than reception. Transmission is an active action—the chip decides when to send and what to send. Reception is a passive action—you don't know when external data will arrive or how much will come. Let's get transmission working first to build confidence, and then we'll handle reception in the next part.

---

## Five Steps of the Initialization Sequence

To get USART1 working, we need to complete the following five steps in order. Each step has a clear reason, so let's go through them one by one.

### Step 1: Enable GPIOA Clock

Both PA9 and PA10 are on GPIOA. Just like in the LED/button tutorial, the GPIO port clock is off by default and must be turned on first.

```cpp
__HAL_RCC_GPIOA_CLK_ENABLE();
```

### Step 2: Configure PA9 (TX) as Alternate Function Push-Pull Output

The previous part explained why the TX pin needs AF_PP mode—the USART peripheral directly controls the level of this pin, and the GPIO controller takes a back seat.

```cpp
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.Pin = GPIO_PIN_9;
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

`GPIO_SPEED_FREQ_HIGH` sets the output toggle rate of the pin. At 115200 baud, each bit lasts about 8.68 microseconds, and the signal edges need to be sharp enough to be stable within the sampling window. High-speed mode ensures this.

### Step 3: Configure PA10 (RX) as Pull-Up Input

Although this part only does transmission, it is common practice to configure RX during initialization to avoid coming back to change it later when adding receive functionality.

```cpp
GPIO_InitStruct.Pin = GPIO_PIN_10;
GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
GPIO_InitStruct.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

The pull-up resistor ensures the RX line stays high when idle, consistent with the UART protocol's idle state. Without a pull-up, the RX line floats and might be triggered by noise to detect a false start bit.

### Step 4: Enable USART1 Clock

```cpp
__HAL_RCC_USART1_CLK_ENABLE();
```

USART1 hangs on the APB2 bus. This macro operates on the USART1EN bit of the RCC_APB2ENR register. Just like GPIO clock enabling, if you don't call this macro, USART registers cannot be written.

### Step 5: Configure and Initialize USART

This is the most critical step. The `UART_InitTypeDef` structure defines the communication parameters for USART:

```cpp
UART_HandleTypeDef huart1;
huart1.Instance = USART1;
huart1.Init.BaudRate = 115200;
huart1.Init.WordLength = UART_WORDLENGTH_8B;
huart1.Init.StopBits = UART_STOPBITS_1;
huart1.Init.Parity = UART_PARITY_NONE;
huart1.Init.Mode = UART_MODE_TX_RX;
huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
huart1.Init.OverSampling = UART_OVERSAMPLING_16;

if (HAL_UART_Init(&huart1) != HAL_OK) {
    // Error handling
}
```

Let's explain the parameters one by one:

- **BaudRate = 115200** — The baud rate we chose. The previous part analyzed that with a 64 MHz clock, the error is only 0.08%, which is completely fine.
- **WordLength = UART_WORDLENGTH_8B** — 8 data bits. This is the standard configuration, covering all ASCII characters and the full range of a byte (0-255).
- **StopBits = UART_STOPBITS_1** — 1 stop bit. The most common configuration.
- **Parity = UART_PARITY_NONE** — No parity. No parity bit is added, so a frame is 1+8+1=10 bits.
- **Mode = UART_MODE_TX_RX** — Enable both transmission and reception. Even if we only transmit now and don't receive, enabling both directions doesn't hurt.
- **HwFlowCtl = UART_HWCONTROL_NONE** — No hardware flow control. Not needed for debugging scenarios.
- **OverSampling = UART_OVERSAMPLING_16** — 16x oversampling. The default and most robust choice.

These parameters combined form what we often call the **8N1** (8 data bits, no parity, 1 stop bit) configuration at 115200 baud. This is the most common UART configuration in the embedded world—if you are unsure what to use, 8N1 + 115200 is the safest choice.

---

## The UartConfig Structure

In our C++ code, these HAL constants are wrapped into type-safe `enum class`es, and then combined into the `UartConfig` structure:

```cpp
enum class BaudRate : uint32_t { /* ... */ };
enum class WordLength : uint32_t { /* ... */ };
// ... other enums ...

struct UartConfig {
    BaudRate baud_rate = BaudRate::Rate115200;
    WordLength word_length = WordLength::Bits8;
    StopBits stop_bits = StopBits::One1;
    Parity parity = Parity::None;
    FlowControl flow_control = FlowControl::None;
    // ...
};
```

The default values are 8N1 + 115200 + full duplex + no flow control. When initializing in `UartDriver`, we only need to write:

```cpp
UartDriver uart1(USART1, { .baud_rate = BaudRate::Rate115200 });
```

Here we use C++20's designated initializer—only specify the fields that need changing (`baud_rate`), and the rest automatically use default values. If you need to change the parity, just write `.parity = Parity::Even`, without having to write all fields.

---

## Blocking Transmission: HAL_UART_Transmit

After initialization, sending data requires just one function call:

```cpp
HAL_UART_Transmit(&huart1, (uint8_t*)data, size, HAL_MAX_DELAY);
```

`HAL_UART_Transmit` works as follows:

1. Write the first byte to the DR register (trigger transmission).
2. Poll waiting for the TXE flag (Transmit Data Register Empty).
3. After TXE is set, write the next byte.
4. Repeat until all bytes are sent.
5. Finally, wait for the TC flag (Transmission Complete).

`HAL_MAX_DELAY` means infinite wait—the function won't return until all data is sent. In a debugging scenario, this is perfectly fine. If your system has strict response time requirements, you can specify a timeout value (in milliseconds), after which the function returns `HAL_TIMEOUT`.

Why is this function called "blocking"? Because it blocks the CPU during transmission. At 115200 baud, sending one byte (10 bits) takes about 87 microseconds. Sending 13 bytes of "Hello UART!\r\n" takes about 1.1 milliseconds. During this 1.1 milliseconds, the CPU can't do anything—it is busy-waiting for the TXE flag. For debug log output, this cost is completely acceptable. But if you need to run a control loop every 100 microseconds in a real-time system, a 1.1 millisecond block is fatal.

---

## In Our Code: send_string

The C++ driver wraps blocking transmission into a more friendly interface. `send_string` accepts a `std::string_view`:

```cpp
void UartDriver::send_string(std::string_view str) {
    auto data = std::as_bytes(std::span(str));
    HAL_UART_Transmit(&huart_, const_cast<uint8_t*>(static_cast<const uint8_t*>(data.data())),
                      data.size(), HAL_MAX_DELAY);
}
```

`std::string_view` is a C++17 string view—it doesn't copy data, only holding a pointer to the original character data and its length. `std::as_bytes` converts the character view into a byte view, then passes it to `HAL_UART_Transmit`. Internally, `HAL_UART_Transmit` returns a `HAL_StatusTypeDef`—but `send_string` simply ignores the return value (`[[maybe_unused]]`), because it is mainly used for debug logs and errors don't need special handling there.

If you need finer error control, you can directly call `send_bytes`:

```cpp
auto status = uart1.send_bytes(std::as_bytes(std::span("Hello")));
```

The detailed error handling mechanism will be covered in Part 39 when we discuss `std::expected`.

---

## First Test

The code is written, flashed to the board, and the terminal is opened (115200, 8N1). You should see:

```text
Hello UART!
```

If you see it—congratulations, your UART transmission chain is fully connected.

If you don't see it, it's likely one of three problems:

**Terminal shows nothing?** Check the wiring. Adapter TX to PA10, Adapter RX to PA9, GND to GND. All three lines are indispensable. Also confirm the terminal is connected to the correct COM port (on Linux it's `/dev/ttyUSB0` or `/dev/ttyACM0`, on Windows it's `COM3` or similar).

**Terminal shows garbled text?** Baud rate mismatch. Confirm both the terminal and the code are set to 115200. If your code uses a different baud rate, the terminal must match it.

**Only the first line is correct, then it goes wrong?** The TX line might have poor contact. This phenomenon occurs when Dupont wires are unstable—the line is connected during the first line transmission, but comes loose during subsequent transmission. Try a different wire.

---

## Summary

In this part, we completed the full UART transmission process: five-step initialization (GPIO clock → TX/RX pin configuration → USART clock → UART_InitTypeDef → HAL_UART_Init) + blocking transmission. The moment "Hello UART!" appears in the terminal, it means the hardware wiring is correct, the clock configuration is correct, the baud rate matches, and the USART peripheral is working normally.

With transmission conquered, in the next part we will do two things: redirect `printf` output directly to the serial port (printf retargeting), and attempt blocking reception—then you will discover the fatal problem with blocking reception, paving the way for introducing interrupt-driven reception.
