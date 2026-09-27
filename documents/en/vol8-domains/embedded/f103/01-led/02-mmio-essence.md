---
title: 'The essence of an operation: what we write is code, what we touch is addresses'
description: 'GPIOC->CRH |= x taken apart as far as it will go: the CMSIS structs and macros unfold layer by layer into a single read-modify-write on *(volatile uint32_t*)0x40011004, checked off instruction by instruction as ldr/orr/str in the disassembly, with 0x40011000 itself resting in the literal pool; then why the region from 0x40000000 upward is not memory but peripherals — writing an address is issuing an order, reading one is receiving a report; volatile draws the boundary of what the compiler may know, backed by a host-runnable online demo where a wait loop is deleted wholesale under -O2; then reinterpret_cast and uintptr_t revisited through modern C++ eyes; and to close, a census of all 778 instructions in the firmware, making good on the claim from the previous piece that the CPU only reads and writes to the outside — 278 memory accesses, all of them ldr/str family, zero exceptions; every address, instruction, and register reading in this piece was actually run and reproducible'
chapter: 1
order: 2
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/02-mmio-essence.md
  source_hash: 371227eb213c53bb7078a69307a890bfd513de840ca5dfe6a3a4773f9e5e8fe3
  translated_at: '2026-09-27T05:40:22+00:00'
  engine: anthropic
  token_count: 9500
---

# The essence of an operation: what we write is code, what we touch is addresses

We have seen the machine for what it is; now it is the code's turn. Take one more look at `main.cpp` from `00_my_blinky`: aha — wall-to-wall HAL library code, `HAL_Init` for initialization, `HAL_GPIO_TogglePin` for the GPIO writes. The part that erects all the details is buried entirely under the library.

> Some of you may be puzzled: why does this have to be spelled out? Can't we just write C++ and be done with it?
> No — and our reason is simple. Precisely because we intend to lean on C++'s zero-overhead abstractions, we must know what is going on underneath our world: what can be abstracted at compile time, and what cannot. Leave that unclear, and sooner or later the compiler's warnings and errors will scare us off, until all we can do is fob the newcomers off with: C++ can't do microcontrollers!

So let's dig it out with our own hands. The author has written a bare-register blinky with no GPIO wrapper whatsoever — it lights the very same PC13 as `01_blinky` — and `arm-none-eabi-size` puts them on the scale at 2380 bytes for one, 3468 bytes for the other.

```cpp
// third_party/libestdx/examples/01_register_led/main.cpp (excerpt)
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;  // turn on GPIOC's clock switch
GPIOC->CRH &= ~(0xFu << 20);         // clear PC13's four CNF/MODE bits
GPIOC->CRH |= (0x2u << 20);          // configure as 2 MHz output

GPIOC->BSRR = 1u << 13;              // PC13 output high (LED off)
GPIOC->BRR  = 1u << 13;              // PC13 output low (LED on)
```

From these five lines, let's pick out the most pressing questions: what are `RCC` and `GPIOC`? What are the names after the `->`? And where do `|=` and `=` part ways once they land on the machine? One at a time.

## Kick these abstractions aside! Answer me! What are they

`GPIOC` looks like a bona fide global object, but crack it open and it turns out to be a **macro** (C++ folks — do you already catch the whiff of `constexpr` waiting to strut its stuff here?). Trace it into the CMSIS device header `stm32f103xb.h`, and the definition chain looks like this:

```cpp
// Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h
#define PERIPH_BASE          0x40000000UL                    // L575
#define APB2PERIPH_BASE      (PERIPH_BASE + 0x00010000UL)    // L583
#define GPIOC_BASE           (APB2PERIPH_BASE + 0x00001000UL)// L604
#define GPIOC                ((GPIO_TypeDef *)GPIOC_BASE)    // L666
```

Run the three layers of macros to the end, and `GPIOC` is `(GPIO_TypeDef *)0x40011000`: a pointer to a fixed address, a C-style cast. There is no object to be found here, no construction — it is an address, pure and simple. And we can check the arithmetic on the spot: `0x40000000 + 0x10000 + 0x1000 = 0x40011000`.

