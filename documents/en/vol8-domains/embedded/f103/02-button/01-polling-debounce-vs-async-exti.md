---
title: 'One burst of bounce, two chains: polling debounce vs. the EXTI edge chain'
description: 'The 117-line main of examples/07_button_events wires one and the same PA0 into two chains: the polling debounce chain samples every 20 ms with Debouncer<20,3> and confirms on three consecutive agreeing reads, while ButtonFsm<700> turns the stable state into Press/LongPress/Release variant events for std::visit to dispatch. The EXTI edge chain grabs HAL_GetTick inside the rising-edge interrupt and pushes it into an SpscQueue<uint32_t,8>, and the main loop drains the queue to log one edge line apiece. Renode injects bounce of three rises and three falls, the head-to-head run shows 3 edge lines against 1 press, and why EXTI_PR clears on writing 1, where the EXTI0 vector belongs, and what backs the SPSC lock-free queue all land in the closing account of costs and trade-offs on each chain'
chapter: 2
order: 1
tags:
  - stm32f1
  - intermediate
  - 状态机
  - variant
  - 无锁
  - 嵌入式
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - 'Putting configuration into types: the Gpio template and compile-time verification'
related:
  - 'Putting the logger to work: two calling styles and two viewing surfaces'
translation:
  source: documents/vol8-domains/embedded/f103/02-button/01-polling-debounce-vs-async-exti.md
  source_hash: 0a8fc08b3e39077430fa04b6b999a495d12461d0c996863ff5be00f341e46b31
  translated_at: '2026-11-01T03:18:31+00:00'
  engine: anthropic
  token_count: 4000
---

# One burst of bounce, two chains: polling debounce vs. the EXTI edge chain

## The opening: one touch, two ways to report it

We drew the v1 boundary in [Putting the logger to work: two calling styles and two viewing surfaces](../06-patterns/07-logger-dual-syntax-usage.md): the logger only accepts calls from thread context and is disabled inside interrupt service routines. Next up comes the first component on the input side: the button. For material I picked libestdx's `examples/07_button_events`, where one 117-line `main.cpp` packs both chains in.

The input is a TTP223-style capacitive touch module. It ships in momentary mode by default: the idle level is low, and a touch pulls it high. diyusthad's page documents the factory configuration plainly<RefLink :id="3" preview="diyusthad, TTP223 touch switch" />. We let the firmware take three touches in a row inside Renode; the log stream below was captured from a real run, and not one byte of it has been touched up:

```text
[0ms][Info ][boot] button events ready
[0ms][Info ][edge] 0
[60ms][Info ][press] 60
[360ms][Info ][release] 360
[1727ms][Info ][edge] 1727
[1780ms][Info ][press] 1780
[2480ms][Info ][long] 2480
[2780ms][Info ][release] 2780
[4876ms][Info ][edge] 4876
[4896ms][Info ][edge] 4896
[4916ms][Info ][edge] 4916
[4920ms][Info ][press] 4920
[5620ms][Info ][long] 5620
[5860ms][Info ][release] 5860
```

Let's narrow the view to the last six lines. In the bounce round, `edge` arrived three times in a row (4876, 4896, 4916), while `press` came exactly once (4920). The same burst of bounce — one chain reported it three times, the other accepted it only once. Why?

The verdict has to wait until we have walked both chains. In the header comment of `renode.resc`, the library author gave the two chains their names: the debouncing poller is Act One, the EXTI recorder is Act Two. This article follows the two acts in order — assembly, the mechanism of each chain, the head-to-head experiment — with the trade-offs saved for last.

## Assembly: one PA0, two chains hung on it at once

Let's go through the hardware piece by piece. The input pin is PA0; the alias `KeyPin` is a `Gpio<GpioPort::A, GPIO_PIN_0, GpioDirection::Input, GpioPull::Down>`, whose internal pull-down clamps the idle level low. The module reads high on touch, so the button's polarity alias is `ActiveHigh`.

