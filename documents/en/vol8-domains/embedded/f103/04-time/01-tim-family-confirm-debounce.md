---
title: 'The TIM family and confirmation debouncing: from a 1Hz heartbeat to a 20ms confirmation window'
description: 'The 302 lines of tim.hpp wrap TIM into a trio: TimBase for periodic beats, TimOneShot for one-shot windows, TimPwm for duty-cycle output, with configuration passed as designated initializers straight into non-type template parameters. A consteval solver resolves PSC and ARR from timclk and the period: exact divisors searched upward from 1, a remainder-absorbing fallback, and a 0.1% precision gate — at 64MHz all three instances (1Hz, 20ms, 10ms) land on ARR 63999. The APB1 ×2 rule behind timclk, the alignment patch that moved the bluepill.repl timer frequency from 10MHz to 64MHz, and the spurious interrupt from EGR.UG setting UIF on load (with the code order that defends against it) are folded into two examples: 09_tim_heartbeat runs a 1Hz heartbeat on a WFI main loop, and 10_button_confirm uses an EXTI edge to arm a 20ms one-shot confirmation window where bounce defers the window and glitches land empty — confirm events and the 10ms slow tick feed ButtonFsm together, same machine, new sources'
chapter: 4
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
  - 寄存器
  - 状态机
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 18
prerequisites:
  - 'One burst of bounce, two chains: polling debounce head-to-head with the EXTI edge'
related:
  - 'Putting the logger to work: two ways to write it and two surfaces to read it'
translation:
  source: documents/vol8-domains/embedded/f103/04-time/01-tim-family-confirm-debounce.md
  source_hash: 5426537b92d9302181a495ef90e67af037d8f2b1642880e3bc746591cc696eb7
  translated_at: '2026-11-01T03:20:13+00:00'
  engine: anthropic
  token_count: 3600
---

# The TIM family and confirmation debouncing: from a 1Hz heartbeat to a 20ms confirmation window

## Introduction: the time dimension the last piece left behind, handed to TIM now

At the close of the last piece we left one shortcoming of act two sitting on the table: the EXTI chain in [One burst of bounce, two chains: polling debounce head-to-head with the EXTI edge](../02-button/01-polling-debounce-vs-async-exti.md) had timestamps precise to the millisecond, yet it could not tell bounce from a genuine press — and telling them apart takes the time dimension. The hardware that supplies it is this piece's protagonist, TIM. On the F103, TIM1 through TIM4 each carry their own counter, prescaler, and interrupt vector, and from this piece on, time can move on its own instead of tracking how fast the main loop spins.

The material comes from the submodule pinned at commit 8026841, and I have run both examples: `examples/09_tim_heartbeat` and `examples/10_button_confirm`. Let's lay the two captured logs side by side — one governs the period, the other governs confirmation.

```text
[0ms][Info ][boot] heartbeat ready
[1000ms][Info ][beat] 1
[2000ms][Info ][beat] 2
[3000ms][Info ][beat] 3
[4000ms][Info ][beat] 4
[5000ms][Info ][beat] 5
```

```text
[0ms][Info ][boot] button confirm ready
[21ms][Info ][touch] 20
[21ms][Info ][press] 20
[321ms][Info ][leave] 321
[321ms][Info ][release] 321
[3169ms][Info ][touch] 3169
[3169ms][Info ][press] 3169
[3870ms][Info ][long] 3870
[4069ms][Info ][leave] 4069
[4069ms][Info ][release] 4069
```

The rhythm of the first log we can count out ourselves: the five heartbeat beats are spaced exactly 1000ms apart, the main loop sleeps in WFI (the wait-for-interrupt sleep instruction) without dropping a single beat, and all of the time comes from the timer's interrupts. The interesting part of the second log is the touch lines: the injected bounce was four level flips at 10ms intervals — two full bounce cycles — yet the log acknowledges only one touch and one press. Each flip pushes the 20ms confirmation window back yet again, and only when the level finally settles does the window come due.

