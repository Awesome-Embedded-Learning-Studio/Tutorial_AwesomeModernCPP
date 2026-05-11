---
title: 'Part 31: From Buttons to Serial Ports — Why UART is the Cornerstone of Embedded
  Communication'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 17
order: 1
---
# Part 31: From Buttons to Serial — Why UART Is the Cornerstone of Embedded Communication

> The LED tutorial taught the chip to "speak," and the button tutorial taught it to "listen." Now it is time to learn something new: how to make the chip "talk" with other devices.

---

## Our Chip Is Still an Island

Let us look back at the path we have taken. Over 13 LED tutorials, we started with GPIO output mode, figured out clock enabling, register configuration, and HAL wrappers, and finally built a zero-overhead LED abstraction using C++ templates and `enum class`. Over 12 button tutorials, we shifted to GPIO input mode, tackled pull-up/pull-down circuits, mechanical bouncing, debounce state machines, the `std::variant` event system, and Concepts-constrained callbacks. After both sets of tutorials, our STM32 can independently handle input and output—pressing buttons, lighting LEDs, debouncing, and state management, we have it all.

But if you take a step back and look at the whole system, you will spot a problem: our chip is essentially still an island. The LED is the chip's own output, and the button is physical-world input to the chip, but neither leaves the board. Want to know the chip's internal state? You have to stare at the LED on the board. Want to send a command to the chip? You have to reach out and press the button. If your project needs the chip to send temperature data to a PC for visualization, or if you want to push configuration parameters from the PC, LEDs and buttons are simply not enough.

What we need is a mechanism for the chip to exchange data with the outside world. Not simple 0s and 1s, but real, structured data streams. That is exactly where serial communication comes in.

---

## UART: The Oldest, Simplest, and Still Ubiquitous Protocol

UART stands for Universal Asynchronous Receiver/Transmitter. Calling it "old" is no exaggeration—the basic principles of this protocol date back to the teletypewriter era of the 1960s. But calling it "obsolete" would be completely wrong, because even today, nearly every microcontroller has at least one UART peripheral. The STM32F103C8T6 chip has three: USART1, USART2, and USART3.

Why has UART survived for so long? The reason is simple: it only needs two wires. One TX (transmit), one RX (receive), plus a common ground. There is no clock line (unlike SPI, which needs SCK), no addressing mechanism (unlike I2C, which needs device addresses and acknowledgments), and no concept of master and slave. As long as two devices agree on "how fast to talk" (the baud rate), they can communicate directly. This extreme simplicity makes UART the default choice for embedded debugging, log output, and sensor communication.

You have probably heard of SPI and I2C. SPI is fast but requires four wires (MOSI, MISO, SCK, CS), making it suitable for high-speed on-board communication (like driving displays or reading Flash). I2C only needs two wires (SDA, SCL) but requires an addressing and acknowledgment mechanism, making it suitable for connecting multiple low-speed devices (like temperature sensors and EEPROMs). UART sits between the two—it uses the fewest wires (two), has the simplest protocol (no address, no acknowledgment, no clock), yet it is sufficient for the vast majority of "chip-to-PC" or "chip-to-chip point-to-point" communication needs.

