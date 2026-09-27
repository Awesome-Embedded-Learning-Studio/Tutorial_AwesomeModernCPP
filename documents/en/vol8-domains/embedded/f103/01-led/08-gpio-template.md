---
title: 'Putting configuration into types: the Gpio template and compile-time verification'
description: 'From the enum class type revolution to the Gpio<Port, Mask, Dir> template: the port base address serves directly as the enumerator value, countr_zero derives the pin number from the mask, and if constexpr picks the circuit breaker automatically at compile time — the externally-wired clock problem is settled on the spot; concept constraints stop "handing an input pin to the LED" at compile time, with a line-by-line read of the real compiler error; the polarity parameter turns the Blue Pill''s active-low lighting into a fact of the type — and finally the disassembly is checked item by item: init() expands into the clock trio plus HAL_GPIO_Init, the polarity translation in on() costs zero runtime overhead, and the piece closes with the final checklist for all five problems'
chapter: 1
order: 8
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 模板
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/08-gpio-template.md
  source_hash: c269a410ccddbff041863477e6262d47344add27d216802d3665c3328aa529ab
  translated_at: '2026-09-27T05:55:03+00:00'
  engine: anthropic
  token_count: 2700
---

# Putting configuration into types: the Gpio template and compile-time verification

Two pieces into this, the count is already settled in our heads: of the five problems the C macros left behind, the HAL dismantled problem 1 (the enum blocks misspellings) and mitigated problems 4 and 5; what remains are problems 2 and 3 — the externally-wired clock is untouched exactly as it was, and the unconstrained-combination problem has only been grazed on one side by the enum: misspellings are indeed blocked, but illegal combinations still have nobody minding them for us. The end of piece 07 left us a sentence: "in the type system, the port and its circuit breaker simply have no relationship" — that is precisely the ceiling of the C language. So in this piece we roll up our sleeves and tie off the remaining loose ends: **promoting configuration from function arguments to types themselves**. Once configuration becomes a type, a misconfiguration stops being a "runtime oddity" and becomes a "compile-time error" — and this is exactly where the "modern C++" promised at station 00 truly lands on the registers.

Every scrap of material we use comes from the sources and real build artifacts of `third_party/libestdx`; every number was actually run.

## Opening move: enum class turns the port into a type

Look back with us at piece 06: one of the macro wrapper's fatal weak spots is that port and pin number are both bare numbers — change `GPIOA` to `GPIOC`, and the macro and the compiler are both kept in the dark. The library author's first step is to gather the ports into an `enum class` (`libestdx/boards/stm32f1/gpio.hpp`):

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,   // 0x40010800
    B = GPIOB_BASE,   // 0x40010C00
    C = GPIOC_BASE,   // 0x40011000
    // ...
};
```

This definition deserves a proper pause: **the enumerator values are the peripheral base addresses themselves**. `GpioPort::C` is at once the type-level identity "port C" and a carrier of the numeric value `0x40011000` — the very address that the CMSIS macros in piece 02 computed by layering additions on additions. When we want to use it, a single cast does the job:

```cpp
static inline GPIO_TypeDef* const port =
    reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
```

Still remember the thread piece 02 left hanging? We said the legitimate embedded use of `reinterpret_cast` is "treating an integer as an address". CMSIS's `(GPIO_TypeDef*)0x40011000` is the same thing in C-style-cast clothing; here it merely changes into the C++ form whose name is more honest — and narrower. One more thing to notice, the storage class: what is written here is actually `inline` plus `const`, not `constexpr` — `reinterpret_cast` cannot enter a constant expression, so writing `constexpr` in earnest would not compile, and the library author had to settle one step short. The result is unaffected, though: the pointer's value is fixed at compile time and still solidifies into a `.word 0x40011000` in the literal pool — it takes up not a single line of code at runtime.

The cost of a typo changes as well: `GpioPort::X` does not exist at all; you have only just written it down and the compiler is already turning on you. Recall the ghost bug of piece 06 — the port moved house, the clock macro was forgotten, and code like that still compiled clean.

## Four dimensions written into one type

```cpp
template <GpioPort PORT, uint16_t MASK,
          gpio::GpioDirection DIRECTION,
          gpio::GpioPull PULL = gpio::GpioPull::NoPull>
