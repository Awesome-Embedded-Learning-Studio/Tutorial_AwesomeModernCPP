---
title: 'The counterpart to blocking send/receive: the RXNE interrupt and the 64-byte ring buffer'
description: 'The 221 lines of examples/08_uart_ring move receive into the USART1 interrupt; UartRx<Usart1,64>''s start(3) sets the preemption level through HAL_NVIC_SetPriority and raises CR1 bit 5 RXNEIE; irq() reads SR once and splits three ways: RXNE reads DR, FE/NE drop the damaged byte, ORE reads DR to clear the overrun; bytes pass through SpscQueue''s 64-slot lock-free ring (power-of-two mask wraparound, one spare slot traded for the head==tail empty check, the index written only after the element, volatile single-writer indices needing no barrier on the cache-less M3); the 63-slot capacity at the 86.8-microsecond line-bit time buys about 5.5 ms of buffering headroom, drained non-blocking by the main loop''s try_pop; parsing moves to std::expected<Command, CommandError> with std::variant dispatch, and errors return carrying evidence (the tokens of unknown command: frobnicate and not a number: xyz both land in the warn log); ping and echo typed during the 300 ms HAL_Delay get answered in order once the loop wakes; captured and checked against real Renode runs; text+data grows from 5488 to 20044 bytes, data+bss from 88 to 460 bytes'
chapter: 3
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
  - 循环缓冲区
  - expected
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 21
prerequisites:
  - 'A blocking command console: the divider arithmetic of UART frames and the 48-byte line buffer'
related:
  - 'Who interrupts whom: how NVIC preemption priorities are arranged'
  - 'One burst of bounce, two chains: polling debounce vs. the EXTI edge chain'
translation:
  source: documents/vol8-domains/embedded/f103/03-uart/02-interrupt-ring-console.md
  source_hash: 78d3196af56c51419a5fb46511a8a788b7396e508e75102ec60da18fa3a58ea9
  translated_at: '2026-11-01T05:17:52+00:00'
  engine: anthropic
  token_count: 4800
---

# The counterpart to blocking send/receive: the RXNE interrupt and the 64-byte ring buffer

## Busy for 300 ms, not one byte lost

The previous article, [A blocking command console: the divider arithmetic of UART frames and the 48-byte line buffer](01-blocking-console.md), closed on the edge of what blocking can take: if the main loop goes busy for 300 ms while send and receive still ride the same loop, the overrun flag ORE goes up and the surplus bytes are gone. This piece picks up exactly that thread, with new material: libestdx's `examples/08_uart_ring`<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />, whose 221-line `main.cpp` packs in the interrupt, the ring buffer, and evidence-carrying command parsing. We look at it pinned to commit 8026841, and every terminal output pasted in this piece is the verbatim text I captured in Renode's PTY.

Let's look at the effect directly. My capture script wrote three commands into the PTY in one burst (`sleep 300` at the front, `ping` and one long `echo` behind it), with no waiting between the three lines:

```text
> sleep 300
[311ms][Info ][sleep] busy 300
[612ms][Info ][sleep] done
> ping
[612ms][Info ][ping] pong
> echo burst-canary-012345678901234567890123456789
[612ms][Info ][echo] burst-canary-012345678901234567890123456789
```

The 301 ms between `busy 300` and `done` (311 to 612) is `HAL_Delay(300)` being busy. During that window the bytes of `ping` and `echo` had long since arrived on the wire, yet the transcript shows no trace of an echo — echoing is the main loop's job, and it was too busy to get to it. Nothing was lost: the interrupt collected every byte into the ring, where they waited. Once `done` landed, the main loop returned to its receive loop and replayed the two accumulated lines in their original order; `pong` and the echo body carry the same [612ms] timestamp, because accumulated work drains fast. The timestamps are millisecond counts from the firmware-side `HAL_GetTick()`; the gap between the boot logs and the first reply is me waiting for the prompt during capture, and has nothing to do with how fast the firmware runs.

