---
title: 'Clock gating: when the LED will not light, start from the clock'
description: 'Every peripheral has an independent clock gate, and bit 4 of APB2ENR is GPIOC''s clock switch: we start from the three read-modify-write instructions that the clock-enabling line becomes in the disassembly, then take apart what the extra dummy read inside the HAL macro is for; then this piece''s main event — writing a register while the clock is off: the manual says the write does not count, a real board raises no error, and even a forced GDB write accomplishes nothing, while a Renode experiment reveals that its GPIO model does not simulate gating at all, and the simulator''s fidelity boundary stands exposed; finally the 64 MHz clock tree worked out: HSI division and PLL multiplication, and the two APB buses'' separate limits — every reading and instruction was actually run and is reproducible'
chapter: 1
order: 3
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/03-rcc-clock-gate.md
  source_hash: 13e218b549a959beb5779b38d0cb70b4ada0cbae2cd89f356d32a9ea60089e58
  translated_at: '2026-09-27T05:44:57+00:00'
  engine: anthropic
  token_count: 2800
---

# Clock gating: when the LED will not light, start from the clock

The previous piece left you with a question at its end: why must that clock-enabling line in `main` be written at all, and what would the scene look like with that one line missing? This piece is where we clear it up.

**When a peripheral refuses to work, the first thing to check is always whether its clock is on**. That is no exaggeration. Of the problems newcomers face — code perfectly correct, compiles without a warning, runs without an error, and the LED just will not light — more than half stumble on this very line, and they stumble silently: no error code, no exception, no hint. The value you write does not take effect, and nobody tells you it did not take effect. We call it a "silent refusal".

## Why every peripheral's clock starts out off

Look at the digital-circuit side: it does all of its work by clock signal. When a flip-flop takes its input, when it flips its output — everything follows the clock's beat. Once the clock stops, the whole module stops with it. It is still physically there, mind you; it just does not respond to writes at all.

So why does ST not keep every peripheral clocked at all times? To save power. The STM32F103C8T6 integrates dozens of peripherals: five GPIO ports, four or five timers, three UARTs, two each of SPI, I2C, and ADC, plus DMA, USB, and CAN on top. Turn them all on, and even if all you do is blink an LED on one pin, every unused peripheral is drawing current. Battery-powered devices would be the first to object. ST's answer is **clock gating**: a gate on each peripheral's clock line, with the gate's opening and closing controlled by software. Whoever you use, you open their gate; the ones you do not use stay closed, and after reset the default is all closed.

The module in charge of these gates is called the **RCC** (Reset and Clock Control), and its base address is `0x40021000` — one of the two addresses that showed up in the previous piece's disassembly literal pool. RCC's job list has three entries: pick the clock source, manage division and multiplication, and switch each peripheral's clock on and off. Today we only touch its gates.

## APB2ENR: quite a row of breakers

The gate registers are split by bus: peripherals on the APB2 bus answer to `RCC_APB2ENR` (offset `0x18`, address `0x40021018`), those on APB1 to `RCC_APB1ENR`. GPIOs across the whole family all hang off APB2, so when we enable GPIOC's clock, this is the one we touch:

```cpp
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;  // close GPIOC's breaker
```

Let's go find `IOPCEN`'s definition in the device header (`stm32f103xb.h` L1287-L1289):

```cpp
#define RCC_APB2ENR_IOPCEN_Pos   (4U)
#define RCC_APB2ENR_IOPCEN_Msk   (0x1UL << RCC_APB2ENR_IOPCEN_Pos)  // 0x00000010
#define RCC_APB2ENR_IOPCEN       RCC_APB2ENR_IOPCEN_Msk             // I/O port C clock enable
```

The one we are after is bit 4. Its neighbors on the same row of breakers: bit 2 is GPIOA, bit 3 is GPIOB, bit 12 is SPI1, and bit 14 is USART1. The three instructions we met in the previous piece now have their full answer — what they were doing is read, modify, write: `ldr r3, [r2, #24]`, `orr.w r3, r3, #16`, `str r3, [r2, #24]`, and bit 4 stands up. Once we get the firmware running and have Renode read this address, the report is exactly `0x00000010`: one single bit standing in the whole register, every other breaker off.

