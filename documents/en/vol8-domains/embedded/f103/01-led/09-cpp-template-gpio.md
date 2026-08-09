---
chapter: 15
difficulty: beginner
order: 9
platform: stm32f1
reading_time_minutes: 8
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 14: The Second Refactor — Templates Take the Stage, Binding Ports and
  Pins at Compile Time'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/09-cpp-template-gpio.md
  source_hash: 55d7bacaa51650b9954186251319214eb433c93865b6c0af401555cf0c572535
  translated_at: '2026-06-16T04:09:56.593421+00:00'
  engine: anthropic
  token_count: 1314
---
# Part 14: The Second Refactor — Templates Arrive, Binding Ports and Pins at Compile Time

> Following the previous part: `enum class` solved type safety issues, but ports and pins were still runtime parameters. This part introduces a core C++ template weapon—Non-Type Template Parameters (NTTP)—to transform ports and pins into compile-time constants.

---

## What is a Template — Embedded Developer Friendly Edition

If you haven't encountered C++ templates before, don't be intimidated by the syntax. A template is essentially a "code generator"—you write a generic "blueprint," and the compiler automatically generates specific code based on the parameters you provide.

You can think of it like a chip design schematic: you draw a generic GPIO port diagram with two blank spaces labeled "Port ID" and "Pin Number." When you need Pin 13 of GPIOC, you fill in "C" and "13" in the blanks, and the compiler generates code specifically for GPIOC Pin 13. If you also need Pin 0 of GPIOA, you just fill in the blanks again. Each generated piece of code is independent and optimized, just as if you had written two different functions by hand.

For embedded development, the power of templates lies in this: you can "bake in" all information known at compile time into the code, so that at runtime, only operations that are "truly necessary" are executed. The GPIO port and pin are determined during hardware design—when you control the PC13 LED on a Blue Pill board, that information never changes from the start to the end of the project. Given that, why not let the compiler "burn" these constants into the code during compilation?

---

## Non-Type Template Parameters — NTTP

C++ templates have two kinds of parameters: type parameters and non-type parameters. Type parameters are what we see most often, declared with `typename` or `class`, representing a type. Non-type parameters (NTTP) are specific values—an integer, an enumeration value, or a pointer.

In embedded development, NTTPs are particularly useful because hardware configuration parameters (port ID, pin number, address) are all compile-time constants. Our GPIO template leverages exactly this:

```cpp
template <GpioPort PORT, uint16_t PIN>
class GPIO {
    // ...
};
```

Here we have two NTTPs: `PORT` is an enum value of type `Port` (like `PortC`), and `PIN` is an integer of type `uint8_t` (like `13`).

When you write `Gpio<PortC, 13>`, the compiler generates a brand new class where `PORT` is replaced by `PortC` and `PIN` is replaced by `13`. This class contains no member variables—`PORT` and `PIN` do not exist inside the object; they only exist in the type system.

This means:

```cpp
GPIO<GpioPort::C, GPIO_PIN_13> led1;
GPIO<GpioPort::A, GPIO_PIN_0> led2;
```

`Gpio<PortC, 13>` and `Gpio<PortA, 0>` are completely different types. They share no virtual function table, have no member variables, and are empty (C++ specifies that an empty class takes up at least 1 byte). The type system helps you distinguish between different pin configurations at compile time, requiring no extra storage at runtime.

---

## constexpr native_port() — Compile-Time Address Conversion

These are the three most technically dense lines of code in the entire GPIO template:

```cpp
static constexpr GPIO_TypeDef* native_port() noexcept {
    return reinterpret_cast<GPIO_TypeDef*>(
        static_cast<uintptr_t>(PORT)
    );
}
```

It does three things, each with a clear rationale.

First, `static_cast<std::underlying_type_t<Port>>(PORT)`: extracts the underlying address value from the `Port` enum. Since `Port` is an `enum class`, the underlying value is `uint32_t`. This operation happens at compile time—`PORT` is a template parameter, so the compiler knows its exact value.

Second, `reinterpret_cast<GPIO_TypeDef*>`: converts the integer address into a pointer to a GPIO register structure. This tells the compiler "there is a group of GPIO registers at this address." `reinterpret_cast` is the C++ way of saying "I know what I'm doing, please trust me"—it performs no checks, because in embedded development, we genuinely know the hardware register addresses.

Third, `constexpr`: the entire function can be evaluated at compile time. Calling `native_port()` is conceptually equivalent to writing the raw address, but it is type-safe and verified by the compiler. `noexcept` promises that this function will not throw exceptions—in a `noexcept` embedded environment, this is a natural guarantee.

