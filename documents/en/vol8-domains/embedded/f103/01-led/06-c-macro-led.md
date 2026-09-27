---
title: "An LED driver from the C-macro era: it runs, but let's put the costs on the table"
description: "The first-generation answer to the manageability problem of bare-register code: C macros. Packing the address and the bits into the LED_ macro family does clean up the call sites; then we add a second LED by hand to feel the weight of copying the whole set, and move a port while forgetting the clock enable, reproducing that ghost bug where everything compiles and the LED stays dark (precisely the macro-era version of the problem the third piece called out); finally we face the nested macro expansions in GDB and count out five costs — and, in fairness, for one person and one chip it is entirely serviceable. The three-step refactoring route starts here and leads to the eighth piece's template"
chapter: 1
order: 6
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/06-c-macro-led.md
  source_hash: 725ea04cd0c8ed9b7adae5650fbd975cc5d57f998dd0b21ed0ab221341cf0d48
  translated_at: '2026-09-27T05:45:10+00:00'
  engine: anthropic
  token_count: 4500
---

# An LED driver from the C-macro era: it runs, but let's put the costs on the table

We have seen through all three layers under the floor tiles: addresses travel by MMIO, the clock switches sit in RCC, and configuration and toggling come down to seven registers. Now we start climbing. The first stair on the way up is a wheel C programmers have been building for decades: **wrapping register code in macros**.

The scene is plain: no HAL library at hand, no C++ templates — just you, one pure-C project, and one LED that has to blink. The bare-register code looks like this (the stock we saved up across the second to fourth pieces):

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);
GPIOC->BSRR = 1u << 13;
GPIOC->BRR  = 1u << 13;
```

It runs, sure, but magic numbers are scattered all over the floor. What is `0xF << 20`? Who is `13`? And `0x2`? Come back to look at it yourself in three months and you will be doing archaeology from scratch. Thus, version one of the macros.

## Version one: packing the address and the bits into names

```c
// led.h — the first LED wrapper of the C-macro era
#define LED_PORT        GPIOC
#define LED_PIN         13
#define LED_CLK_EN()    do { RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; } while (0)

#define LED_MODE_OUT()  do { LED_PORT->CRH = \
        (LED_PORT->CRH & ~(0xFu << 20)) | (0x2u << 20); } while (0)

#define LED_ON()        do { LED_PORT->BRR  = (1u << LED_PIN); } while (0)
#define LED_OFF()       do { LED_PORT->BSRR = (1u << LED_PIN); } while (0)
#define LED_TOGGLE()    do { LED_PORT->ODR ^= (1u << LED_PIN); } while (0)
```

Now look at the call site — a whole new coat of paint:

```c
LED_CLK_EN();
LED_MODE_OUT();
for (;;) {
    LED_ON();
    LED_DELAY_MS(500);
    LED_OFF();
    LED_DELAY_MS(500);
}
```

`LED_DELAY_MS` handles the delaying; led.h ships no definition of it, and its job is exactly the `HAL_Delay(500)` we saw in the disassembly in the second piece — this version does not reinvent it.

Honestly, this version **is better**: the intent has a name, changing the pin means touching exactly one place, and the `do {} while (0)` form is quite properly standard. The compiled artifact is not one byte larger than the hand-written one. A macro is text substitution at the preprocessing stage; runtime overhead is zero. So it is not fair to beat up on C macros as a straw man — for decades this was the orthodox opening of industrial firmware. It really does run; the **costs**, though, are just as real, and we will lay them out item by item right now.

## The second LED: the weight of copy-paste

The requirement arrives: add one more LED, LED2, on PA5. The macro problem shows itself immediately: this set of macros **was born to serve exactly one LED**. You have exactly two routes:

```c
// Route A: copy the whole set and rename
#define LED2_PORT       GPIOA
#define LED2_PIN        5
#define LED2_CLK_EN()   do { RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; } while (0)
#define LED2_MODE_OUT() do { LED2_PORT->CRL = \
        (LED2_PORT->CRL & ~(0xFu << 20)) | (0x2u << 20); } while (0)   // ← the offset 20 was copied over as-is
