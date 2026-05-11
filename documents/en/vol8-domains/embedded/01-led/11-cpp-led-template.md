---
title: 'Part 16: Fourth Refactoring — LED Template, From General-Purpose GPIO to Dedicated
  Abstraction'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 15
order: 11
---
# Part 16: Fourth Refactoring — The LED Template, From General-Purpose GPIO to Domain-Specific Abstraction

## Preface: When Generic Isn't Good Enough

In the previous part, we accomplished something to be proud of — the GPIO template. `gpio::GPIO<PORT, PIN>` is now a truly general-purpose GPIO abstraction: you can use it on any port and any pin, set the mode, read and write levels, and toggle states. All operations are completed through a type-safe interface, with the compiler handling everything behind the scenes.

But generic doesn't mean easy to use.

Think about how much you have to write every time you use the GPIO template to light up an LED:

```cpp
gpio::GPIO<GpioPort::C, GPIO_PIN_13> led;
led.setup(gpio::GPIO<GpioPort::C, GPIO_PIN_13>::Mode::OutputPP,
          gpio::GPIO<GpioPort::C, GPIO_PIN_13>::PullPush::NoPull,
          gpio::GPIO<GpioPort::C, GPIO_PIN_13>::Speed::Low);
led.set_gpio_pin_state(gpio::GPIO<GpioPort::C, GPIO_PIN_13>::State::UnSet);  // 点亮
led.set_gpio_pin_state(gpio::GPIO<GpioPort::C, GPIO_PIN_13>::State::Set);    // 熄灭
```

This code has four problems. First, the `setup()` call requires manually passing in the mode, pull-up/pull-down, and speed — but an LED's mode is always push-pull, no pull-up/pull-down, and low speed. These three things are immutable facts for an LED and shouldn't be the caller's concern. Second, the semantics of `set_gpio_pin_state()` are "set GPIO level," not "turn on LED" or "turn off LED" — you have to know that PC13 is active-low, so turning it on requires passing `UnSet`, and turning it off requires passing `Set`. This cognitive burden shouldn't exist at all. Third, referencing an enum requires writing the lengthy `gpio::GPIO<GpioPort::C, GPIO_PIN_13>::Mode::OutputPP` every time, which is verbose and error-prone. Fourth, if you have a second LED on a different pin, you have to copy an almost identical set of code.

The root cause of these problems is that the GPIO template is "generic" — it doesn't know it's driving an LED. It doesn't know what mode an LED should be configured with, doesn't know whether the LED is active-high or active-low, and certainly doesn't know what "on" and "off" mean.

In this part, we will build a domain-specific template class for LEDs on top of the GPIO template. It encapsulates LED-specific hardware knowledge like "push-pull, active-low, low speed," exposing only three semantically clear interfaces: `on()`, `off()`, and `toggle()`. The user only needs to tell the template "which port and which pin the LED is on," and everything else — clock enabling, mode configuration, level logic — is fully automated.

This is also the fourth and final refactoring of our entire LED series. From the original C macro approach, to a bare C++ class, to the GPIO template, and now to today's LED template — each refactoring hands off more hardware knowledge to the compiler, letting users write less and safer code.

---

## The Complete Design of the LED Template

Let's look at the complete `led.hpp` first, which is only 30 lines in total:

```cpp
#pragma once
#include "gpio/gpio.hpp"

namespace device {

enum class ActiveLevel { Low, High };

template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
class LED : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;

  public:
    LED() {
        Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
    }

    void on() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
    }

    void off() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
    }

    void toggle() const { Base::toggle_pin_state(); }
};

} // namespace device
```

Thirty lines of code, but every line is worth careful scrutiny. Let's break it down section by section.

### Three Template Parameters: Port, Pin, and Active Level

```cpp
template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
```

The first two parameters, `PORT` and `PIN`, are passed directly to the base class `GPIO<PORT, PIN>`. We discussed this in detail in the previous part on the GPIO template — they determine the specific port address and pin number at compile time, allowing the compiler to generate code tailored to specific hardware.

