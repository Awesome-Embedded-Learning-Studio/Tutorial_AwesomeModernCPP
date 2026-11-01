---
title: 'Who counts the millisecond: the SysTick time base'
description: 'The HAL''s 1ms beat is computed by HAL_InitTick from SystemCoreClock on the spot as a reload value; on this 64MHz board that gives LOAD=63999 and CTRL=7, with CLKSOURCE=1 on the processor clock — and a sweep of the whole HAL Src tree finds not one read of SysTick->CALIB. By architecture definition CALIB''s TENMS holds a 10ms reload value, yet the 9000 ST loads on F1 computes to 1ms at 9MHz; the generic comparison says 10ms@9MHz should be 90000. The Renode four-act probe 12_systick_probe reads a boot snapshot of TENMS=640000 and NOREF=1, and a write of CLKSOURCE=0 is bounced back to a read of 7 — DDI 0403''s forced-to-1 rule confirmed on the spot. Act B''s 10ms beat honors the generic value, act B2 carries 9000 verbatim to 64MHz where one beat shrinks to 0.140625ms and uwTick runs 7.11x fast, so HAL_Delay(100) actually waits only about 14ms, and act C restores 1ms with the computed configuration. Four hand-computed formulas close the case: deduction from the manuals gives 9000@8MHz=1.125ms, 12.5% slow, and computing the reload value from the clock frequency is the right answer'
chapter: 4
order: 5
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - 外设管理
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 15
prerequisites:
  - 'Who interrupts whom: how NVIC preemption priorities are arranged'
related:
  - 'Putting the logger to work: two ways to write it and two surfaces to read it'
translation:
  source: documents/vol8-domains/embedded/f103/04-time/05-systick-timebase.md
  source_hash: 901115bbc7360e4f4ea41e73151ea0d0141e3331558171b04b0f2e845b75163b
  translated_at: '2026-11-01T03:20:13+00:00'
  engine: anthropic
  token_count: 3200
---

# Who counts the millisecond: the SysTick time base

## Introduction: SysTick, passed by the door three times

[Who interrupts whom: how NVIC preemption priorities are arranged](04-nvic-preempt-priorities.md) closed with words left behind; we bring them over as-is:

> SysTick lives inside the HAL's time base: that 1ms beat configured in HAL_Init is what HAL_GetTick and HAL_Delay both count. The wipeout we just dissected had it too — the HAL parked it at the lowest level, and it happened to be the very clock that busy-wait loop was watching. It also leaves one clue we haven't used: RM0008's 10.1.1 writes the calibration value 9000, which the 9MHz reference clock turns into 1ms. How is that number used, and on which clock is the 1ms beat actually configured? The next piece takes apart the SysTick time base.

SysTick's full name is the System Timer, a 24-bit down-counter that comes built into the Cortex-M3 core. When it counts down to 0 it fires one interrupt, and once fired it starts counting again from the reload value. The reload value is the starting number the counter loads back up with when it runs out, and we will be dealing with it quite a bit below. You have already met this name three times: in the 00 station's [Why C++, and by what right?](../00-env-setup/00-why-cpp.md), it was the supplier of the 1ms beat that HAL_Init configures. In the same station's [Renode observatory: no board, so who gets the final say?](../00-env-setup/02-renode-observatory.md), it was the model's reference frequency, the one we align to 64MHz through the systickFrequency parameter. And by piece 04, it was also the interrupt source the HAL parked at the lowest 0x0F, present in the wipeout we dissected. All three times we only passed by its door; not one of its registers did we take apart. This piece carries one question: who actually configures the 1ms beat, onto which clock is it configured, and with which number. The two question marks piece 04 left behind — how is the number 9000 used, and who configures the beat — each get an answer backed by measurement.

The material is still the set used by pieces 01 through 04, with our submodule pinned at commit 8026841. The probe is examples/12_systick_probe, newly built for this piece, on a clone tree at the same commit as the submodule (not folded into the submodule). The experiments run in Renode as usual; we won't retell the pipeline discipline.

