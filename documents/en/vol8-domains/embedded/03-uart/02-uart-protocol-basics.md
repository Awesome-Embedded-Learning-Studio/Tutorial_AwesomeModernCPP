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
title: 'Part 32: UART Protocol In-Depth — How to Synchronize Without a Clock Line'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/03-uart/02-uart-protocol-basics.md
  source_hash: b6d86655fcc8eff238a82143d6a1bc6a8bafa81f52bc43289df84bfda408f753
  token_count: 1530
  translated_at: '2026-05-26T12:14:50.042005+00:00'
description: ''
---
# Part 32: UART Protocol In Depth — How to Synchronize Without a Clock Line

> Following up on the previous article: we learned what UART is, why we should learn it, and how to wire the hardware. In this article, we tear apart the protocol itself to understand exactly what "no clock line" means.

---

## The Core Challenge of Asynchronous Communication

If you have worked with SPI or I2C before, you will remember that both have a dedicated clock line. SPI has SCK, and I2C has SCL. The purpose of the clock line is clear: the transmitter places data on one clock edge, and the receiver reads it on the other. The clock acts like a conductor's baton—every beat has a precise moment, and everyone knows exactly when to act.

UART has no baton. The TX and RX lines operate independently—the TX line carries only the signal from the transmitter, and the RX line carries only what the receiver sees. There is no shared clock transition to tell the receiver, "this is the start of a new bit." So how does the receiver know where a data frame begins, where it ends, and where the boundary of each bit lies?

The answer is that both sides agree on a rate before communication begins, and then each side uses its own clock to "count the beats" at that rate. This agreed-upon rate is the baud rate. For example, if both sides agree on 115200 baud, it means 115200 bits are transmitted per second, so the duration of each bit is 1/115200 ≈ 8.68 microseconds. The transmitter places a new bit on the TX line every 8.68 microseconds, and the receiver samples a bit from the RX line every 8.68 microseconds. As long as both clocks are accurate enough, the whole process stays aligned.

This is what "asynchronous" means—no shared clock, but achieving synchronization through a pre-agreed rate plus local clocks. Sounds unreliable? It actually works very well, because the receiver uses a technique called "oversampling" to align its sampling moments.

---

## Anatomy of a Data Frame

A complete UART data frame consists of the following parts. Let's break it down from start to finish:

### Idle State

When no data is being transmitted, the TX line stays high. This is the default state of UART—when nobody is talking, the line is high. This is important because it allows the receiver to distinguish between "nobody is talking" and "some state during an active transmission."

### Start Bit

When the transmitter is ready to send a byte, it first pulls the TX line low for one bit period. This high-to-low transition is the start bit. The start bit is the anchor of the entire frame—when the receiver detects this falling edge, it knows "data is coming" and uses this as the reference point to start sampling the subsequent bits.

Why is the start bit always low? Because the idle state is high. A high-to-low transition is an unambiguous signal change that the receiver cannot confuse with the idle state. If the idle state were also low, the start bit's low level would be indistinguishable from idle.

### Data Bits

Following the start bit is the actual data. UART supports 7, 8, or 9 data bits, with 8 bits being the most common configuration (which is why we often say UART transmits "one byte"). Data is sent starting from the least significant bit (LSB)—bit0 first, then bit1, and so on. This means that if you send the value `0x41` (the letter 'A', binary `01000001`), the actual order on the wire is `1-0-0-0-0-0-1-0`.

Eight data bits cover the standard ASCII character set (0-127) and extended ASCII (128-255). Nine data bits are typically used for address/data marking in multi-drop communication protocols—the 9th bit distinguishes whether the current frame is an address or data.

### Parity Bit — Optional

After the data bits, an optional parity bit can be added. The purpose of the parity bit is to ensure that the total number of "1"s in the frame (data bits + parity bit) satisfies a specific parity requirement. There are three options: no parity (None, most common), even parity (Even, total number of 1s is even), and odd parity (Odd, total number of 1s is odd). When no parity is selected, this bit is omitted entirely, which is the configuration used in the vast majority of embedded projects.