This piece walks down the 302 lines of `tim.hpp`: the family roster and configuration written into types, how the consteval solver resolves the period we ask for into a divisor pair of prescaler PSC and auto-reload ARR, and which branch of the clock tree `timclk` comes from. In the middle there is a spurious UIF (the update interrupt flag, one bit in the status register) whose trick deserves a section of its own; the order of loading and flag-clearing is the defense against it. The close is the full chain of both examples — 09 handles the heartbeat, 10 handles confirmation, and the confirmation window in 10 repays exactly the dimension the last piece owed.

## The family roster: the trio, and configuration written into the type

The entrance to the family is an enum whose members take their values from addresses; let's count off the four members one by one, each name paired with a peripheral's base address:

```cpp
enum class TimInstance : std::uintptr_t {
    Tim1 = TIM1_BASE,
    Tim2 = TIM2_BASE,
    Tim3 = TIM3_BASE,
    Tim4 = TIM4_BASE,
};
```

One aside on the interrupt vectors while we're here: TIM1's update interrupt goes through `TIM1_UP_IRQn`, while the other three use their own `TIMx_IRQn`. That difference is wrapped up inside `TimHardware`'s `irq_number()`, so whoever wires the thing up doesn't have to memorize it.

Let's also bring out the configuration structs — just two thin ones:

```cpp
struct TimConfig {
    TimInstance instance;
    std::uint32_t period_us;
    std::uint32_t timclk_hz = 64'000'000;
};

struct TimPwmConfig {
    TimInstance instance;
    TimChannel channel;
    std::uint32_t freq_hz;
    std::uint32_t timclk_hz = 64'000'000;
};
```

C++20 admitted structs into the ranks of non-type template parameters, and the library author pushed that feature all the way: designated initializers now sit directly inside the template argument. Let's see what 09's wiring looks like:

```cpp
using Heartbeat =
    estdx::stm32f1::TimBase<{.instance = estdx::stm32f1::TimInstance::Tim2, .period_us = 1'000'000},
                            OnBeat>;
```

`period_us` is in microseconds — one glance and you can match it to the period. `timclk_hz` carries a default of 64MHz; why that value in particular, we'll explain in the clock section. Here is how the trio divides the work:

| Template | Purpose | Once it fires |
|---|---|---|
| `TimBase` | Periodic beat — the 1Hz heartbeat in 09 | UIF arrives periodically, and the counter keeps running |
| `TimOneShot` | One-shot window — the 20ms confirmation in 10 | OPM single-pulse mode stops itself after counting out; UIF fires only once |
| `TimPwm` | Duty-cycle output — the breathing LED is left for a later piece | CCR holds up the waveform; no relation to the update interrupt |

`TimBase`'s `init()` sequence, read straight off the source: enable the clock, write PSC and ARR, load with `EGR = TIM_EGR_UG`, enable the update interrupt with `DIER = UIE`, clear the flag with `SR = ~UIF`, set the counter running with `CR1 = CEN`, and finally configure the NVIC preemption priority.

`TimOneShot`'s `init()` runs the other way around: its very first line, `CR1 = 0`, halts the counter, and the real start moves into `arm()` — four steps laid out: set OPM, load, clear UIF, set CEN. The hardware stops itself once it has counted out one period. `disarm()` clears both OPM and CEN, so a window that hasn't finished counting can still be retracted.

As for `TimPwm`, this piece only points it out as a name on the family roster: it runs PWM mode 1 (`OCxM_2 | OCxM_1`), with four channels to choose from; the pin mapping comes from the consteval `pin_map()` — TIM2's CH1 lands on PA0, TIM3's on PA6, TIM4's on PB6, and TIM1's on PA8, all configured as alternate-function push-pull outputs. `set_duty` writes the compare register CCR as a percentage, and anything past 100 clamps to 100. The breathing LED hands-on work is left for a later piece.

