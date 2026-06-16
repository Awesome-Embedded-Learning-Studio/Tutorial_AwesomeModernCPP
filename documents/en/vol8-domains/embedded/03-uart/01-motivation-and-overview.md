---
chapter: 17
difficulty: beginner
order: 1
platform: stm32f1
reading_time_minutes: 11
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 31: From Buttons to Serial — Why UART is the Cornerstone of Embedded
  Communication'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/01-motivation-and-overview.md
  source_hash: 500d146af59c430f821d2bb25483e5cf5f25b2713ac2b9650bc71c72d88f9188
  translated_at: '2026-06-16T04:11:49.056427+00:00'
  engine: anthropic
  token_count: 1999
---
# Part 31: From Buttons to Serial — Why UART is the Cornerstone of Embedded Communication

> The LED tutorial taught the chip to "speak," and the button tutorial taught it to "listen." Now it's time to learn something new: enabling the chip to "converse" with other devices.

---

## Our Chip is Still an Island

Let's look back at the path we've traveled. Over 13 LED tutorials, we started with GPIO output mode, mastered clock enabling, register configuration, and HAL wrappers, and finally built a zero-overhead LED abstraction using C++ templates and RAII. Over 12 button tutorials, we switched to GPIO input mode, tackled pull-up/pull-down circuits, mechanical bouncing, debounce state machines, event systems, and concept-constrained callbacks. After these two series, our STM32 can independently handle input and output—pressing buttons, lighting LEDs, debouncing, and state management are all covered.

But if you take a step back and examine the whole system, you'll notice an issue: our chip is essentially still an island. The LED is an output of the chip itself, and the button is an input from the physical world to the chip, but neither leaves the board. Want to know the chip's internal status? You have to stare at the LED on the board. Want to send a command to the chip? You have to reach out and press the button. If your project requires the chip to send temperature data to a PC for visualization, or if you want to send configuration parameters from the PC, LEDs and buttons are completely inadequate.

We need a mechanism for the chip to exchange data with the outside world. Not just simple 0s and 1s, but real, structured data streams. This is where serial communication comes in.

---

## UART: The Oldest, Simplest, and Still Ubiquitous Protocol

UART stands for Universal Asynchronous Receiver/Transmitter. It is no exaggeration to call it "ancient"—the basic principles of this protocol date back to the teletype era of the 1960s. But calling it "obsolete" would be completely wrong, because today, almost every microcontroller has at least one UART peripheral. The STM32F103C8T6 chip has three: USART1, USART2, and USART3.

Why has UART survived so long? The reason is simple: it only needs two wires. One TX (transmit), one RX (receive), plus a common ground wire. No clock line (unlike SPI which needs SCK), no addressing mechanism (unlike I2C which needs device addresses and acknowledgments), and no concept of master or slave. As long as two devices agree on "how fast to speak" (baud rate), they can communicate directly. This extreme simplicity makes UART the default choice for embedded debugging, log output, and sensor communication.

You may have heard of SPI and I2C. SPI is fast but requires 4 wires (MOSI, MISO, SCK, CS), making it suitable for high-speed on-board communication (like driving displays or reading Flash). I2C only needs 2 wires (SDA, SCL) but requires addressing and acknowledgment mechanisms, making it suitable for connecting multiple low-speed devices (like temperature sensors and EEPROMs). UART sits between the two—fewest wires (2), simplest protocol (no address, no ack, no clock)—yet sufficient to meet the vast majority of "chip-to-PC" or "chip-to-chip peer-to-peer" needs.

For this tutorial, UART has another irreplaceable advantage: it can connect directly to your computer. Buy a cheap USB-TTL adapter (one with a CH340 or CP2102 chip), plug it into USB, open a terminal software (minicom, PuTTY, or the Arduino IDE Serial Monitor), and you can see the text sent by the chip on your computer, and send commands from the computer to the chip. It's not as complex as a JTAG debugger, and doesn't require the extra protocol parsing of SPI/I2C. The content the chip prints out appears directly in your terminal—it's just that simple.

---

## What We Are Building

Before we officially start, let's look at the destination. Here is what our code looks like after completing everything:

```cpp
int main() {
    // Initialize system clocks
    SystemClock::Config();

    // Initialize LED (PC13)
    Led led;
    led.Init();

    // Initialize Button (PA0)
    Button button;
    button.Init();

    // Initialize UART (USART1, PA9/PA10, 115200 8N1)
    UartManager<Usart1, 115200> uart;
    uart.Init();

    // Print welcome message
    uart.Send("System Ready. LED is OFF.\r\n");

    // Enable interrupt-driven receive
    uart.StartReceive();

    while (true) {
        // Process received commands
        uart.ProcessInput();

        // Handle button press locally
        if (button.IsPressed()) {
            led.Toggle();
        }
    }
}
```

If you have completed the LED and button tutorials, the general structure of this code should not be entirely unfamiliar. The `SystemClock`, LED, and Button template instantiations are exactly the same as before. The new parts are concentrated in the UART-related code, which is exactly what we will break down one by one in the next 13 articles.

Let's highlight a few things briefly. `Usart1` is a type alias—locking in "we are using USART1" at compile time via template parameters. `uart.Send` allows the chip to send text to the PC. `uart.StartReceive` starts interrupt-driven reception—whenever the PC sends a byte, a hardware interrupt pushes that byte into a ring buffer. The main loop retrieves bytes from the buffer, assembles them into a line, and then hands them to `uart.ProcessInput` to parse commands. You type "LED ON" in the terminal, press Enter, and the LED lights up—this is how the whole chain works.

---

## The Road Ahead

