---
title: "Why C++, and by what right?"
description: "Four firmwares that differ only in how they blink an LED, on one simulator and at one optimization level — we put C++'s supposed bloat on the scale and weigh its true worth: the template version matches the HAL version instruction for instruction, not one byte off; the old 'C++ equals OOP' impression gets verified on the spot too — the money for virtual functions is only spent where the scenario calls for it, and sometimes the compiler even waives the fee"
chapter: 0
order: 0
tags:
  - stm32f1
  - cpp-modern
  - beginner
  - 嵌入式
  - 零开销抽象
difficulty: beginner
platform: stm32f1
reading_time_minutes: 18
related:
  - "When to use C++"
translation:
  source: documents/vol8-domains/embedded/f103/00-env-setup/00-why-cpp.md
  source_hash: 07012d9430a9ff57ad0ed882bda4450a7efd95eaf0c617104db0f7e1c4b70990
  translated_at: '2026-09-25T08:26:47+00:00'
  engine: anthropic
  token_count: 12000
---

# Why C++, and by what right?

To be clear up front: I am not a C++ pyramid-scheme evangelist, and I don't want any of you to become a pyramid-scheme evangelist for a tool either.

> "Why C++, and by what right? Everybody writes C, you know — will something this fancy actually work?"

That was my inner monologue back when I got roped into building a fun little project to train the junior cohort one year below us, and my good friend @Dessera started chewing this topic over with me. At the time I was a traditionalist (sadly, a traditionalist with lousy skills), thoroughly steeped in the stereotype-driven talking points:

Let me stand at attention and recite the litany:

1. "C++ on a microcontroller? You're running that big lump of a thing on an MCU? Clearly SRAM is too cheap these days."
2. "C++, huh? Playing OOP in C++? The overhead will eat you alive — and now even the firmware grunts are putting on airs."
3. "C++ is harder to follow than C. Hurry up and get back to your actual work — are the drivers finished yet?" (that one, admittedly, is fair)

What I heard were variations on all of these, and some of you on the other side of the screen may have charged in precisely to mock me with them. That's fine — it's normal. For the record: Jacob Beningo's embedded consulting team, citing industry surveys, says C still drives more than sixty percent of embedded projects worldwide<RefLink :id="1" preview="Beningo, The Best Embedded Programming Languages for Engineers Now, 2024" />; and Amar Mahmutbegović's 2025 Packt book *C++ in Embedded Systems* devotes its entire first chapter to Debunking Common Myths — the myth being busted is precisely the thirty-year-old line that "C++ bloats code and costs you at runtime"<RefLink :id="2" preview="Mahmutbegović, C++ in Embedded Systems, Packt, 2025, Ch.1" />. When people write whole books dedicated to busting a myth, you know how far and wide it has traveled.

But folks, this tutorial refuses to stop at "hey, you're not smarter than your ancestors" — that is not learning in the spirit of verification. What we are going to do is put a real side-by-side comparison on the table, and walk all the way through a complete, typical embedded device-driver development flow. Walk it end to end, measure with code, let the data do the talking — that, I believe, is the attitude of someone who does engineering.

## A quick smoke test

**In engineering, item zero is figuring out what to do and why; actually doing it is item one.** We have settled item zero, so now for item one.

The first step of an evaluation is agreeing on the premises. Let me lay them out quickly, so that when we start, you can happily settle on one of the following stances:

1. "Wonderful — the author of this pile happens to use the exact same hardware and dev environment as me! Let me see whether he's hoodwinking me with some mysterious LLM."
2. "I have the hardware, but only Keil — let's see what this guy plans to do about that."
3. "Dying of laughter — I don't do ST, goodbye to you, sir."

That way I waste as little of your time as possible. My working platform is: a Blue Pill (STM32F103C8T6, on-board LED on PC13, lights on low level), and my laptop. Plus a cute little dock to make plugging in convenient.

![Kids, when you read this alternative text, please picture my lovely STM32F103C8T6 — it is adorable](mylovelystm32.jpg)