The contract on the processor side is the same old one we met in the last piece. The `IrqHandler` concept shared with Exti lives in `irq_base.hpp`; let's bring the whole thing over:

```cpp
template <typename H>
concept IrqHandler = requires {
    H::operator()();
};
```

We covered its background last time: on 2026-10-03 it was promoted from exti's own `ExtiHandler` into a shared concept, with exti as its first user and `tim.hpp` as its second. Any type that provides a static `operator()` passes, so the `Handler` parameter slot of the two templates looks exactly alike.

## The consteval solver: from a period to PSC and ARR

The PSC and ARR registers are 16 bits each, so the prescale value and the count are both squeezed under 65536, and only two 16-bit factors multiplied together give the largest tick count the timer can reach. The solver translates the period we ask for into values for PSC and ARR, and the whole job is done inside consteval:

```cpp
consteval std::uint64_t ticks_for_period(std::uint64_t clk_hz, std::uint64_t period_us) {
    return (clk_hz * period_us + 500'000) / 1'000'000;
}

consteval TimDivisors resolve_ticks(std::uint64_t ticks) {
    if (ticks < 1 || ticks > 65'536ull * 65'536ull) {
        return {0, 0, false};
    }
    for (std::uint64_t div = 1; div <= 65'536; ++div) {
        if (ticks % div == 0 && ticks / div <= 65'536) {
            return {static_cast<std::uint32_t>(div - 1),
                    static_cast<std::uint32_t>(ticks / div - 1), true};
        }
    }
    const std::uint64_t div = (ticks + 65'535) / 65'536;
    return {static_cast<std::uint32_t>(div - 1), static_cast<std::uint32_t>(ticks / div - 1), true};
}
```

`ticks_for_period`'s job is folding a period into a tick count; half a million is added before dividing, so the landing point rounds to the nearest microsecond. As for how `resolve_ticks` searches: climbing upward from 1, it looks for an exact divisor — the first `div` that divides `ticks` evenly and whose quotient still fits in 16 bits is the answer, with PSC recording div−1 and ARR recording quotient−1. When no exact pair exists there is, of course, a fallback: the fallback takes `ceil(ticks/65536)` as its div, at the price of carrying the remainder as loss.

Next come the two guarding `static_assert`s, brought over verbatim:

```cpp
static_assert(kDiv.ok, "period below resolution or beyond 16-bit PSC*ARR at this clock");
static_assert(detail::relative_error_ppm(detail::ticks_for_period(CFG.timclk_hz, CFG.period_us),
                                          kDiv) <= 1'000,
              "period misses 0.1% precision at this clock");
```

The first guards the range: if the period or frequency exceeds what 16 bits times 16 bits can cover, compilation fails on the spot, and the error message spells out the reason for the failure. The second is the 0.1% precision gate: it computes the relative error (in ppm) between the configured period and the target, and anything over 1000 does not pass. As for when this gate would genuinely be crossed — we worked that out in [The edge of the range: how long a period 16 bits times 16 bits can cover](03-period-range-boundary.md), so we won't unfold it here. Misfilled parameters are stopped at compile time, and the odd-rhythm bug never even gets seen.

For the three instances in this piece, I compiled the very same algorithm once more with `g++ -std=c++23`; the resolved values are listed below:

| Instance | Period (µs) | Tick count | PSC | ARR |
|---|---|---|---|---|
| 09's 1Hz heartbeat | 1'000'000 | 64'000'000 | 999 | 63999 |
| 10's 20ms confirmation window | 20'000 | 1'280'000 | 19 | 63999 |
| 10's 10ms slow tick | 10'000 | 640'000 | 9 | 63999 |