The focus here is the third parameter: `ActiveLevel LEVEL`.

`ActiveLevel` is an enum class defined in `led.hpp`:

```cpp
enum class ActiveLevel { Low, High };
```

It has only two values: `Low` means active-low (the LED turns on at a low level), and `High` means active-high (the LED turns on at a high level). This concept corresponds to the actual hardware circuit — the PC13 LED on the Blue Pill board is connected to GND, so the LED conducts and lights up when the MCU outputs a low level, and turns off when it outputs a high level. If you soldered an LED connected to VCC yourself, it would be active-high and active-low for turning off.

The default value of `LEVEL` is `ActiveLevel::Low`, because the Blue Pill's on-board LED is active-low. Default template parameters are an elegant feature in C++: when the default value satisfies most use cases, the user doesn't need to explicitly provide this parameter. So for standard Blue Pill usage, you only need to write:

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

The third parameter automatically takes `ActiveLevel::Low`. If your LED is active-high, you just need to add one more parameter:

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> led;
```

This is the design philosophy of default template parameters: keep simple things simple, and make complex things possible.

### Inheritance and Type Aliases: Standing on the Shoulders of GPIO

```cpp
class LED : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;
```

LED inherits from the GPIO template. When LED is instantiated as `LED<GpioPort::C, GPIO_PIN_13>`, the base class becomes `GPIO<GpioPort::C, GPIO_PIN_13>` — a complete GPIO template instance specific to pin 13 of GPIOC. This means LED automatically possesses all the capabilities of the base class: `setup()`, `set_gpio_pin_state()`, `toggle_pin_state()`, `native_port()`, and the internal `GPIOClock` clock enabling logic.

There is a subtle template instantiation mechanism worth noting here. The `PORT` and `PIN` in `gpio::GPIO<PORT, PIN>` are not concrete values, but the LED template's own template parameters. When the compiler sees `LED<GpioPort::C, GPIO_PIN_13>`, it replaces `PORT` with `GpioPort::C` and `PIN` with `GPIO_PIN_13`, and then instantiates the base class `GPIO<GpioPort::C, GPIO_PIN_13>`. This is a two-stage instantiation process: the LED's template parameters are determined first, and then the base class template is instantiated accordingly.

`using Base = gpio::GPIO<PORT, PIN>` is a type alias. It doesn't define a new type; it simply gives a shorter name to an existing type. After this, all occurrences of `Base::` in the code are equivalent to `gpio::GPIO<PORT, PIN>::`. In template programming, the full name of a base class is often very long, making type aliases almost a necessity — otherwise, `Base::Mode::OutputPP` would have to be written as `gpio::GPIO<PORT, PIN>::Mode::OutputPP`, which is both verbose and error-prone during maintenance.

This is a widely used convention in C++ template code. You will see similar patterns in any serious template library: `using Base = ...` or `typedef ... Base`, all aimed at simplifying references to base class members.

### The Constructor: The Secret Behind Zero Configuration

```cpp
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}
```

These three lines are the core of the entire "zero configuration" design.

The LED's constructor directly calls the base class's `setup()` method, passing in three fixed parameters:

- **`Mode::OutputPP`**: Push-pull output mode. Push-pull is the standard configuration for driving LEDs — it can actively output high and low levels with strong driving capability, suitable for driving LEDs directly. In contrast, open-drain mode can only pull the level low and requires an external pull-up resistor to output a high level, so it is generally not used for LED driving.
- **`PullPush::NoPull`**: No pull-up or pull-down. The internal pull-up and pull-down resistors of the GPIO are meaningless for push-pull output mode — push-pull can drive the level by itself without external assistance. Additionally, the PC13 pin on the STM32F103 does not support internal pull-up/pull-down anyway, so specifying `NoPull` here also reflects the hardware reality.
- **`Speed::Low`**: Low speed mode. The GPIO output speed determines the rise and fall times of the pin's level changes. The faster the speed, the steeper the signal edges and the better the high-frequency performance, but it also generates more electromagnetic interference (EMI) and power consumption. The LED blinking frequency is only a few Hertz, so there is absolutely no speed requirement. Choosing low speed is the most reasonable option — it reduces power consumption and minimizes unnecessary signal noise.

These three things are almost invariant for any LED — push-pull, no pull-up/pull-down, low speed. Hardcoding them in the LED's constructor means that anyone using the LED template never needs to worry about these three parameters. The moment an LED object is created, the constructor automatically completes the configuration. This is what "zero configuration" means.

What's even more elegant is that `setup()` internally calls `GPIOClock::enable_target_clock()`, which uses `if constexpr` to determine at compile time which port's clock should be enabled. So the entire initialization chain is: LED construction -> `setup(OutputPP, NoPull, Low)` -> `GPIOClock::enable_target_clock()` -> `__HAL_RCC_GPIOC_CLK_ENABLE()` -> `HAL_GPIO_Init()`. From clock enabling to pin configuration, it's done in one smooth flow.

The user only needs to declare a variable:

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

This single line completes all initialization. No need to call a separate initialization function, no need to manually configure any parameters.

### on() and off(): Compile-Time Level Branching

```cpp
void on() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
}

