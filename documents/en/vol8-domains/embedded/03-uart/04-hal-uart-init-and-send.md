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
title: 'Part 34: HAL UART Initialization and Transmission — Making the Chip Talk'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/03-uart/04-hal-uart-init-and-send.md
  source_hash: 4418b714c7874cb00fde17309b315da325a619bbb39fd5be6aa890c1fb490118
  token_count: 1367
  translated_at: '2026-05-26T12:15:55.985533+00:00'
description: ''
---
# Part 34: HAL UART Initialization and Transmission — Making the Chip Speak

> We've covered hardware fundamentals for three parts, and now we can finally write some code. The goal of this part is simple: make the chip send its first words to your computer via UART.

---

## Our Goal

Before writing any code, let's clarify what we want to achieve. The end result is straightforward: after flashing the code, open a terminal application on your PC (baud rate 115200, 8N1), and you will see "Hello UART!" appear in the terminal. That's it. But this means the entire UART transmission chain—GPIO configuration, USART clock enabling, HAL initialization, and blocking transmission—is fully working.

This part only covers transmission, not reception. The reason is simple: transmitting is much easier than receiving. Transmission is an active action—the chip decides when to send and what to send. Reception is a passive action—you don't know when external data will arrive or how much will come. Let's get transmission working first to build confidence, and then we'll tackle reception in the next part.

---

## Five Steps of the Initialization Sequence

To get USART1 working, we need to complete the following five steps in order. Each step has a clear purpose, so let's go through them one by one.

### Step 1: Enable the GPIOA Clock

Both PA9 and PA10 are on GPIOA. Just like in the LED/button tutorials, GPIO port clocks are off by default, so we must enable them first.

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
```

### Step 2: Configure PA9 (TX) as Alternate Function Push-Pull Output

The previous part already explained why the TX pin needs AF_PP mode—the USART peripheral directly controls the voltage level of this pin, while the GPIO controller takes a back seat.

```c
GPIO_InitTypeDef gpio = {0};
gpio.Pin   = GPIO_PIN_9;
gpio.Mode  = GPIO_MODE_AF_PP;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_SPEED_FREQ_HIGH` sets the output slew rate. At 115200 baud, each bit lasts about 8.68 microseconds, and the signal edges need to be sharp enough to stabilize within the sampling window. High-speed mode ensures this.

### Step 3: Configure PA10 (RX) as Input with Pull-Up

Even though this part only handles transmission, it's common practice to configure RX during initialization to avoid coming back to change it later when adding receive functionality.

```c
gpio.Pin  = GPIO_PIN_10;
gpio.Mode = GPIO_MODE_INPUT;
gpio.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOA, &gpio);
```

The pull-up resistor ensures the RX line stays high when idle, which matches the UART protocol's idle state. Without a pull-up, the RX line floats and might be triggered by noise into detecting false start bits.

### Step 4: Enable the USART1 Clock

```c
__HAL_RCC_USART1_CLK_ENABLE();
```

USART1 hangs off the APB2 bus, and this macro operates on the USART1EN bit of the RCC_APB2ENR register. Just like enabling the GPIO clock, if we don't call this macro, writes to the USART registers won't take effect.

### Step 5: Configure and Initialize the USART

This is the most critical step. The `UART_InitTypeDef` structure defines the USART communication parameters:

```c
UART_InitTypeDef init = {0};
init.BaudRate   = 115200;
init.WordLength = UART_WORDLENGTH_8B;
init.StopBits   = UART_STOPBITS_1;
init.Parity     = UART_PARITY_NONE;
init.Mode       = UART_MODE_TX_RX;
init.HwFlowCtl  = UART_HWCONTROL_NONE;
init.OverSampling = UART_OVERSAMPLING_16;

huart1.Instance = USART1;
huart1.Init     = init;
HAL_UART_Init(&huart1);
```

Let's explain each parameter:

- **BaudRate = 115200** — The baud rate we chose. As analyzed in the previous part, the error at a 64 MHz clock is only 0.08%, which is perfectly fine.
- **WordLength = UART_WORDLENGTH_8B** — 8 data bits. This is the standard configuration, covering all ASCII characters and the full range of a byte (0-255).
- **StopBits = UART_STOPBITS_1** — 1 stop bit. The most commonly used configuration.
- **Parity = UART_PARITY_NONE** — No parity. Without a parity bit, one frame is exactly 1+8+1=10 bits.
- **Mode = UART_MODE_TX_RX** — Enable both transmission and reception. Even if we're only transmitting right now, there's no harm in enabling both directions.
- **HwFlowCtl = UART_HWCONTROL_NONE** — No hardware flow control. Not needed for debugging scenarios.
- **OverSampling = UART_OVERSAMPLING_16** — 16x oversampling. The default and most robust choice.

Combined together, these parameters form what we commonly call the **8N1** (8 data bits, no parity, 1 stop bit) configuration at 115200 baud. This is the most common UART configuration in the embedded world—if you're unsure what to use, 8N1 + 115200 is the safest choice.

---

## The UartConfig Struct

In our C++ code, these HAL constants are wrapped into the type-safe `enum class`, and then combined into the `UartConfig` struct:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
struct UartConfig {
    uint32_t baud_rate      = 115200;
    WordLength word_length  = WordLength::Bits8;
    Parity parity           = Parity::None;
    StopBits stop_bits      = StopBits::One;
    Mode mode               = Mode::TxRx;
    HwFlowControl hw_flow   = HwFlowControl::None;
};
```

