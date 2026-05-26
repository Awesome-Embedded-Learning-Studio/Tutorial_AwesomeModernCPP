---
title: 'Part 26: Refactoring Button Code with `enum class` — Type-Safe Input'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 8
translation:
  source: documents/vol8-domains/embedded/02-button/08-cpp-enum-class-button.md
  source_hash: 337e2f76412a48cf19054b4e6fe3e9bb6509ddd460785ff053c0788bc68cb38c
  translated_at: '2026-05-26T12:11:54.253568+00:00'
  engine: anthropic
  token_count: 795
---
# Part 26: `enum class` Refactoring Button Code — Type-Safe Input

> Following up on the previous article: the 7-state debounce state machine has been thoroughly explained. Now we begin the C++ refactoring journey—just like the LED tutorial, starting with `enum class`.

---

## Pain Points of the C Version

So far, our button code has been C-style. Look at the "magic numbers" in the debounce code:

```c
uint8_t stable_pressed = 0;   // 0 是松开，1 是按下——但类型是 uint8_t，编译器不知道这个语义
uint8_t last_raw = 0;
uint8_t current = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;
```

`uint8_t` could be anything—a pin number, a state value, or a mode selection. The compiler won't stop you from assigning a pin number to a state variable. In 15 lines of code, this isn't a problem; in a 1,500-line project, it's a ticking time bomb.

Part 08 of the LED tutorial covered the exact same issue—C macros lack `#define LED_PIN GPIO_PIN_13` type safety. Buttons face the same problem, only the "magic numbers" have shifted from macros to bare integers.

---

## ButtonActiveLevel Enum

LEDs have `ActiveLevel` to indicate active-high or active-low. Buttons share the same concept—in a pull-up configuration, pressed equals low level (Active Low), and in a pull-down configuration, pressed equals high level (Active High).

```cpp
enum class ButtonActiveLevel { Low, High };
```

This enum is structurally identical to the LED's `ActiveLevel`, but we use a different name (`ButtonActiveLevel`) to distinguish the semantics. The LED's `ActiveLevel` describes "the level needed to turn on the LED," while the button's `ButtonActiveLevel` describes "the level when the button is pressed." Although the underlying values are the same, they are distinct concepts and should not be mixed.

With `ButtonActiveLevel`, the `is_pressed()` method no longer needs `#ifdef` or runtime checks:

```cpp
bool is_pressed() const {
    auto state = Base::read_pin_state();
    if constexpr (LEVEL == ButtonActiveLevel::Low) {
        return state == Base::State::UnSet;  // 低电平 = 按下
    } else {
        return state == Base::State::Set;    // 高电平 = 按下
    }
}
```

`if constexpr` selects the branch at compile time—for a `ButtonActiveLevel::Low` button, the compiler only generates the code for `state == State::UnSet`; for `ButtonActiveLevel::High`, it only generates `state == State::Set`. Zero runtime overhead; the level logic is "baked in" at compile time.

This is the same pattern as the `if constexpr` clock enabling in Part 10 of the LED tutorial—using compile-time branches to replace runtime checks.

---

## Private enum class State

In the previous article, we broke down the 7 states in detail. Now let's see how they are defined in code:

```cpp
enum class State {
    BootSync,
    Idle,
    DebouncingPress,
    Pressed,
    DebouncingRelease,
    BootPressed,
    BootReleaseDebouncing,
};
```

A few design decisions are worth explaining:

**Why `enum class` instead of `enum`?** Scope isolation. Names like `Idle` and `Pressed` are very common—if your code has other state machines (like an LED blinking state machine or a communication protocol state machine), the `Idle` of a plain `enum` will clash. `enum class` requires full `State::Idle` qualification, so identically named members in different `enum class`s don't interfere with each other.

**Why a private enum?** `State` is defined in the `private` section of the `Button` class. External code doesn't need to know that the button internally has 7 states—they just need to call `poll_events()`. Making `State` private is information hiding: implementation details are not exposed to the caller.

**Why not specify an underlying type?** The default underlying type is `int` (usually 32 bits). With only 7 values, wouldn't `uint8_t` save space? In an `sizeof(Button)` context, the `state_` member variable of type `State` could indeed be stored using `uint8_t`. However, compilers typically align to the natural word size, so the actual footprint of `uint8_t` and `int` might be identical. Unless your RAM is so tight that you have to squeeze out every single byte, the default `int` is the safest choice.

---

## Recap: enum class Comparison in the LED and Button Tutorials

| Feature | LED Tutorial | Button Tutorial |
|---------|-------------|-----------------|
| GpioPort | Port address | Reused, no changes |
| Mode | Output mode | Added enum values for input/interrupt modes |
| PullPush | Pull-up/pull-down | Reused, buttons use `PullUp` |
| State | Set/UnSet | Reused, `read_pin_state()` returns it |
| ActiveLevel | LED on/off level | **Added** `ButtonActiveLevel` |
| Internal state | None | **Added** private `State` enum |

`enum class` has two new application scenarios in the button tutorial: `ButtonActiveLevel` as a template parameter (a compile-time constant), and `State` as the state type for the internal state machine. Their purposes are completely different—the former is a configuration parameter面向 callers面向 callers, the latter is an implementation detail—but both benefit from the type safety and scope isolation of `enum class`.

---

## Looking Back

In this article, we used `enum class` to refactor two categories of enums in the button code:

1. **`ButtonActiveLevel`** — A template parameter that determines level logic at compile time, paired with `if constexpr` to achieve zero-overhead branching
2. **`State`** — A private state machine enum with 7 states each serving its own purpose, using scope isolation to prevent naming conflicts

These follow the same lineage as the `enum class` chapters in the LED tutorial—the same tools, different application scenarios. The next article introduces a brand-new C++ feature: `std::variant` and `std::visit`, to express button events in a type-safe manner.
