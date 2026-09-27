---
title: "From blinking to using it well: the subtleties of toggle, and the door to the next stop"
description: "LED station wrap-up: reading HAL_GPIO_TogglePin's implementation to see how a single write of two BSRR half-words sidesteps the ODR read-modify-write race, and why a window still remains between the read and the write; the HAL source comments verbatim confirm F1's odd design — input pull-up/pull-down is selected by ODR, and only from F4 onward does PUPDR become a register of its own; then we tally what each of the three staircase steps solved (macros, HAL, templates plus concepts) and the verification toolbox we stockpiled (size, nm, objdump, Renode sampling), and open the door to station 02-button: the world of inputs, mechanical bounce, and the dimension of time"
chapter: 1
order: 9
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/09-wrap-up.md
  source_hash: 6bef5ec8b833101c4ed3cc76016831a392a85349469c4a38756376ec1ad9eebe
  translated_at: '2026-09-27T05:51:35+00:00'
  engine: anthropic
  token_count: 5000
---

# From blinking to using it well: the subtleties of toggle, and the door to the next stop

The nine articles are done. We set out from a single `Led::on()`, descended all the way down to BSRR, then climbed back up into the type system, turning configuration into a compile-time fact. In this closing piece, we crack open `toggle` — the operation that "looks the simplest" — and see what subtleties it hides. Along the way we dig up the foreshadowing Part 04 buried: on the F1, the input pull-up/pull-down is, astonishingly, decided by ODR. We gather up the assets we have accumulated along the road, and the door to the next station swings open while we are at it.

## toggle: two actions, one write