## The HAL's clock-enabling macro: one extra dummy read

Now let's see how the HAL does the same job: it relies on a macro like this (`stm32f1xx_hal_rcc.h` L517-L523, copied verbatim from the source):

```c
#define __HAL_RCC_GPIOC_CLK_ENABLE()   do { \
                                        __IO uint32_t tmpreg; \
                                        SET_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);\
                                        /* Delay after an RCC peripheral clock enabling */\
                                        tmpreg = READ_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);\
                                        UNUSED(tmpreg); \
                                      } while(0U)
```

`SET_BIT` expands to that same old read-modify-write — no essential difference from our hand-written `|=`. The interesting part is the "dummy read" after it: `tmpreg` is read out and thrown away, `UNUSED` takes care of the unused-variable warning, and the whole thing looks purely superfluous. The answer sits right in the comment inside the macro: `Delay after an RCC peripheral clock enabling`. What the manual demands is a gap of a few clock cycles between closing the breaker and accessing the peripheral's registers, so that the clock signal on the peripheral's side has time to settle. The time one read of APB2ENR takes happens to swallow exactly that delay. The official library has even this timing buffer worked out for you — this is one genuinely valuable layer inside the "wrapping paper", and we will take a closer look in the piece where we unwrap the HAL.

<Anim id="f103-clock-gate-sequence" />

## The experiment with the clock off: three answers from the manual, the board, and the simulator

Now we run the experiment in earnest: with the clock off, write a value to GPIOC's registers — what happens?

We will hear each of them out. **The manual's answer**: the value you write does not count. A peripheral without a clock simply cannot have its internal sequential logic accept the write. ST's STM32F1 reference manual is explicit about this class of behavior — this is the "silent refusal" written down in black and white.

**The board's answer**: the write lands to no effect and raises no error. The most devious scenario is troubleshooting with a debugger: you connect GDB, force a write to CRH with `set *(int*)0x40011004 = 0x00200000`, hit Enter, and the read-back still shows the reset value `0x44444444`. You suspect a wrong address, a syntax slip, a broken debugger — half an hour of thrashing later, it turns out GPIOC's breaker was never closed at all. The debugger can perfectly well read and write these addresses directly, but with GPIOC's clock off, whatever gets written still does not count.

**The simulator's answer**: now the show gets good. We reproduce this experiment in Renode. The machine has the firmware loaded but is halted at reset (not a single user instruction has run; APB2ENR still holds its reset value `0x00000000`), and then we do the firmware's job for it by hand:

```text
=== EXP-1: machine halted, GPIOC clock is OFF (reset state) ===
--- APB2ENR (expect 0x00000000):
0x00000000
--- manual write 0x33333333 to CRH(0x40011004), then read back:
0x33333333        ← the write went in??
--- manual write 0x00002000 to BSRR(0x40011010), ODR(0x4001100C) after:
0x00002000        ← ODR really did flip
```

With the clock off, we write CRH and read back exactly `0x33333333` — the write took effect. When we write BSRR, ODR flips just as obediently. We then close bit 4 by hand and run it all over again, and the behavior is truly identical: **Renode's STM32F1 GPIO model, as it turns out, does not simulate clock gating at all**.

This deviation arrives at a useful moment — it hands us a reminder. The simulator is this volume's main observation instrument, but it is really a **behavioral model**, not an equivalent replica of the circuit: for details it does not implement, it puts on a "looks like it runs" face. Clock gating belongs to the electrical layer of the contract, and Renode's GPIO model chooses not to mind that layer — so code that would hang dead on a real board cheerfully blinks its LED in the simulator. And the converse direction is worth even more: **code that passes in the simulator is not guaranteed to be honored on a real board**. On questions of logic, Renode gets the final say; on timing and electrical conventions, the manual does. We listen to both sides — a habit we start forming today.

## Where the current comes from: how the 64 MHz is worked out

The current flowing behind the gates is the clock signal itself — where it comes from, and at how many volts and how many hertz, deserves a look too. In `01_blinky`, `SystemClock_Config` sets it up like this (an excerpt from `main.cpp`; five of the comments are verbatim from the project, the other two are notes the author added):