And `GPIO_TypeDef`? Its true form is a struct of exactly seven members, every one of them typed `__IO uint32_t`:

```cpp
// stm32f103xb.h L357-L366
typedef struct
{
  __IO uint32_t CRL;   // offset 0x00: configuration of the low 8 pins
  __IO uint32_t CRH;   // offset 0x04: configuration of the high 8 pins
  __IO uint32_t IDR;   // offset 0x08: input data
  __IO uint32_t ODR;   // offset 0x0C: output data
  __IO uint32_t BSRR;  // offset 0x10: atomic set/reset
  __IO uint32_t BRR;   // offset 0x14: atomic reset
  __IO uint32_t LCKR;  // offset 0x18: configuration lock
} GPIO_TypeDef;
```

C and C++ have a ready-made rule for this: a struct member's address is the base address plus the offset. So the true form of `GPIOC->CRH` is `0x40011000 + 0x04`, and of `GPIOC->BSRR`, `0x40011000 + 0x10`. Back at the Getting Started station we repeatedly sampled `0x4001100C` in Renode, and at the time only announced its name: GPIOC's ODR (output data register). Now you can work it out yourself: `0x40011000 + 0x0C` — ODR's true self, nothing else.

The road from macro to address fits into one diagram — the arithmetic of the three macro layers on the left, the addresses the seven members each land on below, with the addresses this piece and the last have already made your acquaintance colored in:

![The GPIOC macro chain mapped onto GPIO_TypeDef offsets: at the top, PERIPH_BASE 0x40000000 goes through two additions (+0x10000, then +0x1000) to yield GPIOC_BASE 0x40011000; below, the seven members (CRL through LCKR, offsets 0x00–0x18) correspond one-to-one with their actual addresses, with the CRH/ODR/BSRR rows highlighted](./02-gpioc-macro-offsets.drawio)

Let's run an identity transformation on the weightiest of those five lines:

```cpp
GPIOC->CRH |= (0x2u << 20);
// equivalent to:
*((volatile unsigned int *)0x40011004) |= 0x00200000;
```

**What we write is code; what we touch is addresses.** In this line there is no function call, no abstraction, no magic — only a three-step move on a hardwired address: read it back, change a bit, write it out. **That one sentence is the entire foundation of embedded peripheral programming. And that is quite enough!**

## What sits behind an address is not memory

So what does sit behind the address `0x40011004`? In the previous piece we saw the blocks the architecture carves the 4 GB into; on our particular STM32F103, the three that matter day to day:

| Address range    | What it is | How it behaves          |
| ---------------- | ---------- | ----------------------- |
| from `0x08000000` | Flash     | Code and constants; survives power-off |
| from `0x20000000` | SRAM      | Variables; gone once power is cut |
| from `0x40000000` | Peripherals | Not storage — machinery |

Flash and SRAM are honest, true memory: whatever you write in is what you read out. The peripheral block is a completely different animal: write to these addresses and you are issuing an **order**; read from them and you are receiving a **report**. When we stuff `0x2000` into `0x40011010` (BSRR), the order is "drive GPIOC pin 13 to the high level". Read `0x4001100C` (ODR) afterwards, and the report is "the current output state of every pin on this port".

The two address personalities show up most clearly side by side:

![MMIO versus real memory: on the left, SRAM reads back the written 0x22 verbatim (symmetric); on the right, peripherals — writing BSRR is issuing an order (PC13 driven high), reading ODR is receiving a report (per-pin output states); a note at the bottom points out that both routes ride the same ldr/str set](./02-mmio-vs-memory.drawio)

In the previous piece we followed one `str` the whole way: the bus matrix routes that write, by address, to GPIOC's circuitry, and the flip-flops set or reset according to BSRR's bit definitions. No software takes part, no operating system either — the hardware looks things up by the address and simply gets the job done. This scheme of "driving peripherals through a unified memory address space" is called MMIO (memory-mapped I/O). Over on x86 there is another route, a separate instruction channel called port I/O; Cortex-M was born knowing only MMIO, which happens to be friendly to the compiler — accessing peripherals and accessing memory use the same set of load/store instructions.

