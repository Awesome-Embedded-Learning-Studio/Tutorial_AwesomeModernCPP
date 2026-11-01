---
title: 'Who interrupts whom: how NVIC preemption priorities are arranged'
description: 'TimBase and TimOneShot in tim.hpp go through CMSIS single-argument NVIC_SetPriority, while exti.hpp and uart_irq.hpp take the HAL two-argument form with the sub-priority forever 0 — the two routes coexist in one firmware, and the library''s stance gives all 16 levels to preemption. RM0008 implements only 4 bits, 16 levels in all, and an out-of-bounds 16 rolls back through (16<<4)&0xFF into the highest level, 0. The Renode three-act probe 11_prio_probe pastes four log segments: higher-priority nesting, same-level non-nesting with suspended-then-served catch-up, IRQ-number ordering, and 16 written but read back as 0. The grouping ladder is recomputed group by group from the NVIC_EncodePriority source; at the reset default PRIGROUP=0 both routes write identical IP values, and change the grouping and they diverge. The HAL parks SysTick at the lowest 0x0F, and busy-waiting on the tick inside an ISR is a self-lock; the fix is NVIC_SetPriority(SysTick_IRQn, 0U) after clock reconfiguration'
chapter: 4
order: 4
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
  - 'The TIM family and confirmation debouncing: from a 1Hz heartbeat to a 20ms confirmation window'
  - 'The edge of the range: how long a period 16 bits times 16 bits can cover'
related:
  - 'Putting the logger to work: two ways to write it and two surfaces to read it'
translation:
  source: documents/vol8-domains/embedded/f103/04-time/04-nvic-preempt-priorities.md
  source_hash: 8fae3d5362eab0252516c393d238d8a6ae87a09b8e6a0218ef7165042ad86fda
  translated_at: '2026-11-01T03:20:13+00:00'
  engine: anthropic
  token_count: 3000
---

# Who interrupts whom: how NVIC preemption priorities are arranged

## Introduction: those numbers we filled in on faith

When [The edge of the range: how long a period 16 bits times 16 bits can cover](03-period-range-boundary.md) closed, it left word behind, and we bring it over as-is:

> Several timers will all want to interrupt the CPU at some moment; who goes first, and whose interrupt has to give way — the next piece takes apart NVIC preemption priorities. We have in fact already used them in piece 01: 09's `init(3)` and example 10's Touch=2, Window=3, SlowTick=4 arrangement — back then we just filled the numbers in as told, without ever taking the rule apart.

NVIC's full name is the Nested Vectored Interrupt Controller, the circuitry inside the Cortex-M3 core that manages interrupt queuing and admission. The preemption priority is that 3 you filled into `init(3)`; the smaller the number, the more entitled the interrupt to interrupt others. This piece carries one question: how does this number decide who interrupts whom, how does the hardware arbitrate when levels tie, and how does the hardware receive a number that lands out of bounds?

The material is still the same set as pieces 01 through 03, with our submodule pinned at commit 8026841. The experiments run in Renode as usual; we won't retell the pipeline discipline.

## Two routes in one firmware

The two lines where `TimBase`'s init reaches the NVIC, brought out of tim.hpp word for word:

```cpp
        NVIC_SetPriority(HW::irq_number(), preempt);
        NVIC_EnableIRQ(HW::irq_number());
```