A parity bit can detect single-bit errors, but the cost is transmitting an extra bit, and in noisy environments the detection rate of a single-bit parity check is not high enough. In practice, projects either skip parity (relying on upper-layer protocols for CRC) or use 9 data bits for special purposes.

### Stop Bit

The end of the frame is the stop bit, which is fixed at a high level and lasts for 1 or 2 bit periods (some devices support 1.5 bits). The stop bit pulls the TX line back to high—也就是 the idle state. It serves two purposes: first, it lets the receiver confirm "this frame is done," and second, it ensures that the next frame's start bit can produce a clean high-to-low transition (because the stop bit has pulled the line back to high).

A complete 8N1 frame (8 data bits, no parity, 1 stop bit) looks like this in timing:

```text
空闲  起始位  D0  D1  D2  D3  D4  D5  D6  D7  停止位  空闲
HIGH   LOW   x   x   x   x   x   x   x   x   HIGH   HIGH
       |<--- 10 bits 总共 (1+8+1) --->|
```

Sending one byte requires transmitting 10 bits (1 start bit + 8 data bits + 1 stop bit). At 115200 baud, the transmission time for one frame is 10/115200 ≈ 86.8 microseconds, and the effective data rate is 11520 bytes per second.

---

## Baud Rate and Oversampling

The baud rate is defined as the number of symbols transmitted per second. In UART, one symbol is one bit, so the baud rate equals the bit rate. Common baud rates include 9600, 19200, 38400, 57600, 115200, 230400, 460800, and 921600. Among these, 115200 is the most common default in embedded projects—it is fast enough to meet most debugging and communication needs, while its demand on clock accuracy is not too stringent.

You might wonder why these numbers are not round tens or hundreds—9600, 115200, rather than 10000, 100000. The reason is that these numbers relate to clock division in early telecommunication systems. 9600 = 9600, 115200 = 9600 x 12. Historically, clock sources were typically 1.8432 MHz or multiples thereof, and dividing by the appropriate integer yielded these baud rates.

### Oversampling: How the Receiver Finds the Center of a Bit

As mentioned earlier, the receiver samples once per bit period at the agreed baud rate. But here is the problem: the receiver's clock and the transmitter's clock can never be perfectly identical. If there is a slight deviation (for example, the transmitter's actual rate is 115201 baud and the receiver's is 115199 baud), the sampling point will gradually drift away from the center as the number of bits increases, eventually leading to incorrect sampled values.

The solution is oversampling. The STM32 USART receiver does not sample just once per bit period; instead, it samples 16 times (16x oversampling) or 8 times (8x oversampling). 16x oversampling is the default mode and the one we use in our code.

The process works like this: after the receiver detects the falling edge of the start bit, it samples at 16 times the baud rate. For 115200 baud, the sampling frequency is 115200 x 16 = 1,843,200 Hz. The receiver confirms the start bit is valid at the middle of the start bit (the 8th sample point), and then reads data every 16 sample points—sampling right at the center of each bit. Even if the two devices' clocks have a minor deviation, as long as the deviation is within 2-3%, the cumulative offset over 16 sample points is not enough for the sampling point to slip out of the current bit's range, and communication remains reliable.