The compiler I use is not armcc — hmm, I'd rather not consider closed-source compilers; just not my taste. So it is arm-none-eabi-gcc 16.2.0, uniformly Release builds (`-O3 -DNDEBUG`). All four firmwares share one skeleton: `HAL_Init`, pulling the clock from the internal 8 MHz up to PLL 64 MHz, and a main loop that lights the LED for 500 milliseconds and dims it for 500 milliseconds, with every delay done via `HAL_Delay`. Same skeleton, so the only differences left are "how the pin gets configured as an output" and "how to toggle it".

> If you find some of the tool-related descriptions genuinely baffling, you can hop over to the Getting Started station's [Working Environment](03-toolchain-anatomy) for a refresher, and come back any time.

That covers the basic environment and our library strategy. Now, how to verify.

Our protagonist libestdx is a public repository: one command pulls it down (`--recursive` also drags in the HAL library it depends on), and once the toolchain is set up, two commands build it:

```bash
git clone --recursive https://github.com/Charliechen114514/libestdx.git
cd libestdx

# Kids, we use our own secret-recipe cmake (quite the eyesore)
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake
cmake --build build
```

Among the library's built-in `examples/`, `01_blinky` is a close cousin of the HAL version about to make its entrance, and `03_led` is a close cousin of the template version — just run them and get acquainted.

> "Shouldn't you pass an optimization level?" asks the well-informed reader.
> A: No — this command needs no optimization level: the library's CMake hard-codes Release as the default whenever nothing is specified explicitly. A library that claims zero-overhead abstraction must produce optimized builds by default. Unless you switch to Debug for a debugging session.

Ahem — show the project! We are all outstanding engineers here; we don't trust what we can't see. So let's lay out the entire skeleton shared by the four firmwares. If you find it lovely later on, ping me for a fuller tour — it will still be there, and it is worth reading top to bottom.

The heart of the skeleton is `clock.hpp` — the name says it, it initializes the clock. All four firmwares share it letter for letter; here it is in full:

```cpp
// clock.hpp — clock configuration: pull the chip from the power-on default internal 8 MHz up to PLL 64 MHz
#pragma once
#include "stm32f1xx_hal.h"

// HSI 8M ÷2 ×16 = 64M PLL, APB1 ÷2, flash latency 2
static void SystemClock_Config() {
    RCC_OscInitTypeDef osc{};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2; // 8M/2 = 4M feeds the PLL
    osc.PLL.PLLMUL = RCC_PLL_MUL16;             // 4M × 16 = 64M
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk{};
    clk.ClockType =
        RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1; // HCLK = 64M
    clk.APB1CLKDivider = RCC_HCLK_DIV2;  // APB1 = 32M
    clk.APB2CLKDivider = RCC_HCLK_DIV1;  // APB2 = 64M
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}
```

The shape of `main` is identical in all four too; the differences we want to compare all land in two spots:

```cpp
#include "clock.hpp"

int main() {
    HAL_Init();
    SystemClock_Config();
    // ← difference #1: how PC13 gets configured as an output
    for (;;) {
        // ← difference #2: how to toggle it (on/off 500 milliseconds each, delays all via HAL_Delay)
    }
}
```

Apart from those two difference points, the remaining files of the four firmwares are identical — bare-minimum boilerplate, all of them. Since we are showing things off, let's show them all the way: here are those two files in full, both copied verbatim from libestdx's `examples/01_blinky/`. `stm32f1xx_it.c`, the interrupt service routines — this is the entirety of it:

```cpp
/**
 * @file  stm32f1xx_it.c
 * @brief The interrupt service routines for 01_blinky: which handlers exist is decided by this firmware.
 *
 * HAL_Init() configures SysTick to interrupt once every 1 ms; HAL_Delay/HAL_GetTick
 * both feed off HAL_IncTick(). Without this, every blocking API hangs forever.
 */
#include "stm32f1xx_hal.h"

void SysTick_Handler(void) {
    HAL_IncTick();
}
```

Just one function — friends who know HAL will recognize it as nothing more than cranking up `uwTicks`, that system variable, by one. `HAL_Init` configures SysTick to interrupt every 1 millisecond; each time an interrupt arrives, this handler increments the library's millisecond count by one, and `HAL_Delay` counts exactly that count. Delete these few lines and all four firmwares hang forever on the first `HAL_Delay(500)` — even the bare-register version does not escape: it never touches HAL's GPIO functions, but its delays still go through `HAL_Delay`.