`NVIC_SetPriority` is a CMSIS function (CMSIS being ARM's register-access layer for Cortex-M cores, the Cortex Microcontroller Software Interface Standard), and its parameter list takes only the IRQ number and the level. `TimOneShot`'s init is the same two lines; we won't repaste them.

exti.hpp and uart_irq.hpp take another road, pasted as well:

```cpp
HAL_NVIC_SetPriority(irq, preempt, 0);
HAL_NVIC_SetPriority(irq_number(), preempt, 0);
```

The upper line is exti.hpp's, the lower is uart_irq.hpp's. The HAL function takes one extra sub-level argument, and both routes fill it with 0. What that argument governs, we'll explain when section five takes apart grouping.

Inside example 10, the two routes live in the same firmware: the Exti that Touch uses walks the HAL route, the TimOneShot that Window uses and the TimBase that SlowTick uses walk the CMSIS route, with preemption priorities arranged as 2, 3, 4. The library's stance is plain in the source: all 16 levels go to preemption, sub-levels go unused, and we never once saw `HAL_NVIC_SetPriorityGrouping` called. But the grounds for that stance still leave one thing unexamined: what sits in the grouping register after reset. That too waits for section five; how the number reaches the hardware, we take next.

## Only the top four bits reach the hardware

The write line where CMSIS's `NVIC_SetPriority` really does its work at the bottom, brought out of core_cm3.h word for word:

```c
    NVIC->IP[((uint32_t)IRQn)]               = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
```

The IP registers (Interrupt Priority registers) are the level slots the NVIC keeps for every interrupt source, one byte each. A negative IRQn numbers a system exception — SysTick's number is −1 — and those take the `SCB->SHP` branch instead; the mechanism is the same as above, so we mention it in passing.

How far to shift is decided by `__NVIC_PRIO_BITS`, which in the F1 device header stm32f103xb.h reads like this<RefLink :id="4" preview="ARM Limited, stm32f103xb.h" />:

```c
#define __NVIC_PRIO_BITS           4U       /*!< STM32 uses 4 Bits for the Priority Levels    */
```

Substitute it into the write line and the expression becomes `(priority << 4) & 0xFF`: the number you fill is shifted left four bits and lands in the byte's top four. Chapter 10 of RM0008, the one on the NVIC, sits at page 197, and the feature page's original sentence is<RefLink :id="1" preview="STMicroelectronics, RM0008" />:

```text
16 programmable priority levels (4 bits of interrupt priority are used)
```

Four bits hold sixteen levels, and the smaller the number, the more entitled the interrupt to interrupt others. Substitute two numbers and watch: with `preempt=2`, `2<<4` gives `0x20`. With `preempt=15`, you get `0xF0`. And what of `preempt=16`? That number gets its own dissection in section five.

Page 198 of RM0008 (§10.1.2) is the vector table. In the peripheral-interrupt column, the Priority entry reads settable — that is, whatever you configure into the IP registers. The core-exception entries read negative and fixed: their levels are pinned by the architecture, out of the IP registers' reach. TIM2 sits at position 28 with table address 0x0000_00B0, TIM3 at 29, TIM4 at 30. The device header's `TIM2_IRQn = 28` is the same 28 as the vector table's. For now this number is mere identity information for us; in the next section it will decide who goes first among simultaneous pendings.

## The three-act probe: nesting, suspension, and going out of bounds

The probe is `examples/11_prio_probe`, newly built for this piece, on a clone tree at the same commit 8026841 as the submodule; we did not fold it into the submodule. It runs the same Renode pipeline piece 01 used. In the code below you will see the handler calling `Log::info` directly. The old boundary followed since piece 01 says don't call it inside an interrupt — the ISR touches only flags, and logging returns to the main loop; that rule stands for production code. This experiment measures millisecond-level timing itself, so the timestamp must be printed at the scene of the event: if we stored a flag and printed back in the main loop, what we would get is the drain moment, not the moment it happened, and the three acts would stop lining up. The logger's send is a bounded wait; in the worst case one line is dropped, and the machine cannot lock up. The log timestamp's zero point is HAL_Init, and we look only at relative spacing. The firmware's shape: Tim3 has a 100ms period and preemption level 3; its handler prints `T3+` on entry, waits 40ms to create an interruptible window, and prints `T3-` on exit. Tim2 has a 25ms period; we change its preemption level across the acts, and its handler prints just one `T2`. Tim3's handler, word for word below. The wait uses `WFI` (Wait For Interrupt, the instruction that lets the core sleep until the next interrupt arrives), relying on the 1ms SysTick beat to wake it up to check the clock:

```cpp
// Slow tick: check in first on ISR entry, wait 40ms to create an interruptible window, then exit.
// The wait uses WFI (waking on the 1ms SysTick beat to check the clock), not busy-spinning —
// in Renode, busy-spinning grinds along by instruction count, while WFI lets virtual time jump straight to the next interrupt
struct OnSlow {
    static void operator()() {
        Log::info(estdx::logger::Tag("ev"), "T3+");
        std::uint32_t t0 = HAL_GetTick();
        while (HAL_GetTick() - t0 < 40) {
            __WFI();
        }
        Log::info(estdx::logger::Tag("ev"), "T3-");
    }
};
```

The key section of main's initialization is the foundation for act A; the three SysTick-opening comment lines we save for later, when we dissect the wipeout:

```cpp
    HAL_Init();
    SystemClock_Config();
    // The library parks SysTick at TICK_INT_PRIORITY (15, the lowest). This probe needs an ISR busy-wait,
    // so the beat has to be raised to the highest level (placed after clock reconfiguration, or HAL_InitTick would write it back):
    // otherwise waiting on HAL_GetTick inside the busy-wait locks itself up — SysTick, pressed down by Tim3, would never advance
    NVIC_SetPriority(SysTick_IRQn, 0U);
    Serial::init();
    Slow::init(3);  // low preemption level (larger number)
    Fast::init(1);  // high preemption level (smaller number)
```

Act B's leveling relies on the runtime call `NVIC_SetPriority(TIM2_IRQn, 3)`; we won't paste its call line. The ordering test and act C use this stretch:

```cpp
    // Inside the interrupts-disabled window, two update events suspend at once; the instant the gate opens,
    // two same-level pendings — served first is TIM2 (28), the one with the smaller IRQ number,
    // and TIM3 (29) with the larger number queues behind
    __disable_irq();
    TIM3->SR = TIM_SR_UIF;
    TIM2->SR = TIM_SR_UIF;
    Log::info(estdx::logger::Tag("ph"), "arm");
    __enable_irq();
    wait_for(500);

    NVIC_SetPriority(TIM2_IRQn, 16);  // out of bounds: everything past the top 4 bits is shifted away
    Log::info(estdx::logger::Tag("ph"), "C");
    Log::info(estdx::logger::Tag("ph"), NVIC_GetPriority(TIM2_IRQn));
```

In act A we gave Tim2 preemption level 1 and Tim3 level 3, and the smaller number does the interrupting:

```text
[100ms][Info ][ev] T3+
[125ms][Info ][ev] T2
[140ms][Info ][ev] T3-
```

Let's read out the spacing of the three log lines: inside the window from T3+ at 100ms to T3- at 140ms, a T2 slips in at 125ms. What the hardware did at that moment we can also say plainly: the CPU was sitting inside Tim3's handler waiting when the higher-preemption Tim2 came due, and the NVIC simply borrowed the CPU out of the handler, running T2 to completion before handing control back. That is what we call nesting — and when we filled the numbers in back in piece 01, nesting is exactly what we were leaning on.

In act B we use the runtime `NVIC_SetPriority(TIM2_IRQn, 3)` to level Tim2 down to the same level 3, and the leveled picture is:

```text
[2400ms][Info ][ev] T3+
[2440ms][Info ][ev] T3-
[2440ms][Info ][ev] T2
```

Look inside the 2400-to-2440 window: no T2 line. That beat on the 25ms grid actually came due at 2425ms, but same levels do not nest, so on expiry T2 was recorded into its pending bit (pending: the state where an interrupt has happened and is queued waiting for the CPU — the hardware keeps a pending bit for every interrupt source). The moment Tim3's handler stepped out at 2440ms, that pending was admitted on the spot, which is why T3- and the catch-up T2 carry the same millisecond stamp. A pending request is not lost — only postponed.

The ordering test sits at the tail of act B, in that window between `__disable_irq` and `__enable_irq` in the code. During the interrupts-off interval we set TIM3's and TIM2's update flags, added one `arm` log line, and only then opened the gate. The instant the gate opens, two same-level interrupts are pending at once — who goes first?

```text
[2440ms][Info ][ph] arm
[2450ms][Info ][ev] T2
[2475ms][Info ][ev] T2
[2500ms][Info ][ev] T3+
```

After `arm`, the first served is TIM2 with IRQ number 28: the manually suspended one gets served at 2450ms, the one that genuinely came due on the grid runs again at 2475ms — TIM2 is served twice in a row — and only at 2500ms does TIM3 with number 29 get its turn. When same-level interrupts pend simultaneously, the smaller number goes first. Even the genuinely-due T2 after the gate opened cut in ahead of TIM3 all the same. This is no Renode coincidence; ST wrote the ordering rule into the comment at the top of hal_cortex.c as three tiers, and here is the original:

```text
     -@- IRQ priority order (sorted by highest to lowest priority):
        (+@) Lowest preemption priority
        (+@) Lowest sub priority
        (+@) Lowest hardware priority (IRQ number)
```

The first tier governs nesting: whether you can interrupt at all is decided by the preemption level. When the first tier ties, the sub-level takes over, ordering simultaneous pendings. And when even the sub-level ties, the IRQ number calls it. In the probe the preemption levels were leveled and the library fills the sub-level with 0 forever, so the deciding vote fell to the third tier, the numbers. One more warning while we're here: the sub-level only orders simultaneous pendings; it cannot produce nesting. If you want another interrupt to interrupt you, the only knob is the preemption level.

Act C's turn is the out-of-bounds case. We write `NVIC_SetPriority(TIM2_IRQn, 16)` and immediately read the level back:

```text
[2940ms][Info ][ph] C
[2940ms][Info ][ph] 0
[3000ms][Info ][ev] T3+
[3025ms][Info ][ev] T2
[3040ms][Info ][ev] T3-
```

That `0` after `C` is the readback of `NVIC_GetPriority(TIM2_IRQn)`: we wrote 16, and what came back is 0. The behavior changed along with it — the 3025ms T2 cut back into T3's window, nesting restored, matching act A's picture again. The arithmetic of 16 becoming 0, we walk step by step in the next section.

After the run I executed the verdict script `check_probe.py` twice as well; five groups of assertions covered every log segment, and every run returned PASS<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />:

```text
prio probe check: PASS (高打低嵌套 / 同级不嵌套+追发 / IRQ号决序 / 16回卷成0并恢复嵌套)
```

## The out-of-bounds 16 and how grouping slices the bits

For the rollback arithmetic, we substitute numbers into that core_cm3.h line and walk it slowly. We already know `__NVIC_PRIO_BITS` is 4; substituted into the write line, the expression becomes `(priority << 4) & 0xFF`:

```text
preempt=2   →  2<<4   = 0x20
preempt=15  →  15<<4  = 0xF0
preempt=16  →  16<<4  = 0x100  →  &0xFF  →  0x00
```

0x00 is the numerically smallest level, and numerically smallest means highest. Writing 16 therefore behaves the same as writing 0 — you meant to make Tim2 the lowest and made it the highest instead. In act C we verified this with our own hands: there is no bounds check on the CMSIS side, and the read-back 0 together with the restored nesting is the evidence it ran.

Now the HAL two-argument three steps, one more look; the source is pasted word for word (stm32f1xx_hal_cortex.c):

```c
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority)
{
  uint32_t prioritygroup = 0x00U;

  /* Check the parameters */
  assert_param(IS_NVIC_SUB_PRIORITY(SubPriority));
  assert_param(IS_NVIC_PREEMPTION_PRIORITY(PreemptPriority));

  prioritygroup = NVIC_GetPriorityGrouping();

  NVIC_SetPriority(IRQn, NVIC_EncodePriority(prioritygroup, PreemptPriority, SubPriority));
}
```

The order of the three steps deserves a reading. Step one, `NVIC_GetPriorityGrouping()`, reads the grouping's runtime state back from AIRCR (the Application Interrupt and Reset Control Register in the SCB — the grouping's PRIGROUP field lives inside it). Step two belongs to `NVIC_EncodePriority`, which per that grouping's rule splices preempt and sub into one 4-bit IP value. Step three lands right back on CMSIS's `NVIC_SetPriority`. The two routes converge at the bottom layer; the only difference is whether the splicing happens. As for the two `assert_param` calls at the top — both are no-ops in the default tier, as piece 02 showed with its two-tier expansion; the out-of-bounds check cannot be counted on here.