void off() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
}
```

This is the most exquisite part of the entire LED template, and the segment that best demonstrates the power of template parameters.

Let's break it down step by step.

`LEVEL` is a template parameter whose specific value is already determined at compile time — either `ActiveLevel::Low` or `ActiveLevel::High`. Therefore, `LEVEL == ActiveLevel::Low` is a compile-time constant expression, and for any given template instantiation, its result has only two possibilities: `true` or `false`.

During optimization (even at the `-O0` level), the compiler can directly select the corresponding branch based on the result of this constant expression, generating machine code without any conditional logic. There is no runtime if-else overhead.

For the Blue Pill's PC13 LED (`LEVEL = ActiveLevel::Low`):

The branch condition of `on()` evaluates to `true`, so `on()` ultimately reduces to:

```cpp
void on() const {
    Base::set_gpio_pin_state(Base::State::UnSet);
    // 展开 -> HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)
    // 物理效果：输出低电平 -> LED导通 -> 点亮
}
```

The branch condition of `off()` also evaluates to `true` (because LEVEL is still Low), so `off()` ultimately reduces to:

```cpp
void off() const {
    Base::set_gpio_pin_state(Base::State::Set);
    // 展开 -> HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET)
    // 物理效果：输出高电平 -> LED截止 -> 熄灭
}
```

For an active-high LED (`LEVEL = ActiveLevel::High`), the situation is exactly reversed:

The branch condition of `on()` evaluates to `false`, selecting `Base::State::Set`:

```cpp
void on() const {
    Base::set_gpio_pin_state(Base::State::Set);
    // 展开 -> HAL_GPIO_WritePin(GPIOx, GPIO_PIN_x, GPIO_PIN_SET)
    // 物理效果：输出高电平 -> LED导通 -> 点亮
}
```

The branch condition of `off()` also evaluates to `false`, selecting `Base::State::UnSet`:

```cpp
void off() const {
    Base::set_gpio_pin_state(Base::State::UnSet);
    // 展开 -> HAL_GPIO_WritePin(GPIOx, GPIO_PIN_x, GPIO_PIN_RESET)
    // 物理效果：输出低电平 -> LED截止 -> 熄灭
}
```

This is the power of template parameters — one piece of source code, two hardware configurations, and the compiler automatically generates the correct level operations with zero runtime overhead. `on()` means "turn on," and `off()` means "turn off," regardless of how your LED circuit is wired. Semantic correctness is guaranteed by the template, and the user doesn't need to care about the underlying level logic.

Another detail worth noting: both methods are declared as `const`. This is because they only call the base class's `set_gpio_pin_state()`, and `set_gpio_pin_state()` itself is also `const` — it simply calls `HAL_GPIO_WritePin()` to write to a register and doesn't modify any member variables. In C++, methods that don't modify the logical state of an object should be declared as `const`. This is good programming practice and also allows these methods to be called on `const LED&` references.

### toggle(): Delegating to the Base Class Toggle

```cpp
void toggle() const { Base::toggle_pin_state(); }
```

The implementation of `toggle()` is the simplest — it directly delegates to the base class's `toggle_pin_state()`.

Why doesn't it need to care about `ActiveLevel`? Because the toggle operation is unconditional: regardless of whether the pin is currently outputting a high or low level, `toggle()` will flip it to the opposite state. If the LED is currently on (low level), after toggling it becomes off (high level), and vice versa. The toggle itself doesn't care "which level represents on"; it only cares about "becoming the opposite of the current state."

So the behavior of `toggle()` is consistent for both active-low and active-high LEDs — it toggles the current state. The underlying `HAL_GPIO_TogglePin()` call reads the corresponding bit in the Output Data Register (ODR), inverts it, and writes it back.

---

## Usage in main.cpp: Simplifying Everything

Now let's look at the complete `main.cpp`:

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
    /* led setups! */
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;

    while (1) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

Let's go through it line by line.

**Line 1: `#include "device/led.hpp"`**