---

## The setup() Method — Combining All Conversions

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIOClock::enable_target_clock();
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

Let's break this down line by line. `enable_clock()` first enables the clock—we'll cover its `constexpr` implementation in the next part. `GPIO_InitTypeDef init{};` uses aggregate initialization to zero all fields. In `init.Pin`, `PIN_MASK` is a template parameter known at compile time, so the compiler will directly embed the mask value into the instruction. The three `static_cast`s extract underlying values from our enums to pass to the HAL. Finally, `HAL_GPIO_Init` calls the HAL initialization—`native_port()` returns the correct pointer at compile time.

Note that `mode` and `pull` parameters have default values, meaning you can simply pass `mode`:

```cpp
gpio.setup(Mode::OutputPP);                                 // 默认NoPull, 默认High
gpio.setup(Mode::OutputPP, PullPush::PullUp);               // 指定PullPush, 默认High
gpio.setup(Mode::OutputPP, PullPush::NoPull, Speed::Low);   // 全部指定
```

Default function arguments are a C++ convenience feature—simplifying the most common calling pattern while maintaining API flexibility.

---

## set_gpio_pin_state() and toggle_pin_state()

```cpp
enum class State { Set = GPIO_PIN_SET, UnSet = GPIO_PIN_RESET };

void set_gpio_pin_state(State s) const {
    HAL_GPIO_WritePin(native_port(), PIN, static_cast<GPIO_PinState>(s));
}

void toggle_pin_state() const {
    HAL_GPIO_TogglePin(native_port(), PIN);
}
```

The `State` enum encapsulates pin states—`High` corresponds to high level, `Low` to low level. `static_cast<GPIO_PinState>` converts our `State` back to the HAL's `GPIO_PinState`. The `const` qualifier indicates these methods don't modify object state—though the object has no member variables anyway.

`PORT` and `PIN_MASK` are known at compile time, so under `-O2` optimization, the compiler will fully inline these two functions. The final generated machine code is identical to directly calling `HAL_GPIO_WritePin`.

---

## Proof of Zero-Overhead Abstraction

When you write:

```cpp
GPIO<GpioPort::C, GPIO_PIN_13> led;
led.set_gpio_pin_state(GPIO<GpioPort::C, GPIO_PIN_13>::State::UnSet);
```

The code generated by the compiler under `-O2` optimization is identical to directly writing:

```c
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
```

Template parameters have been replaced by specific values at compile time, `native_port()` returns the correct pointer at compile time, and `PIN_MASK` is substituted with the constant value. There is no runtime lookup, no virtual function call, and no extra storage overhead.

Speaking of zero overhead, there is a "hidden cost" of templates worth knowing about—code bloat. If you instantiate the GPIO class with 10 different combinations of template parameters, the compiler will generate independent code for each combination. In our scenario, this isn't an issue; we usually only have 2-3 different GPIO configurations. But if you use templates heavily in a large project, keep an eye on the final Flash usage. `size` is your good friend; run it after compiling to see the size of each section.

This is the meaning of "zero-overhead abstraction": you use C++'s advanced features to write safer, more maintainable code, yet the compiled machine code is exactly the same as hand-written C code. Bjarne Stroustrup, the creator of C++, said: "You don't pay for what you don't use." Our GPIO template perfectly embodies this principle—the "cost" of templates is paid at compile time, not in the STM32's 64KB Flash.

⚠️ **Note:** A common pitfall with templates is "code bloat"—if you instantiate the GPIO class with 10 different template parameter combinations, the compiler will generate 10 separate copies of the code. In our scenario, this isn't a problem (usually there are only 2-3 different GPIO configurations), but if you use templates heavily in a large project, check your final Flash usage. `size` is your good friend.

---

## Comparison with the C Macro Approach

In the C macro approach, ports and pins are defined via `#define`, scattered across header files. In the template approach, ports and pins are bound to types at compile time via template parameters. The key difference is: in the C++ solution, the port and pin are part of the type. You cannot "forget" to specify the port or pin—the compiler forces you to provide all template parameters when declaring a variable. In the C macro approach, if you forget a `#define` or a macro isn't defined, the compiler error messages will be very cryptic.

---

## Where We Are Now

The skeleton of the GPIO template is in place, but one critical feature remains unimplemented: clock enabling. The `setup()` method calls `enable_clock()`, but we haven't explained how it works yet. In the next part, we will unravel this mystery—how `enable_clock()` automatically selects the correct clock enable macro at compile time. This is the most elegant part of the entire template design.
