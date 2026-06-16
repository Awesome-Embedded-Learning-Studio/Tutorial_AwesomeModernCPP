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
title: 'Part 42: Command Processor and Complete Code Walkthrough — From Serial Input
  to LED Control'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/12-command-processor-and-main-walkthrough.md
  source_hash: 14ff0b497b42484943c700b9eeb6b3ac98454ae0df6d522ddf603611f1f50538
  translated_at: '2026-06-16T04:12:07.484098+00:00'
  engine: anthropic
  token_count: 1793
---
# Part 42: Command Processor and Complete Code Walkthrough — From Serial Input to LED Control

> All the parts are ready. In this post, we will do a complete walkthrough of the `main.cpp` to see how they work together.

---

## The Full Picture of main.cpp

This is our final code. You have seen its individual segments in previous articles; now let's piece them together into a complete picture:

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

The order of every step cannot be swapped. Calling HAL functions before configuring the clock will cause a hard fault. If GPIO is not configured, USART signals won't reach the pins. If interrupts are not enabled before starting reception, incoming bytes won't trigger the ISR. Placing `send_string` before `uart_start_receive` is intentional—we first send a welcome message to confirm the transmission link works, then start reception.

---

## Two Tasks in the Main Loop

The main loop does two things: handles button events and processes UART reception. Neither blocks.

### Task One: Button Polling → UART Logging

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

This code is identical to the final version of the button tutorial—`poll_events()` samples pin levels, runs the debounce state machine, and calls the callback upon confirming an event. The callback handles `Pressed` and `Released` events via `std::visit` and a generic lambda. The only new element is `Logger::driver().send_string(...)`—sending button events to the PC via UART.

This means: when you press the button, "Button pressed!" appears in the terminal; when released, "Button released!" appears. Button events flow from the chip to the PC—direction is Chip → PC.

### Task Two: UART Reception → Command Parsing

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

This is the UART reception handling in the main loop. `rx.pop(b)` pops a byte from the ring buffer—the ISR pushes into it in the background, while the main loop consumes it here. `while (rx.pop(b))` pops all available bytes at once, ensuring none are missed.

The line parsing logic is straightforward: append popped bytes one by one into `line_buf`. When `\r` or `\n` is encountered, the line is considered complete; the full line is handed to `handle_command()` for processing, and the line buffer is reset. `line_len < line_buf.size() - 1` ensures no overflow—parts exceeding 127 characters are discarded.

The direction is opposite to the button: PC → Chip. You type "LED ON" in the terminal and press Enter; this string travels from the PC to the chip via UART. The ISR pushes bytes into the ring buffer one by one; the main loop pops them, assembles a line, recognizes it as the "LED ON" command, and lights up the LED.

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

The `cmd` parameter is `std::string_view`—pointing to raw data in `line_buf`, zero-copy. `==` compares by direct character matching. Supported commands are: `LED ON` (light on), `LED OFF` (light off), and `HELP` (show help). Unknown commands return an error message. Empty lines (consecutive enters) are ignored.

After each command executes, a confirmation is returned via `send_string`—the PC sees the result immediately. This is a simple request-response pattern: PC sends command, chip executes and confirms.

---

## Zero-Copy Advantage of std::string_view

The line `handle_command({line_buf.data(), line_len}, led)` creates `std::string_view`—it contains only a pointer and a length, copying no character data. Raw characters in `line_buf` are compared directly, with no intermediate `std::string` construction, memory allocation, or deallocation.

In a bare-metal environment, dynamic memory allocation (`new`/`malloc`) can lead to fragmentation and non-determinism. `std::string_view` allows you to manipulate strings without allocating memory—it is just a view into existing data. Combined with the `std::array<char, 128>` line buffer (stack-allocated), the entire command parsing process involves no heap operations.

---

## Two-Way Communication Architecture

Drawing all data flows together, the system architecture looks like this:

![System overall data flow architecture](./12-system-architecture.drawio)

Chip → PC direction: Button events and command responses are sent via `send_string()`. These calls use blocking transmission (`HAL_UART_Transmit`) because the data volume is small (tens of bytes), blocking time is controllable (less than 1 millisecond), and system responsiveness is unaffected.

PC → Chip direction: Commands entered in the terminal enter the ring buffer via interrupt reception, and the main loop consumes and parses them. It is fully non-blocking—the ISR finishes byte queuing in microseconds, and the main loop processes at its own pace.

The LED and Button components come from the previous two tutorials and are fully reused without modification. This is the power of good abstraction—the LED template and Button template don't know about the UART, but they naturally work with the UART command processor.

---

## Summary

This post provided a complete walkthrough of `main.cpp`, assembling all parts into a complete architecture diagram. The system has two independent data flows: button events flow from the chip to the PC (via blocking transmission), and UART commands flow from the PC to the chip (via interrupt reception + ring buffer + line parsing). LED and Button components are perfectly reused—zero modification, zero coupling.

The next post is the finale of this series: a summary of common pitfalls and three progressive exercises.