Three different periods, yet the ARR value is 63999 in every one — let's take this coincidence apart. At 64MHz, every 1ms is exactly 64000 ticks, and all three periods land on whole-millisecond marks, so the first usable factor the solver finds climbing from 1 happens to be the number of whole milliseconds inside the period: 1Hz contains 1000 milliseconds, so PSC is 999. 20ms contains 20 milliseconds, so PSC is 19. 10ms contains 10, and PSC lands on 9. The quotient falling into ARR is 64000 every time, so ARR is naturally 63999 every time too. The way to read this pair is as an arithmetic decomposition: PSC plus 1 is the number of whole milliseconds in the period, ARR plus 1 is the 64000 input ticks inside 1ms, and multiplied together they give the period's total tick count — quite a handy thing to keep around for mental verification.

## Where timclk comes from: the APB1 ×2 rule

The 64MHz default in `TimConfig` was not written offhand — it comes from the `SystemClock_Config` shared by both examples. Here is the configuration pulled out, with the arithmetic listed alongside:

```cpp
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
osc.PLL.PLLMUL = RCC_PLL_MUL16;
clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
clk.APB1CLKDivider = RCC_HCLK_DIV2;
```

```text
HSI 8MHz / 2 × 16 → SYSCLK 64MHz → AHB /1 = 64MHz → APB1 /2 = PCLK1 32MHz
```

TIM2 through TIM4 get their clock from APB1, the peripheral bus this batch of timers hangs off — yet their input is not counted against 32MHz. Let's look up the reason in RM0008 (ST's STM32F1 reference manual). The clock tree's account is plain: when the APB1 prescaler is 1, the timer receives PCLK1 itself; when the prescaler is anything other than 1, the timer clock is PCLK1 times 2<RefLink :id="1" preview="STMicroelectronics, RM0008" />. 32 multiplied by 2 gives 64, matching the default `timclk_hz`. The classic 72MHz setup walks the same road: after APB1's divide-by-two, PCLK1 is 36MHz, and the timers receive 72MHz. DigiKey's introductory tutorial explains the ×2 rule very plainly — its example happens to be the APB2 side, but the F1's APB1 side follows the same rule<RefLink :id="2" preview="DigiKey, Getting Started with STM32 Timers" />.

On the simulation side we have one more table to settle. In commits before 8026841, the four timer models in `sim/stm32f1/bluepill.repl` carried a `frequency` of 10MHz while the firmware resolved divisors against 64MHz — so periods inside the simulation were silently wrong: we would see no error anywhere, the log output would look the same as ever, only the tempo would be off, and the only thing that could catch it was the spacing assertion. The patch changes the models' frequency to 64MHz, lining up with the firmware's timclk:

```text
timer2: Timers.STM32_Timer @ sysbus 0x40000000
    -> nvic@28
    frequency: 64000000
```

## The spurious UIF: EGR.UG's side effect, cleared right after the load

This line, `EGR = TIM_EGR_UG`, deserves to be taken out on its own. Its proper business is loading: it triggers a software-issued update event that carries the prescaler and reload values from the preload registers into the shadow registers, so the change takes effect immediately. The side effect is that the update event also sets UIF in passing — RM0008's account of EGR.UG and the update event states that half of the story quite clearly<RefLink :id="1" preview="STMicroelectronics, RM0008" />.

The price of not clearing it is direct: the very first interrupt let through is a false expiry — before the period has counted even once, UIF is already sitting in the status register waiting. Here is `tim.hpp`'s defense, pasted below; the evidence is the order of the code itself:

```cpp
t->PSC = kDiv.psc;
t->ARR = kDiv.arr;
t->EGR = TIM_EGR_UG;
t->DIER = TIM_DIER_UIE;
t->SR = ~TIM_SR_UIF;
t->CR1 = TIM_CR1_CEN;
```

The `SR = ~UIF` that immediately follows `EGR = UG` clears exactly the UIF that loading set in passing. `TimOneShot::arm()` handles it the same way when re-arming — the four steps laid out: set OPM, load via EGR, clear UIF via SR, set CEN via CR1. Any UIF left behind by the previous expiry was already cleared by `irq()`, so what gets cleared here is the new one the reload just raised.

Inside the interrupt, the posture is the same as `exti.hpp`'s, and the flag is likewise cleared before the work — let's look at the code:

```cpp
static void irq() {
    auto* const t = HW::regs();
    if (t->SR & TIM_SR_UIF) {
        t->SR = ~TIM_SR_UIF;
        Handler::operator()();
    }
}
```

We saw this same ordering last piece on EXTI's PR register: an event arriving mid-work re-sets the flag, so nothing is missed after the interrupt exits. UIF runs on exactly the same principle.

## Case 09: a 1Hz heartbeat, with the main loop asleep in WFI

09's first-line comment carries its own point of comparison:

```cpp
// 09_tim_heartbeat: TIM2 1Hz interrupt heartbeat, main loop sleeps in WFI; compare with 01_blinky's HAL_Delay polling.
```

For the wiring, three things to look at: the work to be done inside the interrupt, the heartbeat template, and the vector function. Here is how `OnBeat` and `Heartbeat` are bound; `TIM2_IRQHandler` takes up just one line in `main.cpp`:

```cpp
volatile std::uint32_t beats = 0;

struct OnBeat {
    static void operator()() {
        beats = beats + 1;
        Led::toggle();
    }
};
using Heartbeat =
    estdx::stm32f1::TimBase<{.instance = estdx::stm32f1::TimInstance::Tim2, .period_us = 1'000'000},
                            OnBeat>;

extern "C" void TIM2_IRQHandler() {
    Heartbeat::irq();
}
```

`OnBeat` does only two things inside the interrupt. The first is adding one to the volatile `beats`; the second toggles the LED. Logging is left to the main loop, and the 3 in `Heartbeat::init(3)` is the NVIC preemption priority. Only flags get touched inside the ISR, and logging returns to thread context — the old boundary we already followed in the last piece, whose full wording lives in [Putting the logger to work: two ways to write it and two surfaces to read it](../06-patterns/07-logger-dual-syntax-usage.md).

The main loop, brought over as-is:

```cpp
std::uint32_t reported = 0;
for (;;) {
    __WFI();
    if (beats != reported) {
        reported = beats;
        Log::info(estdx::logger::Tag("beat"), reported);
    }
}
```

`__WFI()` is the Cortex-M sleep instruction: the main loop suspends, an interrupt wakes it, and once the ISR has finished, control returns to the loop and it keeps spinning. The comparison after waking carries a small deliberate touch of its own: it prints only when `beats` and `reported` have come apart. Set against 01_blinky, where HAL_Delay busy-waiting occupied the whole CPU, CPU time is handed back here and not one line of log output is dropped. The millisecond prefix on each log line comes from `HalTickClock`, which wraps `HAL_GetTick` — we covered that in the logger-usage piece, so we won't reopen that battlefield here.

Renode's automation surface this time is simplicity itself: the log PTY hangs at `/tmp/libestdx-heartbeat`, there are no injection macros beyond the reset macro, and time runs free. What the check script does to advance things is pause and then `RunFor "5.2"`; after pushing forward those five-plus seconds we go back to the PTY and count lines — the five-beat heartbeat quoted in the introduction is a real capture. To quote `check_renode.py`'s assertions: the beat numbers are exactly 1 through 5 consecutive, and adjacent spacings fall inside a 900-to-1100ms window. That day's actual run output, pasted as-is:

```text
tim heartbeat check: PASS (1Hz beats under WFI main loop, spacing 1000ms±10%)
```

I put the firmware on the scale with `arm-none-eabi-size`; the readings were text 14160, data 112, bss 84.

## Case 10: a 20ms confirmation window — bounce defers it, glitches come to nothing

10's input is still that PA0 touch module from the last piece: low when idle, high while touched. Three interrupt sources each mind their own job, with the NVIC preemption priorities laid out as Touch=2, Window=3, SlowTick=4.

