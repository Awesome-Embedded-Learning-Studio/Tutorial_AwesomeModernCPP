# Part 14: The Second Refactoring — Templates Take the Stage, Binding Ports and Pins at Compile Time

> Continuing from the previous article: `enum class` solved the type safety issue, but the port and pin were still runtime parameters. This article introduces the core weapon of C++ templates—non-type template parameters (NTTPs)—to turn ports and pins into compile-time constants.

---

## What Are Templates — An Embedded Developer-Friendly Version

If you haven't encountered C++ templates before, don't let the syntax intimidate you. At their core, templates are "code generators"—you write a generic "blueprint," and the compiler automatically generates specific code based on the parameters you provide.

You can think of it like a chip's design schematic: you draw a generic GPIO port schematic with two blank spaces for "port number" and "pin number." When you need GPIOC Pin13, you fill in the blanks with "C" and "13," and the compiler generates code specifically for GPIOC Pin13. If you also need GPIOA Pin0, you just fill in the blanks again. Each generated piece of code is independent and optimized, just as if you had handwritten two separate pieces of code.

For embedded development, the power of templates lies here: you can hardcode all "known" information at compile time, leaving only "truly necessary" operations for runtime. A GPIO's port and pin are determined at design time—if you are controlling the PC13 LED on a Blue Pill board, this information will not change from the start to the end of the project. Given that, why not let the compiler "burn" these constants into the code during compilation?

---

## Non-Type Template Parameters — NTTPs

C++ templates have two kinds of parameters: type parameters and non-type parameters. Type parameters are what we see most often, declared with `typename` or `class`, representing a type. Non-type parameters (NTTPs) are concrete values—an integer, an enum value, or a pointer.

In embedded development, NTTPs are particularly useful because hardware configuration parameters (port numbers, pin numbers, addresses) are all compile-time constants. Our GPIO template takes advantage of exactly this:

```cpp
template <GpioPort PORT, uint16_t PIN>
class GPIO {
    // ...
};
```

Here we have two NTTPs: `PORT` is an enum value of type `GpioPort` (such as `GpioPort::C`), and `PIN` is an integer of type `uint16_t` (such as `GPIO_PIN_13 = 0x2000`).

When you write `GPIO<GpioPort::C, GPIO_PIN_13>`, the compiler generates a brand-new class where `PORT` is replaced with `GpioPort::C` and `PIN` is replaced with `GPIO_PIN_13`. This class does not contain any member variables—`PORT` and `PIN` do not exist in the object; they only exist in the type system.

This means:

```cpp
GPIO<GpioPort::C, GPIO_PIN_13> led1;
GPIO<GpioPort::A, GPIO_PIN_0> led2;
```

`led1` and `led2` are completely different types. They share no virtual function table, have no member variables, and `sizeof(led1) = sizeof(led2) = 1` (C++ mandates that empty classes occupy at least one byte). The type system helps you distinguish between different pin configurations at compile time, requiring no extra storage at runtime.

---

## constexpr native_port() — Compile-Time Address Conversion

These are the three most technically demanding lines of code in the entire GPIO template:

```cpp
static constexpr GPIO_TypeDef* native_port() noexcept {
    return reinterpret_cast<GPIO_TypeDef*>(
        static_cast<uintptr_t>(PORT)
    );
}
```

It does three things, and each step has a clear rationale.

Step one, `static_cast<uintptr_t>(PORT)`: extracts the underlying address value from the `GpioPort` enum. Because `PORT` is `GpioPort::C`, the underlying value is `GPIOC_BASE = 0x40011000`. This operation completes at compile time—`PORT` is a template parameter, so the compiler knows its exact value.

Step two, `reinterpret_cast<GPIO_TypeDef*>(...)`: converts the integer address into a GPIO register struct pointer. This tells the compiler, "There is a set of GPIO registers at address `0x40011000`." `reinterpret_cast` is the C++ cast that means "I know what I am doing, please trust me"—it performs no checks because, in embedded development, we genuinely know the hardware register addresses.

Step three, `constexpr`: the entire function can be evaluated at compile time. Calling `native_port()` is conceptually equivalent to writing `GPIOC`, but it is type-safe and verified by the compiler. `noexcept` promises that this function will not throw exceptions—in a `-fno-exceptions` embedded environment, this is a natural guarantee.

---

