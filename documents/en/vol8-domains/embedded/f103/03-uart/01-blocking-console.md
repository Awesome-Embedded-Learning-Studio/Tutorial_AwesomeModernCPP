---
title: 'A blocking command console: the divider arithmetic of UART frames and the 48-byte line buffer'
description: 'The 117 lines of examples/05_uart wire USART1 into a command console: init runs clock enable, the consteval pin_map, and HAL_UART_Init; send and receive rest on the polling of HAL_UART_Transmit/Receive, chunks capped at the 16-bit limit of 65535; a HAL error goes straight to __builtin_trap to keep the void contract; the baud rate follows the RM0008 formula baud = fCK/(16×USARTDIV) from PCLK2 at 64MHz to DIV_Mantissa 34 plus DIV_Fraction 12, BRR holds 556, the actual rate is 115107.9 baud with an error of 0.08%; the main loop is line reading that blocks on every byte, a 48-byte buffer with CR/LF normalization, backspace echo, and an overflow flag reported only when Enter arrives; help, ping, echo, a backspace-corrected line, and the 49-character overlong line were all captured live in the Renode PTY and checked'
chapter: 3
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
  - 寄存器
difficulty: intermediate
platform: stm32f1
cpp_standard: [20]
reading_time_minutes: 24
prerequisites:
  - 'Putting configuration into types: the Gpio template and compile-time verification'
  - 'Clock gating: when the LED will not light, start from the clock'
related:
  - 'Putting the logger to work: two calling styles and two viewing surfaces'
  - 'One burst of bounce, two chains: polling debounce vs. the EXTI edge chain'
translation:
  source: documents/vol8-domains/embedded/f103/03-uart/01-blocking-console.md
  source_hash: b381057c54391c91ee7cdd2cf672e17a7313d4b87a45c6fdba0bba5b2ed96c79
  translated_at: '2026-11-01T05:18:00+00:00'
  engine: anthropic
  token_count: 4400
---

# A blocking command console: the divider arithmetic of UART frames and the 48-byte line buffer

## The opening: giving the board a two-way line

The last two stops had us lighting LEDs and pressing buttons; in both cases the board was sending signals in one direction only, and between us and the board there was still no line that could carry a back-and-forth conversation. That line is exactly what this stop adds: USART (universal synchronous/asynchronous receiver/transmitter — the serial port an F103 board leans on most). The material is libestdx's `examples/05_uart`<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />, where one 117-line `main.cpp` packs in the transfers, the line editing, and the command dispatch, with the foundation under the transfers pressed into `boards/stm32f1/uart.hpp`. The material stays pinned at commit 8026841; every piece of terminal output in this article was captured live by me in Renode, and not one byte of it has been touched.

The first thing the firmware does after power-on is say hello. The transcript below is exactly what we captured through the PTY (pseudo-terminal — Renode wires the virtual serial port to a file on the host side); connect a terminal program to it and the feel is the same as plugging in a USB-to-serial cable:

```text
UART CMD ready
Type help for commands.
> help
Commands: help, ping, led on, led off, status, echo TEXT
> ping
pong
>
```

You type `help`, the firmware answers with the command list. Type `ping` instead, and the answer is `pong`. Three pieces of machinery are at work behind this exchange: the divider arithmetic that sets the tempo is covered in the baud-rate section, the job of packing send/receive into a type belongs to the `Uart` template section, and how the main loop assembles one editable command line belongs to the line-buffer section. At the close we lay out the limits of this blocking design — what it drops once the main loop gets busy.

## Ten line bits to a frame: baud rate is divider arithmetic

There is no clock line on a UART link; the two ends keep time against an agreed tempo, and the unit of that tempo is the baud rate. Our console runs 115200 8N1 — reading the notation piece by piece: the line rate is 115200 per second, there are 8 data bits, parity is skipped, and there is 1 stop bit. Sending one byte actually occupies 1 start bit, 8 data bits, and 1 stop bit, which makes 10 line bits in all. Divide it out and one byte takes about 86.8 microseconds of line time, so the bytes squeezed into a second top out at 11520. We are keeping that 86.8-microsecond figure — it takes the stage once more in the next article when we size a buffer.