We don't traffic in unverifiable claims here, so let's go straight to Renode and demand a report from these addresses (with `01_register_led` up and running):

```text
sysbus ReadDoubleWord 0x40011004   →  0x00200000
sysbus ReadDoubleWord 0x40021018   →  0x00000010
```

Who owns `0x40021018`? `RCC`'s base `0x40021000` plus offset `0x18` lands exactly on APB2ENR — the register in charge of the peripheral clock switches. The value read out is `0x10`, in binary `0001 0000`: bit 4 is standing there, steady as a rock — GPIOC's clock switch is closed. As for the `0x00200000` at `0x40011004`, the bit standing is bit 21, and the four configuration bits for PC13 in CRH ([23:20]) hold `0010` at this moment. These numbers were reported in real time by the circuitry behind those addresses — we did not make up a single one.

> RCC is short for reset and clock control — the reset and clock controller; every reset and every clock on the chip answers to it. Its offset table works just like GPIO's, and in the next piece we will go dismantle its gates.

## Disassembly: a read-modify-write in three instructions, a direct hit in one

The transformation was our paper derivation — but what did the compiler actually generate? Let's summon `arm-none-eabi-objdump -d` to do the honors. In the `main` of `01_register_led`, the three configuration lines came out like this:

```text
08000158: 4a0c      ldr   r2, [pc, #48]   ; r2 ← literal pool: 0x40021000 (RCC)
0800015a: 4c0d      ldr   r4, [pc, #52]   ; r4 ← literal pool: 0x40011000 (GPIOC)
0800015c: 6993      ldr   r3, [r2, #24]   ; r3 ← *(0x40021018)  read APB2ENR
0800015e: f043 0310 orr.w r3, r3, #16     ; r3 |= 0x10          set bit 4
08000162: 6193      str   r3, [r2, #24]   ; *(0x40021018) ← r3  write APB2ENR back
08000164: 6863      ldr   r3, [r4, #4]    ; r3 ← *(0x40011004)  read CRH
08000166: f423 0370 bic.w r3, r3, #15728640 ; r3 &= ~0x00F00000 clear [23:20]
0800016a: 6063      str   r3, [r4, #4]    ; write CRH back
0800016c: 6863      ldr   r3, [r4, #4]    ; read CRH again
0800016e: f443 1300 orr.w r3, r3, #2097152 ; r3 |= 0x00200000   set [21]
08000172: 6063      str   r3, [r4, #4]    ; write CRH back
```

Sure enough, `|=` and `&=` go the "read, modify, write" route in three-instruction groups; after the two rounds, CRH has been fully configured to `0x00200000` — not one character off from the value we just read in Renode. The two toggle lines get a different treatment:

```text
08000178: 6125      str   r5, [r4, #16]   ; *(0x40011010) ← 0x2000  write BSRR
0800017a: f000 f887 bl    800028c <HAL_Delay>
0800017e: f44f 70fa mov.w r0, #500
08000182: 6165      str   r5, [r4, #20]   ; *(0x40011014) ← 0x2000  write BRR
```

A single `str`, and it arrives in one step. Why does BSRR get to be this direct while the ODR side has to take the three-step detour? We will leave the full answer — the read-modify-write race under concurrency, and the atomicity engineered into BSRR — for the fourth piece to treat in depth; for now we merely lay the phenomenon on the table: **within the same register bank, different addresses have different temperaments, and those temperaments are set by the hardware designers — not something software gets to choose**.

The author has turned the two treatments into an animation: the top row is the read-modify-write triple of `|=`, the bottom row is the one-shot direct write of `=`. Step through it and watch how each side makes its way:

<Anim id="f103-rmw-vs-direct" />

The observant among you may also have noticed the strange addressing in `ldr r2, [pc, #48]`. Thumb instructions are only ever 16 or 32 bits wide, which cannot hold a 32-bit constant like `0x40021000`, so the compiler parks these constants in a "literal pool" behind the function's code and fetches them with PC-relative addressing. The two lines `.word 0x40021000` and `.word 0x40011000` at the end of `main` are the pool in person: when you want to know "which peripheral addresses does this program actually use", page through the literal pool and every address you are after is caught on the first grab.