The default values are 8N1 + 115200 + full duplex + no flow control. When initializing in `main.cpp`, we only need to write:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
Logger::driver().init(device::uart::UartConfig{.baud_rate = 115200});
```

Here we use C++20's designated initializer—specifying only the fields we need to change (`baud_rate`), while the remaining fields automatically use their default values. If you need to change the parity, just write `.parity = Parity::Even`, without having to list all the fields.

---

## Blocking Transmission: HAL_UART_Transmit

Once initialization is complete, sending data requires just one function call:

```c
uint8_t data[] = "Hello UART!\r\n";
HAL_UART_Transmit(&huart1, data, strlen((char*)data), HAL_MAX_DELAY);
```

`HAL_UART_Transmit()` works as follows:

1. Write the first byte to the DR register (triggering transmission)
2. Poll and wait for the TXE flag (Transmit Data Register Empty)
3. Once TXE is set, write the next byte
4. Repeat until all bytes are sent
5. Finally, wait for the TC flag (Transmission Complete)

`HAL_MAX_DELAY` means wait indefinitely—the function won't return until all data has been sent. This is perfectly fine in a debugging scenario. If your system has strict response time requirements, you can specify a timeout value (in milliseconds), and the function will return `HAL_TIMEOUT` when it times out.

Why is this function called "blocking"? Because it ties up the CPU during transmission. At 115200 baud, sending one byte (10 bits) takes about 87 microseconds. Sending the 13 bytes of "Hello UART!\r\n" takes about 1.1 milliseconds. During those 1.1 milliseconds, the CPU can't do anything else—it's busy-waiting on the TXE flag. For debug log output, this cost is perfectly acceptable. But if you need to run a control loop every 100 microseconds in a real-time system, a 1.1 millisecond block would be fatal.

---

## In Our Code: send_string

The C++ driver wraps the blocking transmission into a more user-friendly interface. `send_string()` accepts a `std::string_view`:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
void send_string(std::string_view str) {
    auto bytes = std::as_bytes(std::span<const char>{str});
    [[maybe_unused]] auto result = send(bytes, HAL_MAX_DELAY);
}
```

`std::string_view` is a C++17 string view—it doesn't copy data, but only holds a pointer to the raw character data and its length. `std::as_bytes()` converts the character view into a byte view, which is then passed to `send()`. Internally, `send()` calls `HAL_UART_Transmit()` and returns `std::expected<size_t, UartError>`—but `send_string()` simply ignores the return value (`[[maybe_unused]]`) because it's primarily used for debug logs, and no special error handling is needed if something goes wrong.

If you need finer-grained error control, you can call `send()` directly:

```cpp
auto result = driver.send(std::as_bytes(std::span<const char>{"Hello\r\n"}), 1000);
if (!result) {
    // 处理错误：result.error() 是 UartError 枚举值
}
```

The detailed error handling mechanism will be covered in Part 39 when we discuss `std::expected`.

---

## First Test

With the code written, flash it to the board, open your terminal (115200, 8N1), and you should see:

```text
Hello UART!
```

If you see it—congratulations, your UART transmission chain is fully working.

If you don't see it, it's most likely one of the following three issues:

**Nothing in the terminal?** Check your wiring. Adapter TX to PA10, adapter RX to PA9, GND to GND. All three wires are essential. Also, confirm that the terminal is connected to the correct COM port (on Linux, it's `/dev/ttyUSB0` or `/dev/ttyACM0`; on Windows, it's something like `COM3`).

**Garbled text in the terminal?** Baud rate mismatch. Confirm that both the terminal and the code are set to 115200. If your code uses a different baud rate, the terminal must match it.

**Only the first line is correct, and the rest is garbled?** The TX line might have a poor connection. This phenomenon occurs when Dupont wires are unstable—the line is still making contact during the first transmission, but comes loose during subsequent transmissions. Try a different wire.

---

## Summary

In this part, we completed the entire UART transmission process: five-step initialization (GPIO clock → TX/RX pin configuration → USART clock → UART_InitTypeDef → HAL_UART_Init) + blocking transmission. The moment "Hello UART!" appears in the terminal means the hardware wiring is correct, the clock configuration is correct, the baud rate matches, and the USART peripheral is working properly.

With transmission sorted out, we'll do two things in the next part: redirect `printf()` output directly to the serial port (printf redirection), and try blocking reception—where you'll discover the fatal flaw of blocking reception, setting the stage for introducing interrupt-driven reception later.
