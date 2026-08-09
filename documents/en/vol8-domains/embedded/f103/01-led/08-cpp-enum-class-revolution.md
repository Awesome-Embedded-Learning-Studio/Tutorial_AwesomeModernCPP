---
chapter: 15
difficulty: beginner
order: 8
platform: stm32f1
reading_time_minutes: 9
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 13: First Refactor — Replacing Macros with `enum class`, The Start of
  Type Safety'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/08-cpp-enum-class-revolution.md
  source_hash: 0f865bc0cbaab5f05171cfaf3e7e7780c5f447e41b96db318d008af4874085c0
  translated_at: '2026-06-16T04:09:56.645520+00:00'
  engine: anthropic
  token_count: 1496
---
# Part 13: The First Refactor — Replacing Macros with `enum class`, The Start of Type Safety

> Following the previous part: The C macro solution works but has issues—lack of type safety, no enforced association between ports and clocks, and code reusability problems. Now we take the first step in our C++ refactor: using `enum class` to replace macro definitions.

---

## Why Replace Macros

The C macro LED driver from the previous part looked decent—macros centralized hardware parameters, and functions encapsulated operation logic. But the problem lies with the macros themselves: `GPIOA` expands to a raw integer address. The compiler won't check if this value is reasonable, nor will it stop you from assigning a random integer to a function expecting a specific port type.

`enum class` is a feature introduced in C++11 that moves us from the "sea of macros" into a "world of type safety." After redefining GPIO parameters with `enum class`, the compiler checks types at compile time—you cannot pass a mode value to a function expecting a pull-up/pull-down parameter, nor can you pass the address of Port A to an operation expecting Port C.

---

## The `GpioPort` Enumeration — Type-Safe Port Addresses

In `GpioPort.hpp`, the port is defined like this:

```cpp
// GpioPort.hpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,
    B = GPIOB_BASE,
    // ...
};
```

Here are a few design decisions to explain. First, why is the underlying type `uintptr_t` instead of `uint32_t`? Because the enumeration values are memory addresses, and `uintptr_t` is the C standard-defined "unsigned integer type capable of holding a pointer"—on a 32-bit ARM it is `uint32_t`, but on 64-bit platforms it automatically becomes 64-bit. Using `uintptr_t` expresses the semantic "this is an address" better than `uint32_t` and makes the code theoretically more portable.

Second, why use `GPIOA_BASE` instead of `GPIOA`? `GPIOA` is a pointer constant defined by CMSIS—it has already been cast to a `GPIO_TypeDef` type. Enumeration values must be integer constant expressions, not pointers. `GPIOA_BASE` is a pure integer address and can serve as an enumeration value. Later, we will see how `static_cast` converts this integer address back into a `GPIO_TypeDef` pointer.

Finally, why use `enum class` instead of a plain `enum`? The reason is scope isolation. Members of a plain `enum` "leak" into the enclosing scope—if you define two plain enumerations, the compiler might not necessarily error, but if you define members with the same name in two enumerations, a conflict occurs. `enum class` members must be accessed via a fully qualified name like `GpioPort::A`, so different `enum class` definitions will never conflict.

---

## `Mode`, `PullPush`, `Speed` — Enumerated HAL Constants

The three core configuration parameters for GPIO are also redefined as `enum class`:

```cpp
enum class Mode : uint32_t {
    Input = GPIO_MODE_INPUT,
    Output = GPIO_MODE_OUTPUT_PP,
    // ...
};

enum class Pull : uint32_t {
    None = GPIO_NOPULL,
    Up = GPIO_PULLUP,
    Down = GPIO_PULLDOWN,
};

enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,
    Medium = GPIO_SPEED_FREQ_MEDIUM,
    High = GPIO_SPEED_FREQ_HIGH,
    VeryHigh = GPIO_SPEED_FREQ_VERY_HIGH,
};
```

There is a design principle at play here: the underlying type `uint32_t` corresponds one-to-one with the HAL library field types. The `Mode`, `Pull`, and `Speed` fields in `GPIO_InitTypeDef` are all `uint32_t` types, so our enumeration underlying types also use `uint32_t`. This means `static_cast` extracts the underlying value with zero overhead—no type conversion cost, the compiler simply treats the stored integer value "as" another type.

Now imagine if you accidentally pass a mode value to a function expecting a pull-up/pull-down parameter:

```cpp
// Error: cannot convert 'Mode' to 'Pull'
init(port, pin, Mode::Output, Pull::None, Speed::Low);
```

The type safety of `enum class` shines here: `Mode` and `Pull` are completely different types, and the compiler will stop you from mixing them. In the world of C macros, `GPIO_MODE_OUTPUT_PP` and `GPIO_NOPULL` are both integer macros, and the compiler sees no difference.

---

## `static_cast` — The Bridge from Enum to HAL

Values of an `enum class` cannot be implicitly converted to integers—this is a safety feature, but the HAL library only recognizes `uint32_t`. So we use `static_cast` for explicit conversion:

