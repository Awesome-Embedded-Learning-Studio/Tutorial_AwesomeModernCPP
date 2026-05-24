---
title: 'Part 42: Command Processor and Complete Code Walkthrough — From Serial Input
  to LED Control'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 17
order: 12
---
# Part 42: Command Handler and Full Code Walkthrough — From Serial Input to LED Control

> All the parts are ready. In this part, we do a complete walkthrough of `main.cpp`, seeing how they work together.

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

```mermaid
graph TD
    A["HAL_Init()<br/>HAL library init (SysTick, etc.)"]
    B["ClockConfig::instance().setup...<br/>System clock config (64 MHz HSI)"]
    C["LED&lt;Port::C, PIN_13&gt; led<br/>LED object construction (zero overhead)"]
    D["Button&lt;Port::A, PIN_0&gt; button<br/>Button object construction (zero overhead)"]
    E["Logger::driver().set_gpio_init(...)<br/>Register GPIO init callback"]
    F["Logger::driver().init(UartConfig)<br/>Enable clock → GPIO → HAL init"]
    G["Logger::driver().enable_interrupt()<br/>NVIC enable USART1 interrupt"]
    H["send_string(\"UART Logger Ready!\")<br/>Blocking send welcome message"]
    I["uart_start_receive()<br/>Start interrupt receive pipeline"]

    A --> B --> C --> D --> E --> F --> G --> H --> I
```

The order of every step cannot be swapped. Calling HAL functions before configuring the clock will cause a hard fault. If GPIO is not configured, USART signals will not reach the pins. If interrupts are not enabled before starting reception, arriving bytes will not trigger the ISR. Placing `send_string` before `uart_start_receive` is intentional—we first send a welcome message to confirm the transmit path is working, then start receiving.

---

## The Two Tasks of the Main Loop

The main loop does two things: handles button events and processes UART reception. Neither blocks.

### Task One: Button Polling → UART Log

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

This code is exactly the same as the final version in the button tutorial—`poll_events()` samples the pin level, runs the debounce state machine, and calls the callback upon confirming an event. The callback handles both `Pressed` and `Released` events through `std::visit` plus a generic lambda. The only new thing is `Logger::driver().send_string(...)`—sending the button event to the PC via UART.

This means: when you press the button, "Button pressed!" appears in the terminal; when you release it, "Button released!" appears. The button event flows from the chip to the PC—the direction is chip → PC.

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

This is the UART reception handling in the main loop. `rx.pop(b)` pops a byte from the ring buffer—the ISR continuously pushes into it in the background, and the main loop consumes it here. `while (rx.pop(b))` pops all available bytes at once, ensuring none are missed.

The line parsing logic is straightforward: append each popped byte to `line_buf`, consider a line complete when encountering `\r` or `\n`, pass the complete line to `handle_command()` for processing, and then reset the line buffer. `line_len < line_buf.size() - 1` ensures no overflow occurs—anything exceeding 127 characters is discarded.

The direction is opposite to the button: PC → chip. When you type "LED ON" in the terminal and press Enter, this string is sent from the PC to the chip via UART, the ISR pushes the bytes into the ring buffer one by one, the main loop pops them out to assemble a line, recognizes it as the "LED ON" command, and then turns on the LED.

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

The `cmd` parameter is `std::string_view`—pointing to the raw data in `line_buf`, zero-copy. `==` compares directly character by character. Supported commands are: `LED ON` (turn on), `LED OFF` (turn off), and `HELP` (show help). Unknown commands return an error message. Empty lines (consecutive enters) are ignored.

After each command executes, a confirmation message is returned via `send_string`—the PC side can immediately see the command execution result. This is a simple request-response pattern: the PC sends a command, and the chip executes and acknowledges it.

---

## The Zero-Copy Advantage of std::string_view

The line `handle_command({line_buf.data(), line_len}, led)` creates a `std::string_view`—it only contains a pointer and a length, without copying any character data. The raw characters in `line_buf` are compared directly, with no intermediate `std::string` construction, memory allocation, or deallocation.

In a bare-metal environment, dynamic memory allocation (`new`/`malloc`) can lead to fragmentation and non-determinism. `std::string_view` allows you to manipulate strings without allocating memory—it is simply a view pointing to existing data. Combined with the `std::array<char, 128>` line buffer (allocated on the stack), the entire command parsing process involves no heap operations.

---

## The Two-Way Communication Architecture

Drawing all the data flows together, the architecture of the entire system looks like this:

```mermaid
graph LR
    subgraph STM32["STM32"]
        TX["TX (PA9)"]
        RX["RX (PA10)"]
        LED["PC13 (LED)"]
        BTN["PA0 (Button)"]
    end

    subgraph ADAPTER["USB-TTL Adapter"]
        TTL_TX["TTL TX"]
        TTL_RX["TTL RX"]
    end

    subgraph PC["PC Terminal"]
        PC_TX["USB TX"]
        PC_RX["USB RX"]
    end

    TX -->|"Button events / Command responses"| TTL_RX
    TTL_RX -->|"USB"| PC_RX
    PC_TX -->|"USB"| TTL_TX
    TTL_TX -->|"Command input"| RX

    BTN -.->|"poll_events()"| TX
    RX -.->|"rx_ring → Line parsing<br/>→ handle_command"| LED
```

Chip → PC direction: Button events and command responses are sent out via `send_string()`. These calls use blocking transmission (`HAL_UART_Transmit`) because the send volume is small (a few dozen bytes), the blocking time is controllable (less than one millisecond), and there is no impact on system responsiveness.

PC → Chip direction: Commands entered in the terminal enter the ring buffer via interrupt reception, and the main loop consumes and parses them. It is completely non-blocking—the ISR completes byte enqueueing at the microsecond level, and the main loop processes at its own pace.

The LED and Button components come from the previous two tutorials and are fully reused without any modifications. This is the power of good abstractions—the LED template and Button template do not know that UART exists, yet they naturally work in concert with the UART command handler.

---

## Summary

In this part, we did a complete walkthrough of `main.cpp`, assembling all the parts into a complete architecture diagram. The system has two independent data flows: button events flow from the chip to the PC (via blocking transmission), and UART commands flow from the PC to the chip (via interrupt reception + ring buffer + line parsing). The LED and Button components are perfectly reused—zero modifications, zero coupling.

The next part is the finale of this series: a compilation of common pitfalls and three progressive exercises.