`main` has had its close-up; now let's pull the lens back. The author's claim: **toward the outside world, the CPU does nothing but read and write.** Now that you can read disassembly, we can verify it on the spot — a census of the instructions in the whole firmware. The command is a three-leg relay: objdump spits out the disassembly, awk splits on tabs to carve out the mnemonic column (a disassembly line naturally falls into four columns — address, encoding, mnemonic, operands — while section names and function labels, which cannot fill three columns, get filtered out), and `sort | uniq -c | sort -rn` does the counting and ranks by frequency:

```text
$ arm-none-eabi-objdump -d build/examples/01_register_led/register_led \
    | awk -F'\t' 'NF>=3 {print $3}' | awk '{print $1}' \
    | sort | uniq -c | sort -rn | head -12
    185 ldr
     56 str
     45 cmp
     45 bl
     40 .word
     38 b.n
     35 movs
     35 lsls
     27 mov
     21 bic.w
     19 subs
     19 orr.w
```

Now let's sieve out the memory-touching family by keyword:

```text
$ arm-none-eabi-objdump -d build/examples/01_register_led/register_led \
    | awk -F'\t' 'NF>=3 {print $3}' | awk '{print $1}' \
    | grep -cE '^(ldr|str|ldrb|strb|ldrh|strh|ldm|stm|push|pop)'
278
```

Of the 278 that fall through the sieve, we have met every single one: they are all the ldr/str family — the byte variants ldrb/strb, the stack variants push/pop, the multi-register variants ldm/stm, all of them counted. The whole firmware comes to 778 instructions (the forty `.word` entries in the histogram are literal-pool data and take up no instruction slots), and the remaining 500 are pure register arithmetic and branches. Zero exceptions: the sentence laid down in the previous piece — "the CPU only reads and writes to the outside" — stands upright across all 778 instructions.

## volatile: drawing the line on what the compiler may know

Let's double back to that `__IO` in the struct definition and follow it all the way into `core_cm3.h` (the CMSIS core header):

```cpp
#define     __IO    volatile
```

Its true form is `volatile`. In this setting it acts as a **right-to-know declaration**, not some "optimization hint": the contents at this address will change at moments we cannot see, in ways we cannot see. Who changes them? The hardware does, an interrupt does — anything except the current code flow. So every read and write the compiler performs on this address must be the genuine article: no caching, no merging, and no freelancing based on "we just read that".

And what goes wrong without it? This time we need no physical board at all — the whole thing reproduces on the host. The example stocks two "status registers": one an ordinary variable without volatile, the other a variable qualified volatile; each waits for its signal — the value turning to 1 — before moving on:

<OnlineCompilerDemo allow-run
  title="volatile and the wait loop: two fates under -O2"
  source-path="code/examples/vol8/01_mmio_volatile.cpp"
  description="Two status variables each wait for a single set bit: press Run and the program terminates normally; open the assembly comparison — the plain version's wait loop is deleted wholesale by the compiler (it sees the assignment above, concludes the condition is always false, and never touches memory once), while the volatile version dutifully keeps its load loop. The right to know a peripheral's state hangs on this one keyword"
/>

Let's put in a fair word for the compiler: it is playing by the book. **Changes to an ordinary variable must be caused by the code flow**; the assignments it can see are the whole truth, so the loop is provably dead code. volatile redraws the boundary of that "seeing": the truth of this address rests in the hardware's hands, and every time you go read it, what you take away is the hardware's genuine state at that instant.

There are two more boundaries that often get stirred together; let's draw them cleanly as well. All `volatile` mandates is that "the access must actually happen" — it **offers no atomicity**: an interrupt landing in the middle of the three instructions of `CRH |= x` still sends things tumbling, and we will leave the ins and outs of that race to the fourth piece. Multi-core synchronization is `<atomic>`'s territory — volatile neither can nor should govern it. As for the full semantic dissection of volatile under the C++ standard, we already worked through it in the C-tutorial piece [Embedded C Programming Patterns](../../../../vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md), so we will not repeat it here.