```cpp
struct OnEdge {
    static void operator()() { Window::arm(); }
};
using Touch = estdx::stm32f1::Exti<KeyPin, estdx::stm32f1::ExtiEdge::Both, OnEdge>;

struct OnConfirm {
    static void operator()() {
        const bool pressed = KeyPin::level();
        if (pressed != confirmed_level) {
            confirmed_level = pressed;
            (void)source_queue.push(Confirmed{pressed, HAL_GetTick()});
        }
    }
};
using Window = estdx::stm32f1::TimOneShot<
    {.instance = estdx::stm32f1::TimInstance::Tim3, .period_us = 20'000}, OnConfirm>;

struct OnTick {
    static void operator()() { (void)source_queue.push(Tick{HAL_GetTick()}); }
};
using SlowTick =
    estdx::stm32f1::TimBase<{.instance = estdx::stm32f1::TimInstance::Tim4, .period_us = 10'000},
                            OnTick>;
```

Let's string the chain together starting from the touch edge. EXTI is configured for both edges, and `OnEdge` does exactly one thing: call `Window::arm()` to restart the 20ms one-shot window. Every bounce edge defers the window, pushing it back until the level finally settles. When the window comes due, `OnConfirm` takes one reading of PA0's level: if it differs from the last confirmation, `confirmed_level` is updated along with it, and what gets pushed into the queue is one `Confirmed`, timestamped with `HAL_GetTick()`. A level that has already sprung back counts as a glitch, and nothing at all enters the queue. TIM4's 10ms slow tick is the third source: `OnTick` pushes only the one kind, `Tick`, supplying the beat that long-press timing checks itself against.

The first-line comment spells out the whole behavioral contract; quoting it as written:

```cpp
// 10_button_confirm: confirmation debouncing (full version) — EXTI edges arm a 20ms one-shot confirm window, one sample on expiry fixes the stable level;
// confirm events + TIM slow tick feed ButtonFsm together (same machine, new sources), main loop WFI throughout.
// Head-to-head with 07: debouncing no longer leans on polling patience, it leans on confirming with time; bounce = the window defers, a glitch = no event.
```

The queue's wiring differs from the last piece in one important way; let's lay it out:

```cpp
struct Confirmed {
    bool pressed;
    std::uint32_t now_ms;
};
struct Tick {
    std::uint32_t now_ms;
};
using SourceEvent = std::variant<Confirmed, Tick>;
using SourceQueue = estdx::base::SpscQueue<SourceEvent, 16>;
```

`push` on `SpscQueue<SourceEvent, 16>` is called from two places — the TIM3 confirmation interrupt and the TIM4 slow-tick interrupt — whereas the last piece had a single producer, EXTI. The contract still holds from the callers' point of view: every `push` call lives in interrupt context, and the only `pop` caller is the main loop, so the two ends never meet. We took the SPSC apart internally last piece; here we only honor the boundary. The queue holds 16, the `push` return value is discarded as before — full means dropped, and the overflow policy belongs to the caller.

The main loop, brought up as well: its first act upon waking is draining the queue, and the two event types are fed in two different ways:

```cpp
for (;;) {
    __WFI();
    SourceEvent event;
    while (source_queue.pop(event)) {
        std::visit(
            [&](const auto& ev) {
                using E = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<E, Confirmed>) {
                    stable_level = ev.pressed;
                    Log::info(estdx::logger::Tag(ev.pressed ? "touch" : "leave"), ev.now_ms);
                    if (const auto out = fsm.feed(ev.pressed, ev.now_ms)) {
                        report(*out, ev.now_ms);
                    }
                } else {
                    if (const auto out = fsm.feed(stable_level, ev.now_ms)) {
                        report(*out, ev.now_ms);
                    }
                }
            },
            event);
    }
}
```

A `Confirmed` that reaches the loop is digested there: `stable_level` is updated right behind it, a touch or leave log follows, and `pressed` is then fed to the state machine. A `Tick` in hand gets fed `stable_level`, and the long-press timing walks to its beat. The state machine is still that `ButtonFsm<700>` we took apart last piece; not a line of `report` has changed — lighting up is press's job, lights-out is release's, and long only prints one log line. What we are watching is merely the source that feeds it: the last piece fed the state machine polling debounce's settled levels, this piece feeds it confirm events plus the slow tick — "same machine, new sources" is the library author's own phrasing.

