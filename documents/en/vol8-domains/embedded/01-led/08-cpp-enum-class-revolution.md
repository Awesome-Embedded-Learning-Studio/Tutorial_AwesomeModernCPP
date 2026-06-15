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
title: 'Part 13: First Refactoring — Replacing Macros with enum class, the Start of
  Type Safety'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/01-led/08-cpp-enum-class-revolution.md
  source_hash: f8b0472a1e1d9b03fb216f2cfc73b47927cf313fc0292523e1115842cca9cdb7
  token_count: 1490
  translated_at: '2026-05-26T12:06:12.539347+00:00'
description: ''
---
# Part 13: The First Refactor — Replacing Macros with enum class, the Start of Type Safety

> Continuing from the previous part: the C macro approach works but has problems—lack of type safety, no enforced association between ports and clocks, and code that cannot be reused. Now we take the first step in our C++ refactor: replacing macro definitions with `enum class`.

---

## Why Replace Macros

The C macro LED driver from the previous part looked decent—macros centralized the hardware parameters, and functions encapsulated the operation logic. But the problem lies with macros themselves: `#define LED_PORT GPIOC` expands to `((GPIO_TypeDef *)0x40011000UL)`—a bare integer address. The compiler won't check whether this value is valid, nor will it stop you from passing a random integer to a function expecting `GPIO_TypeDef*`.

`enum class` is a feature introduced in C++11 that moves us from a "sea of macros" into a "world of type safety." After redefining GPIO parameters with `enum class`, the compiler checks types at compile time—you cannot pass a mode value to a function expecting a pull-up/pull-down parameter, nor can you pass the address of Port A to an operation expecting Port C.

---

## The GpioPort Enum — Type-Safe Port Addresses

In `device/gpio/gpio.hpp`, ports are defined like this:

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,    // 0x40010800
    B = GPIOB_BASE,    // 0x40010C00
    C = GPIOC_BASE,    // 0x40011000
    D = GPIOD_BASE,    // 0x40011400
    E = GPIOE_BASE,    // 0x40011800
};
```

There are a few design decisions here that need explaining. First, why is the underlying type `uintptr_t` instead of `uint32_t`? Because the enum values are memory addresses, and `uintptr_t` is the "unsigned integer type sufficient to hold a pointer" defined by the C standard—on a 32-bit ARM it is `uint32_t`, but on a 64-bit platform it automatically becomes 64-bit. Using `uintptr_t` better expresses the semantics of "this is an address" compared to `uint32_t`, and makes the code theoretically more portable.

Second, why use `GPIOA_BASE` instead of `GPIOA`? `GPIOA` is a pointer constant defined by CMSIS—it has already been cast to a `GPIO_TypeDef*` type. Enum values, however, must be integer constant expressions, not pointers. `GPIOA_BASE` is a pure integer address that can serve as an enum value. Later we will see how `constexpr native_port()` converts this integer address back into a `GPIO_TypeDef*` pointer.

Finally, why use `enum class` instead of a plain `enum`? The reason is scope isolation. Members of a plain `enum` "leak" into the enclosing scope—if you define two plain enums `enum Color { Red, Green }` and `enum Pull { PullUp, PullDown }`, the compiler might not necessarily report an error, but if you define members with the same name in both enums, a conflict will arise. Members of an `enum class` must be accessed using a fully qualified name like `GpioPort::A`, and different `enum class`s will never conflict with each other.

---

## Mode, PullPush, Speed — Enumerating HAL Constants

The three core GPIO configuration parameters are also redefined as `enum class`:

```cpp
enum class Mode : uint32_t {
    Input = GPIO_MODE_INPUT,
    OutputPP = GPIO_MODE_OUTPUT_PP,
    OutputOD = GPIO_MODE_OUTPUT_OD,
    AfPP = GPIO_MODE_AF_PP,
    AfOD = GPIO_MODE_AF_OD,
    Analog = GPIO_MODE_ANALOG,
    ItRising = GPIO_MODE_IT_RISING,
    ItFalling = GPIO_MODE_IT_FALLING,
    // ... 更多模式
};

enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,
    PullUp = GPIO_PULLUP,
    PullDown = GPIO_PULLDOWN,
};

enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,
    Medium = GPIO_SPEED_FREQ_MEDIUM,
    High = GPIO_SPEED_FREQ_HIGH,
};
```

There is a design principle at work here: the underlying type `uint32_t` maps one-to-one with the field types in the HAL library. The `Mode`, `Pull`, and `Speed` fields of `GPIO_InitTypeDef` are all of type `uint32_t`, so our enums also use `uint32_t` as their underlying type. This means extracting the underlying value via `static_cast` is zero-overhead—there is no cost for type conversion; the compiler simply treats the stored integer value "as" another type.

Now imagine accidentally passing a mode value to a function expecting a pull-up/pull-down parameter:

```cpp
// C宏风格：编译通过，运行时LED行为异常
g.Pull = GPIO_MODE_OUTPUT_PP;   // 错了！但编译器不会警告