The tempo on the wire is pressed out by a divider. RM0008 (ST's STM32F1 reference manual) gives the formula in §27.3.4: baud = fCK / (16 × USARTDIV), where fCK is the bus clock this USART hangs on. USART1 hangs on APB2 (one of the peripheral buses below the AHB) and takes PCLK2 (the APB2 bus clock); USART2 and USART3 hang on APB1 (the other peripheral bus below the AHB, one gear slower) and take PCLK1 (the APB1 clock)<RefLink :id="1" preview="STMicroelectronics, RM0008" />. Both bus clocks divide out of SYSCLK (system clock, the chip's main clock), and the trunk they pass through on the way is the AHB (advanced high-performance bus). We lay out the board's clock tree following the walkthrough in the getting-started piece [Clock gating: when the LED will not light, start from the clock](../01-led/03-rcc-clock-gate.md); the configuration of 05_uart lands exactly on these gears:

```text
HSI 8MHz / 2 × 16 → SYSCLK 64MHz → AHB /1 = HCLK 64MHz
HCLK → APB1 /2 = PCLK1 32MHz (clock of USART2 and USART3)
HCLK → APB2 /1 = PCLK2 64MHz (clock of USART1)
```

In `05_uart`'s `SystemClock_Config`, `APB1CLKDivider = RCC_HCLK_DIV2` and `APB2CLKDivider = RCC_HCLK_DIV1` — we checked the source's gear settings against the walkthrough above, and they agree to the last digit. Now the arithmetic for USART1, one step at a time:

```text
USARTDIV = 64MHz / (16 × 115200) = 34.72
DIV_Mantissa (integer part, BRR bits 15:4)   = 34
DIV_Fraction (fractional part, BRR bits 3:0) = 0.72 × 16 ≈ 12
True USARTDIV   = 34 + 12/16 = 34.75
Actual baud rate = 64MHz / (16 × 34.75) = 64MHz / 556 = 115107.9
```

The fraction cannot be made arbitrarily fine: BRR (the baud rate divider register) gives only 4 fractional bits, so the rounding lands on a grid of 1/16<RefLink :id="1" preview="STMicroelectronics, RM0008" />. After rounding, the line actually runs 115107.9 baud, and the gap against 115200 is an error of 0.08% — UART transfers are not sensitive to a deviation that small, and at this magnitude we can rely on it. Put USART2 on APB1's 32MHz instead and the same formula yields 17 for the integer part and 6 for the fraction; BRR holds 278, the actual baud rate is still 115107.9, and the error is again on the order of 0.08%. The two buses divide at different gears, yet after rounding they land on the same spot.

That was the arithmetic by hand — so who does it inside the firmware? The HAL computes it live in `UART_SetConfig` with the same formula: the `UART_BRR_SAMPLING16` macro multiplies the clock by 25 and divides by 4 times the baud rate, rounds the integer and fractional parts separately, then assembles them into BRR — plugging in our numbers gives exactly integer 34 plus fraction 12. The `Uart` template wraps this layer away, and whoever assembles it reports just one baud rate:

```cpp
struct UartConfig {
    std::uint32_t baud = 115200;
    WordLength word_length = WordLength::Bits8;
    StopBits stop_bits = StopBits::One;
    Parity parity = Parity::None;
};
```

The defaults in `uart_types.hpp` — 115200 8N1 — are exactly our console's configuration. The word-length-plus-parity combination has a compile-time gate; here is the assertion from `uart.hpp`, lifted as-is:

```cpp
static_assert(CONFIG.word_length != uart::WordLength::Bits9 ||
                  CONFIG.parity == uart::Parity::None,
              "F1 cannot send nine data bits plus parity");
```

Why does the 9-bit-word-length-plus-parity combination fail to compile? Translating the semantics of PCE makes it plain. A parity bit does not consume an extra line bit — it squeezes into the highest bit of the word. Turning to RM0008 §27.6.4, the record for PCE (parity control enable, the enable bit for parity control) says the parity bit is inserted at the MSB position: with M=1 it takes bit 9, with M=0 it takes bit 8<RefLink :id="1" preview="STMicroelectronics, RM0008" />. Set a 9-bit word length and turn parity on, and only 8 of the 9 data bits remain usable — whoever asked for 9 data bits receives 8. A miswritten configuration gets called out at compile time.