The UART tutorial consists of 13 parts, divided into six stages.

### Stage 1: Motivation (Part 31)

This is the part you are reading right now. It explains why we need to learn UART, what the final result looks like, and what hardware needs to be prepared.

### Stage 2: Hardware Fundamentals (Parts 32-33)

Part 32 dissects the UART protocol itself—how to synchronize without a clock line, what the data frame looks like, how baud rate and oversampling work, and why 115200 is the most common default baud rate. Part 33 shifts to the STM32F103 USART peripheral—the differences between the three USART instances, key registers, GPIO alternate function pin configuration, and a preview of NVIC interrupt connections.

### Stage 3: HAL + Blocking I/O (Parts 34-35)

Part 34 uses the HAL API to complete initialization and the first transmission—making the chip say "Hello" to the PC. Part 35 implements `printf` redirection (making `printf` output directly to the serial port) and attempts blocking reception. Then you will discover the fatal problem with blocking reception: the main loop is stuck. This naturally leads to the theme of the next stage.

### Stage 4: Interrupt-Driven (Parts 36-38)

This is the core stage of the series. Part 36 gives a comprehensive explanation of the Cortex-M3 interrupt mechanism and NVIC configuration. Part 37 designs and implements a lock-free ring buffer as a safe data channel between the ISR and the main loop. Part 38 strings together the complete interrupt reception callback chain—from `HAL_UART_RxCpltCallback` to `UsartManager::OnReceiveComplete` to the ring buffer's push and restart reception.

### Stage 5: C++ Abstractions (Parts 39-42)

Part 39 introduces C++23's `std::expected` for error handling, replacing C-style error codes. Part 40 designs the UART driver template—using NTTP to select the USART instance and empty base optimization (EBO) to eliminate object overhead. Part 41 uses Concepts to constrain the GPIO initialization callback and designs the `UartManager` lifecycle manager. Part 42 does a complete code walkthrough, assembling all the parts together.

### Stage 6: Summary (Part 43)

A summary of common pitfalls (TX/RX reversed, baud rate mismatch, ring buffer overflow, missing `volatile`, etc.) and three progressive exercises.

---

## Hardware Preparation

The good news is that the UART tutorial doesn't require more core hardware than the button tutorial—Blue Pill + ST-Link is still the setup. However, you need to prepare one extra item: a USB-TTL serial adapter.

The specific list is as follows:

- **STM32F103C8T6 Blue Pill Board** — The same board used in the LED/Button tutorials
- **ST-Link V2 Debugger** — For flashing and debugging, same as before
- **USB-TTL Serial Adapter** — One with a CH340 or CP2102 chip is fine, under ten bucks on Taobao. This adapter converts USB signals to UART TTL level signals, allowing the PC and Blue Pill to send data to each other
- **3 Dupont Wires (Female-to-Female)** — To connect the adapter to the Blue Pill

Wiring scheme:

```mermaid
graph LR
    PC[PC / USB] -->|USB| Adapter[USB-TTL Adapter]
    Adapter -->|TX| RX[RX (PA10)]
    Adapter -->|RX| TX[TX (PA9)]
    Adapter -->|GND| GND[GND]
```

Note a key point here: The adapter's TX connects to the Blue Pill's RX, and the adapter's RX connects to the Blue Pill's TX. "Your transmit is my receive"—getting this reversed is the most common wiring error in UART, and we will emphasize this repeatedly later.

Why PA9 and PA10? Because the default alternate function pins for USART1's TX and RX on the STM32F103 are PA9 and PA10. This is set at the factory and not something we chose arbitrarily.

On the software side, you need to install a terminal program on your PC:

- **Linux**: `minicom` (sudo apt install minicom) or `cutecom`
- **Windows**: PuTTY (select Serial mode) or Arduino IDE Serial Monitor
- **macOS**: `screen` or CoolTerm

Set the terminal baud rate to 115200, 8 data bits, no parity, 1 stop bit (abbreviated as 8N1)—this is also our default configuration in the code.

---

## New C++ Features We Will Learn

The UART tutorial involves more C++ features than the previous two series because we need to address new issues like error handling, interrupt callbacks, and template instance selection. Here is a list first, and each subsequent article will break them down:

- **`std::expected`** (C++23) — Error handling in embedded systems, lighter than exceptions, safer than error codes
- **`std::span`** (C++20) — A safe view over contiguous memory, replacing raw pointers + length
- **`std::string_view`** (C++17) — Zero-copy string view, a sharp tool for command parsing
- **`consteval`** (C++20) — Compile-time baud rate error verification
- **Concepts** (C++20) — Constraining the signature of GPIO initialization callbacks
- **`static` inline members** (C++17) — Per-instance independent storage in template classes
- **`volatile`** — Shared variable semantics between ISRs and the main loop
- **C++ to C ISR Bridge** — Bridge pattern between C++ code and C-linked interrupt vectors
- **`if constexpr`** (C++17) — Selecting different USART instances at compile time

None of these features are used just for the sake of it—each solves a practical problem in implementing the UART driver. We won't talk about syntax first and then application; instead, we will introduce features within specific problems so you know "why we need it."

---

## Where to Next

The preparations are complete. You now know what UART is, why learn it, what the final result looks like, and how to connect the hardware.

In the next part, we start from scratch: the UART protocol itself. Without a clock line, how do two devices know where a byte starts and ends? What roles do start bits, data bits, parity bits, and stop bits play? Behind the baud rate number, what is the chip actually doing? Once these questions are clear, you won't be "copying parameters" when writing code later, but rather "I know what this parameter means in the protocol."

Ready? Let's go.
