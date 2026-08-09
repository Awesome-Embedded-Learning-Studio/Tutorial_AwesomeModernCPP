---
chapter: 17
difficulty: beginner
order: 2
platform: stm32f1
reading_time_minutes: 11
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 32: UART Protocol Deep Dive — How to Synchronize Without a Clock Line'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/02-uart-protocol-basics.md
  source_hash: aec81c6ea456d8e3bc30a424594637026766a28af6210b9b2f0f23f370da39b6
  translated_at: '2026-06-16T04:11:45.780695+00:00'
  engine: anthropic
  token_count: 1534
---
# Part 32: UART Protocol Deep Dive — How to Synchronize Without a Clock Line

> Following the previous article: We know what UART is, why we should learn it, and how to connect the hardware. In this article, we dissect the protocol itself to understand exactly what "no clock line" implies.

---

## The Core Challenge of Asynchronous Communication

If you have previously worked with SPI or I2C, you will recall that they both have a dedicated clock line. SPI has SCK, and I2C has SCL. The role of the clock line is clear: the transmitter places data on one clock edge, and the receiver reads it on the other. The clock acts like a conductor's baton—every beat has a precise moment, so everyone knows exactly when to do what.

UART has no conductor's baton. The TX and RX lines are independent—only the transmitter's signal is on the TX line, and only the receiver's signal is on the RX line. There is no shared clock transition to tell the receiver, "This is the start of a new bit." So, how does the receiver know where a data frame begins, where it ends, and where the boundary of every bit lies?

The answer is: both parties agree on a rate before communication starts, then use their own clocks to "count the beat" at that rate. This agreed-upon rate is the **Baud Rate**. For example, if both sides agree on 115200 baud, it means 115200 bits are transmitted per second, so the duration of one bit is 1/115200 ≈ 8.68 microseconds. The transmitter places a new bit on the TX line every 8.68 microseconds, and the receiver samples a bit from the RX line every 8.68 microseconds. If both clocks are precise enough, the entire process stays aligned.

This is the meaning of "asynchronous"—no shared clock, but achieving synchronization through a pre-agreed rate plus local clocks. Does it sound unreliable? In practice, it works very well because the receiver uses a technique called **oversampling** to align the sampling moments.

---

## Dissecting the Data Frame

A complete UART data frame consists of the following parts. Let's break it down from start to finish:

### Idle State

When no data is being transmitted, the TX line remains high. This is the default state of UART—when no one is talking on the line, it is high. This is important because it allows the receiver to distinguish between "no one is talking" and "some state during data transmission."

### Start Bit

When the transmitter is ready to send a byte, it first pulls the TX line low for the duration of one bit. This falling edge from high to low is the **Start Bit**. The start bit is the anchor of the entire frame—when the receiver detects this falling edge, it knows "data is coming" and uses this as a reference point to start sampling the subsequent bits.

Why is the start bit fixed at low level? Because the idle state is high. The transition from high to low is a distinct signal change that the receiver cannot confuse with the idle state. If the idle state were also low, the start bit's low level would be indistinguishable from the idle state.

### Data Bits

After the start bit comes the actual data. UART supports 7, 8, or 9 data bits, with 8 bits being the most common configuration (which is why we often say UART transmits "one byte"). Data is transmitted starting from the **Least Significant Bit (LSB)**—bit0 first, then bit1, and so on. This means if you send the value `0x41` (the letter 'A', binary `01000001`), the actual sequence appearing on the wire is `10000010`.

8-bit data covers the standard ASCII character set (0-127) and extended ASCII (128-255). 9-bit data is typically used for address/data tagging in multi-drop communication protocols—the 9th bit distinguishes whether the current frame is an address or data.

### Parity Bit — Optional

After the data bits, an optional parity bit can be added. The purpose of the parity bit is to ensure that the count of "1"s in the entire frame (data bits + parity bit) meets a specific parity requirement. There are three choices: No Parity (None, most common), Even Parity (total number of 1s is even), and Odd Parity (total number of 1s is odd). When No Parity is selected, this bit is omitted entirely, which is the configuration used in the vast majority of embedded projects.