## The setup() Method — Combining All the Conversions

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

Let's break it down line by line. `GPIOClock::enable_target_clock()` first enables the clock—we will cover its `if constexpr` implementation in detail in the next article. `GPIO_InitTypeDef init_types{}` uses aggregate initialization to zero out all fields. In `init_types.Pin = PIN`, `PIN` is a template parameter known at compile time, so the compiler will directly embed `GPIO_PIN_13` into the instruction. The three `static_cast<uint32_t>()` calls extract the underlying values from `enum class` and pass them to the HAL. Finally, `HAL_GPIO_Init(native_port(), &init_types)` calls the HAL initialization—`native_port()` returns `GPIOC` at compile time.

Note that the `PullPush` and `Speed` parameters have default values, which means you can pass only `Mode`:

```cpp
gpio.setup(Mode::OutputPP);                                 // 默认NoPull, 默认High
gpio.setup(Mode::OutputPP, PullPush::PullUp);               // 指定PullPush, 默认High
gpio.setup(Mode::OutputPP, PullPush::NoPull, Speed::Low);   // 全部指定
```

Default function arguments are a convenient C++ feature—they simplify the most common calling pattern while maintaining API flexibility.

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

The `State` enum encapsulates the pin state—`Set` corresponds to a high level, and `UnSet` corresponds to a low level. `static_cast<GPIO_PinState>(s)` converts our `State` back to the HAL's `GPIO_PinState`. The `const` qualifier indicates that these methods do not modify object state—though the object has no member variables to begin with.

`native_port()` and `PIN` are known at compile time, so the compiler will fully inline these two functions under `-O2` optimization. The resulting machine code is identical to directly calling `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)`.

---

## Proof of Zero-Overhead Abstraction

When you write:

```cpp
GPIO<GpioPort::C, GPIO_PIN_13> led;
led.set_gpio_pin_state(GPIO<GpioPort::C, GPIO_PIN_13>::State::UnSet);
```

The code generated by the compiler under `-O2` optimization is exactly the same as directly writing:

```c
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
```

The template parameters have already been replaced with concrete values at compile time, `native_port()` returns `GPIOC` at compile time, and `PIN` is replaced with `GPIO_PIN_13` at compile time. There is no runtime lookup, no virtual function call, and no extra storage overhead.

Speaking of zero overhead, there is a "hidden cost" of templates worth understanding early—code bloat. If you instantiate the GPIO class with ten different combinations of template parameters, the compiler will generate a separate piece of code for each combination. In our scenario, this is not a problem; we typically only have two or three different GPIO configurations. But if you use templates extensively in a large project, keep an eye on the final Flash usage. `arm-none-eabi-size` is your best friend—run it after compiling to see the size of each section.

This is the meaning of "zero-overhead abstraction": you use C++'s high-level features to write safer, more maintainable code, but the compiled machine code is identical to hand-written C code. C++ creator Bjarne Stroustrup said, "What you don't use, you don't pay for." Our GPIO template perfectly embodies this principle—the "cost" of templates is reflected only in compile time, not in the STM32's 64KB Flash.

> ⚠️ **Warning:** A common pitfall with templates is "code bloat"—if you instantiate the GPIO class with ten different combinations of template parameters, the compiler will generate ten separate pieces of code. In our scenario, this is not a problem (we typically only have two or three different GPIO configurations), but if you use templates extensively in a large project, keep an eye on the final Flash usage. `arm-none-eabi-size` is your best friend.

---

## Comparison with the C Macro Approach

In the C macro approach, ports and pins are defined via `#define` and scattered across header files. In the template approach, ports and pins are bound to the type at compile time through template parameters. The key difference is this: in the C++ approach, the port and pin are part of the type. You cannot "forget" to specify the port or pin—the compiler forces you to provide all template parameters when declaring a variable. In the C macro approach, if you forget `#include "led.h"` or if the `LED_PORT` macro is not defined, the compiler error messages will be extremely cryptic.

---

## Where We Are Now

The skeleton of the GPIO template is in place, but one critical feature remains unimplemented: clock enabling. The `setup()` method calls `GPIOClock::enable_target_clock()`, but we haven't explained how it works yet. In the next article, we will unravel this mystery—how `if constexpr` automatically selects the correct clock enable macro at compile time. This is the most elegant part of the entire template design.