#define LED2_ON()       do { LED2_PORT->BRR  = (1u << LED2_PIN); } while (0)
// ...the five lines below are copied the same way
```

Route A copies a dozen-odd lines, and the flaw sits right on the flagged line: the offset `20` was copied over unchanged from the PC13 set (`(13-8)×4=20`). PA5's offset happens to be `(5-0)×4=20` too, so this time the guess lands — pure luck, two formulas colliding on the same number. The day LED2 moves to PA6 (`6×4=24`), the copied `<< 20` pairs with the wrong pin, the compiler lets it through without a word, and all we can do is watch the fourth piece's wreck happen again in a new costume.

Route B is to rework the macros into parameterized `LED_ON(port, pin)`, but we cannot get through on that one either: port is a pointer, pin is a number, and the configuration offset follows the pin around — these cannot all be squeezed into one type-safe call. And macro parameters carry no types, so a typo still compiles.

## The ghost bug: an error the compiler cannot see

Let's stage one act that is sneakier still. The boss has spoken: LED2 moves to PB12. You change it, briskly:

```c
#define LED2_PORT       GPIOB
#define LED2_PIN        12
// done — compile, flash, run: LED2 does not light up
```

Why? The `IOPAEN` in `LED2_CLK_EN()` never got changed to `IOPBEN`: GPIOA's clock switch is on while GPIOB's is still off. This code is **flawless in the compiler's eyes**: the macros expand into legal C expressions; we check types, check syntax, count warnings — and not one of them turns up a fault. Yet at runtime it is wrong, and wrong in silence: no crash, no error, no printout — the LED simply ignores you.

The third piece already said it: when a peripheral refuses to work, the first thing to check is always the clock. Now let's add the second half of the sentence: **C macros split 'turn on the clock' and 'use the pin' into two places, and their consistency rests on human discipline — and humans forget**. Port and clock are a pair of configurations that must move in lockstep, yet from the language we cannot see the slightest connection between them — that is the root of the ghost bug.

We drew this mismatch into the diagram below:
![The ghost bug born of a port-and-clock macro mismatch](./06-macro-coupling-bug.drawio)

## Macros the debugger cannot read

The third cost shows up at debugging time. We attach GDB and `step` into `LED_MODE_OUT()` — guess where you land? A macro has no function body; one `step` drops you straight onto the expanded expression, and breakpoint line numbers bounce between preprocessed lines and original ones. Want to `print` `LED_PORT`? It is not a variable at all, only compile-time text substitution — there is nothing for the debugger to find. The disassembly does line up (we practiced in the second piece: finding `0x40011000` in the literal pool), but the courtesy of 'source-level debugging' does not exist in the macro era. If three `LED_ON()` call sites misbehave, all you can do in the disassembly is compare addresses one by one.

## Five problems, counted out together

Let's gather the costs and count out the five problems this running macro wrapper carries:

1. **No types**: `pin` is a bare number; write `13` as `0x13` and it still compiles, then lands on the wrong pin at runtime.
2. **The clock left outside**: pairing port with clock switch depends entirely on our own discipline, and discipline always meets the day it cannot hold.
3. **Orthogonal dimensions, all by hand**: reuse, speed, pull-up/pull-down — the dimensions in the fourth piece's combination table — plus polarity; every dimension is its own independent line of code, and nobody vouches for their mutual legality.
4. **Undebuggable**: preprocessing swallows the function boundaries and the symbol names; we cannot get a grip on it in the debugger.
5. **Consistency by copy-paste**: from the second LED on, every change we make gets multiplied by the number of LEDs.

## In fairness

Let's not rush to write C macros off as good-for-nothings. On a project maintained by one person, with fixed requirements and a single chip, this macro set will carry you for ten years without trouble: buttons, buzzers, relays — each peripheral with its own set of macros, the files kept cleanly apart — a full head start over bare registers. Half the firmware in the embedded industry's history was written exactly this way, including code still running on production lines today. The macro's problems lie only in **scale** and **collaboration**: once the LEDs go from one to eight and the maintainers from one person to a team, the five problems come out one by one.

> The full use of the bit-manipulation macro family — set, clear, toggle, test, plus the traps specific to a C context — we already worked through systematically in the C tutorials; for a closer look, the C-tutorial piece [Embedded C Programming Patterns](../../../../vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md) is the one to open, and we will not repeat it here.

## The way out: the three-step refactoring route

We treat the roots we have just exposed, one by one:

1. **Bring in types**: port, pin, and polarity go from bare numbers to `enum class`; a misspelling now fails the build outright — problem 1 solved.
2. **Bind the clock**: 'this pin's clock' becomes an attribute of the type, and the switch closes automatically at initialization — problem 2 solved.
3. **Constrain the combinations**: a concept holds the illegal ones (an input pin used as an output) at compile time — problem 3 solved.

Walk these three steps to the end and the destination is the eighth piece's `estdx::Gpio<Port, Pin, Dir>`. In the next piece we rest our feet and look at the HAL's official route: of the five problems, how many does it solve, and how many bytes does that solution weigh — on the scale it goes.

## Try it yourself

1. Copy the version-one macros into `01_register_led` as a rework, build it, and compare with `arm-none-eabi-size` — verifying that 'zero macro overhead' is not an empty phrase.
2. Try with your own hands whether the ghost bug gives itself away in the simulator: change `LED_PORT` to `GPIOA` and `LED_PIN` to `5`, but deliberately leave `IOPCEN` unchanged, then watch in Renode whether PA5's ODR shows any movement (hint: the second piece taught `sysbus ReadDoubleWord` for reading peripheral addresses; GPIOA's base is `0x40010800`). Before you start, think it over: the third piece measured it — Renode's GPIO model does not simulate clock gating at all — and in the fourth piece's wreck log, writing BSRR without configuring CRH left the ODR equally motionless. Make your guess first, then look at the result.
3. Run `gcc -E` on a .c file that contains nothing but macros, and see with your own eyes what `LED_MODE_OUT()` expands into — feel out the reason why 'the debugger cannot read macros'.
4. Think about why 'bring in types' cannot cure problems 4 and 5 (undebuggable, copy-paste), and what it takes to cure them.

## Hey, hey — self-check before you go

- Why can't the compiler catch the ghost bug (the forgotten clock)? Say it in your own words: what lies between 'type-correct but semantically wrong'?
- `LED_ON` uses BRR and `LED_OFF` uses BSRR — against the Blue Pill's wiring (fifth piece), explain why it is not the other way around.
- Of the five problems, which arrive only with scale and did not exist back when we had one LED? Which were there from day one?
- Match them up: which problem does each of the three refactoring steps solve?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="ISO/IEC"
    title="N2176 — C Preprocessor Working Draft (macro semantics)"
    :year="2007"
    url="https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1425.pdf"
    chapter="6.10 Preprocessing directives"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="stm32f10x_std_periph_lib — STM32F10x Standard Peripheral Library"
    :year="2011"
    url="https://www.st.com/en/embedded-software/stsw-stm32054.html"
    chapter="stm32f10x_gpio.c — the official predecessor of the function-style C wrapper"
  />
  <ReferenceItem
    :id="3"
    author="Hunt, Andrew & Thomas, David"
    title="The Pragmatic Programmer — where the DRY principle comes from"
    :year="1999"
    url="https://pragprog.com/titles/tpp20/the-pragmatic-programmer-20th-anniversary-edition/"
    chapter="Ch.2 A Pragmatic Approach"
  />
</ReferenceCard>