struct Gpio {
    static inline GPIO_TypeDef* const port = /* the cast from the previous section */;
    static constexpr auto mask  = MASK;
    static constexpr auto direction = DIRECTION;
    static constexpr uint8_t pin = std::countr_zero(MASK);  // C++20 <bit>
    // ...
};
```

At use sites, we write the configuration as a type alias:

```cpp
using LedPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Output>;
```

Notice the shape of the template parameters: `PORT` is an enum, `MASK` is an integer — these are the so-called **non-type template parameters (NTTPs)**. Configuration is from now on no longer an argument to a runtime function but a part of the type: `Gpio<GpioPort::C, 0x2000, Output>` and `Gpio<GpioPort::A, 0x20, Input>` are two independent types with no inheritance relationship between them, and the compiler's knowledge of them is accurate down to the last bit.

`std::countr_zero(MASK)` counts the trailing zeros of the mask for us: `0x2000` trails 13 zeros, so `pin == 13`. The pin number is **derived** from the mask — no more writing it out by hand a second time, parallel to the mask. The copy-paste ailment of problem 5 in piece 06 vanishes right here: every fact about a pin is written exactly once in the whole project.

![We write the GPIO's configuration facts into the type system, then hand them to the concept's constraints for gatekeeping](./08-gpio-type-system.drawio)

## init(): the end of the road for problem 2

`Gpio`'s `init()` enables the clock with one hand and does the configuration with the other. Look at the clock-enabling stretch — it is the single most important passage of the whole piece:

```cpp
static void enable_clock() {
    if constexpr (PORT == GpioPort::A) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if constexpr (PORT == GpioPort::B) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if constexpr (PORT == GpioPort::C) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    // ...
}
```

Why must it be `if constexpr`, and not a runtime `if`? Look at these clock-enable macros: one name per port, and the name is fixed at compile time. `__HAL_RCC_GPIOC_CLK_ENABLE` expands into a read-modify-write on a fixed address (piece 03 took apart its `tmpreg` dummy read); at runtime there simply is **no** such thing as "a clock function selectable by port". Back in the C-macro era, this could only rest on human discipline: configure GPIOC, and you had better remember to recite GPIOC's clock macro. Now that discipline is written into the type `GpioPort::C`: **choosing port C automatically chooses C's circuit breaker — you could not get it wrong even if you tried**. Problem 2 is settled right here.

As for the configuration stretch, we fill in a struct and then call the HAL:

```cpp
GPIO_InitTypeDef init{
    .Pin = mask, .Mode = direction_mode(), .Speed = GPIO_SPEED_FREQ_LOW,
    .Pull = pull_mode()};
HAL_GPIO_Init(port, &init);
```

Inside `direction_mode()` and `pull_mode()` there is more `if constexpr`, doing exactly the job of translating enums into HAL constants. What we get is a **compile-time** translation: per the type, the function body instantiates into a plain `return GPIO_MODE_OUTPUT_PP` — there is no runtime switch. The HAL's general-purpose machine keeps turning as usual (the 1060 bytes of piece 07), but the arguments fed into it can no longer be wrong.

## concept: compile-time errors for wrong configuration

The library's constraint system lives in `libestdx/gpio/gpio_base.hpp` and falls into three segments; we take them one at a time:

```cpp
template <typename Concrete>
concept GPIOPin = requires {
    { Concrete::mask } -> std::convertible_to<uint32_t>;
    { Concrete::direction } -> std::convertible_to<GpioDirection>;
    Concrete::port;
    Concrete::pin;
};

template <typename Concrete>
concept GPIOOutputPin =
    GPIOPin<Concrete> && Concrete::direction == GpioDirection::Output && requires {
        Concrete::set();
        Concrete::reset();
        Concrete::toggle();
    };

template <typename Concrete>
concept GPIOInputPin =
    GPIOPin<Concrete> && Concrete::direction == GpioDirection::Input && requires {
        { Concrete::level() } -> std::convertible_to<bool>;
    };
```

`GPIOPin` says "this thing is a pin"; `GPIOOutputPin` adds one **value constraint** on top of it: `direction == Output`. Hidden in here is a genuine faceplant (the source comment tells it in its own words): the equality test must sit **outside** the `requires {}` block, joined by conjunction. Write it inside the block instead, and `Concrete::direction == Output` only verifies that "this expression is legal" — whether "it is true" goes unverified, and a pin configured as Input will pass the constraint just the same. **The `requires` block checks "does it compile"; the `&&` outside the block checks "what the value is"**. This subtlest of dividing lines in concept semantics, the library author has already scouted out on our behalf.

And the job of consuming these constraints finally lands on `LED` (`libestdx/device/led.hpp`); let us watch it at work:

```cpp
template <gpio::GPIOOutputPin Pin,
          gpio::GpioPolarity POLARITY = gpio::GpioPolarity::ActiveHigh>
