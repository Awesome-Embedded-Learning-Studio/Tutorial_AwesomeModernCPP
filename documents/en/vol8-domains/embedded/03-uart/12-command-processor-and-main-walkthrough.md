---
chapter: 17
difficulty: intermediate
order: 12
platform: stm32f1
reading_time_minutes: 7
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 42: Command Processor and Full Code Walkthrough — From Serial Input to
  LED Control'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/03-uart/12-command-processor-and-main-walkthrough.md
  source_hash: 45f223049477b1ca1c04f47775f0dbd144d88c025c01458534238c6d8009ad4d
  token_count: 1789
  translated_at: '2026-05-26T12:18:15.934277+00:00'
description: ''
---
# Part 42: Command Handler and Full Code Walkthrough — From Serial Input to LED Control

> All the pieces are in place. In this part, we do a complete walkthrough of `main.cpp` to see how they work together.

---

## The Full main.cpp

Here is our final code. You have seen its individual fragments in previous articles; now let us piece them together into a complete picture:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
#include "base/circular_buffer.hpp"
#include "device/button.hpp"
#include "device/button_event.hpp"
#include "device/led.hpp"
#include "device/uart/uart_manager.hpp"
#include "system/clock.h"

extern "C" {
#include "stm32f1xx_hal.h"
}

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

extern base::CircularBuffer<128>& uart_rx_buffer();
extern void uart_start_receive();

using Logger = device::uart::UartManager<device::uart::UartInstance::Usart1>;

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

static void handle_command(std::string_view cmd,
                           device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>& led) {
    if (cmd == "LED ON") {
        led.on();
        Logger::driver().send_string("OK: LED ON\r\n");
    } else if (cmd == "LED OFF") {
        led.off();
        Logger::driver().send_string("OK: LED OFF\r\n");
    } else if (cmd == "HELP") {
        Logger::driver().send_string("Commands: LED ON, LED OFF, HELP\r\n");
    } else if (!cmd.empty()) {
        Logger::driver().send_string("ERR: unknown command\r\n");
    }
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;

    Logger::driver().set_gpio_init(usart1_gpio_init);
    Logger::driver().init(device::uart::UartConfig{.baud_rate = 115200});
    Logger::driver().enable_interrupt();
    Logger::driver().send_string("UART Logger Ready!\r\n");

    uart_start_receive();

    std::array<char, 128> line_buf{};
    size_t line_len = 0;

    while (1) {
        button.poll_events(
            [&](device::ButtonEvent event) {
                std::visit(
                    [&](auto&& e) {
                        using T = std::decay_t<decltype(e)>;
                        if constexpr (std::is_same_v<T, device::Pressed>) {
                            led.on();
                            Logger::driver().send_string("Button pressed!\r\n");
                        } else {
                            led.off();
                            Logger::driver().send_string("Button released!\r\n");
                        }
                    },
                    event);
            },
            HAL_GetTick());

        auto& rx = uart_rx_buffer();
        std::byte b{};
        while (rx.pop(b)) {
            char c = static_cast<char>(b);
            if (c == '\r' || c == '\n') {
                if (line_len > 0) {
                    handle_command({line_buf.data(), line_len}, led);
                    line_len = 0;
                }
            } else if (line_len < line_buf.size() - 1) {
                line_buf[line_len++] = c;
            }
        }
    }
}
```

---

## Initialization Sequence

The first half of `main()` is initialization, executed in a strict order:

![main() initialization flow](./12-main-flow.drawio)

The order of each step cannot be swapped. Calling HAL functions before configuring the clock will cause a hard fault. If GPIO is not configured, USART signals will not reach the pins. If interrupts are not enabled before starting reception, incoming bytes will not trigger the ISR. Placing `send_string` before `uart_start_receive` is intentional — we first send a welcome message to confirm the transmit path is working, then start receiving.

---

## The Two Tasks in the Main Loop

The main loop does two things: handling button events and processing UART reception. Neither blocks.

### Task 1: Button Polling → UART Log

```cpp
button.poll_events(
    [&](device::ButtonEvent event) {
        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, device::Pressed>) {
                    led.on();
                    Logger::driver().send_string("Button pressed!\r\n");
                } else {
                    led.off();
                    Logger::driver().send_string("Button released!\r\n");
                }
            },
            event);
    },
    HAL_GetTick());
