---
title: 'From configuration to toggling: four bits per pin, and atomic set/reset'
description: 'The full run of bare-register LED blinking, connected end to end: CRL/CRH set aside four configuration bits per pin, and the pin13 offset (13-8)×4=20 is plain arithmetic; the MODE and CNF combination table carried from the manual into code; a real crash from writing into the wrong half of a register, reproduced in Renode — CRH unconfigured, the output driver never enabled, BSRR written all the same while ODR does not budge; then the three routes for toggling a pin: the three-instruction window of the ODR read-modify-write, the atomic single write of BSRR/BRR, and the bit-band alias region mapping one bit to one address, arithmetic and live test included — every reading was actually run and is reproducible'
chapter: 1
order: 4
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/04-gpio-registers.md
  source_hash: a589719144cec48b818b9eeea5d1194caf3aebda44a01c298ea40a294210ce2c
  translated_at: '2026-09-27T05:48:24+00:00'
  engine: anthropic
  token_count: 7800
---

# From configuration to toggling: four bits per pin, and atomic set/reset

The clock switch is closed (piece three), and we have the lay of the addresses (piece two, MMIO); what is left in our hands today is just two jobs: **configuring the pin** (writing the four mode bits) and **toggling the pin** (writing the output bit). With both done, the whole route of bare-register LED blinking is open through and through. From here on, whenever you read any peripheral library's GPIO code, what you see underneath will be addresses and bits.

## The configuration registers: four bits per pin

Sixteen pins to configure, yet the registers spare only one pair: `CRL` governs pin0-7, `CRH` governs pin8-15. Every pin gets four bits: the low two are `MODE[1:0]`, the high two are `CNF[1:0]`. PC13 sits in the high half that CRH governs, and we can work out its offset with one line of arithmetic: `(13 - 8) × 4 = 20`, so its four bits are CRH's bit23-20. That is exactly how the macros in the device header are written (`stm32f103xb.h` L1576-L1632):

```cpp
#define GPIO_CRH_MODE13_Pos   (20U)
#define GPIO_CRH_MODE13_Msk   (0x3UL << GPIO_CRH_MODE13_Pos)   // 0x00300000
#define GPIO_CRH_CNF13_Pos    (22U)
#define GPIO_CRH_CNF13_Msk    (0x3UL << GPIO_CRH_CNF13_Pos)    // 0x00C00000
```

