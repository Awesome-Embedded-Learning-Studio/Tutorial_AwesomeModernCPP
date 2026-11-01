---
title: 'The edge of the range: how long a period 16 bits times 16 bits can cover'
description: 'The 16-bit PSC and 16-bit ARR are two 15:0 bitfields in RM0008; the full set of 65536 × 65536 is 2^32 ticks, exactly 67.108864s at 64MHz, and a divisor of 1 is always exact while ticks stay at or under 65536. We bring tim.hpp''s resolve_ticks, ticks_for_period, and relative_error_ppm out word for word, recompute a five-row comparison table on the host, and prove with an arm-none-eabi-g++ probe that the 65535/65535 corner compiles while 67108865µs trips both assertions at once; sampling 200,000 periods with mt19937_64 shows 54.93% take the approximation path with a maximum error of 15ppm, from which the fallback''s 1/65536 bound is derived; the 0.1% gate''s real catch is configurations out of range, while the HAL''s IS_TIM_PERIOD only checks the ARR slot width'
chapter: 4
order: 3
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - 编译期计算
  - constexpr
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 10
prerequisites:
  - 'The TIM family and confirmation debouncing: from a 1Hz heartbeat to a 20ms confirmation window'
  - 'Configuration goes into the type: designated initializers as template arguments'
related:
  - 'The price list of zero overhead: dissecting the 18,200-byte logger firmware'
translation:
  source: documents/vol8-domains/embedded/f103/04-time/03-period-range-boundary.md
  source_hash: 805b36227ec06ceb6ff71986cb7f5c73a02bf3f5d77751b9219ffca726cd6f2d
  translated_at: '2026-11-01T03:20:13+00:00'
  engine: anthropic
  token_count: 2200
---

# The edge of the range: how long a period 16 bits times 16 bits can cover

## Introduction: the range question the last piece left open

At the close of [Configuration goes into the type: designated initializers as template arguments](02-config-in-type.md), we pointed the next piece's work at the range: still under the same timclk constraint, which periods can 16 bits times 16 bits actually hold? This piece computes that one thing only — where the boundary sits on either end and what the error looks like near it, worked out in one pass.

The material comes from the submodule pinned at commit 8026841, the same one as [piece 01](01-tim-family-confirm-debounce.md) and piece 02. On the dev machine I recomputed the solver's functions with GCC 16.2.1 and `-std=c++23`. On the cross-toolchain side I built several probes with arm-none-eabi-g++ 16.2.0. A probe is a small translation unit written for the sole purpose of making the compiler instantiate the template; we neither link it nor power anything up — the compile step is all we want, and the errors it reports are themselves the data we're after.

## The hardware's multiplication: two 16-bit bitfields, 2^32 ticks

We verified the PSC and ARR bitfields against RM0008 (ST's STM32F1 reference manual, the same source as piece 01's reference card). The manual is the Rev 21 PDF downloaded from st.com on 2026-10-28; §15 is the chapter on General-purpose timers (TIM2 to TIM5), TIMx_PSC's section number is 15.4.11 and TIMx_ARR's is 15.4.12, both bitfields are written `15:0`, and each spans 16 bits<RefLink :id="1" preview="STMicroelectronics, RM0008" />.

The period arithmetic, read out one role at a time. The prescaler's job is to let one input tick through for every (PSC+1) of them; the counter's job is to count from 0 up to ARR and fold back to the start — so one period consumes (PSC+1)×(ARR+1) input ticks. What the registers record is really N−1: PSC=0 means no division at all, ARR=63 means counting to 64. We covered that conversion when piece 01 walked the solver; no retelling here.

The full set, computed end by end. PSC and ARR each range over 0 to 65535, so (PSC+1) and (ARR+1) each range over 1 to 65536, and the product's upper bound is 65536×65536 = 4'294'967'296 ticks — 2^32. At 64MHz the number of ticks per second is 64'000'000; divide 2^32 by it and you get exactly 67'108'864µs, or 67.108864s written in seconds, without so much as a fractional leftover. Now the small end: the period is filled in microseconds, 1µs converts to exactly 64 ticks, so the PSC=0, ARR=63 combination covers it, and the error lands on zero.