Includes the LED template. `led.hpp` already internally includes `#include "gpio/gpio.hpp"`, so there's no need to include the GPIO header separately. The LED template is the only entry point the user needs to care about; it encapsulates all dependencies on the GPIO template. This is good module design — each layer exposes only the necessary interfaces, and internal implementation details don't leak to the upper layer.

**Line 2: `#include "system/clock.h"`**

Includes the clock configuration. `clock.h` defines the `ClockConfig` class, which is responsible for configuring the STM32's system clock to the target frequency (64MHz).

**Lines 3 to 5: `extern "C" { #include "stm32f1xx_hal.h" }`**

HAL headers must be wrapped in `extern "C"`. This is because `stm32f1xx_hal.h` is a pure C header file, and the function declarations inside use C language name mangling rules. The C++ compiler uses C++ name mangling rules by default, and the two are incompatible. Without `extern "C"`, the linker won't find the definitions of the HAL functions and will report "undefined reference" errors.

`extern "C"` tells the C++ compiler: all declarations within the braces use C linkage specification, so don't apply C++-style name mangling to the function names. This is the standard approach for calling C libraries in C++ projects and is extremely common in embedded development.

**Line 7: `HAL_Init()`**

Initializes the HAL library. This function does a few important things: configures the Flash prefetch buffer, configures the SysTick timer for a 1ms interrupt period, and initializes HAL's internal state machine. All subsequent HAL functions (including `HAL_Delay()`, `HAL_GPIO_Init()`, etc.) depend on this initialization.

**Line 8: `clock::ClockConfig::instance().setup_system_clock()`**

Obtains the clock configuration instance through the singleton pattern, then configures the system clock. This line involves the combined use of two design patterns — a CRTP singleton and hardware initialization encapsulation. We will discuss this design in detail in the next section.

**Line 10: `device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led`**

This single line does everything. Let me list the complete chain of operations it triggers:

1. The compiler instantiates `LED<GpioPort::C, GPIO_PIN_13>`, with `LEVEL` taking the default value `ActiveLevel::Low`
2. Instantiates the base class `GPIO<GpioPort::C, GPIO_PIN_13>`
3. Calls the LED constructor
4. The constructor calls `Base::setup(OutputPP, NoPull, Low)`
5. `setup()` internally calls `GPIOClock::enable_target_clock()`
6. In `GPIOClock::enable_target_clock()`, `if constexpr (PORT == GpioPort::C)` matches successfully, calling `__HAL_RCC_GPIOC_CLK_ENABLE()`
7. `setup()` constructs the `GPIO_InitTypeDef` struct, filling in Pin=GPIO_PIN_13, Mode=OutputPP, Pull=NoPull, Speed=Low
8. Calls `HAL_GPIO_Init(GPIOC, &init_types)` to complete the pin configuration