The injection macros are still the same three: `$press` performs one clean 0.3s touch, `$bounce` is the recipe of four flips at 10ms intervals followed by a 0.9s hold, and `$glitch` is the recipe of a lone 5ms glitch; the log PTY hangs at `/tmp/libestdx-confirm`. Two comments in the resc script are worth quoting as they stand:

```text
# Four flips (2 full bounce cycles) then hold for 0.9s before release: the window defers on every edge, only the final stable level is recognized
# A lone 5ms glitch: by the time the window expires the level has already returned, no event
```

We inject `$press`, `$bounce`, and `$glitch` in turn; the captured stream is the one quoted in the introduction. Now let's take it apart segment by segment.

The clean-touch segment: the touch edge lands at the 0ms mark, the window confirms once it expires, and the payloads of touch and press both sit at 20ms — the check's criterion is that the two differ by no more than 5ms. As for the 1ms between the line prefix's 21 and the payload's 20, that is an old acquaintance of ours: the payload records the moment captured inside the ISR, the prefix records the moment the main loop prints, and as the edge lines in the last piece showed, once the main loop gets busy the two numbers part ways.

Next, the `$bounce` segment: every one of the four flip edges re-arms the window, and the confirmation recognizes only the final settled level, so the touch and press lines both land at 3169. The long-press long lands at 3870 — 3169 plus 700 sums to 3869, but the verdict has to wait for the slow tick after the 700ms line is crossed, hence the extra 1ms. The hand releases after holding 0.9s, and the leave and release lines sit together at 4069.

Finally the `$glitch` segment: the lone 5ms pulse passes and the level returns before the window expires, so when `OnConfirm` reads on expiry it still sees the old level, identical to `confirmed_level` — not one event enters the queue, and the log stays silent for the whole segment.

Verification goes to `check_renode.py`, with three sets of assertions matching the three scenarios:

- `bounce` folds into one confirmation, with one line each of touch, press, long, and release, ordered press ≤ long < release, the long-press interval inside 600 to 1000ms, and press differing from touch by no more than 5ms
- the `glitch` scenario demands complete silence — no events
- the `press` scenario demands one line each of touch, press, and release, with no long permitted

That day's actual run output, pasted as-is:

```text
button confirm check: PASS (bounce->1 confirm, glitch->silence, fsm fed by source events)
```

The 20ms window width has provenance. We cited Ganssle's measurements in the last piece: most switches finish their bounce within 10ms<RefLink :id="3" preview="Jack Ganssle, A Guide to Debouncing, Part 2" />. A 20ms window covers the tail end of bounce entirely — by the time the window expires, the bounce has most likely settled, and the sampled level can be trusted. Last piece the same figure argued for a sampling interval; this piece uses it to argue for a window width.

I measured this firmware's size as well: text 13444, data 176, bss 304. The reference point is the pure-logging example `log_example` frozen in [the price-list piece](../06-patterns/06-zero-overhead-price-list.md) of station 06 — its text is 18200, and I reran it today and got that same number. Three interrupt sources, an FSM, and a queue all went into this firmware, yet the confirmation build's text comes out 4756 bytes smaller than the pure-logging example. The market rate for templates folding dispatch into straight-line code was already listed once, back in the price-list piece.

## The trade: head-to-head with the last piece's polling chain

| | Last piece (polling debounce chain) | This piece (confirmation window chain) |
|---|---|---|
| Semantics | samples accumulate agreement, reports the stable level | waits 20ms after the edge, confirms on expiry |
| Confirmation latency | a fixed 60ms (20ms×3) | anchored on the last edge, about 20ms |
| Main loop | cannot stop; every lap samples and feeds the FSM | WFI throughout; interrupts push the queue |
| Bounce | a single dip pollutes at most one reading, three agreements never line up, the state doesn't flip | every edge defers the window; confirms only once it settles |
| Glitch | likewise never assembles three agreements, state doesn't flip | by expiry the level has returned; zero events |
| Configuration surface | Debouncer, two parameters | EXTI plus two timers, NVIC preemption priorities to arrange |