```cpp
void init(GpioPort port, uint8_t pin, Mode mode, Pull pull, Speed speed) {
    GPIO_InitTypeDef init_struct = {0};
    init_struct.Mode = static_cast<uint32_t>(mode);
    init_struct.Pull = static_cast<uint32_t>(pull);
    init_struct.Speed = static_cast<uint32_t>(speed);
    // ...
}
```

`static_cast` is resolved at compile time—if `mode` is `Mode::Output` (underlying value `0x00000010`), the result of `static_cast<uint32_t>(mode)` is `0x00000010`. This process generates no runtime code; it simply extracts the underlying integer stored in the enumeration.

Contrast this with C-style implicit conversion:

```cpp
// C style: implicit conversion, no type safety
init_struct.Mode = mode;
```

However, this "zero-overhead" safety of `static_cast` has a notable boundary. While it doesn't check value validity at runtime—if you add a new enumeration value in `Mode` but forget to define it in the corresponding HAL macro, `static_cast` won't error; it will faithfully pass the underlying value. This is why our enumeration values must correspond one-to-one with HAL macros, a relationship the developer must maintain.

---

## `ActiveLevel` — Enumerating Application Layer Concepts

```cpp
enum class ActiveLevel : bool {
    Low = false,
    High = true,
};
```

Note that this enumeration doesn't specify an underlying type—its default underlying type is `int`. This is intentional. `Low` and `High` are not HAL macro values but application-layer concepts we define ourselves—they express whether "this LED circuit is active-low or active-high." This concept is completely unrelated to the HAL library; it is an abstraction at the LED driver level.

The default underlying type for `enum class` is `int`, which is fine in C++—embedded environments fully support the `int` type. If you want more precise control over size, you can explicitly specify `uint8_t`, but for an enumeration with only two values, this storage optimization isn't worth the added code complexity.

---

## The `State` Enumeration — Encapsulating Pin States

```cpp
enum class State : uint32_t {
    High = GPIO_PIN_SET,
    Low = GPIO_PIN_RESET
};
```

The value of `State::High` is 1, and `State::Low` is 0. `High` indicates the pin is at a high logic level, `Low` indicates it is at a low logic level. This enumeration wraps the HAL's `GPIO_PinState` type into a type-safe version—just like `Mode` and `Pull` earlier, you cannot pass `State::High` to a function expecting a `Mode` parameter.

---

## C++23's `std::to_underlying` — The Elegant Future Alternative

Our current code uses `static_cast<uint32_t>` to extract the underlying value from an enumeration. C++23 introduces a more elegant utility function, `std::to_underlying`, which is shorthand for `static_cast<std::underlying_type_t<T>>(val)`:

```cpp
// C++23 version
auto val = std::to_underlying(MyEnum::Value);
```

`std::to_underlying` is more concise and doesn't require you to manually write out the underlying type—the compiler deduces it automatically. However, our code doesn't currently use it because `arm-none-eabi-gcc` paired with the standard library may not yet have complete support for the C++23 `<utility>` header. `static_cast` is a feature available since C++11 and offers better compatibility.

Once you confirm your toolchain supports the full C++23 standard library, you can safely replace all `static_cast<uint32_t>` with `std::to_underlying`. This is a purely mechanical replacement involving no logic changes.

---

## The Effect of Refactoring So Far

After this `enum class` refactor, our GPIO configuration code is much safer than the pure C macro version. Ports can only be one of `GpioPort::A` through `GpioPort::G`, making it impossible to pass invalid addresses. Modes can only be members of the `Mode` enumeration, preventing random `uint32_t` values. Furthermore, `Mode` and `Pull` are distinct types, so the compiler stops you from mixing them.

But some problems remain unsolved: ports and pins are still runtime parameters, not compile-time bound constants. Clock enabling is still manual—you have to remember to call `__HAL_RCC_GPIOA_CLK_ENABLE()`. These issues will be resolved when we introduce templates—that is the topic of the next part.

---

⚠️ **Note:** While `enum class` solves type safety issues, it introduces a new one—inability to implicitly convert to integers. Every time you pass to the HAL API, a `static_cast` is needed. If you find this conversion tedious to write, C++23 offers `std::to_underlying` as a more elegant alternative—but since our `arm-none-eabi` toolchain might not support the complete C++23 standard library, using `static_cast` is the safest choice for now.

---

## Looking Back

In this part, we did three things: used `enum class` to replace macros for type safety, used `static_cast` for zero-overhead conversion between enumerations and the HAL, and used `enum class` to express application-layer concepts. These are preparations for the upcoming template refactor—template parameters require compile-time constants, and `enum class` members happen to be compile-time constant expressions.

In the next part, we will introduce a core weapon of C++ templates—Non-Type Template Parameters (NTTP)—to transform ports and pins from runtime parameters into parts of compile-time types. This is the most critical refactoring step in the entire series.