A parity bit can detect a single-bit error, but at the cost of transmitting an extra bit, and in noisy environments, the detection rate of a single-bit check is not high enough. In actual projects, we either don't use parity (relying on upper-layer protocols for CRC) or use 9-bit data for special purposes.

### Stop Bit

The end of the frame is the **Stop Bit**, which is fixed at a high level and lasts for 1 or 2 bit times (some devices support 1.5 bits). The stop bit pulls the TX line back to high—returning it to the idle state. It serves two purposes: first, to confirm to the receiver that "this frame has ended," and second, to ensure the next frame's start bit produces a clean high-to-low transition (because the stop bit pulled the line back up).

A complete 8N1 frame (8 data bits, no parity, 1 stop bit) looks like this in timing:

```text
空闲  起始位  D0  D1  D2  D3  D4  D5  D6  D7  停止位  空闲
HIGH   LOW   x   x   x   x   x   x   x   x   HIGH   HIGH
       |<--- 10 bits 总共 (1+8+1) --->|
```

Transmitting one byte requires transferring 10 bits (1 start bit + 8 data bits + 1 stop bit). At 115200 baud, the transmission time for one frame is 10/115200 ≈ 86.8 microseconds, and the effective data rate is 11520 bytes per second.

---

## Baud Rate and Oversampling

The **Baud Rate** is defined as the number of symbols transmitted per second. In UART, one symbol equals one bit, so the baud rate equals the bit rate. Common baud rates include 9600, 19200, 38400, 57600, 115200, 230400, 460800, and 921600. Among these, 115200 is the most common default in embedded projects—it is fast enough to satisfy most debugging and communication needs, while not being too demanding on clock precision.

You might wonder why these numbers aren't round tens or hundreds—9600, 115200, instead of 10000, 100000. The reason lies in clock division in early telecommunications systems. 9600 is 9600, and 115200 is 9600 x 12. Historically, clock sources were often 1.8432 MHz or multiples thereof, and dividing by the appropriate integer yields these baud rates.

### Oversampling: How the Receiver Finds the Center of a Bit

As mentioned earlier, the receiver samples once per bit time based on the agreed baud rate. But there is a problem: the receiver's clock and the transmitter's clock cannot be perfectly identical. If there is a slight deviation (e.g., the transmitter is actually 115201 baud and the receiver is 115199 baud), as the number of bits increases, the sampling point will gradually drift from the center, eventually leading to sampling the wrong value.

The solution is **oversampling**. The STM32 USART receiver does not sample just once per bit time; instead, it samples 16 times (16x oversampling) or 8 times (8x oversampling). 16x oversampling is the default mode and the one used in our code.

The process is as follows: after detecting the falling edge of the start bit, the receiver samples at 16 times the baud rate frequency. For 115200 baud, the sampling frequency is 115200 x 16 = 1,843,200 Hz. The receiver confirms the start bit is valid at the middle of the start bit (the 8th sample), then reads data every 16 samples—effectively sampling at the center of each bit. Even if the two devices' clocks have a slight deviation, as long as the deviation is within 2-3%, the cumulative offset of 16 samples is not enough to push the sampling point out of the current bit's range, ensuring reliable communication.

This is why the oversampling configuration we see in `uart_config.hpp` is fixed to `UART_OVERSAMPLING_16`:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
huart_.Init.OverSampling = UART_OVERSAMPLING_16;
```

### Baud Rate Error: Why "Correct Configuration" Still Results in Garbage

Ideally, the receiver and transmitter baud rates are identical. In reality, however, the baud rate is derived by dividing the system clock, and division can only result in integer values. If your system clock is 64 MHz (our configuration), and you want to generate 115200 baud:

```text
BRR = 64,000,000 / 115200 = 555.555...
```

After rounding, BRR = 556, actual baud rate = 64,000,000 / 556 = 115107.9, error = (115200 - 115107.9) / 115200 = 0.08%. This error is within the tolerance of UART (usually 2-3%), so communication works fine.

But if you set a higher baud rate, say 921600:

```text
BRR = 64,000,000 / 921600 = 69.444...
```

After rounding, BRR = 69, actual baud rate = 64,000,000 / 69 = 927536.2, error = (927536.2 - 921600) / 921600 = 0.64%. This is still within tolerance, but it is an order of magnitude larger than for 115200.

Our code includes a `consteval` function that checks this error at compile time to ensure it does not exceed three percent (3%):

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
template <uint32_t APBClockHz, uint32_t BaudRate>
consteval bool is_baud_rate_valid() {
    uint32_t brr    = (APBClockHz + BaudRate / 2) / BaudRate;
    uint32_t actual = APBClockHz / brr;
    uint32_t error_permille =
        (actual > BaudRate) ? (actual - BaudRate) * 1000 / BaudRate
                            : (BaudRate - actual) * 1000 / BaudRate;
    return error_permille < 30;
}
```