`syscalls.c`, newlib runtime stubs — likewise in full:

```cpp
/**
 * @file  syscalls.c
 * @brief newlib runtime stubs.
 *
 * -nostartfiles drops the default _init/_fini from crt, while newlib's
 * __libc_init_array (the initiator of global constructors) calls them; without these stubs the link simply fails.
 */
void _init(void) {}
void _fini(void) {}
```

Two empty functions; for their story, just read the comment: the link option `-nostartfiles` discards the C runtime's default `_init`/`_fini`, while newlib's `__libc_init_array` — the initiator of global constructors — insists on calling them. Without these two empty stubs, the link fails outright.

Our build criteria are unified too: the same CMake toolchain file (which explicitly carries `-fno-exceptions -fno-rtti` — a detail the third weigh-in will need), the same linker script (the C8T6's 64K Flash / 20K SRAM memory layout), and uniformly Release (`-O3 -DNDEBUG`).

**Alright! Everyone sit up!** I shall now, humbly, present the differences. Bachelor No. 1 chose to go straight all-in on bare registers! Ribbit! A true embedded hotshot, yes sir!

```cpp
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; // Enable GPIOC's clock
// PC13's configuration lives in bits [23:20] of the CRH register:
// CNF=00 general-purpose push-pull, MODE=10 output mode 2MHz
GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);

for (;;) {
    GPIOC->BSRR = 1u << 29; // bit29 = reset PC13, output low, LED on
    HAL_Delay(500);
    GPIOC->BSRR = 1u << 13; // bit13 = set PC13, output high, LED off
    HAL_Delay(500);
}
```

First let's be clear about what names like `RCC` and `GPIOC` are: not functions, but address mappings defined in ST's official CMSIS headers — `GPIOC->CRH` simply means "write a value to the address 0x40011004". This is the theoretical floor for this chip; no style of writing can be leaner than it, since at the end of the day we absolutely must write something to this address.

Bachelor No. 2 is the HAL version — this is how I wrote things back in my traditionalist days. The answer to "why" is that this is the ST official library's standard usage; a project you generate by clicking around in CubeMX basically looks exactly like this:

```cpp
// This part here, you see, I trimmed down a bit — CubeIDE has its mandatory comment structure, all very cluttered, so I just tossed it out~
__HAL_RCC_GPIOC_CLK_ENABLE();

GPIO_InitTypeDef led{};
led.Pin = GPIO_PIN_13;
led.Mode = GPIO_MODE_OUTPUT_PP;
led.Pull = GPIO_NOPULL;
led.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &led);

for (;;) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // LED on
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   // LED off
    HAL_Delay(500);
}
```

Bachelor No. 3 is this tutorial's protagonist libestdx, in modern C++ template style — every station from here on will use it as the toolbox:

```cpp
using LedPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Output>;
using Led = estdx::device::LED<LedPin, estdx::gpio::GpioPolarity::ActiveLow>; // on-board LED lights on low level

static_assert(estdx::gpio::GPIOOutputPin<LedPin>); // compiler: 🤔 hmm... this is indeed an output pin, you may pass!

int main() {
    HAL_Init();
    SystemClock_Config();
    LedPin::init();

    for (;;) {
        Led::on(); // one blink
        HAL_Delay(500);
        Led::off(); // one blink
        HAL_Delay(500);
    } // sparkly
}
```

Look at the first two `using` lines: port, pin number, direction, polarity — all written into the type. Inside `Led::on()`, the choice between set and reset is made at compile time based on the `ActiveLow` template parameter; at runtime there is no such thing as "looking up the polarity". This is the **type-as-configuration** idea that every later station uses again and again: the pin's identity lives in the type, rather than being scattered across function arguments and global macros.

Bachelor No. 4 is the OOP version — the style many friends picture as "C++ means object-oriented", and the C++ some big shots lectured me I ought to be writing. Let's not stand on ceremony; lay one out as-is: abstract base class `IGpio` declares virtual `set`/`reset` functions, `GpioPin` derives and implements, and `LedV` holds a reference to the base:

```cpp
struct IGpio {
    virtual void init() = 0;
    virtual void set() = 0;
    virtual void reset() = 0;
    virtual ~IGpio() = default;
};

struct GpioPin final : IGpio { // Kids, this is the most concrete GPIO of all; no more fancy tricks on my watch
    GpioPin(GPIO_TypeDef* port, uint16_t mask) : port_(port), mask_(mask) {}
    void set() override { HAL_GPIO_WritePin(port_, mask_, GPIO_PIN_SET); }
    void reset() override { HAL_GPIO_WritePin(port_, mask_, GPIO_PIN_RESET); }
    // ...
};

GpioPin pc13{GPIOC, GPIO_PIN_13};
LedV led{pc13};
```

All four just compile, link, and get tossed into Renode. Running in the GUI looks like this — the PC13 LED in the peripheral tree toggles right along:

<video controls muted src="./blinky.mp4" width="640"></video>

Um, this may look a bit slapdash. I am currently developing micro-forge, an open-source simulator in C++ (qaq) — if it turns out well down the road, I will give Renode a more dignified exit~

I verified all four in the simulator by sampling on virtual time; the output register read back alternates in pairs between `0x00002000` and `0x00000000`.

## Oh, and then? Where's your argument? Hurry up

You scared the "think" tags right out of me. No rush: the four firmwares are built inside a separate comparison project I set up (the library ships no ready-made bare-register or OOP versions); we just point it at the path of the cloned library:

```bash
# <libestdx> is where you cloned libestdx, by the way
# Run this inside the comparison project directory, not inside the libestdx repo
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=<libestdx>/cmake/arch/stm32f103c8t6.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Our weighing instrument for size is `arm-none-eabi-size`. Run it against the linked artifact, and a single one's verbatim output looks like this:

```text
$ arm-none-eabi-size build/bare
   text    data     bss     dec     hex filename
   5492      12       4    5508    1584 build/bare
```

The columns to watch are the first three: `text` is code and constants, living in Flash; `data` is initialized globals — the initial values live in Flash and get moved to SRAM at power-on; `bss` is the zero-initialized section, occupying only SRAM. Weigh each of the four and copy the results side by side — there is the outcome:

```text
Version           text    data    bss
Bare registers     5492      12       4
HAL                5520      12       4
libestdx template  5520      12       4
OOP                5520      12       4
```

The template version and the HAL version are **identical to the byte** — the row in this whole table that deserves our extra glance. That template wrapping — "the port is a type parameter, the pin is a type parameter, the direction is a type parameter, and the polarity is still a type parameter" — costs zero size in the final firmware. The bare-register version saves 28 bytes; that difference comes from it not calling `HAL_GPIO_Init` and `HAL_GPIO_WritePin`, so the linker simply strips those two unreferenced functions out of the firmware. A 5.4 KB Flash footprint (text 5520 + data 12) may sound like a lot, but the C8T6 has 64 KB, and the bulk of that 5.4 KB is the shared skeleton — clock configuration, `HAL_Delay`, the serial stubs — none of which has anything to do with how you blink an LED.

## Oho, what an amateur — judging by size alone?

No, no, that's not it — we still have to profile by viewing Assembly, right? An unchanged size can be hand-waved as coincidence, so let's drill one layer down and dig the machine code out for a face-off. The machine-code digger is `arm-none-eabi-objdump`: add `-d` to expand all instructions, and `--disassemble=main` to make it spit out only the `main` function:

```text
arm-none-eabi-objdump -d build/estdx --disassemble=main
```

Every instruction we paste below is taken from the main-loop section of that kind of output. In the bare-register version, the single action "LED on" is exactly one instruction:

```text
8000174: 6125    str r5, [r4, #16]   ; r5=0x20000000(1<<29), write GPIOC->BSRR
```

The compiler computed `1u << 29` ahead of time into a register; inside the loop, all that is left for us to execute is writing one word to BSRR's address. The theoretical floor: a 4-byte instruction.

And the HAL and template versions? Let's paste both complete loop bodies so you can see for yourself — the instruction column on the left matches, not one character off on either side:

```text
800017e: 2200        movs r2, #0            ; GPIO_PIN_RESET
8000180: f44f 5100   mov.w r1, #8192        ; GPIO_PIN_13 = 0x2000 = 1<<13
8000184: 4809        ldr  r0, [pc, #36]     ; GPIOC base address 0x40011000
8000186: f001 f98f   bl   HAL_GPIO_WritePin
800018a: f44f 70fa   mov.w r0, #500
800018e: f000 f8e9   bl   HAL_Delay
```

Now! Watch closely! Fix your eyes on the few characters `Led::on()`: after compilation, they are the four instructions above; `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)` — those forty-odd characters — after compilation is still those four instructions. The template parameter `ActiveLow` picked off the "on = write RESET" branch at compile time; the path `if constexpr` leaves behind and the path a C programmer writes by hand converge into the same one. Which also means — **the abstraction lives in the source code, not in the firmware**

So how much more expensive is the HAL path than bare registers? We `bl` over there to look: the function body itself is four instructions:

```text
80014a8: b902    cbnz r2, ...    ; is the argument SET or RESET?
80014aa: 0409    lsls r1, r1, #16 ; it is RESET: shift the mask left 16 bits, landing in BSRR's reset half
80014ac: 6101    str  r1, [r0, #16] ; write BSRR
80014ae: 4770    bx   lr
```

One call plus four instructions — and what we buy with it is not having to memorize details like "PC13's configuration bits sit at bits 20 through 23 of CRH". Whether that price is worth paying is every team's own trade-off, but a trade-off should at least be weighed with real numbers: the impression that "C++ is big" mostly comes from mistaking the worst way of writing it for the whole of the language.

## Is C++'s OOP just plain bloated? Not necessarily! But stay careful

There is a fourth row in the size table worth stopping on: the OOP version is also 5520, identical to the HAL version to the byte. A little counterintuitive, no? Don't virtual functions have to go through indirect calls and drag a vtable along? Let's check the vtable in the fourth firmware:

```text
arm-none-eabi-nm build/virtual | grep _ZTV    # _ZTV = vtable symbol prefix
# (output is empty: the vtable never made it into the firmware at all)
```

No vtable. Because the object `pc13` is constructed in place inside `main`, the compiler can see its complete type — and where it can see, there is no such thing as "not knowing who gets called until runtime": GCC rewrote the virtual call into a direct call to `GpioPin::reset`, then inlined it into the HAL call as usual; nobody referenced the vtable, so the linker's `--gc-sections` recycled it. This optimization is called devirtualization; it is no new trick, but among my friends who have written C++ for years, not many have seen it with their own eyes.

> Aha — this is what the folks in the TAMCPP group said: when the optimizer is aggressive enough, even the vtable gets tossed out!

So when do virtual functions actually charge you RAM? Let's build a fifth firmware to find out: when **which object it is only becomes known at runtime**. Two pin objects hide in **another translation unit (that is, a different C++ file)**; `main` receives only a base-class reference, and which pin gets picked is decided by a runtime condition:

```text
8000198: f000 f848  bl   _Z4pickb        ; pick the object at runtime, returns IGpio&
800019c: 6803        ldr  r3, [r0, #0]   ; read the vptr from the object's head
80001a0: 681b        ldr  r3, [r3, #0]   ; fetch the target function's slot from the vtable
80001a2: 4798        blx  r3             ; indirect call
```

This time the vtable really is in the firmware (`_ZTV7GpioPin`, lying in Flash); every object grows an extra 4-byte vptr at its head, and every call costs two extra memory reads plus an indirect jump. This firmware's text climbs to 5964, and data from 12 to 96 — those extra bytes we pay are the true cost of virtual functions.

So the sentence "C++ means OOP" is wrong in two places in an embedded context — and in my opinion, spectacularly so!

First: when modern C++ writes embedded code, its main force is simply not inheritance and virtual functions. Flip back through libestdx's source: across the whole library, `Gpio` and `LED` have zero inheritance, zero virtual functions, zero `new` — the abstraction is done entirely at compile time by templates and concepts

Second: even if one day you genuinely need runtime polymorphism (say, a plugin-style protocol stack), the money paid for virtual functions is the price of the requirement "we don't know who gets called until runtime" — not a toll the language forcibly collects. And that requirement itself, in C, you would pay for too, with a struct of function pointers — that is a hand-written vtable in C. That cost, I am afraid, genuinely cannot be saved — unless the optimization level is cranked absurdly high.

## When do errors get caught

"What you C++ folks cook up is a bowl of soggy porridge — so what? Can't C do all this too?"

Yes — size and instructions have proven "write your abstract code; we pay no RAM and no runtime CPU ticks". Only with that on the table do I have the confidence to say that C++ really is a decent choice.

But C++ has another benefit — one I came to appreciate while writing ZerOS, that is, a C++23 OS. C++'s extra abstraction greatly shrinks the class of errors that can only be discovered at runtime. Again, claims are worthless without proof — let's go! Deliberately commit the most common beginner mistake: configure the LED pin as an input, then try to light it.

What happens to the HAL version written wrong this way? Fill `GPIO_InitTypeDef`'s `Mode` with `GPIO_MODE_INPUT`, then call `HAL_GPIO_WritePin` as usual to write a level onto this "input pin". We compile it with `-Wall -Wextra` fully on:

```text
arm-none-eabi-g++ -Wall -Wextra ... -c hal_broken.cpp
echo $?
0                          # compiles clean, not a single warning
```

Zero warnings. This error stays hidden until the firmware is flashed onto the board, the LED does not light, and you are paging through registers one notch at a time with a debugger. In HAL's API design, `GPIO_InitTypeDef` is a struct filled in at runtime and `Mode` is a plain integer — the compiler has no standing to intervene.

Commit the same mistake in the template version — write `GpioDirection::Output` as `GpioDirection::Input`:

```text
arm-none-eabi-g++ ... -c estdx_broken.cpp
error: template constraint failure for 'template<class Pin, ...>
       requires GPIOOutputPin<Pin>' struct estdx::device::LED'
note: constraints not satisfied
  • required for the satisfaction of 'GPIOOutputPin<Pin>'
    [with Pin = estdx::stm32f1::Gpio<..., estdx::gpio::GpioDirection::Input, ...>]
```

Compilation is refused on the spot, and the error message answers all three questions for us: which constraint went unsatisfied (`GPIOOutputPin`), which type fell short (that `Gpio<...>`), and what direction it was actually configured as (`Input`). The error moved up from "some night after the board was flashed" to "this very second you hit Enter" — and the ones catching it are the `GPIOOutputPin` constraint on `LED`'s template parameter, plus the double insurance of that `static_assert` line before `main`. This is what the abstraction earns back: **runtime parameter checking becomes compile-time type checking**.

If this is not enough to win you over, what will be! So — come with me, and let's find out together what embedded development under real modern C++ is actually like!

## Hey hey! A few words before you go — don't skip to the next article yet~

Finally, two notes on methodology. Every number in the main text comes from Release builds (`-O3 -DNDEBUG`); if you manually specify another level or skip optimization entirely, the numbers will change — **the "zero" in zero-overhead abstraction presupposes optimization being on** — and we will tackle that head-on in the later performance-related stations. As for the bare-register and OOP versions, the library's `examples/` does not ship them ready-made: following the skeleton and difference snippets pasted in the main text, add two targets to your own project and you can reproduce them — a perfect hands-on exercise for this station.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Jacob Beningo"
    title="The Best Embedded Programming Languages for Engineers Now"
    :year="2024"
    url="https://www.beningo.com/the-best-embedded-programming-languages-for-engineers-now/"
    chapter="Industry survey figure: C drives over 60% of embedded projects worldwide"
  />
  <ReferenceItem
    :id="2"
    author="Amar Mahmutbegovic"
    title="C++ in Embedded Systems: A practical transition from C to modern C++"
    :year="2025"
    url="https://www.packtpub.com/en-us/product/c-in-embedded-systems-9781835881149"
    chapter="Packt Publishing, Chapter 1: Debunking Common Myths"
  />
</ReferenceCard>