```cpp
using LedPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Output>;
using Led = estdx::device::LED<LedPin, estdx::gpio::GpioPolarity::ActiveLow>;

using KeyPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::A, GPIO_PIN_0,
                                    estdx::gpio::GpioDirection::Input, estdx::gpio::GpioPull::Down>;
using Key = estdx::device::Button<KeyPin, estdx::gpio::GpioPolarity::ActiveHigh>;

using Serial = estdx::stm32f1::Uart<estdx::stm32f1::UartInstance::Usart1>;
using Log = estdx::logger::Log<estdx::logger::UartLogSink<Serial>, estdx::logger::LogLevel::Info,
                               128, estdx::stm32f1::HalTickClock>;
static_assert(estdx::logger::LogSink<estdx::logger::UartLogSink<Serial>>);
static_assert(estdx::logger::LogClock<estdx::stm32f1::HalTickClock>);

using KeyDebouncer = estdx::device::Debouncer<20, 3>;
using KeyFsm = estdx::device::ButtonFsm<700>;
using EdgeQueue = estdx::base::SpscQueue<std::uint32_t, 8>;

static_assert(estdx::base::StateMachine<KeyFsm>);
```

The LED is still the PC13 one that lights on low — the same lamp the getting-started station used. Where the `Gpio` template comes from is told in [Putting configuration into types: the Gpio template and compile-time verification](../01-led/08-gpio-template.md); here we treat it as a familiar face. The `Log` alias is letter-for-letter the one from the logger series, and we will not repeat the anatomy of the line format — for the details, turn to [Putting the logger to work: two calling styles and two viewing surfaces](../06-patterns/07-logger-dual-syntax-usage.md). The genuinely new faces are the last few lines: the debouncer, the state machine, the queue, plus one static contract checking the state machine.

EXTI is the external-interrupt controller: it watches a pin for edges, and the moment an edge arrives, the interrupt goes out. Its assembly is likewise packed into aliases. `OnTouch` is the little bit of work to be done inside the interrupt; KeyEvent binds the pin, the edge, and the handler into one type; and the 2 in `init(2)` is the preemption priority handed to the NVIC — the interrupt controller inside the Cortex-M core, which ranks interrupts by urgency. The vector function is pasted below too; it is three lines in full:

```cpp
struct OnTouch {
    static void operator()() { edge_queue.push(HAL_GetTick()); }
};
using KeyEvent = estdx::stm32f1::Exti<KeyPin, estdx::stm32f1::ExtiEdge::Rising,
                                      OnTouch>;

extern "C" void EXTI0_IRQHandler() {
    KeyEvent::irq();
}
```

Why doesn't this vector function live in `stm32f1xx_it.c`? The template project's `it.c` answers with a single comment: "the EXTI0 vector is defined in main.cpp (KeyEvent::irq is visible only to a C++ TU)." `SysTick_Handler` did stay in `it.c`. Translated into plain words: `KeyEvent::irq` is a static function of a template, and only a translation unit that can see the C++ type can reach it (TU is short for translation unit). It cannot be placed in the HAL's C files, so the vector moved into `main.cpp` along with it.

## Act One: the polling debounce chain, where a press takes three agreeing samples

The main loop is the entire stage of Act One; here it is, lifted verbatim:

```cpp
for (;;) {
    std::uint32_t edge_ms = 0;
    while (edge_queue.pop(edge_ms)) {
        Log::info(estdx::logger::Tag("edge"), edge_ms);
    }

    const std::uint32_t now = HAL_GetTick();
    debouncer.update(Key::is_pressed(), now);
    if (const auto event = fsm.feed(debouncer.stable(), now)) {
        report(*event, now);
    }
}
```

Every pass of Act One is the same three steps: the raw level that was read goes to the debouncer, the debounced stable state feeds the state machine, and whatever event comes out goes to `report` for dispatch. The dispatch code is one `std::visit` block; that comes over as well:

```cpp
void report(const estdx::device::ButtonEvent& event, std::uint32_t now_ms) {
    std::visit(
        [&](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, estdx::device::Press>) {
                Log::info(estdx::logger::Tag("press"), now_ms);
                Led::on();
            } else if constexpr (std::is_same_v<E, estdx::device::LongPress>) {
                Log::info(estdx::logger::Tag("long"), now_ms);
            } else {
                Log::info(estdx::logger::Tag("release"), now_ms);
                Led::off();
            }
        },
        event);
}
```

Lighting the lamp is press's job and extinguishing it is release's, while long shows up only in the log. We now see all three events plainly: they are empty tag types, the branch decision is pressed entirely into compile time, and this is exactly what the everyday variant-plus-`if constexpr` idiom looks like.

### Debouncer: sample when due, count when agreeing

The two parameters of `Debouncer<20, 3>` are a 20 ms sampling interval and an agreement threshold of 3. Let's read the algorithm off the source:

```cpp
bool update(bool raw, std::uint32_t now_ms) {
    if (now_ms - last_sample_ms_ < INTERVAL_MS) {
        return false;
    }
    last_sample_ms_ = now_ms;

    if (raw == candidate_) {
        if (count_ < STABLE_COUNT) {
            ++count_;
        }
    } else {
        candidate_ = raw;
        count_ = 1;
    }

    if (count_ >= STABLE_COUNT && candidate_ != stable_) {
        stable_ = candidate_;
        return true;
    }
    return false;
}
```

Its approach is "periodic sampling plus consecutive-agreement confirmation": no sample before 20 ms have passed; when a sample matches the candidate, the count climbs; when it does not, the candidate is replaced and the count resets to 1; and only when the count reaches 3 and the candidate differs from the stable state does the stable state flip. What it guards is a threshold of agreement: three samples, 20 ms apart each, all reading the same — only then does this gate let us through.

The constructor hides one small mechanism: `last_sample_ms_` starts at `0 - INTERVAL_MS`, and the header comment puts it plainly: "the initial value makes the first update due immediately: 0 - (0 - INTERVAL) = INTERVAL, no less than the threshold." Under unsigned subtraction semantics, the first `update` at time 0 counts as due, and the wraparound subtraction is handled along the way.

Why 20 and 3 for the parameters? The input module is capacitive-touch, carries no metal contacts, and every bit of bounce in this article was injected by Renode. Ganssle's measurements were taken on mechanical switches, which makes them a fine conservative baseline for choosing parameters: if the numbers cover the worst grade of mechanical switch, they cover injected bounce. Most switches settle within 10 ms<RefLink :id="2" preview="Jack Ganssle, A Guide to Debouncing, Part 2" />, a 50 ms response delay is imperceptible to the human hand, and the average sits around 1.6 ms<RefLink :id="5" preview="Jack Ganssle, A Guide to Debouncing, Part 1" />. A 20 ms sampling interval lands after the bounce has died down, and a 60 ms confirmation window sits within the human perception budget — the supporting evidence is in place on both ends.

### ButtonFsm: boolean stable state in, variant events out

Flipping the stable state is `ButtonFsm<700>`'s job. Here are the three outward-facing messages, with the comments the library author gave them:

```cpp
struct Press {};     // kids, I'm pressed down
struct Release {};   // kids, I've let go
struct LongPress {}; // kids, I'm being held
using ButtonEvent = std::variant<Press, Release, LongPress>;
```

The empty structs serve as tags, and what the variant holds is "what happened", with no extra payload. Inside, the state machine speaks a vocabulary of its own. The internal events number three: Pressed, Released, Tick. So do the states: Idle, Down, LongDown — and Down carries a `since_ms` with it. The job of `feed` is to translate the boolean stable state into internal events: a change sends Pressed or Released; no change sends Tick.

```cpp
struct Idle {
    Transition on(const Pressed& e) const { return Transition{Down{e.now_ms}, Press{}}; }
};

struct Down {
    std::uint32_t since_ms;
    Transition on(const Tick& t) const {
        if (t.now_ms - since_ms >= LONG_PRESS_MS) {
            return Transition{LongDown{}, LongPress{}};
        }
        return Transition{*this};
    }
    Transition on(const Released&) const { return Transition{Idle{}, Release{}}; }
};

struct LongDown {
    Transition on(const Released&) const { return Transition{Idle{}, Release{}}; }
};
```

Let's read the transition rules once through: when Idle receives Pressed it enters Down, and what gets reported is Press. Each Tick inside Down checks the clock against `since_ms`; once past 700 ms it promotes itself to LongDown and reports LongPress. When either Down or LongDown receives Released, both return to Idle, and what gets reported is Release. The long-press judgment is passive: the state machine keeps no time of its own and relies entirely on Tick after Tick from outside to check the clock.

The chassis is `base::VariantStateMachine`: inside `handle` we can see two `std::visit` calls, one visiting the event and the other the current state; only when `if constexpr` matches `HandlesEvent` does it call `on` to make the transition, and a combination with no match returns `nullopt` and stays in the original state. The first state must be default-constructible — that is exactly what the `static_assert` in the chassis is waiting to check. We carry this static contract in two places: one copy in `main.cpp`, squeezed in among the aliases, checking `KeyFsm`; one at the end of `button.hpp`, checking `ButtonFsm<700>`. Both spots check the same contract. The library author's closing comment reads: "just stashing this here; a Button really is a classic state machine."