From over 30 lines of code in the C macro version, down to this single declaration. This is the power of abstraction.

**Lines 12 to 17: The Main Loop**

```cpp
while (1) {
    HAL_Delay(500);
    led.on();
    HAL_Delay(500);
    led.off();
}
```

The main loop logic couldn't be clearer: wait 500 milliseconds, turn on the LED, wait 500 milliseconds, turn off the LED, and repeat. `HAL_Delay()` implements millisecond-level delays based on the SysTick interrupt, with accuracy depending on the system clock configuration. The semantics of `led.on()` and `led.off()` are self-evident, requiring no comments to explain what they do.

What if you want to add another LED on a different pin? You only need one declaration:

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0> led2;
```

Then call `led2.on()` and `led2.off()` in the loop. No need to copy any header or source files, no need to modify any macro definitions, no need to manually configure GPIO. Each LED is just an object — create it and use it, each minding its own business.

---

## The CRTP Singleton: Clock Configuration Design

In `main.cpp`, there is a line of code that uses a pattern we haven't discussed in detail yet:

```cpp
clock::ClockConfig::instance().setup_system_clock();
```

Behind this line of code lies a singleton pattern based on CRTP. Let's first look at the two source files.

The first is `base/simple_singleton.hpp`, a generic CRTP singleton base class:

```cpp
#pragma once

namespace base {
template <typename SingletonClass> class SimpleSingleton {
  public:
    SimpleSingleton() = default;
    ~SimpleSingleton() = default;

    static SingletonClass& instance() {
        static SingletonClass _instance;
        return _instance;
    }

  private:
    /* Never Shell A Single Instance Copyable And Movable */
    SimpleSingleton(const SimpleSingleton&) = delete;
    SimpleSingleton(SimpleSingleton&&) = delete;
    SimpleSingleton& operator=(const SimpleSingleton&) = delete;
    SimpleSingleton& operator=(SimpleSingleton&&) = delete;
};
} // namespace base
```

The second is `system/clock.h`, where `ClockConfig` inherits from this base class to gain singleton capabilities:

```cpp
#pragma once
#include "base/simple_singleton.hpp"
#include <cstdint>

namespace clock {
class ClockConfig : public base::SimpleSingleton<ClockConfig> {
  public:
    /* Setup the System clocks */
    void setup_system_clock();

    [[nodiscard("You should accept the clock frequency, it's what you request!")]]
    uint64_t clock_freq() const noexcept;
};
} // namespace clock
```

CRTP stands for Curiously Recurring Template Pattern. The name sounds strange, but the principle isn't complicated: the derived class `ClockConfig` passes itself as a template argument to the base class `SimpleSingleton<ClockConfig>`. This way, the `instance()` method in the base class returns `ClockConfig&`, rather than some generic base class reference.

The advantage of this approach is that it doesn't require virtual functions. Traditional singleton patterns often use virtual functions to provide a polymorphic `instance()` method, but virtual functions require a virtual function table (vtable), which is unnecessary overhead in an embedded environment. CRTP determines the specific derived class type at compile time through templates, completely eliminating runtime polymorphic overhead.

The implementation of the `instance()` method leverages a guarantee from C++11: a `static` local variable inside a function is initialized the first time execution reaches that declaration, and the initialization is thread-safe. Therefore, `static SingletonClass _instance` will only be constructed once. Even if multiple threads call `instance()` simultaneously, the compiler guarantees that only one thread executes the constructor while the others wait. In bare-metal embedded environments this isn't very important (there's usually only one thread), but in more complex systems this is a valuable guarantee.

The `private` part of the base class deletes the copy constructor, move constructor, copy assignment operator, and move assignment operator. These four `= delete` declarations ensure the singleton cannot be accidentally copied or moved — if you write `auto copy = ClockConfig::instance()`, the compiler will directly report an error. The word "Shell" in the comment "Never Shell A Single Instance Copyable And Movable" should be a typo for "Share," but the intent is clear: a singleton should never be copied.

Why does the clock configuration need to be a singleton? The STM32F103 has only one clock tree, and the system clock has only one configuration. If creating multiple `ClockConfig` instances were allowed, you could end up with code like this:

```cpp
clock::ClockConfig config1;
config1.setup_system_clock();  // 配置为64MHz

