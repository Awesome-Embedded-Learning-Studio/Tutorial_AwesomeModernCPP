---
title: 'Unwrapping the HAL: which bits GPIO_Init writes on our behalf'
description: 'Putting the HAL''s GPIO layer on the scale and unwrapping it: of the 1088-byte difference, HAL_GPIO_Init alone takes 1060, while the two mains differ by just 8 bytes — initialization pays the bulk, and the runtime paths are nearly a draw. A line-by-line walk through HAL_GPIO_Init''s bit-by-bit loop, the switch that translates Mode into CNF/MODE, and the same (position-8)×4 half-split arithmetic as piece 04; then the read-modify-write trio wrapped inside the MODIFY_REG macro, WritePin''s high-half-word trick of giving one BSRR two faces, and the truth that assert_param is absent by default — and finally the recheck: of the five problems, the HAL dismantles one, half-dismantles one, mitigates two, and the clock still sits outside'
chapter: 1
order: 7
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/07-hal-unwrapped.md
  source_hash: d4ffa2c49e83c1c71573bcc2aaff35b2e78d7a9839cfe2a8cbd1ca6ed46e0134
  translated_at: '2026-09-27T05:47:27+00:00'
  engine: anthropic
  token_count: 2000
---

# Unwrapping the HAL: which bits GPIO_Init writes on our behalf

In the last piece we laid out the price of the C-macro approach: it runs, but it carries five problems. The HAL is the other road, the one ST offers officially; it takes the same "wrap the registers up" route, but the official wrapping paper is much thicker — and much more expensive. Our job today is to unwrap it and put a price tag on every layer, without taking sides on "is the HAL good or not": what is rolled up inside the wrapping, which parts are solid goods, and which parts we will never use yet cannot escape — onto the scale they go, one item at a time.

## On the scale: what the 1088 bytes are made of

What goes on the scale are two firmwares we already know: `01_blinky` (the HAL handles everything) and `01_register_led` (bare registers, written straight). Here are the sizes:

```text
$ arm-none-eabi-size blinky register_led
   text    data     bss
   3468      12       4    blinky        (pure HAL)
   2380      12       4    register_led  (bare registers)
   ─────────────────────────────
   difference      1088 bytes
```

Where did the difference go? Have `arm-none-eabi-nm --size-sort` line the symbols up by size, and the answer jumps out on its own:

| Symbol | Size | Carried by |
|------|------|--------|
| `HAL_GPIO_Init` | **1060 bytes** | blinky only |
| `HAL_RCC_OscConfig` | 1004 bytes | both sides |
| `HAL_RCC_ClockConfig` | 384 bytes | both sides |
| `HAL_SYSTICK_Config` + NVIC odds and ends | ~80 bytes | both sides |
| `main` | **144 vs 136** | 8 bytes apart |

The main force of the difference is visible at a glance: `HAL_GPIO_Init`, this configuration machine, single-handedly takes up **1060 bytes**. More interesting still, the two `main` functions are almost the same size: once the initialization overhead has been paid, the runtime path ends in a draw between the HAL camp and the bare-register camp. There is also a detail we could easily miss: `register_led` carries `OscConfig` and `ClockConfig` too — it likewise calls `HAL_Init` and `SystemClock_Config` — so the ~80-byte SysTick odds and ends in the table hang on both sides as well. These 1088 bytes are therefore the **net difference after the shared overhead is subtracted**; whether or not you bought the HAL's GPIO layer is the only structural difference between the two.

## Taking HAL_GPIO_Init apart, line by line

The machine our 1060 bytes bought has a struct for its feed chute:

```cpp
GPIO_InitTypeDef gpio{};
gpio.Pin   = GPIO_PIN_13;
gpio.Mode  = GPIO_MODE_OUTPUT_PP;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &gpio);
```

Compared with the macro wrapping of piece 06, the situation is considerably better: the bare numbers like `13` and `push-pull 2 MHz` now live in named enum fields. Misspell `GPIO_MODE_OUTPUT_PP`, and the compiler turns on you on the spot. Problem 1 is solved right here.

Next we climb inside the machine (from `stm32f1xx_hal_gpio.c` L178): the first thing it does is a **bit-by-bit scan**:

```c
while (((GPIO_Init->Pin) >> position) != 0x00u)
{
    ioposition = (0x01uL << position);
    iocurrent  = (uint32_t)(GPIO_Init->Pin) & ioposition;
    if (iocurrent == ioposition)
    {
```

`Pin` is actually a mask, not a pin number: write `GPIO_PIN_13 | GPIO_PIN_14`, and configuring two pins is still a single call — the per-bit work is handed to the loop. Here is the first cost of generality: even when we configure just one pin, the loop, the test, and the whole bit-iterating machinery are all there, not one piece missing. Moving on, let's see how `Mode` gets translated into the four configuration bits — with a switch:

```c
case GPIO_MODE_OUTPUT_PP:
    config = GPIO_Init->Speed + GPIO_CR_CNF_GP_OUTPUT_PP;  // Speed is 0/1/2/3, added on top of the CNF base
    break;
```

There is a trick hiding in this `Speed + CNF` addition: the target four bits are laid out as `CNF<<2 | MODE`, and the `Speed` enum value is itself the 0-3 MODE bits. As for `GPIO_CR_CNF_GP_OUTPUT_PP`, it happens to equal `0x0` (push-pull output) shifted left by two. Add the two numbers together, and the four bits assemble themselves. Further down come the two lines we know best of all:

```c
configregister = (iocurrent < GPIO_PIN_8) ? &GPIOx->CRL : &GPIOx->CRH;
registeroffset = (iocurrent < GPIO_PIN_8) ? (position << 2u) : ((position - 8u) << 2u);
```

**Splitting the family into halves, four bits of offset**: the very arithmetic we derived by hand in piece 04, factory-original and the same model — right down to the `(position - 8) << 2` spelling being identical to our `(13-8)×4`. Reading all the way to this point, the two lines that actually land on the register are these:

```c
MODIFY_REG((*configregister), ((GPIO_CRL_MODE0 | GPIO_CRL_CNF0) << registeroffset),
           (config << registeroffset));
```

Let's go to `stm32f1xx.h` L189 and look at `MODIFY_REG` in its true form:

```c
#define MODIFY_REG(REG, CLEARMASK, SETMASK) \
    WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))
```

Peel off the three layers of macro skin, and what we see is **read it back, clear the mask, OR in the new value, write it back**. We already met it in the disassembly in piece 02: the read-modify-write trio — this time merely wrapped in a macro.

We saw the same read-modify-write again in piece 03's clock-enable macro: the `SET_BIT` inside `__HAL_RCC_GPIOC_CLK_ENABLE()` does those same three things once expanded. Back then we agreed to take a closer look at that extra throwaway read into `tmpreg` when we got around to unwrapping the HAL — and now that its true form sits before our eyes, the rationale is clear: `SET_BIT`'s job is to stand the bit up, while that read-then-discard bite into `tmpreg` manages time itself — the manual asks for a few clock cycles after closing the switch before touching the peripheral, and the time one register read takes happens to top up that buffer exactly. In the wrapping paper around clock enabling, even the waiting has been worked out on our behalf.

With the unwrap this far along, most of the wrapping paper's contents are on the table: `HAL_GPIO_Init` holds no magic we have not already learned — it simply packs piece 04's craftsmanship, the bit-by-bit loop, the switch translation, and the EXTI branch into a single function. The EXTI (external interrupt) branch runs several dozen lines further down from L288; we will not walk it in this station, but the space it takes up, you have already paid for.

<Anim id="f103-hal-gpio-init" />

## WritePin: one register, two faces

On the runtime path, `HAL_GPIO_WritePin` (L465-L479) is short enough that we will just copy the whole thing over:

```c
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
  assert_param(IS_GPIO_PIN(GPIO_Pin));
  assert_param(IS_GPIO_PIN_ACTION(PinState));

  if (PinState != GPIO_PIN_RESET)
    GPIOx->BSRR = GPIO_Pin;
  else
    GPIOx->BSRR = (uint32_t)GPIO_Pin << 16u;   // high half-word = the reset side
}
```

Two spots deserve our attention. The first is that `<< 16`: BSRR sets through the low half-word and resets through the high half-word (as we covered in piece 04). The HAL does not use the BRR register — it puts both faces of the same BSRR to work. What is saved is the addressing of one peripheral address; the price is one extra shift on the reset path.

The other spot is that `if`. `PinState` is a variable whose value is known only at runtime, so the compiler cannot cut this branch; every call carries the overhead of one comparison and one jump. When we put things on the scale at the start, we saw "the mains differ by only 8 bytes" — this is the detail behind it: the branch overhead is there, but it is cheap. The atomicity, though, is not discounted: both paths ultimately land in a **single write**, and not even an arriving interrupt can get a foot in the door.

## assert_param: parameter checking, off by default

Those two opening `assert_param` lines are often taken for a HAL selling point: parameter validity checking. Let's look at their true form in `stm32_assert_template.h` L42-L46:

```c
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
// ...
#define assert_param(expr) ((void)0U)      // ← the default: this one
#endif /* USE_FULL_ASSERT */
```

Under the default configuration (with `USE_FULL_ASSERT` off), what `assert_param` expands to is exactly `(void)0U` — **after compilation, not a single byte of it remains**. The ternary-operator version with the real check only shows up if you explicitly turn the macro on, and even then `assert_failed` is something you have to implement yourself. So the correct way to read "the HAL ships with parameter checking" is: **the checking logic is written, but it leaves the factory switched off**. That is really no bad thing — a firmware stuffed full of checks would be the bad thing. Knowing this, we simply keep the score in mind when we use it.