```cpp
osc.OscillatorType   = RCC_OSCILLATORTYPE_HSI;      // on-chip 8 MHz RC oscillator
osc.PLL.PLLSource    = RCC_PLLSOURCE_HSI_DIV2;      // 8M/2 = 4M into the PLL
osc.PLL.PLLMUL       = RCC_PLL_MUL16;               // 4M × 16 = 64M
clk.SYSCLKSource     = RCC_SYSCLKSOURCE_PLLCLK;     // PLL output as the system master clock
clk.AHBCLKDivider    = RCC_SYSCLK_DIV1;             // HCLK  = 64M
clk.APB1CLKDivider   = RCC_HCLK_DIV2;               // APB1  = 32M
clk.APB2CLKDivider   = RCC_HCLK_DIV1;               // APB2  = 64M
```

Let's draw the whole chain as a diagram:

![The simplified clock tree under our project's configuration: HSI 8 MHz is divided by 2 into the PLL for a 16x multiply to 64 MHz, AHB runs undivided, APB1 is divided by 2 down to 32 MHz, and APB2 passes 64 MHz straight through](./03-clock-tree.drawio)

We do the arithmetic in three steps.

Step one: the 8 MHz HSI (the on-chip RC oscillator — no external crystal to solder) is too slow for a Cortex-M3, so we rely on the PLL (phase-locked loop: essentially a frequency multiplier) to lift it to 64 MHz. How? Divide by 2, then multiply by 16 — `8 / 2 × 16 = 64`. The F103 manual's ceiling is 72 MHz, so 64 leaves a safety margin.

Step two: SYSCLK is out; it passes through the AHB divider to give HCLK (the beat the CPU and the bus matrix march to). We leave it undivided — HCLK = 64 MHz.

Step three: we hand HCLK out to the two APB peripheral buses. APB2 passes 64 MHz straight through, and everything that wants to run fast — GPIO, USART1, SPI1, TIM1 — hangs off it. APB1 gets divided by 2 for 32 MHz, because the peripherals sitting on APB1 — USART2/3, TIM2-4, I2C, SPI2/3 — carry a manual limit of 36 MHz; slamming 64 straight onto them would end badly, while 32 is exactly safe.

So when we "enable GPIOC's clock", what we actually enable is a 64 MHz beat that sets out from the HSI, gets multiplied through the PLL, rides AHB undivided, and is distributed by APB2 — with one last gate before it enters the GPIOC block, a gate that answers to RCC alone. The single-instruction `BSRR` write we laid down in the previous piece walks into its flip-flops on exactly this beat. The clock tree's full picture (the HSE, CSS, RTC, and watchdog branches) will wait for the stations that actually use them; the chain we covered today is enough for our needs.

## Hey, hey — run the self-check before you go

- Peripheral clocks default to all-off — for what purpose? Whose power do you think this design saves?
- Who does bit 4 of `RCC_APB2ENR` correspond to? And bit 14? Check the header file to verify.
- That dummy `tmpreg` read in the HAL macro — do you remember which problem it solves?
- Between "it runs in the simulator" and "it works on the board", how many kinds of difference still stand in between? Which kind did today's piece cover?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="6 Reset and clock control (RCC); 6.3.7 APB2 peripheral clock enable register"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="stm32f1xx_hal_rcc.h — HAL RCC Driver Header"
    :year="2016"
    url="https://github.com/STMicroelectronics/stm32f1xx_hal_driver/blob/master/Inc/stm32f1xx_hal_rcc.h"
    chapter="L517-L523 __HAL_RCC_GPIOC_CLK_ENABLE"
  />
  <ReferenceItem
    :id="3"
    author="Antmicro"
    title="Renode Documentation — STM32 Family Support"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/"
    chapter="Platform description; known model limitations"
  />
  <ReferenceItem
    :id="4"
    author="STMicroelectronics"
    title="STM32F103x8/xB Datasheet (DS5319)"
    :year="2015"
    url="https://www.st.com/resource/en/datasheet/stm32f103c8.pdf"
    chapter="5.3.22 Electrical characteristics; maximum frequencies"
  />
</ReferenceCard>