## The beat chain: 1ms is computed on the spot from SystemCoreClock

The chain's head sits in kernel.hpp; the two opening macros, moved out word for word<RefLink :id="1" preview="Awesome-Embedded-Learning-Studio, libestdx" />:

```c
#define USE_RTOS 0U             /* no RTOS, the SysTick time base belongs to the HAL */
#define TICK_INT_PRIORITY 0x0FU /* SysTick gets the lowest priority */
```

USE_RTOS is filled with 0, so the SysTick time base belongs to the HAL. TICK_INT_PRIORITY's 0x0F you took apart in piece 04 — the last of the sixteen levels. HAL_Init takes it and calls HAL_InitTick. We didn't dissect that function in piece 04; all we saw there was how a re-run of it writes a preemption-level change back. What really puts the number into the hardware is HAL_InitTick's body, pasted word for word from stm32f1xx_hal.c<RefLink :id="2" preview="STMicroelectronics, stm32f1xx_hal.c" />:

```c
__weak HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  /* Configure the SysTick to have interrupt in 1ms time basis*/
  if (HAL_SYSTICK_Config(SystemCoreClock / (1000U / uwTickFreq)) > 0U)
  {
    return HAL_ERROR;
  }

  /* Configure the SysTick IRQ priority */
  if (TickPriority < (1UL << __NVIC_PRIO_BITS))
  {
    HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0U);
    uwTickPrio = TickPriority;
  }
  else
  {
    return HAL_ERROR;
  }

  /* Return function status */
  return HAL_OK;
}
```

Let's read the expression in the first if slowly. uwTickFreq sits at hal.c L81, the comment beside its type reads `/* 1KHz */`, and its value is 1, so 1000U divided by it is still 1000, and the number fed down is SystemCoreClock / 1000. The HAL_SYSTICK_Config it passes through has a one-line body; we read it as-is: `return SysTick_Config(TicksNumb);`. The work is handed entirely over to CMSIS's SysTick_Config, and the four lines that really write the hardware are these, moved out of core_cm3.h word for word<RefLink :id="3" preview="ARM Limited, core_cm3.h" />:

```c
  SysTick->LOAD  = (uint32_t)(ticks - 1UL);                         /* set reload register */
  NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL); /* set Priority for Systick Interrupt */
  SysTick->VAL   = 0UL;                                             /* Load the SysTick Counter Value */
  SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk |
                   SysTick_CTRL_TICKINT_Msk   |
                   SysTick_CTRL_ENABLE_Msk;                         /* Enable SysTick IRQ and SysTick Timer */
```

The first line writes the reload value; watch that minus one: a full lap of the counter from LOAD down to 0 is LOAD+1 beats, so to get 64000 beats, what goes into LOAD must be 63999. The second line calls the very NVIC_SetPriority piece 04 dissected, only this time it is embedded inside SysTick_Config, which hard-writes SysTick's preemption level to the lowest, 15. The third line clears the current value to 0, letting it start counting from the fully loaded point. The fourth line turns on three switches: ENABLE, which governs run and stop, TICKINT, which governs the interrupt, and CLKSOURCE, which governs clock selection — 1 selects the processor clock. The function also opens with a check: if ticks-1 leaves the 24-bit range it returns 1, and that is exactly what HAL_InitTick's first if receives.

After the count reaches 0, who is it that increments the millisecond count? Look over at the interrupt vectors: in the examples' stm32f1xx_it.c, a three-line function is in charge:

```c
void SysTick_Handler(void) {
    HAL_IncTick();
}
```

HAL_IncTick's body is the single line `uwTick += uwTickFreq;`, and HAL_GetTick's body is `return uwTick;`. The chain closes here: SysTick fires an interrupt on reaching 0, the handler receiving it adds one to uwTick, and when we call HAL_GetTick, what we get back is this count. tick.hpp wraps it into the clock the logger uses; we bring it up together with its comment<RefLink :id="1" preview="Awesome-Embedded-Learning-Studio, libestdx" />:

```cpp
// Satisfies logger::LogClock. HAL SysTick at 1 kHz (HAL_Init + the
// firmware's SysTick_Handler). uint32 wraps at ~49.7 days: display-only.
struct HalTickClock {
    static std::uint32_t now_ms() { return HAL_GetTick(); }
};
```

Half the answer can stand now. In piece 01's example 09 we configured this board's clock as HCLK=64MHz; substituted into the expression, it gives exactly 64000/64MHz=1ms: the HAL's 1ms is computed on the spot from SystemCoreClock and has nothing to do with CALIB. The grounds for that claim: I went through the whole HAL Src/ directory — SysTick->CALIB never appears once in stm32f1xx_hal.c. A few macros elsewhere in the directory carry the letters CALIB, belonging to the RCC, RTC, and ADC drivers; not one of them reads SysTick's calibration value. Reading the calibration value is work the HAL never does, start to finish. We speak only within the HAL's scope; other libraries are outside this sentence's jurisdiction.

## Four registers and two clocks

SysTick occupies four registers in the core's memory map; CMSIS's SysTick_Type, moved over word for word:

```c
typedef struct
{
  __IOM uint32_t CTRL;                   /*!< Offset: 0x000 (R/W)  SysTick Control and Status Register */
  __IOM uint32_t LOAD;                   /*!< Offset: 0x004 (R/W)  SysTick Reload Value Register */
  __IOM uint32_t VAL;                    /*!< Offset: 0x008 (R/W)  SysTick Current Value Register */
  __IM  uint32_t CALIB;                  /*!< Offset: 0x00C (R/ )  SysTick Calibration Register */
} SysTick_Type;
```

The first three are readable and writable; CALIB alone is read-only — after that R/ in the comment, the write column is empty. LOAD and VAL are both 24 bits wide, and that out-of-bounds check from the last section checks exactly this ceiling. Four bits in CTRL we need to know: bit0's ENABLE, bit1's TICKINT, bit2's CLKSOURCE, and bit16's COUNTFLAG. COUNTFLAG exists for counting beats by polling; we go by interrupt, so it is of no use to us. With all three low bits on, the value read back is 7 — a number we are about to meet in the probe's snapshot.

CLKSOURCE governs clock selection, and on F1 there are two candidates. The original sentence from RM0008's clock-tree page (page 93)<RefLink :id="4" preview="STMicroelectronics, RM0008" />:

```text
The RCC feeds the Cortex® System Timer (SysTick) external clock with the AHB clock (HCLK) divided by 8. The SysTick can work either with this clock or with the Cortex® clock (HCLK), configurable in the SysTick Control and Status register.
```

In plain words: the RCC feeds SysTick's external clock with HCLK divided by eight, and SysTick can run either on that clock or on the processor's own HCLK, the choice written in CTRL. The first kind's name in the architecture manual is the external reference clock — in plain terms, a clock independent of the processor clock, reserved specially for SysTick, which F1 implements as HCLK/8. F1's maximum HCLK is 72MHz, and dividing by eight lands exactly on 9MHz — that is the clock premise behind the 9000 sentence.

## The calibration value CALIB: a 10ms title, a 1ms number

CALIB (the calibration register) is assembled from three fields: the low 24 bits, TENMS, hold the calibration value itself; bit31's NOREF reports whether the chip has an independent reference clock — a read of 1 means not implemented and only 0 means implemented; and bit30's SKEW flags whether the calibration value is accurate. What TENMS should hold by the architecture's definition, we ask DDI 0403 (ARM's ARMv7-M architecture manual); the SYST_CALIB page's original sentence<RefLink :id="5" preview="ARM Limited, DDI 0403" />:

```text
Optionally, holds a reload value to be used for 10ms (100Hz) timing, subject to system clock skew errors. If this field is zero, the calibration value is not known.
```

What the field should hold, this sentence says plainly: TENMS holds a reload value for 10ms (100Hz) timing, and a read of 0 in the field means the vendor did not provide one. What ST loads on F1, from RM0008's 10.1.1 (page 197), original sentence<RefLink :id="4" preview="STMicroelectronics, RM0008" />:

```text
The SysTick calibration value is set to 9000, which gives a reference time base of 1 ms with the SysTick clock set to 9 MHz (max HCLK/8).
```

Put the two sentences side by side. The title the architecture assigns this field is a 10ms reload value, yet the 9000 ST loads computes to 1ms at 9MHz. The generic comparison is easy to work out too: 10ms@9MHz calls for 90000, and ST ships a tenth of that. If you don't know this relationship and use 9000 as the 10ms value, or carry it around as a 1ms value that works anywhere, you will misconfigure the clock and the number together.

Now NOREF, which carries a hard rule of its own; DDI 0403's original sentence<RefLink :id="5" preview="ARM Limited, DDI 0403" />:

```text
When this bit is 1, the CLKSOURCE bit of the SYST_CSR register is forced to 1 and cannot be cleared to 0.
```

And CLKSOURCE's own definition, from the SYST_CSR page of the same manual<RefLink :id="5" preview="ARM Limited, DDI 0403" />:

```text
0 = SysTick uses the implementation defined external reference clock. 1 = SysTick uses the processor clock. If no external clock is provided, this bit reads as 1 and ignores writes.
```

Read together: when the chip has no external reference clock (NOREF=1), CLKSOURCE is pressed up against 1 — write 0 in and it is ignored, and what reads back is still 1. As for SKEW, this piece only reports its measured value; the rest we leave alone.

Now switch over to the real hardware: what should the three fields look like there? We proceed by deduction from the manuals. RM0008's RCC sentence says F1 supplies an HCLK/8 external clock, and DDI 0403's definition says that when NOREF equals 0, the independent reference clock is implemented. Pushing through the definitions: real hardware's NOREF should be 0, TENMS should hold 9000, and CLKSOURCE should tolerate 0. But watch the wording: the status of these claims is deduction, and we hold not one reading. This piece never measured F1 real hardware's CALIB value; what we run here is simulation, and in what follows, every claim on the real-hardware side stays at the manual-deduction level.

## The four-act probe: what happens when 9000 is carried verbatim into LOAD

The probe's method comes from the same family as experiment three of the 00 station's [Renode observatory: no board, so who gets the final say?](../00-env-setup/02-renode-observatory.md): two independent clocks checked head-to-head. The reference clock is Tim3, a TimBase wired for a 100ms period beat with preemption level 3; the wiring details were dissected in piece 01. Its handler does one thing — write one array slot — and exits, with all logging done back in the main loop. Piece 04's probe logged inside the ISR, a measurement privilege for timing timing itself; this side measures SysTick and doesn't even need the privilege. The reference clock's code, moved out word for word:

```cpp
// Reference clock: one beat per 100ms; the ISR only records the beat number and the HAL millisecond count at that moment, no logging
volatile std::uint32_t beats = 0;
volatile std::uint32_t ring[128];
struct OnGate {
    static void operator()() {
        ring[beats & 127u] = HAL_GetTick();
        beats = beats + 1;
    }
};
```

The main loop's job is to wait for the reference clock to walk off 20 full intervals — 2000ms of ground truth — and we take the HAL millisecond difference between the first and last samples:

```cpp
// Wait for the reference clock to advance n more beats (n samples, n-1 intervals), return the millisecond difference between first and last samples
// On exit beats == b0+n; the slots written are b0 .. b0+n-1
std::uint32_t span_over_beats(std::uint32_t n) {
    std::uint32_t b0 = beats;
    while (beats - b0 < n) {
        __WFI();
    }
    return ring[(b0 + n - 1) & 127u] - ring[b0 & 127u];
}
```