## Recheck: where the five problems stand

Let's bring back the five problems we counted in piece 06 and pay the HAL a follow-up visit:

| Problem | The HAL's answer |
|----|-----------|
| 1. No types | **Dismantled** — `GPIO_InitTypeDef` + enums; a misspelling fails to compile |
| 2. Clock left outside | **Still there** — `HAL_GPIO_Init` does not handle clock enabling; you still have to recite `__HAL_RCC_GPIOC_CLK_ENABLE()` yourself, and forgetting it still breeds ghost bugs |
| 3. Orthogonal dimensions unconstrained | **Half dismantled** — enums block misspellings, but not illegal combinations (stuff 50 MHz onto PC13, and the wrongness only shows up at runtime) |
| 4. Undebuggable | **Mitigated** — real functions, real symbols; breakpoints line up with the source |
| 5. Copy-paste | **Mitigated** — one struct, one call; the configuration machine reused across many pins |

Look at problem 2, the sorest thumb of the lot. The HAL's initialization wraps "configure the pin" but does not wrap "enable the clock" along with it: nowhere in `GPIO_Init`'s signature can you find a field tied to the clock, and the two tasks still rely on the caller's manual discipline to stay in sync. This is not an oversight by ST — the `__HAL_RCC_GPIOC_CLK_ENABLE` macro has been sitting there waiting all along. What truly holds us back is the ceiling of C's expressive power: **in the type system, the port and its circuit breaker simply have no relationship**.

Whether the generality, readability, and debuggability these 1060 bytes bought are worth it depends on the project's appetite. But the tails of problems 2 and 3 call for a different, language-level line of thinking to close out: promote "port, pin, direction, speed" from function parameters into **types themselves**, so that a wrongly combined configuration makes the compiler turn on you the moment you hit Enter. That is the job of the next piece's `estdx::Gpio<Port, Mask, Dir>` — and it is the final step of the whole staircase.

## Your turn to get your hands dirty

1. Run `arm-none-eabi-nm --size-sort --print-size` over `blinky`, find `HAL_GPIO_Init`'s 1060 bytes with your own eyes, then check whether `register_led` contains it at all.
2. Change `blinky`'s `gpio.Speed` to `GPIO_SPEED_FREQ_HIGH`, then disassemble and compare the call site of `HAL_GPIO_Init` and `main`, and get a feel for "Speed is just a number inside a struct".
3. Define `USE_FULL_ASSERT` in the project and implement an `assert_failed` (with `printf` or an infinite loop), deliberately set `gpio.Pin` to `0`, and see what the parameter checking looks like once it is genuinely running.
4. Read through the disassembly of `HAL_GPIO_WritePin`, count the instructions the `PinState` branch brings in, and compare them with the single `str` of `register_led` from piece 02.

## Hey, hey — run the self-check before you go

- Tell us: who is the main force behind the 1088-byte difference? And why are the two `main` functions almost the same size?
- The `(position - 8) << 2` in `HAL_GPIO_Init` — do you still remember which piece of arithmetic from piece 04 it is the same thing as?
- What does `MODIFY_REG` expand to? Which two of our handwritten lines is it equivalent to?
- Why does `HAL_GPIO_WritePin` use `BSRR << 16` instead of BRR? Count them up: how many instructions does each spelling take?
- Of the five problems, how many did the HAL solve? And the one left unsolved — do you know why the C language cannot cure it?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="stm32f1xx_hal_gpio.c — GPIO HAL Driver Source"
    :year="2016"
    url="https://github.com/STMicroelectronics/stm32f1xx_hal_driver/blob/master/Src/stm32f1xx_hal_gpio.c"
    chapter="L178-L284 HAL_GPIO_Init; L465-L479 HAL_GPIO_WritePin"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="UM1850 — Description of STM32F1xx HAL drivers"
    :year="2016"
    url="https://www.st.com/resource/en/user_manual/um1850-description-of-stm32f1xx-hal-drivers-stmicroelectronics.pdf"
    chapter="GPIO HAL API; How to use this driver"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx.h / stm32_assert_template.h — CMSIS Device & Assert Template"
    :year="2016"
    url="https://github.com/STMicroelectronics/cmsis_device_f1/blob/master/Include/stm32f1xx.h"
    chapter="L189 MODIFY_REG; assert_param configuration"
  />
  <ReferenceItem
    :id="4"
    author="GNU Binutils"
    title="nm(1) — List Symbols from Object Files"
    :year="2024"
    url="https://sourceware.org/binutils/docs/binutils/nm.html"
    chapter="--size-sort --print-size"
  />
</ReferenceCard>