## Act Two: the EXTI edge chain, where the interrupt records nothing but a timestamp

Act One's three components all run inside the main loop; Act Two moves half the work into the interrupt. String the chain together starting from the touch edge: a rising edge fires the EXTI0 interrupt, `EXTI0_IRQHandler` calls `KeyEvent::irq()`, and the order inside `irq()` is fixed — only after the pending bit is cleared does OnTouch get its turn. `OnTouch` pushes a snapshot of `HAL_GetTick()` into the queue, and the main loop then drains the queue and logs one edge line apiece.

One place where the two chains mesh deserves spelling out: why doesn't `OnTouch` just call `Log::info`? Because logger v1 only accepts thread context and is disabled inside interrupts — we listed that boundary in the logger-usage article. So the interrupt does the lightest possible work: grab one `HAL_GetTick()` snapshot and push it into the queue. Formatting and pushing bytes out the UART are all left to the main loop. The payload of an edge line is the moment captured inside the ISR (interrupt service routine), while the line prefix is the moment the main loop prints it. Right now the two numbers are equal only because the main loop spins fast; once the main loop gets busy, the two numbers drift apart. That is precisely one of the reasons for pushing the timestamp from the ISR and leaving the logging to the main loop.

The drop policy deserves a word too: the return value of `push` is ignored inside `OnTouch`, and when the queue is full the new edge is simply dropped. That is a deliberate simplification: with a capacity-8 queue and at most 3 injected edges in the checks, the tempo of the example never comes close to filling it. When you do start caring, the `push` comment already has the words ready: `the caller owns the overflow policy (drop, count, overwrite — but never block in an ISR)` — the drop policy belongs to the caller, and an ISR never blocks.

### The Exti template: from claiming a line number to NVIC setup

The `Exti` template takes three parameters: the input pin, the edge, and the handler. The handler must satisfy the `irq::IrqHandler` requirement — providing a static `operator()` is enough to pass. We also looked into where this concept came from: on 2026-10-03 it was promoted from exti's own ExtiHandler into a shared concept, and exti naturally became its first user.

Let's walk `init` in order: claim the line number, configure the pin, turn on the clock for AFIO (the alternate-function IO module, which owns the EXTI-line-to-port mapping), write the port into EXTICR, set RTSR/FTSR for the edge, enable the interrupt in IMR, clear leftover pending bits in PR, and finally set the NVIC preemption priority and enable it. The line-claiming step carries a guard against duplicate claims: when a second template declares the same EXTI line, `claim_line` hands out `__builtin_trap()`.

```cpp
static void init(std::uint32_t preempt) {
    claim_line();

    Pin::init();

    __HAL_RCC_AFIO_CLK_ENABLE();
    map_source();

    if constexpr (EDGE == ExtiEdge::Rising || EDGE == ExtiEdge::Both) {
        EXTI->RTSR |= Pin::mask;
    }
    if constexpr (EDGE == ExtiEdge::Falling || EDGE == ExtiEdge::Both) {
        EXTI->FTSR |= Pin::mask;
    }
    EXTI->IMR |= Pin::mask;
    EXTI->PR = Pin::mask;

    const auto irq = irq_number();
    HAL_NVIC_SetPriority(irq, preempt, 0);
    HAL_NVIC_EnableIRQ(irq);
}

static void irq() {
    if (EXTI->PR & Pin::mask) {
        EXTI->PR = Pin::mask;
        Handler::operator()();
    }
}
```

The EXTICR mapping is a fill-in-the-blank: in register `EXTICR[line/4]`, the 4 bits starting at `(line%4)*4` hold the number of the port. PA0's line number is 0 — let's run it once: it lands in the lowest 4 bits of `EXTICR[0]`, and the value written is the number of port A.

PR's semantics are the single most worth-remembering item of Act Two, and we checked it against the original text in §10.3.6 of RM0008 (ST's STM32F1 reference manual)<RefLink :id="1" preview="STMicroelectronics, RM0008" />: writing 1 to PR is the clearing action, and writing 0 does nothing at all. Inside `irq()`, the PR clear sits before the handler runs: if another edge arrives while the work is in progress, PR gets set again, and after the interrupt exits the new edge will not be missed. That is exactly why the clear comes before the work.