Across the busy 300 ms the main loop never once returned to the receive code, yet no byte went missing and the order stayed intact. That is the division of labor in the title: the RXNE interrupt catches the bytes, and the 64-slot ring buffer carries the pace mismatch. We take the three pieces one at a time — how the interrupt gets switched on, how the ring turns, and how parsing hands errors back with their evidence attached.

## The RXNE interrupt: when the byte arrives, the hardware comes calling

The blocking approach has the main loop sitting on `receive`, stubbornly waiting out the flag; the interrupt approach turns that wait around — the moment a byte you type arrives, the hardware comes over and calls us. Assembly is still a matter of one alias line:

```cpp
using Rx = estdx::stm32f1::UartRx<estdx::stm32f1::UartInstance::Usart1, 64>;
```

The template parameters carry the instance number and the ring's capacity, and 64 is a deliberate number: it has to be a power of two, the library guards that with a static_assert, and anything else simply fails to compile. In the power-on assembly sequence `Rx::start(3)` follows `Serial::init()`; the three lines in `uart_irq.hpp` read as follows, straight from the source:

```cpp
void start(std::uint32_t preempt) {
    HAL_NVIC_SetPriority(irq_number(), preempt, 0);
    HAL_NVIC_EnableIRQ(irq_number());
    usart()->CR1 |= USART_CR1_RXNEIE;
}
```

The three lines, one at a time. The first line sets the level. The HAL has already arranged the grouping as `NVIC_PRIORITYGROUP_4` inside `HAL_Init`: all 4 bits of weight go to the preemption level, not one bit is left for the sub-level, and sixteen levels are there for the choosing — so the second argument of `start` lands entirely on the preemption level. The smaller the number, the more urgent the interrupt; 3 sits at a middle rung, one that more urgent interrupts can break into and that can in turn break into slower ones. The full picture of the level mechanism (nesting, same-level suspension, the grouping ladder) gets its own treatment in [Who interrupts whom: how NVIC preemption priorities are arranged](../04-time/04-nvic-preempt-priorities.md) over at the time station; for this piece the middle rung of start(3) is all we need.

Lines two and three next. The second line enables USART1's interrupt line, with the number fetched per instance by `irq_number()`: in CMSIS (ARM's register-access layer for Cortex-M cores, the Cortex Microcontroller Software Interface Standard) USART1's number is 37 (`USART1_IRQn` in `stm32f103xb.h`); counting the 16 system exceptions ahead of it, its position in the Cortex-M3 exception table is 53, and its vector address is 0x0000_00D4 — that is how the row reads in RM0008's vector table (Table 63)<RefLink :id="1" preview="STMicroelectronics, RM0008" />. The third line is where the USART itself comes in: bit 5 of CR1, RXNEIE, goes up, and from then on, every time the hardware sets RXNE (short for read data register not empty, the flag that says the receive register is not empty), USART1 raises an interrupt request to the NVIC. ORE (short for overrun error, the overrun flag) travels the same request line, which is why overrun notification also arrives through this interrupt.

The name in the vector table is decided by the startup file: `startup_stm32f103xb.s` declares `USART1_IRQHandler` as a weak symbol. A weak symbol is a placeholder definition left standing for the linker: when the firmware supplies a real definition of the same name, the placeholder steps aside. We supply the same-named definition, and the interrupt lands on our function:

```cpp
extern "C" void USART1_IRQHandler() {
    Rx::irq();
}
```

We write the body as a single line that hands straight off to `Rx::irq()`. The less work done inside an interrupt the better — that is ISR discipline. Inside `irq()` there is no logging, no echoing, no call of any kind that would wait: it drops the byte into the ring and exits, returning the main loop's time to the main loop.

## The three paths of irq(): one read of SR, each goes its own way

The full body of `irq()` lives in `uart_irq.hpp`, a dozen-odd lines; we lift the whole passage over:

```cpp
void irq() {
    auto* const regs = usart();
    const auto sr = regs->SR;
    if (sr & USART_SR_RXNE) {
        const auto data = static_cast<std::byte>(regs->DR);
        if ((sr & (USART_SR_FE | USART_SR_NE)) == 0) {
            (void)ring.push(data);
        }
    } else if (sr & USART_SR_ORE) {
        (void)regs->DR;
    }
}
```

The first step reads SR (short for status register), and the snapshot of the flags lands in `sr`; every branch after that judges from the values in the snapshot. Reading one snapshot has its reasons: the flags change the moment DR moves, and by judging from the snapshot we stay undisturbed by anything that flips mid-flight. The three branches each go their own way.

The RXNE branch is the main road. The byte sits waiting in DR (short for data register), and the very act of reading DR clears RXNE — that is the F1 hardware convention, and it is exactly what RM0008 gives as RXNE's clear procedure<RefLink :id="1" preview="STMicroelectronics, RM0008" />. With the byte in hand, the snapshot still has to be checked for FE (short for framing error, the framing-error flag) and NE (short for noise error, the noise-error flag): if either of the two stands, this byte's line bits were laid out wrong or disturbed during sampling, and the firmware throws it away — it does not even qualify for the ring. Only clean bytes get `push`ed in. As for the return value discarded by `(void)`, we take it up in the ring section.

The ORE branch is overrun cleanup. RM0008's clear recipe for ORE is a read of SR followed by a read of DR<RefLink :id="1" preview="STMicroelectronics, RM0008" />. On the ordinary path the RXNE branch reads SR once and DR once, so if ORE went up before that, the combination clears it along the way. This branch covers the narrow window between reading the snapshot and reading DR: if ORE went up after the snapshot landed but before the DR read went through, the clear sequence was left unfinished, and the separate interrupt the hardware raises for ORE lands right here — this branch exists to read DR and finish the clear sequence, which is what dismisses the overrun. As for ORE's semantics, we follow RM0008's text: when it goes up, the content of the receive register is not lost; it is the shift register that gets overwritten by a later reception, and the byte genuinely missing is the one that never made it into the receive register.

About the FE and NE paths and ORE's cleanup path, I owe you the boundary of the evidence: these paths cannot be provoked in the Renode experiments. A PTY is a clean channel — the frames Renode's terminal model delivers are all good ones, and there is no injection point for framing or noise errors. Overrun is a different story, but with the ring holding 63 slots of headroom and interrupt response on the order of microseconds, bursting it by typing is not realistic. So the basis for these paragraphs is the source branches and RM0008's text, not a scene produced by injection. We count them as defensive lines in the design, and to say it plainly: these branches never fired once in any experiment that actually ran.

## The 64-slot ring: one writes, one reads, neither waits for the other

The interrupt puts bytes into the ring; the main loop takes them out; both ends press against the same data structure. The top of `spsc_queue.hpp` writes the contract out in full, and we lift a large passage of it:

```cpp
// Single-producer single-consumer queue, no locks, no atomics.
//
// Concurrency contract: exactly ONE context calls push() (typically an ISR)
// and exactly ONE other context calls pop() (typically the main loop).
// Adding a second producer or consumer voids the contract — that needs
// critical sections, not this class.
//
// Why plain volatile indices suffice on a single-core Cortex-M: each index
// has exactly one writer, aligned 32-bit loads/stores are single bus
// accesses (no tearing), and a reader observing a stale index merely
// revisits "empty"/"full" — it can never see a half-written element, because
// the writer publishes the index AFTER storing the element. volatile only
// stops the compiler from caching indices in registers across the ISR
// boundary; it is not a memory barrier and not needed as one here (no data
// cache on the M3, and code ordering within push/pop is fixed by the
// data dependency).
//
// Why one slot stays unused: head == tail unambiguously means empty, and
// full is detected one slot early. The classic alternative — an extra count
// variable — buys back the slot but pays with a second shared variable that
// both sides must reason about. One spare byte of RAM is the cheaper trade.
template <typename T, std::size_t N>
class SpscQueue {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two (mask-based wrap)");
    static_assert(std::is_trivially_copyable_v<T>, "elements cross an ISR boundary by plain copy");

public:
    // Producer side (ISR). Returns false when full; the caller owns the
    // overflow policy (drop, count, overwrite — but never block in an ISR).
    bool push(const T& value) {
        const auto head = head_;
        const auto next = wrap(head + 1);
        if (next == tail_) {
            return false;
        }
        storage_[head] = value;
        head_ = next;  // element first, index last — the release order readers rely on
        return true;
    }

    // Consumer side (main loop). Returns false when empty.
    bool pop(T& out) {
        const auto tail = tail_;
        if (tail == head_) {
            return false;
        }
        out = storage_[tail];
        tail_ = wrap(tail + 1);
        return true;
    }
```