SysTick's preemption level we keep at the HAL default of 15, and the reference clock gets preemption level 3. The long window that wiped out in piece 04 does not exist here — both sides' ISRs do small jobs of one or two items, and number 15 runs along as usual. The first thing done at boot is to read each of the four registers after the HAL finishes configuring, with CALIB then reporting its three fields separately. The captured log, as-is:

```text
[0ms][Info ][reg] 64000000
[0ms][Info ][reg] 7
[0ms][Info ][reg] 63999
[0ms][Info ][reg] 2148123648
[0ms][Info ][reg] 640000
[0ms][Info ][reg] 1
[0ms][Info ][reg] 0
```

Let's read the seven lines one by one. The first line's 64000000 is SystemCoreClock; the second line's 7 is CTRL, all three low bits on. The third line's 63999 is LOAD — direct evidence of SysTick_Config(64M/1000). Its route there took two hops: when HAL_Init ran, the clock wasn't configured yet, and at 8MHz the LOAD written was 7999. Only when SystemClock_Config set the clock to 64MHz did the tail of HAL_RCC_ClockConfig re-call HAL_InitTick and swap 63999 in. After a clock reconfiguration the reload value does follow on its own, and this number in the snapshot is the on-site evidence. The fourth line's 2148123648 is CALIB's raw value, hexadecimal 0x8009_C400. The last three lines are the fields reported separately: the TENMS field reads 640000, NOREF reads 1, and SKEW reads 0. 640000's origin is exactly 64000000/100 — the model strictly follows the generic 10ms@64M, and algorithmically it computes systickFrequency/100. That is our inference, and the text says it as an inference.

The model's values don't match what real hardware should look like; lay the two sides side by side. The real-hardware column is entirely manual-deduction wording — don't read it as measurements:

| | Real hardware (manual deduction, unmeasured) | Renode model (measured) |
|---|---|---|
| NOREF | 0 (RCC supplies HCLK/8, pushed through the definition) | 1 |
| TENMS | 9000 (RM0008's sentence) | 640000 (=64M/100) |
| The period when TENMS is loaded into LOAD | @9MHz=1ms; 1.125ms at this board's 8MHz | @64MHz=10ms |
| Writing CLKSOURCE=0 | writable per the RCC sentence, runs at 8MHz | write 3, read back 7, bounced |

Across the four acts, only SysTick itself changes; the reference clock never moves from start to finish. Act A changes nothing and measures the HAL's status quo. Act B takes the calibration route, configured by the generic usage: CLKSOURCE written 0 to switch to the external reference clock, and LOAD loaded with TENMS. The firmware also keeps a fallback: if TENMS is 0, go with 9000. The model's value is 640000, so the fallback never fires. The four configuration lines, word for word:

```cpp
    SysTick->CTRL = 0;
    SysTick->LOAD = tenms - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;  // CLKSOURCE=0
```

Act B2 takes the typical wrong way, and serves as act B's control group: reading RM0008 as "9000 is 1ms", the reload value carried over verbatim is 8999. The firmware likewise clears CLKSOURCE to 0, the model bounces it back as before, and the clock is still the 64MHz processor clock. The configuration, word for word:

```cpp
    SysTick->CTRL = 0;
    SysTick->LOAD = 9000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
```

Act C restores the HAL's configuration with the single line `SysTick_Config(SystemCoreClock / 1000u)`. The four acts run back to back; the log, as-is, with the timestamp zero at HAL_Init:

```text
[0ms][Info ][ph] A
[2100ms][Info ][d] 2000
[2100ms][Info ][reg] 7
[2100ms][Info ][reg] 639999
[2100ms][Info ][ph] B
[2309ms][Info ][d] 200
[2309ms][Info ][ph] B2
[17243ms][Info ][d] 14223
[17243ms][Info ][ph] C
[19342ms][Info ][d] 2000
[19343ms][Info ][ph] done
```

Act A's measured d is 2000: 20 intervals hold up 2000ms of ground truth, and the difference we measured matches it to the tick — under the HAL's status quo, the tick period is 1.000ms.

Act B's log has two spots to look at. One is the two reg lines before B: CTRL read back 7, we wrote in 3 — the firmware plainly cleared CLKSOURCE to 0, and the model bounced it back to 1. With NOREF=1, the forced-to-1 rule is confirmed on the spot on the model. The other spot is d=200: LOAD was swapped to 640000, the clock is still the 64MHz processor clock, and one beat's length became 10.00ms. The generic value was honored — it really is 10ms — but the clock never got swapped. Look at the timestamps too: the window is the same 2000ms of ground truth, yet the timestamp walked only from 2100 to 2309, 209 units, uwTick running ten times slow — the log recorded this itself.

Act B2's d is 14223. With 9000 configured at 64MHz, one beat shrinks to just 0.140625ms; the theoretical value works out to 2000/0.140625=14222.2, and the sample differs by 1 — they line up. The consequence, computed for you: uwTick runs 7.11 times fast, and HAL_Delay(100) actually waits only about 14ms. The timestamps report it too — in this act they walk from 2309 to 17243.

Pull the four acts' timestamp strides out on their own: each act's window is the same 2000ms of ground truth, yet in act one the timestamp walked from 0 to 2100, in act two it moved only 209 units, in act three it walked from 2309 all the way to 17243, and in act four it came back to around 2000. What the timestamp counts is exactly uwTick, and uwTick's speed is the very thing under test — so the stride itself is the configuration taking effect, self-evidencing and dependent on no other instrument.

After the runs I executed the verdict script check_probe.py twice as well: the four band assertions (A in 1995 to 2005, B in 190 to 210, B2 in 14100 to 14400, C in 1995 to 2005) plus the boot snapshot's three items and the phase sequence passed every time<RefLink :id="1" preview="Awesome-Embedded-Learning-Studio, libestdx" />, and the two runs' logs matched byte for byte. The line the checker prints at the end, as-is:

```text
systick probe check: PASS (快照 reg/幕A ≈2000/幕B ≈200/幕B2 ≈14222/幕C 还原 ≈2000)
```

## Hand computation: the real-hardware arithmetic, step by step

With the four acts measured, let's walk the real-hardware arithmetic by hand, skipping no step in between:

```text
9000's intent         →  9MHz ÷ 9000 = 1ms          (9MHz = 72MHz max HCLK divided by 8)
This board, external clock →  64MHz/8 = 8MHz, 9000@8MHz = 1.125ms  (12.5% slow)
9000 carried verbatim →  9000@64MHz = 0.140625ms     (cross-checked by act B2's measurement)
Generic comparison    →  10ms@9MHz = 90000           (ST ships 9000)
The HAL's way         →  64000@64MHz = 1ms           (computed from SystemCoreClock, CALIB never read)
```

With the formulas laid out, the whole piece's answer folds into a few sentences. On this 64MHz board of ours, the calibration route cannot produce an exact 1ms: on real hardware, deduction from the manuals gives 1.125ms, 12.5% slow, and on the model it is blunter still — CLKSOURCE is simply bounced back, and the external clock cannot be attached at all. The HAL's configuration is the right answer; it takes a reload value computed on the spot from the clock frequency. The title CALIB holds by the architecture is a 10ms reference configurable without knowing the clock frequency, and ST changed it on F1 into a special-purpose 1ms@9MHz value. That gap between the name and the reality is the root of wiping out when you carry the number away verbatim.

## Wrap-up: the time station's five main pieces, done

Writing up to here, the time station's main pieces are done: from piece 01's TIM family and debouncing, through configuration in the type, the edge of the range, and NVIC preemption priorities, to this piece's SysTick time base — all five have landed. Looking back, one thread stands out clearer: in piece 01 we used HalTickClock as the logger's clock, saying at the time that we wouldn't reopen that battlefield. This piece finally opened it, following the HAL_GetTick it wraps all the way down to that 63999 in LOAD. In the 00 station's experiment three we checked uwTick head-to-head against virtual time, trusting uwTick. This piece swapped the roles: what we trust is Tim3's reference beat, and uwTick became the one under test.

One small thing to hand over to you: uwTick is a 32-bit unsigned integer, and one wraparound takes about 49.7 days — tick.hpp's comment says display-only. That is exactly what [Putting the logger to work: two ways to write it and two surfaces to read it](../06-patterns/07-logger-dual-syntax-usage.md) covered: don't subtract two log timestamps directly as an interval; across a wraparound, what you get is wrong.

The time station ends here for now. The PWM breathing-light piece waits until the library-side material matures. The next batch returns to the button station for its follow-ups. The titles will be announced once the pieces land; we won't reserve space for them here.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| Carrying RM0008's 9000 into the reload value verbatim, the whole board's time runs 7 times fast | 9000 is 1ms only at 9MHz; on the 64MHz processor clock, one beat is just 0.140625ms | Do it the HAL way: SysTick_Config(SystemCoreClock / 1000) |
| Writing CLKSOURCE=0 hoping to switch to the external clock, but 7 reads back | the Renode model has NOREF=1 and bounces the write per DDI 0403's forced-to-1 rule; real hardware should accept it per the manuals | Read CALIB.NOREF before choosing the route; on the model, configure by the processor clock |
| Taking CALIB.TENMS as a universal value and carrying it to other boards | TENMS's architectural title is a 10ms reload value, and F1's 9000 is special to 9MHz — change the board or the clock and it must be recomputed | Work out the clock before picking the number; verify head-to-head against a reference clock |
| Assuming the HAL reads the calibration value as a fallback | not one place in the whole HAL Src tree reads SysTick->CALIB; the reload value is only ever computed from SystemCoreClock | After clock reconfiguration, the tail of HAL_RCC_ClockConfig re-calls HAL_InitTick and the reload value follows on its own; check other libraries separately |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (tick.hpp and hal/kernel.hpp under include/libestdx/boards/stm32f1/, submodule pinned at commit 8026841)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="HalTickClock, USE_RTOS, and TICK_INT_PRIORITY; the probe examples/12_systick_probe is new in this piece and not folded into the submodule — the library itself remains pinned at 8026841"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="stm32f1xx_hal.c and stm32f1xx_hal_cortex.c (vendored source)"
    chapter="The HAL_InitTick body, HAL_SYSTICK_Config's one-line hand-off, HAL_IncTick/HAL_GetTick, uwTickFreq's 1KHz default; text frozen in the libestdx submodule third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/, both files in the Src/ subdirectory"
  />
  <ReferenceItem
    :id="3"
    author="ARM Limited / STMicroelectronics"
    title="CMSIS core_cm3.h (vendored source)"
    chapter="The four SysTick_Type registers, the CTRL bit definitions, and SysTick_Config's four write lines; text frozen in the libestdx submodule third_party/STM32F1/Drivers/CMSIS/"
  />
  <ReferenceItem
    :id="4"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107（Rev 21）"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="10.1.1 SysTick calibration value register (page 197); the clock-tree sentence in the RCC chapter feeding SysTick the HCLK/8 external clock (page 93)"
  />
  <ReferenceItem
    :id="5"
    author="ARM Limited"
    title="ARMv7-M Architecture Reference Manual（DDI 0403）"
    url="https://support.arm.com/documentation/ddi0403/d"
    chapter="The SYST_CSR.CLKSOURCE definition page; the SYST_CALIB page's TENMS and NOREF field definitions and the forced-to-1 rule"
  />
</ReferenceCard>