struct LED {
    static void on() {
        if constexpr (POLARITY == gpio::GpioPolarity::ActiveHigh) { Pin::set(); }
        else { Pin::reset(); }
    }
    // off() is symmetric; the polarity picks the other way
    static void toggle() { Pin::toggle(); }  // Take it easy :)
};
```

Now we run an experiment that piece 06 would not have dared to imagine: deliberately stuffing an input pin into the LED.

```cpp
using BadPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Input>;
using Led = estdx::device::LED<BadPin>;   // ← an input pin lighting the LED?
```

We save those lines as `/tmp/concept_fail.cpp`, give `arm-none-eabi-g++` one pass over them, and the compiler throws an error on the spot. Here is the genuinely-run error text for you (excerpted from arm-none-eabi-g++ 16.2):

```text
error: template constraint failure for 'template<class Pin, ...> requires
       GPIOOutputPin<Pin> struct estdx::device::LED'
  7 | using Led = estdx::device::LED<BadPin>;
    |                                      ^
note: constraints not satisfied
required for the satisfaction of 'GPIOOutputPin<Pin>'
    [with Pin = estdx::stm32f1::Gpio<GpioPort::C, 8192,
                   GpioDirection::Input, GpioPull::NoPull>]
the expression '(Concrete::direction) == estdx::gpio::GpioDirection::Output'
evaluated to 'false'
```

There is a trick to reading this kind of error: start at `template constraint failure` and hunt downward for the **`evaluated to 'false'`** line — it speaks plain human language: this `Pin`'s `direction` is `Input` rather than `Output`, so `GPIOOutputPin` is not satisfied. Which type failed, which constraint went unsatisfied, which sub-expression broke — all of it is confessed within one screen. Compare: piece 06's macro with a misspelled argument compiled anyway, and piece 07's `assert_param` does nothing by default; by this point we have not even finished writing `main`. Problem 3 is settled here too.

## Polarity enters the type: cashing in the circuit knowledge

Now look at `LED`'s second template parameter, `POLARITY`: the default is `ActiveHigh`, while this particular Blue Pill's PC13 needs `ActiveLow` — and so the circuit lesson of piece 05 (the LED's anode tied to VCC, conducting on sink current) turns into a type parameter here. The `if constexpr` inside `on()` makes a choice of two at instantiation: with `ActiveLow`, `on` compiles into `Pin::reset()` and `off` into `Pin::set()`. **The hardware fact that a low level lights the lamp is from now on written into the type**. Whoever writes the application no longer needs to remember PC13's circuit topology. A board revision moves the LED's wiring — you change one enumerator literal, and `on`/`off` swap themselves, all automatically.

## Checking the disassembly: what compile time paid, and what it did not

The firmware we actually ran is `examples/03_led` (the whole of it is just the `LedPin` and `Led` from above, plus the standard clock configuration); built with `-O3` it comes out at 3476 bytes of text, against the bare-register version's 2380. Next, let us see what `LedPin::init()` inside `main` expanded into (a real excerpt from `arm-none-eabi-objdump -d`):

```text
8000154: ldr   r3, [pc, #80]    @ 0x40021000  ← RCC base address lands in the literal pool
8000158: ldr   r2, [r3, #24]                   ← APB2ENR
800015c: orr.w r2, r2, #16                     ← |= IOPCEN
8000160: str   r2, [r3, #24]
8000162: ldr   r3, [r3, #24]                   ← tmpreg dummy read (the subtlety from piece 03)
        ...  filling in the on-stack GPIO_InitTypeDef ...
800017a: bl    HAL_GPIO_Init
```

Just look: the clock trio, the `tmpreg` dummy read, the struct filling — **all of it is inlined into `main`; not a single function call remains**. `enable_clock()` has vanished as a function. That is the compile-time semantics of `if constexpr`: the branch that does not hold is never instantiated at all, and once the branch that holds is inlined, it is exactly those few instructions we hand-wrote in piece 02.

Now the runtime path, `Led::on()` / `Led::off()`:

```text
8000180: mov.w r1, #8192        @ GPIO_PIN_13
8000184: ldr   r0, [pc, #36]    @ 0x40011000  ← GPIOC
8000186: bl    HAL_GPIO_WritePin ← r2=0 (on) / r2=1 (off)
```

When `on()` is called, `r2` holds `0` (`GPIO_PIN_RESET`); for `off()` it holds `1`: **the polarity translation is finished at compile time — at runtime there is no if**. But at the same time, one thing I must honestly confess: estdx's `set`/`reset` are thin forwardings to `HAL_GPIO_WritePin`, and with LTO off, they do not get inlined. That `bl` jump is still there, and so is the `PinState` branch inside WritePin (piece 07 took it apart). Set that against the bare-register version's single `str r5, [r4, #16]`: **the template machinery itself is zero-overhead (the type vanishes at compile time, and the address lands directly in the literal pool), but the implementation path chose HAL functions, and so it honestly carries the HAL's call cost**. That is the accurate way to read the phrase "zero-overhead abstraction": for an abstraction you do not use, we do not pay a single byte. For the implementation you did choose, every byte paid is visible, and you can see exactly where it lands.

## The final verdict on the five problems

| Problem | C macros (piece 06) | HAL (piece 07) | Template + concept (this piece) |
|----|--------------|--------------|----------------------|
| 1. No types | Bare numbers everywhere | The enum blocks misspellings | **The type is the configuration** — misspellings cannot exist |
| 2. Clock left outside | Human discipline | The macro still sits outside | **Choosing the port chooses the breaker** |
| 3. Unconstrained combinations | Does not blow up even at runtime | Only acts up at runtime | **Compile-time error** |
| 4. Undebuggable | Macro expansions unreadable | Real functions, real symbols | Types have names; instantiations are traceable |
| 5. Copy-paste | Adding a pin is pure copying | The struct is reused | **One type, one pin — derived, not copied** |

This is the road C has walked: we climbed from bit macros to SPL, then on to HAL; the struct abstraction swallowed a thousand-odd bytes, and problems 2 and 3 never got solved. Why? Look: "the port is configured but its breaker is not closed" and "an input pin used as an output" — the fault lies in the same place: **nobody is answerable for the consistency between configurations**. And C's function arguments each go their own way; nothing vouches for them. That is precisely the business the type system runs: once configuration becomes a type, consistency turns into the question of whether that type exists at all. In this station of ours — from `Led::on()` all the way down to the wires of BSRR — the main line is now complete, and one closing piece remains: the finer points of `toggle` and the verification tools we have gathered along the way are all stowed into it. See you in the next piece.

## Your turn to get your hands dirty

1. Run `arm-none-eabi-objdump -d` on the `led_example` built from `03_led`, find the clock trio and the two `bl HAL_GPIO_WritePin` calls inside `main`, and check the values of `r2` against the semantics of `ActiveLow`.
2. Change `03_led`'s polarity to `ActiveHigh` and rebuild; watch in the disassembly how the `r2` of `on`/`off` trade places, and get a feel for "change one literal, and the circuit knowledge rewires itself".
3. Stage a compile-time crash of your own: change `GpioPort::C` into the nonexistent `GpioPort::X`, and compare that error with the `direction` one.
4. In `gpio_base.hpp`, move `Concrete::direction == GpioDirection::Output` inside the `requires {}` block, recompile `/tmp/concept_fail.cpp`, and see with your own eyes the difference between "the expression is legal" and "the expression is true" — reproducing by hand the very trap the library comment warns about.

## Hey, hey — self-check before you go

- Tell it in your own words: how does `GpioPort::C` carry both the "port identity" and the "base address" at once? How much code space does it take up at runtime?
- Why must clock enabling be `if constexpr` rather than a runtime `if`? Which property of the macros decides this — can you point at it?
- Why does writing `direction == Output` inside the concept's `requires {}` block fail to stop an input pin? Where does the correct form live — do you still remember?
- Does `ActiveLow`'s `on()` compile into `Pin::set()` or `Pin::reset()`? And what is the `r2` we saw in the disassembly?
- What is the precise statement of "zero-overhead abstraction"? Which of estdx's costs comes not from templates but from a choice — can you pull the two apart?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference"
    title="constraints and concepts — requires clause vs requires-expression"
    :year="2025"
    url="https://en.cppreference.com/w/cpp/language/constraints"
    chapter="Atomic constraints; requires-expression semantics"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="if statement — constexpr if"
    :year="2025"
    url="https://en.cppreference.com/w/cpp/language/if"
    chapter="Constexpr if; discarded statements are not instantiated"
  />
  <ReferenceItem
    :id="3"
    author="Stroustrup, B."
    title="The Design and Evolution of C++ — zero-overhead principle"
    :year="1994"
    url="https://www.stroustrup.com/dne.html"
    chapter="Zero-overhead rule: What you don't use, you don't pay for"
  />
  <ReferenceItem
    :id="4"
    author="cppreference"
    title="std::countr_zero — count trailing zeros"
    :year="2025"
    url="https://en.cppreference.com/w/cpp/numeric/countr_zero"
    chapter="<bit> library, C++20"
  />
</ReferenceCard>