```

This code is identical to the final version in the button tutorial — `poll_events()` samples the pin level, runs the debounce state machine, and invokes the callback upon confirming an event. The callback handles both `Pressed` and `Released` events via `std::visit` and a generic lambda. The only new addition is `Logger::driver().send_string(...)` — sending the button event to the PC over UART.

This means that when you press the button, "Button pressed!" appears in the terminal, and when you release it, "Button released!" appears. The button event flows from the chip to the PC — the direction is chip → PC.

### Task 2: UART Reception → Command Parsing

```cpp
auto& rx = uart_rx_buffer();
std::byte b{};
while (rx.pop(b)) {
    char c = static_cast<char>(b);
    if (c == '\r' || c == '\n') {
        if (line_len > 0) {
            handle_command({line_buf.data(), line_len}, led);
            line_len = 0;
        }
    } else if (line_len < line_buf.size() - 1) {
        line_buf[line_len++] = c;
    }
}
```

This is the UART reception handling in the main loop. `rx.pop(b)` pops a byte from the ring buffer — the ISR continuously pushes bytes into it in the background, and the main loop consumes them here. `while (rx.pop(b))` pops all available bytes at once, ensuring none are missed.

The line parsing logic is straightforward: appended popped bytes one by one into `line_buf`, treating `\r` or `\n` as the end of a line, passing the complete line to `handle_command()` for processing, and then resetting the line buffer. `line_len < line_buf.size() - 1` ensures no overflow — anything exceeding 127 characters is discarded.

The direction is the opposite of the button: PC → chip. When you type "LED ON" in the terminal and press Enter, this string travels from the PC to the chip via UART, the ISR pushes the bytes one by one into the ring buffer, the main loop pops them out to assemble a line, recognizes it as the "LED ON" command, and turns on the LED.

---

## handle_command: A Mini Shell

```cpp
static void handle_command(std::string_view cmd,
                           device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>& led) {
    if (cmd == "LED ON") {
        led.on();
        Logger::driver().send_string("OK: LED ON\r\n");
    } else if (cmd == "LED OFF") {
        led.off();
        Logger::driver().send_string("OK: LED OFF\r\n");
    } else if (cmd == "HELP") {
        Logger::driver().send_string("Commands: LED ON, LED OFF, HELP\r\n");
    } else if (!cmd.empty()) {
        Logger::driver().send_string("ERR: unknown command\r\n");
    }
}
```

The `cmd` parameter is a `std::string_view` — a pointer to the raw data in `line_buf`, zero-copy. `==` performs a direct, character-by-character match. Supported commands are: `LED ON` (turn on), `LED OFF` (turn off), and `HELP` (show help). Unknown commands return an error message. Empty lines (consecutive presses of Enter) are ignored.

After each command executes, a confirmation message is returned via `send_string` — the PC side can immediately see the command's result. This is a simple request-response pattern: the PC sends a command, and the chip executes and acknowledges it.

---

## The Zero-Copy Advantage of std::string_view

The line `handle_command({line_buf.data(), line_len}, led)` creates a `std::string_view` — it only contains a pointer and a length, without copying any character data. The raw characters in `line_buf` are compared directly, with no intermediate `std::string` construction, memory allocation, or deallocation.

In a bare-metal environment, dynamic memory allocation (`new`/`malloc`) can lead to fragmentation and non-determinism. `std::string_view` lets you manipulate strings without allocating memory — it is simply a view pointing to existing data. Paired with the `std::array<char, 128>` line buffer (allocated on the stack), the entire command parsing process involves zero heap operations.

---

## The Bidirectional Communication Architecture

Drawing all the data flows together, the overall system architecture looks like this:

![Overall system data flow architecture](./12-system-architecture.drawio)

Chip → PC direction: Button events and command responses are sent out via `send_string()`. These calls use blocking transmission (`HAL_UART_Transmit`) because the send volume is small (a few dozen bytes), the blocking time is predictable (less than one millisecond), and there is no impact on system responsiveness.

PC → Chip direction: Commands entered in the terminal enter the ring buffer via interrupt-driven reception, and the main loop consumes and parses them. This is completely non-blocking — the ISR enqueues bytes in microseconds, and the main loop processes them at its own pace.

The LED and Button components come from the previous two tutorials and are fully reused without any modifications. This is the power of good abstractions — the LED template and Button template have no knowledge of UART's existence, yet they naturally work in concert with the UART command handler.

---

## Summary

In this part, we did a complete walkthrough of `main.cpp`, assembling all the pieces into a complete architecture diagram. The system has two independent data flows: button events flow from the chip to the PC (via blocking transmission), and UART commands flow from the PC to the chip (via interrupt reception + ring buffer + line parsing). The LED and Button components are perfectly reused — zero modifications, zero coupling.

The next part is the finale of this series: a roundup of common pitfalls and three progressive exercises.