SPSC in the name is short for single-producer single-consumer — one producer paired with one consumer. The second block of the comment is the contract itself: only the interrupt calls `push`, only the main loop calls `pop`, and one extra caller of either kind voids the class — that is critical-section work, not this class's work. The remaining members of the class (`empty`, `available`, `capacity`, and the private storage) are all small queries; we take each one up where the prose needs it.

Let's do the capacity arithmetic right here. N is 64, and `wrap` does its wraparound with `index & 63` — the modulo operation is replaced by a single AND, which is why N must be a power of two. Of the 64-slot array only 63 slots are truly usable: head equal to tail means empty, and the full check fires one slot early (`next == tail_` already counts as full). The comment lays out the other road as well: an extra count variable would buy the spare slot back, at the price of a second shared variable both sides must reason about, and the library chose the cheaper one-slot-spare design. Multiply 63 slots by the 86.8-microsecond line-bit time (10 line bits per byte, computed in the previous article), and the ring can hold about 5.5 ms of traffic in full. That arithmetic reads two ways. For a sustained stream (11520 bytes per second at full line rate) the ring lasts only a few milliseconds, and the main loop has to keep turning and drain the ring constantly. For a short burst (the 54 bytes the script writes in one go after `sleep` — `ping` with its carriage return takes 5, `echo` with its body and carriage return takes 49) the ring took all of them, and no matter how late the main loop comes back, there is nothing to fear.

Each index has exactly one writer: only the interrupt writes `head_`, only the main loop writes `tail_`, and volatile is there for exactly these two. The comment covers the mechanics in full; here it is again in our own words. On a single-core M3 an aligned 32-bit load or store completes in a single bus access, so there is no tearing to be had. What volatile blocks is the compiler caching the indices in registers across the interrupt boundary; it is not serving as a memory barrier, and a barrier would be redundant here anyway, because the M3 carries no data cache and the ordering inside push and pop is held in place by the data dependency. And what happens if a reader holds a stale index? At worst it judges empty when it is empty and full when it is full, and takes another look. The writer stores the element first and the index last, so a reader can never meet half an element.

`push` returns false when full. The ring itself does not block, and overwriting is not among its options — the comment states outright that dropping or not is the caller's policy, so let's read the choice `irq()` made. That `(void)ring.push(data)` throws the return value away: when the ring is full, a byte quietly goes missing. The firmware keeps no count either, and certainly says nothing. This is a trade built into the design — the 63-slot headroom exists precisely so that the full branch stays out of reach under a real typing pace. On the far side of `push` sits the interrupt; on the main loop's side the opening is `try_pop`, which wraps `ring.pop` and returns false the instant the ring is empty — the main loop never waits on it for a moment.

## Parsing moves to expected, and errors come back carrying evidence

The previous article's `run_command` was one if-chain answering in place, and for anything it could not recognize it replied with a generic `Unknown command. Type help.` — which word it failed on, it never told you. Example 08 (`examples/08_uart_ring`) splits recognizing from acting into two steps: `parse_line` does the recognizing, the recognized result goes into a std::variant, and the unrecognized error goes into the error slot of a std::expected, evidence and all. The error and command types, brought over from the source:

```cpp
enum class ParseError { Unknown, MissingArg, BadNumber };

// The error carries evidence: which word was not recognized / the name of the missing argument
struct CommandError {
    ParseError code;
    std::string_view token;
};

struct Help {};
struct Ping {};
struct LedOn {};
struct LedOff {};
struct Status {};
struct Echo {
    std::string_view text;
};
struct Sleep {
    std::uint32_t ms;
};
using Command = std::variant<Help, Ping, LedOn, LedOff, Status, Echo, Sleep>;
```

Each command gets its own small struct, and the seven structs go into one std::variant — a standard piece since C++17 whose talent is letting one variable hold any one of several types. The skeleton of `parse_line` is still an if-chain, but the return type becomes `std::expected<Command, CommandError>`, a standard piece since C++23: expected packs the has-value and has-error branches into the same return type. The 23 in this article's frontmatter cpp_standard is there precisely for it:

```cpp
std::expected<Command, CommandError> parse_line(std::string_view line) {
    if (line == "help") {
        return Help{};
    }
    if (line == "ping") {
        return Ping{};
    }
    if (line == "led on") {
        return LedOn{};
    }
    if (line == "led off") {
        return LedOff{};
    }
    if (line == "status") {
        return Status{};
    }
    if (line.starts_with("echo ")) {
        return Echo{line.substr(5)};
    }
    if (line.starts_with("sleep ")) {
        const auto arg = line.substr(6);
        std::uint32_t ms = 0;
        const auto result = std::from_chars(arg.data(), arg.data() + arg.size(), ms);
        if (arg.empty() || result.ec != std::errc{} || result.ptr != arg.data() + arg.size()) {
            return std::unexpected(CommandError{ParseError::BadNumber, arg});
        }
        return Sleep{ms};
    }
    return std::unexpected(CommandError{ParseError::Unknown, line});
}
```

The sleep branch deserves its own reading. The argument comes out of `line.substr(6)`, and the number parsing uses `std::from_chars` — it does not touch locale and throws no exceptions. The reason for a failed parse sits in `result.ec`, and if `result.ptr` did not reach the argument's end, characters that are not digits still hang off the tail. Checking the two together, `BadNumber` comes back with the offending text attached, and `token` holds that argument. Unrecognized commands take the final line: `Unknown` returns together with the whole line's original text.

As for what the main loop does once it has the parse result, the code shows it: with a value it goes to `execute(*parsed)`; without one it consults the error code through `parsed.error()`:

```cpp
const auto parsed = parse_line({line.data(), length});
if (parsed.has_value()) {
    execute(*parsed);
} else {
    const auto& error = parsed.error();
    switch (error.code) {
    case ParseError::Unknown:
        Log::warn(estdx::logger::Tag("cmd"), "unknown command: ", error.token);
        break;
    case ParseError::MissingArg:
        Log::warn(estdx::logger::Tag("cmd"), "missing argument: ", error.token);
        break;
    case ParseError::BadNumber:
        Log::warn(estdx::logger::Tag("cmd"), "not a number: ", error.token);
        break;
    }
}
```

The three errors come out as three warn lines, each carrying the offending text — you can see on the spot which line of input caused the trouble. This is the most tangible difference from the previous article.

The answering itself lives in `execute`, where std::visit dispatches on the variant's real type. The full body, brought over:

```cpp
void execute(const Command& command) {
    std::visit(
        [&](const auto& cmd) {
            using C = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<C, Help>) {
                Log::info(estdx::logger::Tag("help"),
                          "help, ping, led on, led off, status, echo TEXT, sleep MS");
            } else if constexpr (std::is_same_v<C, Ping>) {
                Log::info(estdx::logger::Tag("ping"), "pong");
            } else if constexpr (std::is_same_v<C, LedOn>) {
                Led::on();
                led_on = true;
                Log::info(estdx::logger::Tag("led"), "on");
            } else if constexpr (std::is_same_v<C, LedOff>) {
                Led::off();
                led_on = false;
                Log::info(estdx::logger::Tag("led"), "off");
            } else if constexpr (std::is_same_v<C, Status>) {
                Log::info(estdx::logger::Tag("led"), led_on ? "LED: on" : "LED: off");
            } else if constexpr (std::is_same_v<C, Echo>) {
                Log::info(estdx::logger::Tag("echo"), cmd.text);
            } else {
                Log::info(estdx::logger::Tag("sleep"), "busy ", cmd.ms);
                HAL_Delay(cmd.ms);
                Log::info(estdx::logger::Tag("sleep"), "done");
            }
        },
        command);
}
```

The lambda's parameter is auto, and `if constexpr` unrolls the seven command branches at compile time; at run time the real type jumps straight to the matching branch, with no run-time string comparison. In the sleep branch `HAL_Delay(cmd.ms)` busy-waits in place — the 300 ms of busyness in the title experiment is exactly this.

Example 08 also splits the answering into two channels; the comment at the top of `main.cpp` is a single line, and we bring it over as-is:

```cpp
// Terminal mechanical actions (echo/prompt) go out as raw bytes; info lines all go through Log
```

The terminal's mechanical actions (echo, the prompt) go out as `write_raw` bare bytes; info lines are invariably `Log::info`'s work. Why split them? Lay the two jobs side by side and it becomes clear. Echo has to match the typed character one for one, and speed is the only requirement. A Log line carries a Tag and a timestamp — an event record for humans to read — and travels through `UartLogSink<Serial>`'s `try_send`, the bounded-timeout route from the previous article, so the caller of a log line never hangs along with a dead line.

## The experiment floor: before and after the busy window, side by side

The simulation setup shares the same board description with the previous article. Let's read the two setup lines once more: `sim/stm32f1/bluepill.repl` describes this F103 board, and two lines in `renode.resc` connect usart1 to a PTY:

```text
emulation CreateUartPtyTerminal "uartTerminal" $uart_pty true
connector Connect usart1 uartTerminal
```

I captured the whole session with the capture script (boot, help, ping, the busy-window three-shot burst, two bad commands), and here is the original text, not one byte deleted:

```text
[0ms][Info ][boot] ring console ready
[0ms][Info ][boot] type help for commands
> help
[301ms][Info ][help] help, ping, led on, led off, status, echo TEXT, sleep MS
> ping
[304ms][Info ][ping] pong
> sleep 300
[311ms][Info ][sleep] busy 300
[612ms][Info ][sleep] done
> ping
[612ms][Info ][ping] pong
> echo burst-canary-012345678901234567890123456789
[612ms][Info ][echo] burst-canary-012345678901234567890123456789
> frobnicate
[1127ms][Warn ][cmd] unknown command: frobnicate
> sleep xyz
[1134ms][Warn ][cmd] not a number: xyz
>
```

Let's go through it section by section. The two boot Log lines and the prompt appear at [0ms]. The help reply lands at [301ms]; the gap in between is the capture script waiting for the PTY to come up and for the prompt, not the firmware's speed. The scouting ping returns pong at [304ms] — the send/receive chain is alive. The three-shot burst after `sleep 300` is the title segment we walked through millisecond by millisecond in the opening. The `frobnicate` warn carries the original text `unknown command: frobnicate`, and the `sleep xyz` warn carries `not a number: xyz` — the evidence of each error sits right there in the log.

The automated check runs `check_ring_renode`; underneath it, `check_renode.py` turns the busy-window three-shot burst into assertions — the replies after waking must come in order, and the error logs must carry their tokens. The output of the real run, pasted as-is:

```text
ring console check: PASS (burst survives 300ms busy main loop, in order; errors carry reasons)
```

I measured the firmware sizes as well; set them in one table beside the previous article's 05:

| Firmware | text | data | bss | text+data (flash) | data+bss (RAM) |
|---|---|---|---|---|---|
| 05_uart, blocking | 5476 | 12 | 76 | 5488 | 88 |
| 08_uart_ring, interrupt-driven | 19740 | 304 | 156 | 20044 | 460 |

Where did the extra 14556 bytes of flash and 372 bytes of RAM go? Let's estimate from the increments. The previous article's 05 carried no logging library; 08's `Log` template brings formatting plus a 128-byte buffer, and the variant-and-expected dispatch and `from_chars` are also new parts 05 never had. The biggest share is in the log formatting. The interrupt service and the ring's own code are in fact tiny; the expense is in presenting the information.

## The trade-off: two lists, each for its own setting

Set the two examples side by side in one table:

| | 05 blocking | 08 interrupt-driven |
|---|---|---|
| When bytes are received | Main loop waits on `receive` | Into the ring from the interrupt; main loop `try_pop` |
| What a 300 ms busy main loop costs | ORE goes up, bytes lost | Ring holds them; drained in order on return |
| Shape of the API | `receive` blocks until a byte arrives | `try_pop` returns false immediately when empty |
| The error reply | One generic line: Unknown command. Type help. | warn carrying the offending text |
| Cost of interrupts | none | One ISR entry/exit plus the ring read and write |
| flash (text+data) | 5488 bytes | 20044 bytes |
| RAM (data+bss) | 88 bytes | 460 bytes |
| Where it fits | The console is the main loop's only real job | The main loop has other pressing work to do |

I put my stance plainly. For exercises and toy firmware, 05's blocking is enough: the console is the main loop's only job anyway, the code stays simple, and flash stays cheap. Once the main loop has a second real job, 08's structure pays for itself. Busy for 300 ms, and not one byte lost — those 14556 flash bytes are not spent in vain.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| You type and nothing echoes; the console is entirely silent | `USART1_IRQHandler` is not defined, the startup file's weak symbol falls into the default handler, and no one collects the bytes | Add the `extern "C"` definition following `main.cpp`, with the body handing off to `Rx::irq()` |
| `try_pop` is always false | `Rx::start(3)` was never called, RXNEIE never went up, the interrupt never comes | In the assembly sequence, place `Rx::start` after `Serial::init()` |
| The build fails with `capacity must be a power of two` | The ring's template argument is not a power of two | Pick a power of two for the capacity, such as 64 or 128 |
| Interrupt behavior turns strange after touching the priority grouping | The meaning of `start`'s second argument follows the grouping, and `HAL_Init`'s default is `NVIC_PRIORITYGROUP_4` | Before touching the grouping, read through Who interrupts whom: how NVIC preemption priorities are arranged |
| Two interrupts both push into the ring | The SPSC contract is one producer paired with one consumer | Adding a producer means switching approaches — a critical section or a different queue |
| Answering or delaying inside the interrupt | Calling `Log` or `HAL_Delay` in the ISR stalls both ends of the ring at once | The ISR only collects bytes; answering belongs to the main loop |
| Treating `available()` as an exact remaining margin | The other side's index moves at the same time; the comment words it as a hint rather than a fact — the original line is `treat this as a hint, not a fact` | Use it as a hint, and size the capacity against the worst case of 63 slots |
| Busy longer than 5.5 ms with bytes streaming in without pause | Once the 63 slots fill, `push` returns false and bytes quietly go missing | Shorten the busy stretch, or enlarge the ring (capacity still a power of two) |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="27.6.1 USART_SR (setting and clearing of RXNE/ORE/FE/NE); 27.6.4 USART_CR1 (RXNEIE); Table 63, the vector table"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (examples/08_uart_ring, boards/stm32f1/uart_irq.hpp, base/spsc_queue.hpp)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="UartRx's start and irq, SpscQueue's single-writer indices and the one-spare-slot design, command parsing with expected plus variant"
  />
</ReferenceCard>