clock::ClockConfig config2;
config2.setup_system_clock();  // 又配置一次——可能中断正在使用时钟的外设
```

Although calling `setup_system_clock()` repeatedly doesn't necessarily cause an immediate hardware fault (HAL functions typically reconfigure the registers), it is a design flaw — allowing multiple instances implies that "each instance can have a different configuration," whereas the clock configuration should be physically globally unique. The singleton pattern prevents this kind of misuse at the type system level.

The `clock_freq()` method is annotated with the `[[nodiscard("You should accept the clock frequency, it's what you request!")]]` attribute. This is a feature introduced in C++17 that tells the compiler: this return value should not be ignored. If you write `config.clock_freq()` without capturing the return value, the compiler will issue a warning. In embedded development, querying the clock frequency is usually for subsequent calculations (such as baud rate or timer period), so ignoring the return value is almost certainly a bug.

The CRTP singleton isn't the focus of this part — it will be explored in detail in later chapters. But you need to understand its role in `main.cpp`: it provides a globally unique, thread-safe, non-copyable entry point for clock configuration. `ClockConfig::instance()` returns a reference to the sole instance, and `.setup_system_clock()` calls the configuration method on that instance. The entire expression chains together, completing clock initialization in a single line of code.

---

## A Pitfall Regarding Construction Timing

Before we move on to the comparison, there is a pitfall directly related to how the LED template is used that is worth discussing specifically.

> ⚠️ **Warning:** The LED template's constructor configures the GPIO immediately when the object is created. This means that if you declare an LED object in the global scope, its construction will occur before `main()` (during the C++ static initialization phase), at which point HAL may not yet be initialized. Therefore, LED objects must be declared after `HAL_Init()` and clock configuration — that is, inside the `main()` function. This order must not be disrupted; otherwise, although the GPIO configuration won't report an error, register writes will be silently ignored by the hardware when the clock is not enabled.

So LED objects must be declared after `HAL_Init()` and clock configuration — that is, inside the `main()` function. This is exactly what we do in our `main.cpp`: first `HAL_Init()`, then `clock::ClockConfig::instance().setup_system_clock()`, and only then do we declare `device::LED<...> led`. This order must not be disrupted.

---

## Final Comparison with the C Macro Approach

From the first part to this one, we've gone through four refactorings. Now it's time for a thorough comparison.

### The Complete Code of the C Macro Approach

A typical C macro LED driver is divided into a header file and a source file.

**led.h:**

```c
#ifndef LED_H
#define LED_H

#include "stm32f1xx_hal.h"

#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13
#define LED_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define LED_ON_LEVEL    GPIO_PIN_RESET   /* 低电平点亮 */
#define LED_OFF_LEVEL   GPIO_PIN_SET     /* 高电平熄灭 */

void led_init(void);
void led_on(void);
void led_off(void);
void led_toggle(void);

#endif
```

**led.c:**

```c
#include "led.h"

void led_init(void) {
    LED_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
}

void led_on(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_ON_LEVEL);
}

void led_off(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_OFF_LEVEL);
}

void led_toggle(void) {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}
```

**main.c:**

```c
#include "led.h"

int main(void) {
    HAL_Init();
    SystemClock_Config();
    led_init();
    while (1) {
        led_on();
        HAL_Delay(500);
        led_off();
        HAL_Delay(500);
    }
}
```

About 40 lines of driver code plus 15 lines for the main function. It looks fairly tidy. But the problem is — each LED requires its own separate pair of header and source files.

### The Complete Code of the C++ Template Approach

**device/led.hpp (LED template, ~30 lines):**

```cpp
#pragma once
#include "gpio/gpio.hpp"