One more property we'll need later: as long as the tick count stays at or under 65536, the divisor-1 decomposition is always exact. With 1 as the divisor, the division is exact for any tick count, the quotient is simply the original tick count, and as long as the quotient stays at or under 65536 it fits inside ARR. 65536 divided by 64 gives 1024, meaning every period up to 1024µs at 64MHz has an exact decomposition — the approximation path never even gets a chance to take the stage here.

## How the solver picks: exact divisors from 1 upward, a fallback when none exists

The three functions, brought out word for word from the tim.hpp pinned at 8026841 in the submodule<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />. The first one hunts for the divisor:

```cpp
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

The second one — piece 01 already read out the job of folding a period into a tick count; the folding formula adds half a million before dividing, and the landing point rounds to the nearest microsecond:

```cpp
consteval std::uint64_t ticks_for_period(std::uint64_t clk_hz, std::uint64_t period_us) {
    return (clk_hz * period_us + 500'000) / 1'000'000;
}
```

The third one computes the relative error, and it is what the newly added err column in this piece reports:

```cpp
consteval std::uint64_t relative_error_ppm(std::uint64_t target, TimDivisors d) {
    const std::uint64_t actual = (std::uint64_t)(d.psc + 1) * (d.arr + 1);
    return actual > target ? (actual - target) * 1'000'000 / target
                           : (target - actual) * 1'000'000 / target;
}
```

ppm is short for parts per million and counts deviations of one part in a million; 15ppm is a relative deviation of fifteen parts per million. Piece 01 took `resolve_ticks` apart: climbing upward from 1, it looks for a factor that divides evenly and whose quotient fits in 16 bits, and the first `div` that hits is the answer. When none can be found, the fallback takes `ceil(ticks/65536)` as its div, at the price of carrying the remainder as loss. We copied these three functions word for word onto the dev machine, took timclk as the default 64MHz, and what ran out was a five-row table — the output, pasted as it came:

```text
         1 us | ticks=          64 | PSC=    0 ARR=   63 | err=0 ppm
     20000 us | ticks=     1280000 | PSC=   19 ARR=63999 | err=0 ppm
   1000000 us | ticks=    64000000 | PSC=  999 ARR=63999 | err=0 ppm
  67108863 us | ticks=  4294967232 | PSC=65535 ARR=65534 | err=15 ppm
  67108864 us | ticks=  4294967296 | PSC=65535 ARR=65535 | err=0 ppm
```

We recognize the middle rows: the 20ms row is the confirmation window of example 10 from piece 01, the 1s row is 09's heartbeat, and piece 01 already unpacked where that pair of values comes from — no retelling here. Look at the three new rows. 1µs takes exactly the div=1 exact decomposition: 64 ticks, ARR=63, matching the previous section's inference. The bottom two rows both press against the top of the range: 67'108'863µs takes the approximation path and its error lands at 15ppm, while 67'108'864µs, surprisingly, has zero error. Why the two rows are treated differently — the next section verifies that inside the compiler.

## The corner and going out of range: 67108864 compiles, one microsecond more gets stopped

The probe has this shape, with build flags identical to 09's build: `STM32F103xB`, `USE_HAL_DRIVER`, `-mcpu=cortex-m3`, `-mthumb`, standard `-std=gnu++23`, toolchain arm-none-eabi-g++ 16.2.0. `OnBeat` is a hollow type that satisfies the `IrqHandler` concept — all we want is the template instantiation itself, and the `"force"` assertion at the end of the line exists to force the compiler to actually compute `kDiv`:

```cpp
using Probe = estdx::stm32f1::TimBase<
    {.instance = estdx::stm32f1::TimInstance::Tim2, .period_us = 67'108'864}, OnBeat>;
static_assert(Probe::kDiv.arr >= 0, "force");
```

We filled `period_us` with `67'108'863`; it compiled, the fallback's resolved values were PSC=65535, ARR=65534 — consistent with that row of the table. Swapping in `67'108'864` compiled too. Then we switched to an assertion that asks for the corner's two values directly — the corner being the configuration where PSC and ARR both top out at 65535 — with the type renamed to `Corner` accordingly:

```cpp
static_assert(Corner::kDiv.psc == 65'535 && Corner::kDiv.arr == 65'535);
```

That compiled as well. The product of PSC=65535 and ARR=65535 is exactly 2^32 ticks; the full-set upper bound computed in the previous section gets used here down to the last tick. Its err=0 is also plain to see: 65536 itself is a factor of 2^32, so the division is exact, and with it the exact decomposition.

What stands guard at the range's edge are precisely those two assertions inside `TimBase`, brought over from tim.hpp as-is:

```cpp
    static_assert(kDiv.ok, "period below resolution or beyond 16-bit PSC*ARR at this clock");
    static_assert(detail::relative_error_ppm(detail::ticks_for_period(CFG.timclk_hz, CFG.period_us),
                                              kDiv) <= 1'000,
                  "period misses 0.1% precision at this clock");
```

We will see both error texts in the output shortly. `TimOneShot` has two more assertions of the same pattern, with period replaced by timeout in the error text; this piece won't unfold them. Now we fill `period_us` one microsecond further, `67'108'865`, and both assertions fire together — the output, pasted as it came:

```text
.../tim.hpp:106:24: error: static assertion failed: period below resolution or beyond 16-bit PSC*ARR at this clock
  • '...::kDiv.estdx::stm32f1::detail::TimDivisors::ok' evaluates to false
.../tim.hpp:108:53: error: static assertion failed: period misses 0.1% precision at this clock
  • the comparison reduces to '(999999 <= 1000)'
```

The `...` in the error paths is our abbreviation of the full namespace path — the elision is marked, and not one character of the actual output was changed. The first error reports the range: `67'108'865µs` demands 4'294'967'360 ticks, which is 64 more than 2^32; a number that size does not fit in 16 bits times 16 bits, and `kDiv.ok` folds to false. The 999999 in the second looks abrupt, but recomputing the expression makes it clear: when the range fails, `resolve_ticks` returns `{0, 0, false}`, so the `actual` reaching `relative_error_ppm` is (0+1)×(0+1) = 1 tick, while the target is over four billion two hundred ninety-some million; plug into (target−1)×1'000'000÷target, and after integer division truncates, what lands is exactly 999999. That is nearly a thousand times larger than 1000, so the second assertion fails along with it.

We verified the other end of going out of range too: `period_us` set to 1 and `timclk_hz` set to `400'000`, built once more. The ticks rounded to 0, the below-resolution half of the tim.hpp:106 message fired, with the error text unchanged by a single character. Over at tim.hpp:108 it became `error: non-constant condition for static assertion`, because the precision expression divided by zero and constant evaluation broke on that line. The first half of the range assertion's error text exists precisely for this kind of low-clock configuration.

## The approximation path's error: 200,000 sampled periods, capped at 15ppm

Exact decomposition cannot cover every period, so how large is the fallback path's error? I measured it by sampling. The random source was `mt19937_64` — the standard library's 64-bit Mersenne Twister generator — with the seed 20261028; I drew 200,000 periods uniformly from the interval 65'537 to 67'108'864µs, and the host solved each one with the copied functions. The output, pasted as it came:

```text
sampled 200000 periods in (65.536ms, 67.1s): fallback=109867 (54.93%), max_err=15 ppm at 62860349 us
```

Periods without an exact decomposition made up 54.93%, and for them the approximation path is the only road. The measured maximum error was 15ppm, landing at 62'860'349µs. The number 15 is not luck; let's derive where it comes from, step by step.

Step one: what does the fallback lose? The fallback's div is `ceil(ticks/65536)`, and the actual tick count it produces is the integer quotient of ticks divided by div, multiplied back by div; what gets dropped is the remainder of ticks modulo div. The remainder's upper bound is div−1 — that comes from the definition of the modulo.

Step two: the denominator's lower bound. Since div is computed by rounding up, ticks must be greater than (div−1)×65536 — otherwise a smaller div would have done. On the numerator side we just obtained the bound div−1, and the denominator is greater than (div−1)×65536; sandwich the two inequalities together and the relative error's upper bound becomes (div−1)÷((div−1)×65536), which is exactly the 1/65536 we were after. In ppm that reads 15.26, and after integer truncation at most 15ppm can remain. The capped value the sampling measured was exactly 15 — matching the bound we derived.

Step three: that 0.1% precision gate. The 1000ppm threshold is more than sixty times the approximation error's cap, so the fallback path's error can never reach the 1000ppm gate — the approximation error alone cannot trigger it. What it truly catches are configurations out of range: that `999999 <= 1000` error line from the previous section is what it looks like when this gate sweeps up an out-of-range configuration along the way. This is our reading, derived from the solver's arithmetic; what the library author had in mind at the time, we won't speak for.

## Head-to-head with the HAL: IS_TIM_PERIOD checks the slot

How is the same range question treated in the HAL's world? Here is the macro governing Period in stm32f1xx_hal_tim.h — we quote the line at L1657<RefLink :id="3" preview="STMicroelectronics, stm32f1xx_hal_tim.h" />:

```c
#define IS_TIM_PERIOD(__PERIOD__)      (((__PERIOD__) > 0U) && ((__PERIOD__) <= 0xFFFFU))
```

It checks one thing only: that the number is greater than 0 and fits inside `0xFFFF` — together, the 16-bit slot of the ARR register. Piece 02 quoted the line where `TIM_Base_SetConfig` puts pen to paper: `TIMx->ARR` receives `Structure->Period`'s original value, so what the macro checks is the slot width. Whether the configuration can express the period you want at the given clock — the macro never asks. Deciding what PSC and what ARR to set is, in the HAL's model, the engineer's own job: the range knowledge is all written in RM0008, and we look it up ourselves, multiply ourselves, compute ourselves. And besides, the default-tier `assert_param` is a no-op — piece 02 pasted both tiers of its expansion — so after preprocessing, checks of this kind are simply gone.

Back to the tim.hpp side: the very same multiplication moved into consteval, and range and precision both became compile-time assertions. That is exactly what the second dividend promised at the close of piece 02 turned out to be.

## Wrap-up: the range fits; next comes the order of interrupts

The range's edge is computed: at 64MHz, every period from 1µs to 67.108864s fits, and at the very top the error is exactly zero. One more question is waiting for us on the time axis. At the moment several timers all want to interrupt the CPU, who goes first, and whose interrupt has to give way — the next piece takes apart NVIC preemption priorities. We have in fact already used them in piece 01: 09's `init(3)` and example 10's Touch=2, Window=3, SlowTick=4 arrangement — back then we just filled the numbers in as told, without ever taking the rule apart.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| The error text is the below-resolution half | timclk too low, ticks rounded to 0 — say 1µs at 400kHz | Check that `timclk_hz` holds the real clock; at low clocks, periods on the 1µs scale cannot be configured |
| The error text is the beyond-16-bit-PSC*ARR half | the period exceeds the 67.108864s range ceiling at 64MHz | Switch to a slower timclk — the range ceiling rises inversely (halve timclk to 32MHz and the top doubles to 134.217728s) — or split the long period and continue counting in software |
| Both assertions fire, the second reading 999999 <= 1000 | the range assertion already caught it, and the precision expression received an out-of-range input with ok=false | Fix the range one; the second goes quiet along with it |
| The table shows err=15ppm and you worry about precision | the approximation path was taken, where the error is bounded by 1/65536 ≈ 15.3ppm | 15ppm is plenty for most timing scenarios; for exactness, pick periods with exact divisors |
| You want a period spanning the full range | 67'108'864µs equals exactly 2^32 ticks; the corner compiles with zero error | One microsecond more is out of range; periods longer than that split into software counting |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="15 General-purpose timers (TIM2 to TIM5); 15.4.11 TIMx_PSC; 15.4.12 TIMx_ARR"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (include/libestdx/boards/stm32f1/tim.hpp, submodule pinned at commit 8026841)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="resolve_ticks, ticks_for_period, relative_error_ppm, and the range assertions of TimBase/TimOneShot"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_tim.h (vendored source, the IS_TIM_PERIOD macro at L1657)"
    chapter="Text frozen in the libestdx submodule third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/Inc/"
  />
</ReferenceCard>