The word grouping, we haven't taken apart yet. What it slices is those 4 level bits: Cortex-M3 lets you cut them into two segments at runtime. The first segment serves as preemption bits, deciding precisely who interrupts whom. The second serves as the sub-level, ordering simultaneous pendings only when the preemption bits tie — it cannot produce nesting by itself. The cut is set by AIRCR's PRIGROUP field, and the HAL gives the common cuts macro names, the macro values being PRIGROUP's raw values. `NVIC_PRIORITYGROUP_4` carries the macro value 0x00000003U and governs 4 preemption bits plus 0 sub bits. `NVIC_PRIORITYGROUP_3` carries 0x00000004U, cutting 3 preemption bits plus 1 sub bit. `NVIC_PRIORITYGROUP_0` carries 0x00000007U, handing all 4 bits to the sub-level.

How one and the same number lands in IP under each grouping, we compute group by group straight off the `NVIC_EncodePriority` source:

| group (AIRCR.PRIGROUP) | HAL macro name | Preemption bits | Sub bits | IP for (2,0) | IP for (2,1) | IP for CMSIS single-arg 2 |
|---|---|---|---|---|---|---|
| 0 (reset default) | — (no macro) | 4 | 0 | 0x20 | 0x20 (sub discarded) | 0x20 |
| 3 | NVIC_PRIORITYGROUP_4 | 4 | 0 | 0x20 | 0x20 (sub discarded) | 0x20 |
| 4 | NVIC_PRIORITYGROUP_3 | 3 | 1 | 0x40 | 0x50 | 0x20 |
| 7 | NVIC_PRIORITYGROUP_0 | 0 | 4 | 0x00 (preempt discarded) | 0x10 | 0x20 |