## A second look through modern C++ eyes

With the sugar coating peeled all the way down, let's look again at that C-style cast of `GPIOC`, `(GPIO_TypeDef *)0x40011000`. In everyday C++ this style of cast is always looked down on, but here is one of its few legitimate scenes: **the address is fixed in stone by the hardware manual, not computed at runtime**. All modern C++ asks is to write the fact more honestly:

```cpp
#include <cstdint>

// address as a compile-time constant; use the explicit form for the cast
constexpr auto gpio_c =
    reinterpret_cast<volatile std::uint32_t *>(0x4001'1000u);

gpio_c[1] &= ~0x00F0'0000u;  // CRH: base + 4 bytes = offset 0x04
```

See what `reinterpret_cast` has over the parenthesized cast: it is searchable across the whole codebase, its intent is self-declared, and unlike the function-style cast it will not casually strip off `const` while it is at it. `std::uintptr_t`, for its part, is the correct vessel for "doing arithmetic on a pointer as an integer": the intermediate form of an address plus an offset rides in it safely, instead of overflowing on narrow platforms the way an `int` shoehorned into the job would.

One more step forward: fold "address plus offset" into a function too:

```cpp
constexpr auto &reg32(std::uintptr_t base, unsigned offset) {
    return *reinterpret_cast<volatile std::uint32_t *>(base + offset);
}

reg32(0x4001'1000u, 0x10) = 0x2000;  // GPIOC->BSRR = BS13
```

At this point the abstraction we have saved up is still very thin: one function, two lines of semantics, and in exchange every MMIO access has a unified entry point. Can we push one layer further? Pack the address and offset into types, so that a "GPIO output pin" and a "UART baud-rate register" are two different things at the type level, and a mis-assigned pin is rejected at compile time? That is the home ground of the eighth piece in this station — the `estdx::stm32f1::Gpio<Port, Mask, Dir>` template carries this very step to its end.

## At this point, the whole chain is connected

Let's string the parts we took apart today back together, in order: the source line `GPIOC->BSRR = 1u << 13` becomes the fixed address `0x40011010` after macro expansion; the compiler emits nothing but a single `str`; the bus routes that write to the GPIOC block; the flip-flop circuit drives pin 13's level up — and in Renode, a read of `0x4001100C` brings back the report `0x00002000`. Half a second later BRR is written as well, and the reading falls back to zero. **From a line of code to a voltage level, there is no unknowable link anywhere in between.** This is the foundation of this station, and the seven pieces that follow all build on it.

But you have probably been sitting on a question: why must that clock-enabling line in `main` be written at all? If `RCC->APB2ENR |= 0x10` goes unwritten, what does everything after it look like? In the next piece we will walk through RCC's gates and the clock tree, and along the way see what "writing a register while the peripheral is unpowered" — the single easiest trap for a newcomer to step into — looks like in Renode.

## Hey, hey — run the self-check before you go

- The address of `GPIOC` is `0x40011000` — can you add your way there from `PERIPH_BASE` without looking anything up?
- How many instructions does `GPIOC->CRH |= x` come to at the instruction level? And `GPIOC->BSRR = x`? Can you rattle them off yet?
- Who lives at `0x4001100C`? Why was it exactly the one the sampling macro at our Getting Started station read?
- What does `volatile` actually protect here: speed, or correctness? How would you answer?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="2 Memory map; 9 GPIO and AFIO registers"
  />
  <ReferenceItem
    :id="2"
    author="Arm Ltd."
    title="Arm Cortex-M3 Processor Technical Reference Manual (DDI 0337)"
    :year="2024"
    url="https://developer.arm.com/documentation/ddi0337"
    chapter="3 System address map; 3.3 Peripheral memory map"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f103xb.h — CMSIS Device Header"
    :year="2016"
    url="https://github.com/STMicroelectronics/cmsis_device_f1/blob/master/Include/stm32f103xb.h"
    chapter="L357 GPIO_TypeDef; L583-L666 Peripheral memory map"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="cv (const and volatile) type qualifiers"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/cv"
    chapter="Uses of volatile"
  />
</ReferenceCard>