We will break down this function in detail in Part 40 when discussing C++ template drivers. For now, you just need to know: the compiler checks for you at compile time whether "this baud rate is acceptable given your clock frequency." If not, the compilation fails directly—rather than you discovering after flashing the board that everything received is garbage.

---

## Flow Control

UART has an optional mechanism called **Flow Control**, used to prevent data loss when the receiver cannot process data in time. There are two methods:

**Hardware flow control** uses two extra signal lines: **RTS** (Request To Send) and **CTS** (Clear To Send). When the receiver's buffer is nearly full, it pulls RTS high to tell the transmitter "pause transmission"; when there is space in the buffer, it pulls RTS low to resume. CTS is the reverse direction—the transmitter checks the CTS signal to decide whether to continue transmitting.

**Software flow control** uses special control characters **XON** (0x11) and **XOFF** (0x13) instead of hardware lines. The receiver sends XOFF to pause the other party and XON to resume. The advantage is no extra wires are needed; the disadvantage is that these control characters cannot appear in the normal data stream.

Our code is configured for no flow control (`HwFlowControl::None`), which is the simplest setup. For 115200 baud debug communication, the data volume is usually low, so flow control is not needed. In high-speed communication or high-volume scenarios (such as transferring a firmware image via UART), you might need to enable hardware flow control.

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
enum class HwFlowControl : uint32_t {
    None   = UART_HWCONTROL_NONE,
    Rts    = UART_HWCONTROL_RTS,
    Cts    = UART_HWCONTROL_CTS,
    RtsCts = UART_HWCONTROL_RTS_CTS,
};
```

---

## Signal Levels: TTL vs RS-232

The final concept to clarify is signal levels.

The USART pins on the STM32F103 output **TTL levels**: Logic High = 3.3V (close to VDD), Logic Low = 0V (close to GND). This voltage range is suitable for chip-to-chip communication or connecting to a PC via a USB-TTL adapter.

Historically, however, UART used **RS-232 levels**: Logic High = -3V to -15V, Logic Low = +3V to +15V. The voltage range of RS-232 is much higher than TTL, allowing for longer transmission distances and better noise immunity. If your project needs to connect to legacy equipment with an RS-232 serial port (like industrial PCs or old instruments), you will need a level-shifting chip (such as MAX232) between the STM32 and the RS-232 device.

We use a USB-TTL adapter—one end is USB (connecting to the PC), and the other is TTL-level TX/RX/GND (connecting to the Blue Pill). Both sides use TTL levels, so we can connect them directly with Dupont wires without level shifting.

Let's reiterate the wiring, because it is really easy to get this backwards:

```text
USB-TTL 适配器      Blue Pill
  TX ────────────── PA10 (USART1 RX)
  RX ────────────── PA9  (USART1 TX)
  GND ───────────── GND
```

The adapter's TX connects to the Blue Pill's RX, and the adapter's RX connects to the Blue Pill's TX. "I transmit, you receive; you transmit, I receive"—remember this crossover relationship, and you can avoid the number one wiring error in UART debugging.

---

## Summary

In this article, we dismantled the complete mechanism of the UART protocol: no shared clock, relying on pre-agreed baud rates + oversampling for synchronization; data frames consist of a start bit, data bits, an optional parity bit, and a stop bit; baud rate error must be kept within 3%; and TTL levels can connect directly to a USB-TTL adapter to communicate with a PC.

In the next article, we turn to the hardware: what exactly does the USART peripheral on the STM32F103 look like, what registers does it have, and how do we configure the clocks and GPIO multiplexing? Once we understand these, we will be ready to write code in the following article.