// enum class风格：编译直接报错
setup(Mode::OutputPP, Mode::OutputPP);  // 编译错误！第二个参数期望PullPush类型
```

The type safety of `enum class` shines here: `Mode` and `PullPush` are completely different types, and the compiler will prevent you from mixing them up. In the world of C macros, both `GPIO_MODE_OUTPUT_PP` and `GPIO_PULLUP` are just macros for `uint32_t`, and the compiler sees absolutely no difference.

---

## static_cast — The Bridge from Enums to HAL

Values of an `enum class` cannot be implicitly converted to integers—this is a safety feature, but the HAL library only accepts `uint32_t`. So we use `static_cast` for explicit conversion:

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

`static_cast<uint32_t>(gpio_mode)` is resolved at compile time—if `gpio_mode` is `Mode::OutputPP` (underlying value `0x01`), the result of `static_cast` is simply `0x01`. This process generates no runtime code; it merely extracts the integer stored in the enum.

Compare this with C-style implicit conversion:

```c
// C风格：宏展开后是裸整数，类型信息完全丢失
g.Mode = GPIO_MODE_OUTPUT_PP;  // 等价于 g.Mode = 0x01;

// C++风格：枚举类型在编译时验证，然后零开销地提取底层值
init_types.Mode = static_cast<uint32_t>(gpio_mode);  // gpio_mode必须是Mode类型
```

However, this "zero-overhead" safety of `static_cast` has a notable boundary. While it does not check value validity at runtime—if you add a new enum value in `enum class Mode` but forget to define it in the corresponding HAL library macro, `static_cast` will not report an error; it will faithfully pass the underlying value through. This is why our enum values must correspond one-to-one with the HAL macros, and this mapping must be maintained by the developer.

---

## The ActiveLevel Enum — Enumerating Application-Layer Concepts

```cpp
enum class ActiveLevel { Low, High };
```

Note that this enum does not specify an underlying type—its default underlying type is `int`. This is intentional. `Low` and `High` are not HAL macro values; they are application-layer concepts we defined ourselves—expressing "is this LED circuit active-low or active-high?" This concept is completely unrelated to the HAL library; it is an abstraction at the LED driver level.

The default underlying type of `enum class` is `int`, which is perfectly fine in C++—embedded environments fully support the `int` type. If you want more precise control over the size, you can explicitly specify `enum class ActiveLevel : uint8_t`, but for an enum with only two values, this minor storage optimization is not worth the added code complexity.

---

## The State Enum — Encapsulating Pin States

```cpp
enum class State { Set = GPIO_PIN_SET, UnSet = GPIO_PIN_RESET };
```

The value of `GPIO_PIN_SET` is 1, and the value of `GPIO_PIN_RESET` is 0. `Set` means the pin is high, and `UnSet` means the pin is low. This enum wraps the HAL's `GPIO_PinState` type into a type-safe version—just like `Mode` and `PullPush` earlier, you cannot pass `State::Set` to a function expecting a `Mode` parameter.

---

## C++23's std::to_underlying — The Elegant Future Alternative

Our current code uses `static_cast<uint32_t>(value)` to extract the underlying value from an enum. C++23 introduces a more elegant utility function, `std::to_underlying(enum_value)`, which is shorthand for `static_cast<std::underlying_type_t<E>>(e)`:

```cpp
// 当前写法（C++11兼容）
init_types.Mode = static_cast<uint32_t>(gpio_mode);

// C++23的std::to_underlying写法（未来目标）
init_types.Mode = std::to_underlying(gpio_mode);
```

`std::to_underlying` is more concise and does not require you to manually write out the underlying type—the compiler deduces it automatically. However, our code does not use it yet because the `arm-none-eabi-g++` paired with the `newlib-nano` standard library might not fully support the C++23 `<utility>` header yet. `static_cast` is a feature available since C++11 and has better compatibility.

Once you confirm that your toolchain supports the full C++23 standard library, you can safely replace all `static_cast<uint32_t>(xxx)` instances with `std::to_underlying(xxx)`. This is a purely mechanical replacement involving no logic changes.

---

## The Result of This Refactor

After the `enum class` refactor, our GPIO configuration code is much safer than the pure C macro version. Ports can only be one of `GpioPort::A` through `GpioPort::E`, making it impossible to pass in invalid addresses. Modes can only be members of the `Mode` enum, making it impossible to pass in a random `uint32_t`. Furthermore, `Mode` and `PullPush` are distinct types, and the compiler will prevent you from mixing them up.

But there are still unresolved issues: the port and pin are still runtime parameters, not compile-time bound constants. Clock enable is still manual—you have to remember to call `__HAL_RCC_GPIOx_CLK_ENABLE()`. These problems will not be solved until we introduce templates—and that is the subject of the next part.

---

⚠️ **Warning:** Although `enum class` solves the type safety problem, it also introduces a new one—it cannot be implicitly converted to an integer. Every time you pass a value to a HAL API, you need `static_cast<uint32_t>(value)`. If you find this conversion tedious to write, C++23 offers `std::to_underlying(enum_value)` as a more elegant alternative—but since our arm-none-eabi toolchain might not support the complete C++23 standard library, using `static_cast` for now is the safest choice.

---

## Looking Back

In this part, we did three things: we replaced `#define` with `enum class` to gain type safety, used `static_cast` for zero-overhead conversion between enums and HAL, and used `ActiveLevel` to express application-layer concepts. All of these prepare us for the upcoming template refactor—template parameters require compile-time constants, and the members of an `enum class` happen to be compile-time constant expressions.

In the next part, we will introduce the core weapon of C++ templates—non-type template parameters (NTTPs)—to turn ports and pins from runtime parameters into part of compile-time types. This is the most important refactoring step in the entire series.