This is why the oversampling configuration we see in `uart_config.hpp` is fixed to `UART_OVERSAMPLING_16`:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
huart_.Init.OverSampling = UART_OVERSAMPLING_16;
```

### Baud Rate Error: Why "Correct Configuration" Still Produces Garbled Data

Ideally, the receiver's and transmitter's baud rates are perfectly matched. In reality, the baud rate is derived by dividing the system clock, and the divisor can only be an integer. If your system clock is 64 MHz (as in our code) and you want to generate 115200 baud:

```text
BRR = 64,000,000 / 115200 = 555.555...
```

After rounding, BRR = 556, actual baud rate = 64,000,000 / 556 = 115107.9, error = (115200 - 115107.9) / 115200 = 0.08%. This error is well within UART's tolerance (typically 2-3%), so communication is fine.

But if you set a higher baud rate, such as 921600:

```text
BRR = 64,000,000 / 921600 = 69.444...
```

After rounding, BRR = 69, actual baud rate = 64,000,000 / 69 = 927536.2, error = (927536.2 - 921600) / 921600 = 0.64%. This is still within tolerance, but it is an order of magnitude larger than with 115200.

Our code has a `consteval` function that checks this error at compile time, ensuring it does not exceed three-thousandths (3%):

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

We will break down this function in detail in Part 40 when we discuss C++ template metaprogramming. For now, you just need to know that the compiler checks for you at compile time: "is this baud rate's error acceptable at your clock frequency?" If it is not acceptable, compilation fails directly—rather than waiting until you flash the board and discover that everything you receive is garbled.

---

## Flow Control

UART also has an optional mechanism called flow control, used to prevent data loss when the receiver cannot process data fast enough. There are two approaches:

Hardware flow control uses two additional signal lines: RTS (Request To Send) and CTS (Clear To Send). When the receiver's buffer is nearly full, it pulls the RTS signal high to tell the transmitter "pause transmission"; when the buffer has space again, it pulls RTS low to resume. CTS works in the opposite direction—the transmitter checks the CTS signal to decide whether to continue sending.

Software flow control uses special control characters XON (0x11) and XOFF (0x13) instead of hardware signal lines. The receiver sends XOFF to tell the other side to pause, and XON to resume. The advantage is that no extra wires are needed; the disadvantage is that these control characters must not appear in the normal data stream.

Our code is configured with no flow control (`HwFlowControl::None`), which is the simplest setup. For 115200 baud debug communication, the data volume is usually small, so flow control is unnecessary. In high-speed or high-volume scenarios (such as transferring a firmware image over UART), you may need to consider enabling hardware flow control.

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

The last concept to clarify is signal levels.

The USART pins on the STM32F103 output TTL levels: logic high = 3.3V (close to VDD), logic low = 0V (close to GND). This voltage range is well-suited for chip-to-chip communication or for connecting to a PC via a USB-TTL adapter.

Historically, however, UART used RS-232 levels: logic high = -3V to -15V, logic low = +3V to +15V. The RS-232 voltage range is much higher than TTL, so it supports longer transmission distances and better noise immunity. If your project needs to connect to the RS-232 serial port on legacy equipment (such as industrial PCs or old instruments), you will need a level-shifter chip (such as the MAX232) between the STM32 and the RS-232 interface.

We are using a USB-TTL adapter—one end is USB (connected to the PC), and the other end has TTL-level TX/RX/GND (connected to the Blue Pill). Both sides use TTL levels, so we can connect them directly with Dupont wires—no level shifting needed.

Let's go over the wiring one more time, because this is really easy to get backwards:

```text
USB-TTL 适配器      Blue Pill
  TX ────────────── PA10 (USART1 RX)
  RX ────────────── PA9  (USART1 TX)
  GND ───────────── GND
```

The adapter's TX connects to the Blue Pill's RX, and the adapter's RX connects to the Blue Pill's TX. "I send, you receive; you send, I receive"—remember this crossover relationship, and you will avoid the number one wiring mistake in UART debugging.

---

## Summary

In this article, we broke down the complete mechanism of the UART protocol: no shared clock, relying on a pre-agreed baud rate plus oversampling for synchronization; data frames consist of a start bit, data bits, an optional parity bit, and a stop bit; baud rate error must be kept within 3%; and TTL levels can connect directly to a USB-TTL adapter for PC communication.

In the next article, we shift to the hardware: what exactly does the USART peripheral on the STM32F103 look like, what registers does it have, and how do we configure the clock and GPIO alternate functions? Once we understand these, we will be ready to write code in the article after that.
