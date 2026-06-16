---
chapter: 16
difficulty: intermediate
order: 8
platform: stm32f1
reading_time_minutes: 4
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 26: Refactoring Button Code with `enum class` — Type-Safe Input'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/08-cpp-enum-class-button.md
  source_hash: 36189e18e942c050bc7d74d92640957d1abdb8f954e907e65495a4f8bede4cf7
  translated_at: '2026-06-16T04:11:06.123777+00:00'
  engine: anthropic
  token_count: 801
---
# Part 26: Refactoring Button Code — Type-Safe Input

> Following up on the previous part: Part 7 fully explained the debounce state machine. Now, let's begin the C++ refactoring journey—just like the LED tutorial, we'll start with `enum class`.

---

## Pain Points of the C Version

So far, our button code has been in C style. Let's look at the "magic numbers" in the debounce code:

```c
uint8_t stable_pressed = 0;   // 0 是松开，1 是按下——但类型是 uint8_t，编译器不知道这个语义
uint8_t last_raw = 0;
uint8_t current = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;
```

`uint8_t` can be anything—pin numbers, state values, or mode selections. The compiler won't stop you from assigning a pin number to a state variable. In 15 lines of code, this isn't a problem, but in a 1,500-line project, it's a ticking time bomb.

The LED tutorial Part 08 discussed the same problem—C macros lack `#define LED_PIN GPIO_PIN_13`. Buttons face the same issue, only the "magic numbers" have shifted from macros to bare integers.

---

## ButtonActiveLevel Enum

LEDs have `ActiveLevel` to indicate active high or active low. Buttons share the same concept—in a pull-up scheme, pressed = low (Active Low), and in a pull-down scheme, pressed = high (Active High).

```cpp
enum class ButtonActiveLevel { Low, High };
```

This enum is isomorphic to the LED's `ActiveLevel`, but we use different names (`ButtonActiveLevel`) to distinguish semantics. The LED's `ActiveLevel` describes "the level required to light the LED," while the button's `ButtonActiveLevel` describes "the level when the button is pressed." Although the underlying values are the same, they are different concepts and should not be mixed.

With `ButtonActiveLevel`, the `is_pressed()` method no longer needs `#ifdef` or runtime judgments:

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

`if constexpr` selects branches at compile time—for a `ButtonActiveLevel::Low` button, the compiler generates only `state == State::UnSet` code; for `ButtonActiveLevel::High`, only `state == State::Set`. Zero runtime overhead; the level logic is "hard-coded" at compile time.

This follows the same pattern as the `if constexpr` clock enable in LED tutorial Part 10—using compile-time branching to replace runtime judgment.

---

## Private enum class State

In the previous part, we detailed the seven states. Now let's see how they are defined in the code:

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

Several design decisions are worth explaining:

**Why `enum class` instead of `enum`?** Scope isolation. Names like `Idle` and `Pressed` are very common. If your code has other state machines (like an LED blinking state machine or a communication protocol state machine), the `Idle` of a plain `enum` will conflict. `enum class` requires the `State::Idle` to be fully qualified, so members with the same name in different `enum class` do not interfere with each other.

**Why a private enum?** `State` is defined in the `private` section of the `Button` class. External code doesn't need to know that the button has seven internal states—they just need to call `poll_events()`. Making `State` private is information hiding: implementation details are not exposed to the caller.

**Why not specify the underlying type?** The default underlying type is `int` (usually 32 bits). With only seven values, wouldn't `uint8_t` save space? In the context of `sizeof(Button)`, a member variable `state_` of `State` type could indeed be stored using `uint8_t`. However, compilers usually align to the natural word length, so the actual footprint of `uint8_t` and `int` might be the same. Unless your RAM is so tight that you have to squeeze every single byte, the default `int` is the safest choice.

---

## Review: enum class Comparison in LED and Button Tutorials

| Feature | LED Tutorial | Button Tutorial |
|------|---------|---------|
| GpioPort | Port address | Reused, no change |
| Mode | Output mode | Added input/interrupt mode enum values |
| PullPush | Pull-up/pull-down | Reused, buttons use `PullUp` |
| State | Set/UnSet | Reused, `read_pin_state()` returns it |
| ActiveLevel | LED on/off level | **Added** `ButtonActiveLevel` |
| Internal State | None | **Added** private `State` enum |

`enum class` has two new application scenarios in the button tutorial: `ButtonActiveLevel` acts as a template parameter (compile-time constant), and `State` acts as the state type for an internal state machine. Their uses are completely different—the former is a configuration parameter for the caller, the latter is an implementation detail—but both benefit from the type safety and scope isolation of `enum class`.

---

## Looking Back

In this part, we used `enum class` to refactor two types of enumerations in the button code:

1. **`ButtonActiveLevel`** — Template parameter, determines level logic at compile time, combined with `if constexpr` for zero-overhead branching.
2. **`State`** — Private state machine enumeration, seven states each with its own role, scope isolation prevents naming conflicts.

These are consistent with the `enum class` section of the LED tutorial—same tools, different application scenarios. The next part introduces a brand new C++ feature: `std::variant` and `std::visit`, to express button events in a type-safe way.