The interrupt-number mapping has a ready-made table too: line numbers 0 through 4 each get their own dedicated vector, 5 through 9 share EXTI9_5, and 10 through 15 share EXTI15_10. PA0's line number is 0, so its vector is EXTI0 — the very `EXTI0_IRQHandler` we saw earlier. One entry for the vocabulary as well: the `ExtiEdge` enum also has a Both option; both-edge triggering is not used in this article.

### SpscQueue: the case for a lock-free queue

Let's go through the queue's contract as the header comment states it. The heart of the contract is single-producer single-consumer (SPSC): exactly one caller of `push` (the ISR) and exactly one caller of `pop` (the main loop) — one more of either voids the deal. Storage is a ring array of N slots, N must be a power of two, and capacity is reckoned as N-1: the queue counts as full when head has caught up to the slot before tail, and as empty when head equals tail; one wasted slot buys an unambiguous distinction between empty and full. Write ownership of the two indices is split: writes to head belong to the producer, writes to tail belong to the consumer, and both indices are volatile.

```cpp
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
```

The confidence behind going lock-free with no atomics is in the comments too: a single-core Cortex-M3 has no data cache, each index has exactly one writer, and 32-bit reads and writes on natural boundaries are a single bus access, so half a value can never be read. Even if a reader sees a stale index, the worst that happens is a short stretch of misjudging empty versus full — reading half an element is categorically impossible, because the write order is: the element goes in first, and only then does the index move. What volatile stops is the compiler caching the index in a register; memory barriers are beyond it, and here we do not need them anyway.

## The head-to-head run: bounce injected, each chain gives its own account

All the injection work lives in `renode.resc`. The platform description file is `sim/stm32f1/bluepill.repl`; logs go through a PTY (pseudo-terminal) on USART1 with the default path `/tmp/libestdx-button`; the question-marked `$bin?=` / `$uart_pty?=` style is the same one as the logger series and means an overridable default. The model knows nothing about the pin's pull configuration, so the reset macro pulls PA0 low ahead of time, imitating the module's idle-low level. The three injection macros, lifted from its header comment:

```text
runMacro $press    # a clean touch (hold for 0.3s, then release)
runMacro $long     # hold for 1s (triggers long press), then release
runMacro $bounce   # touch with bounce (3 rising edges) → hold 0.9s → release
```

On timing discipline, the resc header comment puts it this way:

```text
monitor sleep waits on host time and is out of proportion with virtual time —
injection macros always advance precisely via pause → OnGPIO → emulation
RunFor "<virtual seconds>".
```

Read the three macros' logs against the timeline:

| Injection | What it does | Log lines (times in ms) | Numbers worth noting |
|---|---|---|---|
| `$press` | a clean touch, released after 0.3 s | edge 0 / press 60 / release 360 | press lands 60 ms after the touch — exactly the 20×3 confirmation window |
| `$long` | held for 1 s, then released | edge 1727 / press 1780 / long 2480 / release 2780 | 2480 = 1780 + 700; the long-press judgment is not off by a hair |
| `$bounce` | three rises and falls at 10 ms, then held for 0.9 s | edge 4876 / 4896 / 4916 / press 4920 / long 5620 / release 5860 | 3 edge lines against 1 press; 5620 = 4920 + 700 |

The bounce round deserves its own telling. `$bounce` works by three rises and falls at 10 ms intervals, with the final move holding the touch for 0.9 s before releasing; the comment in the resc file states the expectation in plain terms: "Act Two should report 3 edge lines; Act One accepts only 1 press (the 20ms×3 agreement window filters the glitches)." The captured log walks exactly that path: the `edge` lines came three in a row, at 4876, 4896, 4916; `press` came exactly once (4920); and long and release arrived on schedule too — noted down as 5620 and 5860.

Why does Act One accept only one? Look at the width of the dips: each of the two low-level dips lasts a mere 10 ms — less than the spacing between two sample points. Even if a sample happens to land inside a dip, at most one reading is polluted, three consecutive agreements cannot be mustered, and the stable state never flips. That is precisely what the resc comment's "agreement window filters the glitches" is about.

Verification is handed to `check_renode.py`. For the `$bounce` run, the assertion list is: exactly 3 edge lines, exactly 1 each of press/long/release, ordering asserted as press ≤ long < release, the long-press interval falling inside a 600 to 1000 ms window, and release arriving within 500 ms after long. For the `$press` run, the list becomes 1 edge line, 1 press, 1 release — and long is forbidden to appear. The command and that day's real run output, pasted together:

```sh
cmake --build build --target check_button_renode
```

```text
button events check: PASS (3 edges vs 1 debounced press; long/release ordered)
```

That day's verification all ran against the same submodule pinned at commit 8026841, regressions for the logger example included, and everything passed. The firmware measures text 16016, data 168, bss 136 — copied down as-is. The simulation target is called `sim_button_events` and the flash target `flash_button_events`; the latter burns the same firmware onto a physical board.

## The trade-off: EXTI got the timestamp but cannot tell bounce from a real press

The two chains' temperaments, compared side by side:

| | Act One (the polling debounce chain) | Act Two (the EXTI edge chain) |
|---|---|---|
| Semantics | Confirmation: everything reported is stable | Recording: every edge is logged, genuine or not |
| Latency | Carries a fixed 60 ms confirmation window | The timestamp hugs the edge; nothing is lost even when the main loop sleeps |
| Cost | The main loop must keep spinning; every pass samples and feeds the state machine | A wide configuration surface: AFIO, EXTICR, NVIC, the vector — and an ISR can only do short work |
| Debouncing | Built in (sampling plus agreement confirmation) | No ability to tell bounce from a real press |

In my view, the polling chain's strength is clean semantics: the moment press fires, it is the stable state, and time-dimensional plays like long-press are easy to build on it. The costs are just as clear: the 60 ms confirmation delay is fixed, and the main loop can never stop. The EXTI chain's strengths are precise timing and independence from the main loop's diligence; its costs are the wide configuration surface, an ISR limited to the shortest of work, and edges dropped once the queue fills.

Act Two holds nothing but edge events — there is no time dimension in its hand, so it cannot tell bounce from a real press; the contrast of 3 edge lines against 1 press is right there. Telling them apart means adding the time dimension, and that dimension we hand to TIM (the timer): the article on the time theme picks it up right at its start.

## Pitfalls and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| `__builtin_trap()` fires right at power-on | A second `Exti` declared the same EXTI line — exactly what `claim_line` guards against | Assemble only one `Exti` per line |
| The linker complains about a duplicate `EXTI0_IRQHandler` | The vector function is already defined in `main.cpp`, and `it.c` holds another copy | Keep only the comment in `it.c`; the vector belongs to `main.cpp` |
| Fewer edge lines than injected edges | `push` is ignored when the queue is full; new edges get dropped | Plan capacity against the `available()` reference value, and keep the injection tempo sparse |
| edge and press timestamps do not line up | The line prefix is the main loop's print time; the payload is what the ISR captured | For the moment of the edge, read the payload number |
| press always lags the touch by 60 ms | The 20ms×3 confirmation window is latency by design | To go faster, tune the two `Debouncer` parameters — never below the bounce settling time |
| Calling `Log::info` in an interrupt produces no output | logger v1 is thread-context only; disabled inside an ISR | Follow `OnTouch`: push to the queue, log from the main loop |
| Logs come out scrambled after a `sleep` injection in Renode | monitor `sleep` waits on host time, out of proportion with virtual time | Always inject as `pause` followed by `RunFor` in virtual seconds |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="10.3.6 EXTI pending bit; 10 Interrupts and events"
  />
  <ReferenceItem
    :id="2"
    author="Jack Ganssle"
    title="A Guide to Debouncing, Part 2"
    url="https://www.ganssle.com/debouncing-pt2.htm"
    chapter="Switch bounce measurements: most contacts settle within 10ms"
  />
  <ReferenceItem
    :id="3"
    author="diyusthad"
    title="How to use a TTP223 based touch switch"
    :year="2019"
    url="https://diyusthad.com/2019/06/how-to-use-ttp223-based-touch-switch.html"
    chapter="TTP223 factory default momentary mode, idle low / touch high"
  />
  <ReferenceItem
    :id="4"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (examples/07_button_events and the device/base/boards modules)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="Debouncer, ButtonFsm, Exti, SpscQueue"
  />
  <ReferenceItem
    :id="5"
    author="Jack Ganssle"
    title="A Guide to Debouncing, Part 1"
    url="https://www.ganssle.com/debouncing.htm"
    chapter="16 switches averaged 1557 µsec of bouncing (max 6200 µsec)"
  />
</ReferenceCard>