namespace device {

enum class ActiveLevel { Low, High };

template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
class LED : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;
  public:
    LED() {
        Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
    }
    void on() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
    }
    void off() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
    }
    void toggle() const { Base::toggle_pin_state(); }
};

} // namespace device
```

**main.cpp:**

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    while (1) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

### Item-by-Item Comparison

The `main` functions in both approaches are similarly concise, both just a dozen or so lines. The difference doesn't seem significant. But the real difference lies in extensibility — when you need to add a second LED to your project.

**Adding a second LED with the C approach (e.g., PA0):**

You need to copy `led.h` to `led2.h`, copy `led.c` to `led2.c`, and then modify all the macro definitions — change `LED_PORT` to `GPIOA`, change `LED_PIN` to `GPIO_PIN_0`, and change the clock enable to `__HAL_RCC_GPIOA_CLK_ENABLE()`. If the LED is active-high, you also need to swap `LED_ON_LEVEL` and `LED_OFF_LEVEL`. Two files, at least six modifications.

Even worse, what if you have 10 LEDs? Ten pairs of header and source files, each manually maintained. If the HAL library's API changes, you have to modify 10 places.

**Adding a second LED with the C++ approach (e.g., PA0, active-high):**

You only need to add one line in `main.cpp`:

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> led2;
```

One line of code. Clock enabling, mode configuration, and level logic are all handled automatically by the template. No need to create new files, no need to copy code, no need to modify any existing code.

This is the true value of template metaprogramming in embedded systems — it's not about making `main()` look shorter (the length of `main()` is about the same in both approaches), but about driving the marginal cost of extension toward zero. For each additional LED, the C approach has a linear cost (new files, new code, new maintenance), while the C++ approach has a constant cost (one line of declaration).

### Comparison of Build Artifacts

A frequently asked question is: will the C++ template approach produce a larger code size?

The answer is no. Because all parameters of the LED template are constants at compile time, the compiler can perform complete inlining optimization. The machine code ultimately generated by `led.on()` is exactly the same as directly calling `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)`. There is no virtual function table, no runtime polymorphism, and no additional function call overhead. This is what we call "zero-overhead abstraction" — what you pay is compile time (template instantiation requires the compiler to do more work), and what you get back is zero runtime performance loss.

If you use `arm-none-eabi-objdump -d` to disassemble the final firmware, you'll find that the machine code generated by the C++ template approach and the C macro approach is almost identical at the instruction level. The cost of abstraction is completely shifted to compile time.

---

## Wrapping Up

The LED template is complete. From the original C macro approach, to bare C++ class encapsulation, to the general-purpose GPIO template, and now to the domain-specific LED template — four refactorings, each step transforming more hardware knowledge from "things the developer needs to remember" into "things the compiler handles automatically."

Looking back at the evolution of these four steps: in the first step, the C macro approach centralized hardware parameters in the header file's macro definitions — centralized but still text substitution, without type safety. In the second step, C++ class encapsulation turned macro definitions into member functions, adding scope and type checking, but it could only handle specific ports and pins. In the third step, the GPIO template parameterized the port and pin, achieving a general-purpose GPIO abstraction, but users still needed to know how to configure an LED. In the fourth step, the LED template built a domain-specific abstraction on top of the GPIO template, encapsulating all LED-specific hardware knowledge — push-pull, active-low, low speed — in 30 lines of code.

The final result is that the user only needs to write one line of declaration to get a fully configured LED object. The semantics of `on()`, `off()`, and `toggle()` are clear and unambiguous, with no need to care about the underlying level logic. Template parameters determine everything at compile time, with absolutely no additional runtime overhead. The cost of adding a new LED is one line of code, not a pair of files.

In the next part, we will wrap up the C++23 and modern C++ features involved in this LED series, systematically reviewing the specific applications of `constexpr`, `if constexpr`, `enum class`, `[[nodiscard]]`, `extern "C"`, and other features in embedded scenarios. We will also use actual comparisons of build artifacts to prove that these abstractions are indeed zero-overhead. We don't just want to write elegant code — we want to prove that it is just as efficient as hand-written register operations.