For this tutorial, UART has another irreplaceable advantage: it can connect directly to your computer. Buy a dirt-cheap USB-TTL adapter (one with a CH340 or CP2102 chip will do), plug it into a USB port, open a terminal program (minicom, PuTTY, or the Arduino IDE's Serial Monitor), and you can see the text sent by the chip on your PC, and send commands from the PC to the chip. It is not as complex as a JTAG debug probe, and it does not require the extra protocol parsing of SPI/I2C. The content the chip `printf` appears right there in your terminal—it is that simple.

---

## What We Are Going to Build

Before we officially start, let us take a look at the destination. This is what `main.cpp` looks like after we finish all the code:

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

If you completed the LED and button tutorials, the general structure of this code should not feel completely unfamiliar. The `HAL_Init()`, system clock, and template instantiations for the LED and Button are exactly the same as before. The new parts are concentrated in the UART-related code, and these are exactly what we will break down one by one over the next 13 articles.

Let us highlight a few key points. `UartManager<UartInstance::Usart1>` is a type alias—locking in "we want to use USART1" at compile time through template parameters. `send_string()` enables the chip to send text to the PC. `uart_start_receive()` starts interrupt-driven reception—whenever the PC sends a byte, a hardware interrupt pushes that byte into a ring buffer. The main loop pulls bytes from the buffer, assembles them into a line, and hands them to `handle_command()` for command parsing. You type "LED ON" in the terminal, press Enter, and the LED turns on—that is how the entire chain works.

---

## The Road Ahead

The UART tutorial consists of 13 articles, divided into six stages.

### Stage 1: Motivation (Part 31)

The article you are reading right now. It explains why we need to learn UART, what the final result looks like, and what hardware to prepare.

### Stage 2: Hardware Fundamentals (Parts 32-33)

Part 32 breaks down the UART protocol itself—how synchronization works without a clock line, what a data frame looks like, how baud rate and oversampling work, and why 115200 is the most common default baud rate. Part 33 shifts to the STM32F103's USART peripheral—the differences between the three USART instances, key registers, GPIO alternate function pin configuration, and a preview of the NVIC interrupt connections.

### Stage 3: HAL + Blocking I/O (Parts 34-35)

Part 34 uses the HAL API to complete initialization and the first transmission—making the chip say "Hello" to the PC. Part 35 implements `printf` redirection (making `printf()` output directly to the serial port) and tries blocking reception. Then you will discover the fatal flaw of blocking reception: the main loop gets stuck. This naturally leads into the theme of the next stage.

### Stage 4: Interrupt-Driven (Parts 36-38)

This is the core stage of the series. Part 36 provides a comprehensive explanation of the Cortex-M3 interrupt mechanism and NVIC configuration. Part 37 designs and implements a lock-free ring buffer to serve as a safe data channel between the ISR and the main loop. Part 38 strings together the complete callback chain for interrupt reception—from `USART1_IRQHandler` to `HAL_UART_RxCpltCallback` to the ring buffer's push and reception restart.

### Stage 5: C++ Abstractions (Parts 39-42)

Part 39 introduces C++23's `std::expected` for error handling, replacing C-style error codes. Part 40 designs the UART driver template—using NTTP to select the USART instance, and employing empty base optimization (EBO) to eliminate object overhead. Part 41 uses Concepts to constrain the GPIO initialization callback, and designs a UartManager lifecycle manager. Part 42 does a complete `main.cpp` walkthrough, assembling all the pieces together.

### Stage 6: Summary (Part 43)

A collection of common pitfalls (reversed TX/RX, baud rate mismatch, ring buffer overflow, missing volatile, etc.) along with three progressive exercises.

---

## Hardware Preparation

The good news is that the UART tutorial does not require any more core hardware than the button tutorial—the Blue Pill + ST-Link setup remains the same. But you do need to prepare one extra item: a USB-TTL serial adapter.

The specific list is as follows:

- **STM32F103C8T6 Blue Pill development board** — the same board used in the LED/button tutorials
- **ST-Link V2 debug probe** — for flashing and debugging, same as before
- **USB-TTL serial adapter** — one with a CH340 or CP2102 chip will do, under ten bucks on Taobao. This adapter converts USB signals into UART TTL-level signals, allowing the PC and Blue Pill to send data to each other
- **3 female-to-female DuPont wires** — to connect the adapter and the Blue Pill

Wiring scheme:

```text
适配器 TX  → PA10（Blue Pill RX）
适配器 RX  → PA9 （Blue Pill TX）
适配器 GND → GND （Blue Pill GND）
```

Note a key point here: the adapter's TX connects to the Blue Pill's RX, and the adapter's RX connects to the Blue Pill's TX. "Your transmit is my receive"—getting this backwards is the most common wiring mistake in UART, and we will emphasize it repeatedly later.

Why PA9 and PA10? Because the default alternate function pins for USART1's TX and RX on the STM32F103 are PA9 and PA10. This is fixed at the factory; we did not just pick them arbitrarily.

On the software side, you need to install a terminal program on your PC:

- **Linux**: `minicom` (`sudo apt install minicom`) or `screen /dev/ttyUSB0 115200`
- **Windows**: PuTTY (select Serial mode) or the Arduino IDE's Serial Monitor
- **macOS**: `screen /dev/tty.usbserial* 115200` or CoolTerm

Set the terminal's baud rate to 115200, 8 data bits, no parity, 1 stop bit (abbreviated as 8N1)—this is also the default configuration in our code.

---

## New C++ Features We Will Learn

The UART tutorial involves more C++ features than the previous two series, because we need to handle new problems like error handling, interrupt callbacks, and template instance selection. Here is a checklist upfront; we will break each one down in subsequent articles:

- **`std::expected<T, E>`** (C++23) — error handling in embedded systems, lighter than exceptions, safer than error codes
- **`std::span`** (C++20) — a safe view over contiguous memory, replacing raw pointers + length
- **`std::string_view`** (C++17) — zero-copy string view, a powerful tool for command parsing
- **`consteval`** (C++20) — compile-time baud rate error verification
- **Concepts** (C++20) — constraining the signatures of GPIO initialization callbacks
- **`static inline` members** (C++17) — per-instance independent storage in template classes
- **`volatile`** — shared variable semantics between the ISR and the main loop
- **`extern "C"` ISR bridging** — a bridging pattern between C++ code and C-linkage interrupt vectors
- **`if constexpr`** (C++17) — compile-time selection of different USART instances

None of these features are used just for the sake of using them—each solves a practical problem in implementing the UART driver. We will not teach the syntax first and then the application; instead, we will introduce features within the context of specific problems, so you know "why we need it."

---

## Where to Go Next

The preparation is done. What UART is, why we should learn it, what the final result looks like, how to wire the hardware—you already know all of this.

In the next article, we start from the very beginning: the UART protocol itself. Without a clock line, how do two devices know where a byte starts and ends? What roles do the start bit, data bits, parity bit, and stop bits play? Behind the baud rate numbers, what is the chip actually doing? Once you understand these questions, you will not be "copying parameters blindly" when writing code later, but rather "knowing what this parameter means in the protocol."

Ready? Let us go.