Take the first two rows together. The reset default PRIGROUP=0 and GROUP_4's PRIGROUP=3 are bit-for-bit equivalent on a 4-bit implementation. The library tree indeed holds not one call to `HAL_NVIC_SetPriorityGrouping`; what sets the register at run time is the firmware's first step: `HAL_Init` (stm32f1xx_hal.c:157) sets the grouping to GROUP_4, and AIRCR lands on the GROUP_4 notch. Whether you compute from the reset value or from GROUP_4, the two notches are bit-for-bit equivalent, so the two routes (with sub forever 0 on the HAL side) write identical IP values. The premise is that the grouping stays at GROUP_4 or at a reset value bit-for-bit equivalent to it; the moment anyone changes the grouping, the IP values the two sides write part ways.

The third row is the `NVIC_PRIORITYGROUP_3` grouping (3 preemption bits, 1 sub bit). The CMSIS single-argument route still writes `preempt<<4`, but under this grouping the preemption level read back must be computed as `IP>>5`. We write 2 in, the IP becomes 0x20, and the preemption level read back is 1. The same 2 was written, the grouping changed once, and the meaning drifted from preemption level 2 to preemption level 1 plus sub-level 0. Not one digit changed — what changed is which bits the hardware takes as preemption bits.

The last row, `NVIC_PRIORITYGROUP_0` (0 preemption bits, 4 sub bits), is the extreme: not one preemption bit remains, the preempt argument is discarded wholesale, the IP written for (2,0) is 0x00, every interrupt in the system becomes same-level, and nesting will never happen once. This is not our inference; the HAL's comment sits right there in the original:

```text
@note When the NVIC_PriorityGroup_0 is selected, IRQ preemption is no more possible. The pending IRQ priority will be managed only by the subpriority.
```

The ladder table was computed group by group off the source; why ARM and ST designed it this way back then, we won't speak for. On the field-level details of the registers and grouping, RM0008's chapter 10 only points a direction — the body text covering those details is the PM0056 programming manual. This piece never fetched the actual document, quotes not one word of it, and leaves the card at the end.

## A SysTick wipeout: waiting on a beat you are yourself pressing down

The usage notes at the top of hal_cortex.c say it too; the original:

```text
Configures the SysTick IRQ priority to the lowest value 0x0F.
```

And the library side does exactly that; kernel.hpp's line 10, word for word:

```c
#define TICK_INT_PRIORITY 0x0FU /* SysTick gets the lowest priority */
```

0x0F is the last of the sixteen levels; anyone who comes along can press it down. While gathering material, I genuinely wiped out here: the first probe version had `OnSlow` busy-waiting on `HAL_GetTick` inside the handler, and the running log looked like this — after T3+, T2 lines came one after another several dozen strong, all their timestamps landing on the same millisecond, and the beat stopped from then on. The causal chain is not complicated: the moment Tim3 at preemption level 3 entered its handler, SysTick's 15 was pressed down. What `HAL_GetTick` reads is exactly the count SysTick feeds, and with it stopped, the busy-wait loop's condition stayed forever 40ms short and could not get out. Tim2, with the higher preemption level, did keep interrupting that stuck loop as usual, which is why dozens of T2 lines crowded onto one timestamp.

Levels in the NVIC form a total order: for any two interrupt sources we can say who presses whom. Waiting, inside an ISR, on a lower-level resource that you yourself are pressing down — the destination of that wait is a self-lock. The more common variant is calling HAL_Delay inside an ISR; it sticks for exactly the same reason, counting the very same counter. The fix we already pasted in the probe code: that `NVIC_SetPriority(SysTick_IRQn, 0U)` after `SystemClock_Config`, whose job is to raise the beat to the highest level. The placement cannot be too early — put before clock reconfiguration, the later-running `HAL_InitTick` would write it back.