In my view, the confirmation window's strength is that its response hugs the edge: a clean touch gets confirmed after waiting just 20ms, while the polling chain needs its full 60ms window to accumulate. The cost is a spread-out configuration surface — three interrupt sources with preemption priorities to line up, a queue and a variant both entering the firmware; with the 13444 text sitting right there, though, we can't complain about the size. The polling chain wins on simplicity: getting started takes only a `Debouncer` plus two parameters, and the main loop's rotation speed is its sampling clock — when the main loop gets busy, the sampling jitters right along with it. Both chains feed the same `ButtonFsm`; the button-semantics layer is shared property.

With this dimension in hand, we have also banked two possessions: a periodic beat that repeats and a one-shot window that fires once. The third member on the family roster, `TimPwm`, hasn't taken the stage yet — pin mapping and `set_duty` are both ready and waiting; the work of turning time into a duty cycle is left to the breathing LED of a later piece.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| An expiry handler fires immediately after the interrupt is enabled | `EGR.UG`'s load sets UIF in passing, and it wasn't cleared before enabling | Follow `tim.hpp`: `SR = ~UIF` immediately after loading |
| The period is silently wrong in simulation | `bluepill.repl`'s `frequency` disagrees with the firmware timclk | Change the model frequency to follow the firmware clock — that is what 8026841 changed |
| Compilation reports `period beyond 16-bit PSC*ARR` | the period exceeds the 16-bit × 16-bit range at this timclk | Switch to a slower timclk, or split the long period and continue counting in software |
| The precision gate reports `misses 0.1% precision` | the period is out of range, and this gate reports alongside the range assertion — in-range fallback error can never reach it | Same fix as the row above: bring the period back inside the range first |
| The timer runs twice as fast or twice as slow as expected | when the APB1 prescaler is not 1, the timer clock gets ×2, and it was forgotten | Compute timclk by RM0008's rule, then fill in `timclk_hz` |
| TIM1's update interrupt never enters | TIM1's vector is `TIM1_UP_IRQn`, different from TIM2 through TIM4 | Let the vector name follow `TimInstance` |
| Calling `Log::info` inside the interrupt produces no output | the logger serves thread context only; it is banned in ISRs | Follow `OnBeat`: touch flags only, and log once the main loop wakes |
| The long-press verdict lands a beat early or late | long press times itself against the 10ms slow tick, so the verdict lands on the tick that crosses 700ms | Shrink `period_us` for finer timing, at the price of denser interrupts |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="7.2 Clocks; 15.4.5 TIMx_SR UIF; 15.4.6 TIMx_EGR"
  />
  <ReferenceItem
    :id="2"
    author="Shawn Hymel"
    title="Getting Started with STM32 Timers and Timer Interrupts"
    :year="2020"
    url="https://www.digikey.gr/en/maker/projects/getting-started-with-stm32-timers-and-timer-interrupts/d08e6493cefa486fb1e79c43c0b08cc6"
    chapter="Introductory walkthrough of the APB ×2 timer clock rule (APB2 side; the F1's APB1 side follows the same rule)"
  />
  <ReferenceItem
    :id="3"
    author="Jack Ganssle"
    title="A Guide to Debouncing, Part 2"
    url="https://www.ganssle.com/debouncing-pt2.htm"
    chapter="Switch bounce measurements: most contacts settle within 10ms"
  />
  <ReferenceItem
    :id="4"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (examples/09_tim_heartbeat, examples/10_button_confirm, and boards/stm32f1/tim.hpp)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="TimBase, TimOneShot, TimPwm, consteval PSC/ARR solver"
  />
</ReferenceCard>