Let's trace down through `estdx`'s `LED::toggle()`: it forwards to `Pin::toggle()`, which finally lands on `HAL_GPIO_TogglePin` (`stm32f1xx_hal_gpio.c` L487). Strip out the argument checks that compile to nothing by default (Part 07 dissected what those really are), and the remaining body is something we can take in at a single glance:

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  uint32_t odr;

  odr = GPIOx->ODR;
  /* Set selected pins that were at low level, and reset ones that were high */
  GPIOx->BSRR = ((odr & GPIO_Pin) << GPIO_NUMBER) | (~odr & GPIO_Pin);
}
```

At first glance it looks unremarkable; only after a second and third look does its real kung fu show. The naive approach is read ODR, invert, write back — the read-modify-write trio we dissected in Part 04: at the instant we write ODR back, we may trample someone else's concurrent write. Here the idea is different: "toggle" gets translated into **a single write to BSRR**. The low half-word (`~odr & GPIO_Pin`) carries the bits that are currently low and need raising; the high half-word (`(odr & GPIO_Pin) << 16`) carries the bits that are currently high and need pulling low. We write once, and both actions are done. Part 04 covered how BSRR's high and low half-words divide the work between them; by this point, that division of labor has been used by the HAL as a handy design pattern.

<Anim id="f103-toggle-bsrr" />

But one thing we have to call out plainly: between the read on the line `odr = GPIOx->ODR` and the write that follows, **there is still a window**. If an interrupt toggles the same pin between the two steps, the ISR's toggle gets overwritten by our write — and that is exactly how a lost update happens. For strict immunity, we would have to disable interrupts before the read and re-enable them after the write. As things stand, the only actor on stage at this station is the main loop, so it is good enough. Once interrupts formally enter the scene (the third station's UART is interrupt-driven), this window gets its turn as the main course. We will pocket this trick for now; the problem itself we will come back to claim when the time comes.

## Digging up the foreshadowing: the input pull-up/pull-down knob lives on ODR

In Part 04's configuration table, the "input with pull-up/pull-down" row carried a remark: on the F1, pull-up versus pull-down is decided by the corresponding ODR bit ("see Part 09"). When Part 05 covered pull-up and pull-down resistors, we looked at this once more from the circuit end — but back then, all we had in hand was the manual's word for it. Now that we can read our way through the HAL, let's summon the official library's source as evidence and look at the input branch of `HAL_GPIO_Init` verbatim (L252-L265):

```c
else if (GPIO_Init->Pull == GPIO_PULLUP)
{
    config = GPIO_CR_MODE_INPUT + GPIO_CR_CNF_INPUT_PU_PD;

    /* Set the corresponding ODR bit */
    GPIOx->BSRR = ioposition;
}
else /* GPIO_PULLDOWN */
{
    config = GPIO_CR_MODE_INPUT + GPIO_CR_CNF_INPUT_PU_PD;

    /* Reset the corresponding ODR bit */
    GPIOx->BRR = ioposition;
}
```

The official comment says it in black and white: `Set the corresponding ODR bit`. Translated into our terms: **in input mode, the output data register moonlights as the pull-up/pull-down selector switch**. Writing 1 to the corresponding ODR bit selects pull-up; writing 0 selects pull-down. Why such a design? The F1 was one of ST's earlier Cortex-M3 products, its register bit budget was tight, and this reuse is baggage left behind by history. By the F4, PUPDR had become a register of its own, with output and pull configuration each minding its own business — and now only the F1 still carries that baggage.

What does this mean for those of us writing the code? **The old habit of poking ODR directly will, in input mode, quietly change the pin's bias.** Suppose we carry over the bare-register flow from Part 04 to configure an input, and then casually follow up with `ODR = 0`: the pull-up has become a pull-down, every level reading is inverted — and not one word in the code tells you that you just touched the input settings. estdx seals this knowledge inside `Gpio`'s template parameters (`Gpio<GpioPort::A, GPIO_PIN_0, Input, GpioPull::Down>`), and the HAL writes ODR correctly for you inside `GPIO_Init`. But you are someone who has climbed up from the floor-tile layer; the next time you see `BSRR = ioposition` appear inside input-configuration code, I trust you will allow yourself a knowing smile: it is selecting the pull-up, and has nothing to do with lighting an LED.

## Looking back: what each of the three staircase steps solved

The staircase this station built has three steps — which problems did each one solve? Let's summarize in one table (for the list itself, see Part 06):

| | C macros (Part 06) | HAL (Part 07) | Templates + concepts (Part 08) |
|---|---|---|---|
| Problem 1: no types | raw numbers | enums block misspellings | the type is the configuration |
| Problem 2: clock left outside | human discipline | still outside, via macros | `if constexpr` throws the right breaker automatically |
| Problem 3: unconstrained combinations | won't even blow up at runtime | only acts up at runtime | compile-time error |
| Problem 4: undebuggable | unreadable macros | real functions, real symbols | types you can trace |
| Problem 5: copy-paste | adding a pin is all copying | struct reuse | derived, not copied |
| Cost | zero abstraction, all debt | ~1 KB of general-purpose machinery | zero-overhead code; you pay only for the paths you choose |

Still, more valuable than the answers are **the means of verification**. The toolbox we assembled along the way will keep earning its keep at every station to come:

- `arm-none-eabi-size`: the three columns of numbers we keep seeing are the firmware's inventory list.
- `arm-none-eabi-nm --size-sort`: we use it to attribute size differences to function names — big or small, each has a name attached.
- `arm-none-eabi-objdump -d`: every abstraction eventually has to show its true form here; we insist on seeing even one extra `bl` hop.
- Renode sampling (`sysbus ReadDoubleWord`): without powering anything on we can still watch — whether a write gets swallowed when the clock is off, whether ODR actually toggled, what the reset value is.
- Deliberately-broken compile experiments: we break the code on purpose to practice reading concept errors, from `template constraint failure` all the way down to `evaluated to 'false'`.

Over the course of the LED station we have already run into **the simulator's fidelity boundary** twice: Part 03's Renode does not simulate clock gating, and Part 04's CRH reset value disagreed with the manual. Both cases deserve their own entry in our notebook. Proving "the mechanism works this way" with a simulator is quick, but "it also works this way on a real board" is for the hardware to say. A passing simulation is not a clean bill of health — at every station from here on, we have to keep that string taut.

## The door to the next station: the world of inputs

This station's LED is an **output**: whatever we write, the pin goes and does. The next station, 02-button, flips the direction around: the pin becomes an **input**, the world's levels flow in, and it is our turn to read. The keys to that door have already been placed in your hand by this article:

- You have already walked the configuration route once (enable the clock, the four CRL/CRH bits per pin, `Gpio<Port, Pin, Input, Pull>`); the only difference is CNF set to `10` (input with pull-up/pull-down).
- The pull-up/pull-down knob lives on ODR. So for the most classic circuit of all — "one leg of the button to ground, configure the input with pull-up, a read of 0 means pressed" — you already know the why of every layer.
- But a button is a **mechanical contact**: at the instant of pressing and releasing, the metal leaf bounces for a few to a few dozen milliseconds, and what you read on IDR is not one clean transition but a burst of glitches. **The dimension of time** enters the stage for the first time: debouncing, sampling, state machines — the crafts the LED station had no use for are the next station's main course.

`Gpio`'s concept system left the door ready long ago: `GPIOInputPin` requires `level() -> bool`. Reading a pin's level is, at the type level, a first-class citizen on par with `set()`/`reset()`. The building is already up; all that remains is for you to push the door open.

## Your turn to get hands-on

1. Change `03_led`'s `main` from `on`/`off` to `Led::toggle()`, rebuild, and compare `main`'s disassembly: find `HAL_GPIO_TogglePin` and see how its two steps — the ODR read plus the BSRR write — appear in the firmware.
2. Run a lost-update experiment of your own: in Renode, write ODR by hand and then call `TogglePin` (or run the modified firmware), and check whether the toggle result is consistent with the ODR value you wrote.
3. Read through `HAL_GPIO_Init`'s input branch (L238-L266) in full, find the evidence that enums like `GPIO_MODE_IT_RISING` travel the same configuration code as `GPIO_MODE_INPUT`, and think about why input mode and interrupt mode are "one family".
4. Do a round of previewing: open the IDR (port input data register) section of RM0008 (that is ST's STM32F1 reference manual, the one we paged through in Part 01), see how its bit definitions differ from ODR's, and bring the answer with you to the next station.

## Hey hey! Self-check before you go

- Tell me: how does `HAL_GPIO_TogglePin` complete a toggle with a single write? What goes into the high half-word, and what into the low?
- What window remains between toggle's read and write? In what scenario must we deal with it?
- For an input pull-up on the F1, should the corresponding ODR bit be written 0 or 1? And do you still recall why this design had disappeared by the F4?
- Which problems did each of the three staircase steps solve? Can you point out the one that only the type system can solve?
- Of the five verification tools we stockpiled at this station, which question does each one answer? And which tool's answers need a discount?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 — STM32F103 Reference Manual"
    :year="2018"
    url="https://www.st.com/resource/en/reference_manual/cd00171190.pdf"
    chapter="Table 20-25: Port bit configuration table; Section 9.3.4 IDR"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="AN4488 — Getting started with STM32F4 GPIO hardware"
    :year="2016"
    url="https://www.st.com/resource/en/application_note/dm00123199.pdf"
    chapter="Section 3: PUPDR — F4's independent pull-up/pull-down register, for comparison"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_gpio.c — GPIO HAL Driver Source"
    :year="2016"
    url="https://github.com/STMicroelectronics/stm32f1xx_hal_driver/blob/master/Src/stm32f1xx_hal_gpio.c"
    chapter="L252-L265 Input pull via ODR; L487-L499 TogglePin"
  />
</ReferenceCard>