## Wrap-up: next piece takes apart SysTick

SysTick lives inside the HAL's time base: that 1ms beat configured in HAL_Init is what HAL_GetTick and HAL_Delay both count. The wipeout we just dissected had it too — the HAL parked it at the lowest level, and it happened to be the very clock that busy-wait loop was watching. It also leaves one clue we haven't used: RM0008's 10.1.1 writes the calibration value 9000, which the 9MHz reference clock turns into 1ms. How is that number used, and on which clock is the 1ms beat actually configured? The next piece takes apart the SysTick time base.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| You fill 16 or larger hoping for the lowest level, and the behavior becomes the highest, interrupting everyone | (16<<4)&0xFF=0x00 — the out-of-bounds value rolls back, and neither CMSIS nor HAL bounds-checks | Use only 0 through 15; after configuring, read back with NVIC_GetPriority to confirm |
| The number set through the HAL two-argument form disagrees with the CMSIS single-argument read-back | the HAL encodes by the runtime AIRCR grouping while CMSIS single-arg always writes preempt<<4; change the grouping and the two sides' meanings fork | Check the live state with NVIC_GetPriorityGrouping and recompute against the grouping ladder table |
| Preemption numbers carried over from another project behave completely differently | the original project called HAL_NVIC_SetPriorityGrouping; the grouping differs, so the same IP reads back a different preemption level | Rearrange the numbers per the current grouping; this library never calls grouping, the run-time grouping is the GROUP_4 that HAL_Init sets, bit-for-bit equivalent to the reset default |
| Calling HAL_Delay or busy-waiting on HAL_GetTick inside an ISR sticks forever | the HAL parks SysTick at the lowest 0x0F; an ISR with a higher preemption level presses the beat down and the count stops | NVIC_SetPriority(SysTick_IRQn, 0U) after clock reconfiguration; move long work out of the ISR |
| You want one interrupt to interrupt another via sub-priority | sub-priority only orders simultaneous pendings and never produces nesting; nesting looks only at preemption bits | For nesting, give the interrupts different preemption levels |
| During a same-level interrupt's long handler, another interrupt runs one beat late, and you worry it was lost | same levels do not nest; on expiry the interrupt pends and is served catch-up after the handler exits — one expiry, one service | Not lost, only late; don't put long-window work in an ISR |

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107（Rev 21）"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="10 Nested vectored interrupt controller (NVIC); 10.1.1 SysTick calibration value register; 10.1.2 Interrupt and exception vectors"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (tim.hpp, exti.hpp, uart_irq.hpp, and hal/kernel.hpp under include/libestdx/boards/stm32f1/, submodule pinned at commit 8026841)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="TimBase/TimOneShot init, the HAL two-argument calls, TICK_INT_PRIORITY; the probe examples/11_prio_probe is new in this piece and not folded into the submodule — the library itself remains pinned at 8026841"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_cortex.c and stm32f1xx_hal_cortex.h (vendored source)"
    chapter="HAL_NVIC_SetPriority, the three-layer ordering comment, the NVIC_PriorityGroup_0 @note, the SysTick 0x0F line in the file-header usage notes, and the grouping macro values; text frozen in the libestdx submodule third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/, with stm32f1xx_hal_cortex.c in the Src/ subdirectory and stm32f1xx_hal_cortex.h in Inc/"
  />
  <ReferenceItem
    :id="4"
    author="ARM Limited / STMicroelectronics"
    title="CMSIS core_cm3.h and stm32f103xb.h (vendored source)"
    chapter="The __NVIC_SetPriority write line, __NVIC_PRIO_BITS=4, TIM2_IRQn=28; text frozen in the libestdx submodule third_party/STM32F1/Drivers/CMSIS/"
  />
  <ReferenceItem
    :id="5"
    author="STMicroelectronics"
    title="PM0056 Programming Manual — STM32F10xxx/20xxx/21xxx/L1xxxx Cortex-M3 programming manual"
    url="https://www.st.com/resource/en/programming_manual/pm0056-stm32f10xxx20xxx21xxxl1xxxx-cortexm3-programming-manual-stmicroelectronics.pdf"
    chapter="Programming manual; the field-level details of the NVIC registers and priority grouping live here; the body text of this piece does not quote it"
  />
</ReferenceCard>