Four bits can make sixteen combinations in all; we list the eight commonly used ones in a table (the complete table is in chapter 9 of RM0008 — RM0008 being ST's STM32F1 reference manual, which we already paged through in piece 01; every row below is carried over straight from the manual):

| MODE | CNF | Meaning        | Typical use                                    |
| ---- | --- | -------------- | ---------------------------------------------- |
| 00   | 00  | Analog input   | ADC sampling                                   |
| 00   | 01  | Floating input | Reset default; fine when the external signal's level is stable |
| 00   | 10  | Input with pull-up/pull-down | Buttons (on F1, pull-up vs pull-down is decided by ODR — see piece nine) |
| 01   | —   | 10MHz output   | Medium-speed drive                             |
| 10   | 00  | 2MHz push-pull output | Our choice for lighting the LED          |
| 10   | 01  | 2MHz open-drain output | Buses like I2C (see the fifth station)  |
| 11   | 00  | 50MHz push-pull output | High-speed drive                        |
| 1x   | 1x  | Alternate-function output | When an on-chip peripheral takes over the pin |

We want PC13 to serve as an ordinary 2MHz push-pull output pin: `CNF=00, MODE=10`, the four bits together making `0010`, and landed on its bit positions that becomes `0x2 << 20 = 0x00200000`. Line it up bit by bit against the `CRH = 0x00200000` we read back in Renode in the second piece, and every single bit matches.

We have drawn PC13's positions in CRH, BSRR, and ODR into the comparison diagram below — one glance and you can line them up:
![PC13's bit positions in CRH, BSRR, and ODR](./04-gpio-bit-layout.drawio)

The post-reset default values also deserve a look. The manual gives the CRL/CRH reset value as `0x44444444` (so many fours); split apart, every pin's share is `0x4` — that is, `CNF=01, MODE=00`, floating input. So after power-on, every pin defaults to input, waiting for you to configure it.

> Renode's model diverges from the manual on one more point: the reset value it gives is `0x00000000`, and that zero is exactly what we read in the simulator. The previous piece's gating discrepancy was "the model under-counts a constraint"; this time the fork is "the model does not set defaults per the manual". The principle stands: **for logic, trust the model; for default values and timing, trust the manual**.

With that, we can now read exactly what these two configuration lines are doing:

```cpp
GPIOC->CRH &= ~(0xFu << 20);   // clear PC13's four old configuration bits
GPIOC->CRH |=  (0x2u << 20);   // write the new config: 0010 = 2MHz push-pull output
```

We already ran an identity check on these two lines in the second piece's disassembly: `bic.w` clears the bits, `orr.w` sets them, and both read-modify-write rounds landed in `0x40011004`.

## A crash on the record: the price of writing into the wrong half

The example's first version contains a crash the author personally lived through, with a comment left behind at the scene. Thanks to git, it has been dug back out:

```cpp
// 2) PC13 mode: the config slot for pin ≥ 8 lives in CRH (4 bits per pin:
//    pin13 → offset (13-8)*4=20, CNF takes bit23:22, MODE takes bit21:20).
//    Version one wrote this slot into CRL (configuring pin5 instead), so
//    PC13 stayed at its reset-default floating input and BSRR/BRR writes
//    drove no pin — in Renode the ODR reading never budged, caught live.
```

Where was the mistake? The author wrote the configuration into `CRL` — the low half, the one governing pin0-7. With `0x2 << 20` landing in CRL, the pin that got configured was `(20-0)/4 = 5`: PC5 inexplicably became a 2MHz push-pull output, while PC13's four bits stayed untouched, still the reset-default floating input.

Let's reproduce the crash scene in Renode: clock switch closed, **CRH deliberately left unconfigured**, write to BSRR straight away:

```text
--- CRH before (model reset):
0x00000000
--- write 0x00002000 to BSRR, then ODR:
0x00000000        ← ODR does not budge
```

What makes this bug so sneaky? Zero warnings at compile time, zero errors at run time. The write to BSRR "succeeds" and returns, the program keeps running its loop, and the pin never moves. When you really do crash like this, the first reflex of our debugging routine is to read CRH/ODR and take a look; if the configuration never landed, then every BSRR write that follows is spinning its wheels. As for "why does the output register not drive the pin in floating-input mode"? The circuit relationship between the two MOS transistors inside the pin and the four configuration bits is exactly the next piece's main topic, so we will not expand on it here.

## Three routes to toggle

The configuration is written and PC13 is now an output pin. Next we want it to toggle between high and low, and the manual offers three routes.

The handiest route is to modify ODR directly: `GPIOC->ODR ^= 0x2000`, or use `|=` to set a bit and `&= ~` to clear one. The work is done by the read-modify-write trio we met in the second piece: `ldr` reads back, `orr`/`bic`/`eor` modifies, `str` writes back. The trouble with this first route: **the read and the write are two separate accesses, with a window of time in between**.

What can go wrong inside that window? Let's lay out the worst ordering: the main loop's read-modify-write of CRH has just finished its "read" and "modify", the "write" step has yet to come, and in walks an interrupt — the ISR (interrupt service routine) touches the same register too. When the interrupt arrives, the CPU goes off to execute precisely that. When the interrupt returns, the main loop carries on with its unfinished "write": it writes the stale value it was holding, plus its own modification, back into CRH — and whatever the ISR had just written in gets silently overwritten. On a single-threaded bare-metal machine nobody competes with you, so the window is harmless. But the moment an interrupt and the main loop share a peripheral, it becomes a genuine race condition. We will note this hazard today and deal with it when we actually meet it — that waits for the button interrupts of the next station.

The protagonists of the second route are BSRR and BRR: the manual sets this pair of registers aside so that one write can set one bit right. We just met BSRR in the crash log — with the configuration not landed, it was the one that no amount of writing could get to drive the pin. This time the configuration is correct, so let's go straight to the instructions:

```text
08000178: 6125    str   r5, [r4, #16]   ; BSRR ← 0x2000, set PC13
08000182: 6165    str   r5, [r4, #20]   ; BRR  ← 0x2000, reset PC13
```

Count along: one `str`, and the job is done.

Where does the atomicity come from? From BSRR's semantics: **writing 1 takes effect, writing 0 is ignored**. This 32-bit register is used as two halves. The low half-word, bit0-15, is the set zone: whichever bit you write, that pin is driven to the high level. The high half-word, bit16-31, is the reset zone: whichever bit you write, that pin is pulled low. Nowhere in the action is there a "read back the old value" step: the hardware processes the write bit by bit, independently, and each bit looks only at what you wrote. The window the previous route left behind simply does not exist here.

BRR's job is simpler still: it pulls BSRR's reset half out on its own and makes it a register dedicated to pulling low, which is more straightforward for us to use. And what if a single write sets 1 in both the set bit and the reset bit of the same pin? The set zone wins — that is spelled out in RM0008's own words.

Our example `01_register_led` takes this second route. Turning the LED off is done by writing `0x2000` (1<<13) to BSRR — writing `BS13` to 1 drives PC13 high. Turning it on is done by writing `0x2000` to BRR — writing `BR13` to 1 pulls PC13 low. Here is a detail that feels backwards at first sight: on the Blue Pill the LED hangs off PC13, and it lights on a low level specifically, so pulling low means lit and driving high means dark — "reset" is what lights the LED. It is easy to memorize backwards when starting out; write it twice and your hand settles.

<Anim id="f103-odr-vs-bsrr" />

The last route is the bit band: give a single bit its own dedicated address — Cortex-M3 builds this mechanism into the hardware. It covers the first megabyte of the peripheral region (`0x40000000`-`0x400FFFFF`, GPIOC's `0x40011000` included) plus the first megabyte of SRAM. Every bit in those two ranges has its own 32-bit address out in the alias region. When you write a value to an alias address, the hardware looks only at bit[0] of the value written: write 1 and the original bit is set, write 0 and the original bit is cleared. This is a hardware guarantee, so a single access is inherently atomic. As for the address arithmetic, let's work it out live:

Grab a little notebook and write along with me, OwO:

```text
alias address = 0x42000000 + (byte offset × 32) + (bit number × 4)

bit13 of GPIOC_ODR:
0x42000000 + (0x4001100C - 0x40000000) × 32 + 13 × 4
= 0x42000000 + 0x1100C × 0x20 + 0x34
= 0x42000000 + 0x220180 + 0x34
= 0x422201B4
```

But our word alone proves nothing — here is what Renode actually measured (clock switch closed):

```text
--- bit-band write 0x1 to 0x422201B4, then ODR:
0x00002000        ← bit13 rises on the spot
--- bit-band write 0x0 to 0x422201B4, then ODR:
0x00000000        ← and falls on the spot
```

The bit band's advantage: one bit is one dedicated address, a single instruction finishes the write, even BSRR's "compute the mask" step is saved, and reading a single bit is just as atomic. The price is that the magic-number address reads terribly, which is why bare bit-band writes are rare in library code — when we use it, we usually wrap it in a macro. Now let's lay the three routes side by side:

| Route                 | Instructions | Atomicity          | Reading back         | Best for                                  |
| --------------------- | ------------ | ------------------ | -------------------- | ----------------------------------------- |
| ODR read-modify-write | 3            | no window guard    | can read on the way | single-threaded, no interference to mind  |
| BSRR/BRR              | 1            | atomic single write | no                 | the right pick when interrupts are present |
| Bit-band alias        | 1            | atomic read and write | single bit readable | high-frequency single-bit ops, ISR flags |

With this step reached, the bare-register LED job is fully done: turning on the clock was the third piece's business, and both configuration and toggling have landed in this one. But a few questions we have been using all along remain unexplained: why exactly does `CNF=00` deserve the name "push-pull"? Where exactly is a floating input "floating"? And when the four configuration bits get turned, what happens inside the pin's circuitry? In the next piece we go down to the circuit level and settle these questions.

## Your turn to get hands-on

1. Work out PC2's four bits yourself: does it live in CRL or CRH? Which four bits are they? With a 2MHz push-pull output, how would you write the two configuration lines?
2. After closing the clock switch in Renode, configure CRH by hand to `0x00300000` (MODE=11, 50MHz), run the firmware again, watch whether the toggling behavior shows any visible difference, and think about why.
3. Use the bit-band formula to hand-compute the alias address of bit0 of IDR (`0x40011008`); read it in Renode and compare the result against reading IDR directly.
4. Finally, change the BSRR line in `register_led`'s `main` to `GPIOC->ODR ^= 0x2000`, disassemble both versions side by side, and see how many instructions `main` gains or loses.

## Hey hey! Self-check before you go

- Why do PC13's four bits sit at CRH's bit23-20? Derive it on the spot with `(13-8)×4`.
- Take the reset default `0x44444444`, split it apart and count: what is each pin's default configuration? Why does this default matter for power-on safety?
- BSRR's atomicity: does it rest on "a single instruction", or on the semantics of "write 1 takes effect, write 0 is ignored"? Would it survive losing either one?
- Where does the `0x34` in the bit-band address `0x422201B4` come from? Substitute back into the formula above and compute it.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="9.1 GPIO functional description; 9.2.1-9.2.2 CRL/CRH; 9.2.5-9.2.6 BSRR/BRR"
  />
  <ReferenceItem
    :id="2"
    author="Arm Ltd."
    title="Arm Cortex-M3 Processor Technical Reference Manual (DDI 0337)"
    :year="2021"
    url="https://developer.arm.com/documentation/ddi0337"
    chapter="4.2 Bit-banding"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f103xb.h — CMSIS Device Header"
    :year="2016"
    url="https://github.com/STMicroelectronics/cmsis_device_f1/blob/master/Include/stm32f103xb.h"
    chapter="L1576-L1632 GPIO_CRH_MODE/CNF definitions"
  />
</ReferenceCard>