## The Uart template: polling send/receive packed into a type

Let's look at the assembly of the transfer type. The alias in `main.cpp` is one line; its template parameters carry the instance number and the configuration:

```cpp
using Serial = estdx::stm32f1::Uart<estdx::stm32f1::UartInstance::Usart1>;
```

`UartInstance` takes its values from the peripheral base addresses — USART1's is 0x40013800 — and with no configuration supplied you get the default 115200 8N1. The order of `init()`, read straight off the source: turn on the USART1 clock, configure the pins, fill in the HAL handle, and let `HAL_UART_Init` finish:

```cpp
static void init() {
    enable_clock();
    init_pins();

    auto& handle = detail::UartState<INSTANCE>::handle;
    handle.Instance = reinterpret_cast<USART_TypeDef*>(static_cast<std::uintptr_t>(INSTANCE));
    handle.Init.BaudRate = CONFIG.baud;
    handle.Init.WordLength = word_length();
    handle.Init.StopBits = stop_bits();
    handle.Init.Parity = parity();
    handle.Init.Mode = UART_MODE_TX_RX;
    handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    handle.Init.OverSampling = UART_OVERSAMPLING_16;
    check(HAL_UART_Init(&handle));
}
```

Where the pins land is decided at compile time. Reading `pin_map()`'s answer once through — a consteval function the compiler evaluates, one pin group per instance: USART1's TX sits on PA9 and RX on PA10, USART2 takes PA2 and PA3, and USART3 takes PB10 and PB11. The PA9/PA10 pairing matches the remapping table in RM0008 §9.3.8: with `USART1_REMAP = 0`, TX is PA9 and RX is PA10<RefLink :id="1" preview="STMicroelectronics, RM0008" />. TX is configured as alternate-function push-pull (the pin is handed to the peripheral to drive), RX as input pull-up (the line is pulled high while idle, and the UART idle state is high anyway).

The body of the transfers is polling; let's see how it slices. Both `send` and `receive` go through `transfer`, which cuts the data into chunks and feeds them to the HAL one chunk at a time:

```cpp
template <typename Span, typename Operation>
static void transfer(Span data, Operation operation) {
    constexpr auto max_chunk = std::numeric_limits<std::uint16_t>::max();
    auto& handle = detail::UartState<INSTANCE>::handle;
    for (std::size_t offset = 0; offset < data.size();) {
        const auto remaining = data.size() - offset;
        const auto count =
            static_cast<std::uint16_t>(remaining > max_chunk ? max_chunk : remaining);
        check(operation(&handle, data.data() + offset, count));
        offset += count;
    }
}
```

`send` feeds the polling function `HAL_UART_Transmit`, `receive` feeds `HAL_UART_Receive`, and both get `HAL_MAX_DELAY` as their timeout. Reading the HAL's posture once through: the two functions wait the same stubborn way, watching a flag in the status register. On the TX side the flag is TXE (transmit data register empty); on the RX side it is RXNE (read data register not empty). Once the flag arrives, one byte moves, and whether the timeout has run out is decided by millisecond differences of `HAL_GetTick()`. `HAL_MAX_DELAY` is 0xFFFFFFFF, which steps over the timeout comparison entirely, so the semantics here are to wait with no upper bound. The chunk ceiling of 65535 exists because the HAL count parameter is only 16 bits wide; anything longer goes out in slices.

Let's pull the two lines of `check` out on their own — the comment is the library author's own words:

```cpp
static void check(HAL_StatusTypeDef status) {
    if (status != HAL_OK) {
        // Preserve the void contract: returning here would claim a full transfer.
        __builtin_trap();
    }
}
```

Let's spread out `send`'s contract. Its return type is void, so a normal return is a declaration that all the data went out; if the HAL reports an error, that declaration is a lie. Rather than come back with a false normal return, it calls `__builtin_trap()` on the spot and leaves the failure scene where it happened. There is exactly one error path and it ends in a halt — that is the trade a blocking API makes.

Polling send also has a side branch with a bounded timeout, `try_send`, for callers that cannot afford to hang alongside a dead line. Its comment explains the design of the formula in full; we carry the whole thing over:

```cpp
// Best-effort path for callers that must not die on a dead line
// (logging): bounded timeout instead of HAL_MAX_DELAY — with the UART
// clock off, HAL_MAX_DELAY would hang forever; a bound turns the hang
// into a false return. Timeout derives from the compile-time baud:
// 8N1 = 10 line bits per byte, doubled for margin (HAL polls in 1 ms
// ticks; a slow-but-alive line must not time out).
static bool try_send(std::span<const std::byte> data)
    requires(CONFIG.word_length == uart::WordLength::Bits8)
{
    auto& handle = detail::UartState<INSTANCE>::handle;
    constexpr auto max_chunk = std::numeric_limits<std::uint16_t>::max();
    for (std::size_t offset = 0; offset < data.size();) {
        const std::size_t remaining = data.size() - offset;
        const auto count =
            static_cast<std::uint16_t>(remaining > max_chunk ? max_chunk : remaining);
        const std::uint32_t timeout =
            static_cast<std::uint32_t>(count) * 20000 / CONFIG.baud + 10;
        if (HAL_UART_Transmit(&handle,
                              reinterpret_cast<const std::uint8_t*>(data.data() + offset),
                              count, timeout) != HAL_OK) {
            return false;
        }
        offset += count;
    }
    return true;
}
```

Let's do the timeout formula's numbers out in the open. One byte occupies 10 line bits, so the per-byte line time is `10000/baud` milliseconds; the formula uses `20000/baud`, exactly a doubling of margin, with another 10 milliseconds of floor padded on behind. Plug 115200 in and sending 128 bytes gets a timeout ceiling of about 32 milliseconds, while that data's true line time is about 11 milliseconds — the ceiling is three times the real cost, so a slow-but-alive line is never misjudged, and a genuinely dead line costs only that brief wait before `false` comes back. Who is using it? The logger's `UartLogSink` takes the same bounded-timeout road; `sinks.hpp` labels `try_send` the best-effort path, because a logging caller must not hang with a dead line — a stance we already signed off on in [Putting the logger to work: two calling styles and two viewing surfaces](../06-patterns/07-logger-dual-syntax-usage.md).

## The main loop: line reading that blocks on every byte

With assembly done, we step into the main loop. The send-side helper is four lines: the string view is cut into a byte span and handed over, with `Serial::send` taking it from there:

```cpp
static void write(std::string_view text) {
    Serial::send(std::as_bytes(std::span<const char>{text.data(), text.size()}));
}
```

Command dispatch is one if chain, pasted as-is:

```cpp
static void run_command(std::string_view line, bool& led_on) {
    if (line == "help") {
        write("Commands: help, ping, led on, led off, status, echo TEXT\r\n");
    } else if (line == "ping") {
        write("pong\r\n");
    } else if (line == "led on") {
        Led::on();
        led_on = true;
        write("LED on\r\n");
    } else if (line == "led off") {
        Led::off();
        led_on = false;
        write("LED off\r\n");
    } else if (line == "status") {
        write(led_on ? "LED: on\r\n" : "LED: off\r\n");
    } else if (line.starts_with("echo ")) {
        write(line.substr(5));
        write("\r\n");
    } else if (!line.empty()) {
        write("Unknown command. Type help.\r\n");
    }
}
```

Read the dispatch out loud and it is match entry by entry, reply entry by entry: `echo` recognizes its prefix with `starts_with` and slices off the rest as the payload, and anything unrecognized gets one line of advice. An empty line returns nothing at all — the firmware's courtesy to whoever pressed Enter twice in a row. The `led on` and `led off` in the command list drive the PC13 onboard lamp, an old friend from the getting-started station — the same lamp that lit at stop 01 — and `status` reads back a boolean.

The line-reading loop is the most important stretch of the whole example; here it is, the whole thing carried over:

```cpp
std::array<char, 48> line{};
std::array<std::byte, 1> input{};
std::size_t length = 0;
bool led_on = false;
bool overflow = false;
bool after_cr = false;

for (;;) {
    Serial::receive(input);
    const auto ch = std::to_integer<unsigned char>(input[0]);

    if (ch == '\n' && after_cr) {
        after_cr = false;
        continue;
    }
    after_cr = ch == '\r';

    if (ch == '\r' || ch == '\n') {
        write("\r\n");
        if (overflow) {
            write("Line too long.\r\n");
        } else {
            run_command({line.data(), length}, led_on);
        }
        length = 0;
        overflow = false;
        write("> ");
    } else if (ch == '\b' || ch == 0x7f) {
        if (length > 0 && !overflow) {
            --length;
            write("\b \b");
        }
    } else if (ch >= 0x20 && ch <= 0x7e) {
        if (!overflow && length < line.size()) {
            line[length++] = static_cast<char>(ch);
            Serial::send(input);
        } else {
            overflow = true;
        }
    }
}
```

The loop's very first statement, `Serial::receive(input)`, blocks: with no byte arriving, the main loop hangs right there. Let's follow one byte through. `HAL_UART_Receive` waits stubbornly on the RXNE flag; with the byte in hand, the road forks three ways: a printable character (the range 0x20 through 0x7e) enters the buffer and echoes back unchanged; a backspace (`\b` or 0x7f) walks the length back by one and echoes the three bytes of `\b \b` (backspace, space, backspace — on screen the effect is that the character gets wiped); a carriage return or a line feed hands the accumulated line over.

The case of carriage return and line feed arriving together deserves an extra look, because a small mechanism is hiding here: plenty of terminals send `\r\n` as two bytes, and if each one were taken as an end of line, one command would be dispatched twice. The boolean `after_cr` records whether the previous byte was a carriage return; a line feed that follows a carriage return gets skipped — the first if in the code does exactly this.

That number 48 is the buffer's capacity and the boundary named in this section's title. The check in the printable-character branch, `length < line.size()`, stops the 49th character: the blocked character does not enter the buffer and gets no echo either; the firmware only raises `overflow`, and the real error waits for Enter: `Line too long.`. Deferring the error to the end of the line is deliberate — the first 48 characters of an overlong line look exactly like the echo of a legal line, so the only feedback a user has before pressing Enter is the count of echoed characters. We will count them out for you in the lab.

## The lab: running the console in a PTY

Let's go through the simulation setup, written in `renode.resc`: Renode is an open-source emulator, `sim/stm32f1/bluepill.repl` describes our F103 board, and `CreateUartPtyTerminal` wires usart1 to a PTY endpoint:

```text
emulation CreateUartPtyTerminal "uartTerminal" $uart_pty true
connector Connect usart1 uartTerminal
```

The PTY's default path is `/tmp/libestdx-uart`; the question-marked `$uart_pty?=` notation means an overridable default, and it is exactly how the check script injects its own temporary path. The recipe for playing with it by hand is in the library's README, and we follow it: `sim_uart_example` starts the simulation, we open another terminal and run `screen /tmp/libestdx-uart 115200`, and the screen is ours.

I captured the entire session live. The two greeting lines you have already seen in the opening; pasted below is the complete transcript from `help` all the way to the overlong line, with not one byte deleted:

```text
> help
Commands: help, ping, led on, led off, status, echo TEXT
> ping
pong
> echo hello console
hello console
> led on
LED on
> status
LED: on
> nope
Unknown command. Type help.
> pinh\b \bg
pong
> xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
Line too long.
>
```

Let's read it segment by segment. The `help`, `ping`, and `echo hello console` stretches are the if chain speaking literally — type one thing, get that thing back. `led on` is followed by `status`, which reads back `LED: on`: the command changed the boolean, and the readback followed. `nope` walks to the last branch of the if chain; `Unknown command. Type help.` is the only fallback there is apart from an empty command.

The `pinh\b \bg` line of the transcript deserves a look on its own. `\b` is the written form of the 0x08 backspace byte: my hand slipped and turned `ping` into `pinh`, I followed with one 0x7f backspace, then typed `g`. On receiving the backspace, the firmware walks the length back by one and echoes the three bytes of `\b \b`; on screen the `h` gets wiped, `g` fills the vacated slot, and after Enter it is the repaired `ping` that gets dispatched — the `pong` that comes back is the hardest evidence there is. The erasing happens on the firmware side: what the line buffer stores is the repaired version, while in the raw byte stream at the terminal both the `h` and the `g` did arrive.

The long line of `x` characters is the proof of that 48 boundary. I typed 49 `x` characters into it, and only 48 show up echoed in the transcript: the 49th character raised `overflow`, and the echo quietly came up one short — that absence is the boundary's first hint. When Enter lands, the error arrives only after it: `Line too long.`. The error waits for Enter, and once it is reported everything resets for the next line — that is the buffer's whole behavior, in three moves.

Automated verification runs the `check_uart_renode` target, and underneath it `check_renode.py` lines up every scenario above as assertions: the echo is compared character by character, CRLF counts as one line rather than two, the line repaired by backspace must answer `pong`, and after the 49th character the report must be `Line too long.`. The real run's output, pasted as-is:

```text
Renode UART CMD: b'UART CMD ready\r\nType help for commands.\r\n> '; commands, CRLF and line editing passed
```

I measured the firmware's size too. `arm-none-eabi-size` reports: text 5476, data 12, bss 76. One if chain plus one 48-byte array — 88 bytes of resident RAM, and this size is where every comparison later in the series starts from.

## The blocking boundary: what gets dropped when the main loop gets busy

Everything in the transcript looks fine, but the conditions that make it fine deserve spelling out. I typed the whole session one character at a time; a human hand moves at a few characters per second, while the console's longest wait for one byte is a single 86.8-microsecond line time, so in the overwhelming majority of moments it sits idle, hanging on `receive`. The price of blocking never shows itself at a human hand's tempo, because the console is the main loop's only real job to begin with.

Let's change one condition. Suppose the main loop picks up a second real job — say a computation that occupies it for 300 milliseconds — and the transfers stay pressed into the same loop. During those busy 300 milliseconds, `receive` does not run even once; of the bytes you type in that window, at most two have anywhere to wait, and the waiting places are the receive data register and the shift register, one byte to each. If the next byte arrives before RXNE has been read, the hardware raises the overrun flag — its name is ORE (overrun error), and what it reports is exactly an overrun. RM0008's annotation on ORE: the contents of the receive register are not lost, while the shift register is overwritten by the reception that came later — and that is how the extra byte quietly disappears. A human hand cannot type 63 bytes in 300 milliseconds, but an automated peer sending at line rate finishes a 63-byte transmission in 63 × 86.8 microseconds ≈ 5.5 milliseconds, gone in a blink — and then what gets dropped is bytes by the handful.

This boundary is exactly the starting point of libestdx's `examples/08_uart_ring` (example 08): move the receive-a-byte step into an interrupt, and while the main loop minds its own business, someone else is there to catch the bytes. [The blocking console's counterpart: the RXNE interrupt and the 64-byte ring buffer](02-interrupt-ring-console.md) replays this very same command console — see you there.

## Pitfalls and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| The terminal fills with garbage | The baud rates do not match; each end divided on its own assumptions | Configure both ends at 115200, and verify the clock tree before computing BRR |
| Typing produces no echo, and `ping` gets no reply | TX and RX are swapped: PA9 is the firmware's TX and must connect to the adapter's RX | Check the RM0008 remapping table and connect TX and RX crossed |
| One command executes twice | The terminal sends `\r\n` as two bytes, and each was taken as an end of line | Skip the line feed that follows a carriage return, the way `after_cr` does |
| The compiler reports `F1 cannot send nine data bits plus parity` | 9-bit word length and parity were configured together, and the parity bit claims the top bit | Choose between the two as your requirements dictate; 9 data bits and parity cannot both be had |
| `receive` never returns | The RX line floats or the peer sends nothing, and `HAL_MAX_DELAY` has no timeout | Verify the wiring before running, or switch to a receive path with a timeout |
| The 49th character does nothing | The 48-byte buffer is full, `overflow` is raised, and the echo is silently skipped | Press Enter to collect the `Line too long.` error, then retype the line |
| After `send`, the firmware halts and stops running | The HAL returned something other than OK, and `check` trapped on the spot, honoring the void contract | Inspect the HAL arguments and the peripheral state; the trap is a designed failure path |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="9.3.8 USART1 remapping; 27.3.4 Fractional baud rate generation; 27.6.1 USART_SR; 27.6.3 USART_BRR; 27.6.4 USART_CR1"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (examples/05_uart and boards/stm32f1/uart.hpp)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="Uart, UartConfig, try_send bounded timeout, consteval pin_map"
  />
</ReferenceCard>
