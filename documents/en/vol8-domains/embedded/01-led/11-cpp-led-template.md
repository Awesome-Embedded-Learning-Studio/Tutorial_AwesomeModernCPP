---
chapter: 15
difficulty: beginner
order: 11
platform: stm32f1
reading_time_minutes: 25
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 16: Fourth Refactor — LED Template, from Generic GPIO to Dedicated Abstraction'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/11-cpp-led-template.md
  source_hash: af31e89ade6afe5c02b88a142ca99d4fe5eb6deffdfcf4cc968928b38b37c087
  translated_at: '2026-06-16T04:10:35.109292+00:00'
  engine: anthropic
  token_count: 4394
---
# Part 16: Fourth Refactor — LED Templates, From General GPIO to Specific Abstraction

## Preface: When Generic Isn't Good Enough

In the previous post, we accomplished something to be proud of — a GPIO template. `GpioTemplate` is now a truly general-purpose GPIO abstraction: you can use it on any port and any pin, set modes, read and write levels, and toggle states. All operations are performed through type-safe interfaces, with the compiler handling everything behind the scenes.

But general-purpose doesn't necessarily mean easy to use.

Think about how much you need to write every time you use the GPIO template to light up an LED:

```cpp
GpioTemplate<GPIOC, 13, GPIO_MODE_OUTPUT_PP> led;
led.init(GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
led.write(GPIO_PIN_RESET); // Light on
```

This code has four problems. First, the `init` call requires manually passing the mode, pull-up/pull-down, and speed — but for an LED, the mode is always push-pull output, no pull-up/pull-down, and low speed. These three are unchanging facts for an LED and shouldn't be the caller's concern. Second, the semantics of `write` are "set GPIO level," not "light LED" or "extinguish LED" — you must know that PC13 is active-low, so to light it you pass `RESET`, and to extinguish it you pass `SET`. This cognitive burden shouldn't exist. Third, referencing the enumeration requires writing the long string `GPIO_` every time, which is verbose and error-prone. Fourth, if you have a second LED connected to a different pin, you have to copy a set of almost identical code.

The root of these problems is that the GPIO template is "general-purpose." It doesn't know it's driving an LED. It doesn't know what mode an LED should be configured with, doesn't know if the LED is active-high or active-low, and doesn't know what "light" and "extinguish" mean.

In this post, we will build a dedicated LED template class on top of the GPIO template. It encapsulates hardware-specific knowledge like "push-pull output, active-low, low speed," exposing only three semantically clear interfaces: `on()`, `off()`, and `toggle()`. The user only needs to tell the template "which port and pin the LED is on," and everything else — clock enabling, mode configuration, level logic — is handled automatically.

This is the fourth and final refactor of our LED series. From the initial C macro approach, to bare C++ classes, to GPIO templates, and to today's LED template, every refactor shifts more hardware knowledge to the compiler, allowing users to write less, safer code.

---

## Complete Design of the LED Template

Let's look at the complete `device/led.hpp` first, only 30 lines in total:

```cpp
#ifndef DEVICE_LED_HPP_
#define DEVICE_LED_HPP_

#include "device/gpio.hpp"

template <Port PortId, uint16_t PinId, ActiveLevel Level = ActiveLevel::Low>
class LedTemplate : public GpioTemplate<PortId, PinId> {
 public:
  using Base = GpioTemplate<PortId, PinId>;

  LedTemplate() {
    Base::init(GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
  }

  void on() const {
    if constexpr (Level == ActiveLevel::Low) {
      Base::write(GPIO_PIN_RESET);
    } else {
      Base::write(GPIO_PIN_SET);
    }
  }

  void off() const {
    if constexpr (Level == ActiveLevel::Low) {
      Base::write(GPIO_PIN_SET);
    } else {
      Base::write(GPIO_PIN_RESET);
    }
  }

  void toggle() const {
    Base::toggle();
  }
};

#endif // DEVICE_LED_HPP_
```

Thirty lines of code, but every line is worth careful consideration. Let's break it down section by section.

### Three Template Parameters: Port, Pin, Active Level

```cpp
template <Port PortId, uint16_t PinId, ActiveLevel Level = ActiveLevel::Low>
class LedTemplate : public GpioTemplate<PortId, PinId> {
```

The first two parameters, `PortId` and `PinId`, are passed directly to the base class `GpioTemplate`. We discussed this in detail in the previous GPIO template post — they determine the specific port address and pin number at compile time, allowing the compiler to generate code for specific hardware.

The focus is on the third parameter: `Level`.

`ActiveLevel` is an `enum class` defined in `device/gpio.hpp`:

```cpp
enum class ActiveLevel { Low, High };
```

It has only two values: `Low` means active-low (the LED lights up when the level is low), and `High` means active-high (the LED lights up when the level is high). This concept corresponds to the actual hardware circuit — the PC13 LED on the Blue Pill board is connected to GND, so the LED conducts and lights up when the MCU outputs low, and cuts off and turns off when the MCU outputs high. If you soldered an LED to VCC yourself, it would be active-high and active-low.

The default value for `Level` is `Low`, because the on-board LED of the Blue Pill is active-low. Default template parameters are an elegant feature in C++: when the default value satisfies most use cases, the user doesn't need to provide this parameter explicitly. So for the standard Blue Pill usage, you only need to write:

```cpp
LedTemplate<Port::C, 13> led;
```

The third parameter automatically takes `ActiveLevel::Low`. If your LED is active-high, you just need to add one parameter:

```cpp
LedTemplate<Port::C, 13, ActiveLevel::High> led;
```

This is the design philosophy of default template parameters: keep simple things simple, make complex things possible.

### Inheritance and Type Aliases: Standing on GPIO's Shoulders

```cpp
using Base = GpioTemplate<PortId, PinId>;
```

LED inherits from the GPIO template. When `LedTemplate` is instantiated as `LedTemplate<Port::C, 13>`, the base class becomes `GpioTemplate<Port::C, 13>` — a complete GPIO template instance for pin 13 of GPIOC. This means LED automatically possesses all capabilities of the base class: `init`, `read`, `write`, `toggle`, and internal `enablePortClock` logic.

There is a subtle template instantiation mechanism worth noting here. `PortId` and `PinId` in `GpioTemplate<PortId, PinId>` are not concrete values but the LED template's own template parameters. When the compiler sees `GpioTemplate<PortId, PinId>`, it substitutes `PortId` with `Port::C` and `PinId` with `13`, then instantiates the base class `GpioTemplate<Port::C, 13>`. This is a two-stage instantiation process: the LED's template parameters are determined first, and then the base class template is instantiated.

`Base` is a type alias. It doesn't define a new type but gives a shorter name to an existing type. After this, all `Base` in the code is equivalent to `GpioTemplate<PortId, PinId>`. In template programming, the full name of the base class is often long, so type aliases are almost mandatory — otherwise `Base::init` would have to be written as `GpioTemplate<PortId, PinId>::init`, which is verbose and error-prone during maintenance.

This is a convention widely used in C++ template code. You will see similar patterns in any serious template library: `using Base = ...` or `using Super = ...`, all aimed at simplifying references to base class members.

### Constructor: The Mystery of Zero Configuration

```cpp
LedTemplate() {
  Base::init(GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
}
```

These three lines are the core of the entire "zero configuration" design.

The LED constructor directly calls the base class's `init` method, passing three fixed parameters:

- **`GPIO_MODE_OUTPUT_PP`**: Push-pull output mode. Push-pull is the standard configuration for LED driving — it can actively output high and low levels with strong driving capability, suitable for driving LEDs directly. In contrast, open-drain mode can only pull down the level and requires an external pull-up resistor to output high, which is generally not used for LED driving.
- **`GPIO_NOPULL`**: No pull-up or pull-down. Internal pull-up and pull-down resistors are meaningless for push-pull output mode — push-pull drives the level itself and doesn't need external help. Additionally, the PC13 pin of the STM32F103 doesn't support internal pull-ups/pull-downs anyway, so filling in `GPIO_NOPULL` here also reflects the hardware reality.
- **`GPIO_SPEED_FREQ_LOW`**: Low speed mode. The output speed of GPIO determines the speed of the rising and falling edges of the pin level change. The faster the speed, the steeper the signal edge, the better the high-frequency performance, but it also generates more electromagnetic interference (EMI) and power consumption. LED blinking frequency is only a few Hertz, so there is no requirement for speed at all. Choosing low speed is the most reasonable — it reduces power consumption and reduces unnecessary signal noise.

These three things are almost invariant for any LED — push-pull output, no pull-up/pull-down, low speed. Hard-coding them in the LED constructor means that users of the LED template never need to worry about these three parameters. The moment the LED object is created, the constructor automatically completes the configuration. This is the meaning of "zero configuration."

Even better, `Base::init` internally calls `enablePortClock`, which determines which port's clock to enable via `if constexpr` at compile time. So the entire initialization chain is: LED constructor -> `Base::init` -> `enablePortClock` -> `__HAL_RCC_GPIOx_CLK_ENABLE` -> `RCC->APB2ENR |= ...`. From clock enabling to pin configuration, it's done in one go.

The user only needs to declare a variable:

```cpp
LedTemplate<Port::C, 13> led;
```

This single line completes all initialization. No need to call a separate initialization function, no need to manually configure any parameters.

### on() and off(): Compile-Time Level Branching

```cpp
void on() const {
  if constexpr (Level == ActiveLevel::Low) {
    Base::write(GPIO_PIN_RESET);
  } else {
    Base::write(GPIO_PIN_SET);
  }
}
```

This is the most ingenious part of the entire LED template and the section that best demonstrates the power of template parameters.

Let's break it down step by step.

`Level` is a template parameter, and its specific value is determined at compile time — either `ActiveLevel::Low` or `ActiveLevel::High`. Therefore, `Level == ActiveLevel::Low` is a compile-time constant expression, and for any given template instantiation, its result has only two possibilities: `true` or `false`.

When optimizing (even at `-O0` level), the compiler can directly select the corresponding branch based on the result of this constant expression, generating machine code with no conditional judgment. There is no runtime if-else overhead.

For the Blue Pill's PC13 LED (`ActiveLevel::Low`):

The branch judgment of `Level == ActiveLevel::Low` is `true`, so `on()` ultimately equates to:

```cpp
void on() const {
  Base::write(GPIO_PIN_RESET);
}
```

The branch judgment of `Level == ActiveLevel::Low` is also `true` (because LEVEL is still Low), so `off()` ultimately equates to:

```cpp
void off() const {
  Base::write(GPIO_PIN_SET);
}
```

For an active-high LED (`ActiveLevel::High`), the situation is exactly reversed:

The branch judgment of `Level == ActiveLevel::Low` is `false`, selecting the `else` branch:

```cpp
void on() const {
  Base::write(GPIO_PIN_SET);
}
```

The branch judgment of `Level == ActiveLevel::Low` is also `false`, selecting the `else` branch:

```cpp
void off() const {
  Base::write(GPIO_PIN_RESET);
}
```

This is the power of template parameters — one source code, two hardware configurations, the compiler automatically generates the correct level operations, with zero runtime overhead. `on()` is "light," `off()` is "extinguish," regardless of how your LED circuit is connected. Semantic correctness is guaranteed by the template, and the user doesn't need to care about the underlying level logic.

There is a detail worth noting: both methods are declared as `const`. Because they only call the base class's `write`, and `write` itself is also `const` — it just calls `HAL_GPIO_WritePin` to write registers and doesn't modify any member variables. In C++, methods that do not modify the object's logical state should be declared as `const`. This is a good programming habit and also allows these methods to be called on `const` references.

### toggle(): Delegating to the Base Class Flip

```cpp
void toggle() const {
  Base::toggle();
}
```

`toggle()` has the simplest implementation — it delegates directly to the base class's `toggle`.

Why doesn't it need to care about `ActiveLevel`? Because the toggle operation is unconditional: regardless of whether the current pin output is high or low, `toggle` will make it the opposite state. If the LED is currently lit (low), after toggling it becomes extinguished (high), and vice versa. Toggling itself doesn't care "which level represents lit," it only cares "become the opposite of the current state."

So `toggle()`'s behavior is consistent for both active-low and active-high LEDs — flip the current state. The underlying `Base::toggle()` will read the corresponding bit of the current Output Data Register (ODR), invert it, and write it back.

---

## Usage in main.cpp: Simplifying Everything

Now let's look at the complete `main.cpp`:

```cpp
#include "device/led.hpp"
#include "driver/clock.hpp"

extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
  HAL_Init();
  Clock::instance().configure(64'000'000);

  LedTemplate<Port::C, 13> led;

  while (true) {
    HAL_Delay(500);
    led.on();
    HAL_Delay(500);
    led.off();
  }
}
```

Let's look at it line by line.

**Line 1: `#include "device/led.hpp"`**

Introduces the LED template. `device/led.hpp` already internally `#include "device/gpio.hpp"`, so there's no need to include the GPIO header separately. The LED template is the only entry point the user needs to care about; it encapsulates all dependencies on the GPIO template. This is good module design — each layer only exposes necessary interfaces, and internal implementation details don't leak to the upper layer.

**Line 2: `#include "driver/clock.hpp"`**

Introduces clock configuration. `driver/clock.hpp` defines the `Clock` class, which is responsible for configuring the STM32 system clock to the target frequency (64MHz).

**Lines 3-5: `extern "C" { ... }`**

HAL headers must be wrapped in `extern "C"`. This is because `stm32f1xx_hal.h` is a pure C header file, and the function declarations inside use C language name mangling rules. The C++ compiler defaults to C++ name mangling rules, and the two are incompatible. Without `extern "C"`, the linker won't be able to find the definitions of the HAL functions and will report an "undefined reference" error.

`extern "C"` tells the C++ compiler: all declarations within the braces use C linkage conventions; do not apply C++ style name mangling to function names. This is the standard way to call C libraries in C++ projects and is extremely common in embedded development.

**Line 7: `HAL_Init();`**

Initializes the HAL library. This function does several important things: configures the Flash prefetch buffer, configures the SysTick timer for a 1ms interrupt period, and initializes HAL's internal state machine. All subsequent HAL functions (including `HAL_Init`, `HAL_Delay`, etc.) depend on this initialization.

**Line 8: `Clock::instance().configure(64'000'000);`**

Gets the clock configuration instance via the singleton pattern, then configures the system clock. This line involves the combined use of two design patterns — CRTP singleton and hardware initialization encapsulation. We will discuss this design in a dedicated section in the next part.

**Line 10: `LedTemplate<Port::C, 13> led;`**

This line does everything. Let me list the complete chain of operations it triggers:

1. Compiler instantiates `LedTemplate<Port::C, 13, ActiveLevel::Low>`, `Level` takes default value `ActiveLevel::Low`
2. Instantiates base class `GpioTemplate<Port::C, 13>`
3. Calls LED constructor
4. Constructor calls `Base::init(...)`
5. `Base::init` internally calls `enablePortClock()`
6. In `enablePortClock()`, `if constexpr (PortId == Port::C)` matches successfully, calls `__HAL_RCC_GPIOC_CLK_ENABLE()`
7. `HAL_GPIO_Init` constructs `GPIO_InitTypeDef` structure, fills in Pin=GPIO_PIN_13, Mode=OutputPP, Pull=NoPull, Speed=Low
8. Calls `HAL_GPIO_Init` to complete pin configuration

From 30+ lines of code in the C macro version, to this one line declaration. This is the power of abstraction.

**Lines 12-17: Main Loop**

```cpp
while (true) {
  HAL_Delay(500);
  led.on();
  HAL_Delay(500);
  led.off();
}
```

The main loop logic couldn't be clearer: wait 500ms, light LED, wait 500ms, extinguish LED, and repeat. `HAL_Delay` implements millisecond-level delay based on the SysTick interrupt, with accuracy depending on the system clock configuration. The semantics of `led.on()` and `led.off()` are clear at a glance, needing no comments to explain what they do.

If you want to add another LED on another pin? You only need one line of declaration:

```cpp
LedTemplate<Port::A, 0, ActiveLevel::High> led2;
```

Then call `led2.on()` and `led2.off()` in the loop. No need to copy any header or source files, no need to modify any macro definitions, no need to manually configure GPIO. Each LED is an object, created and ready to use, each performing its own duties.

---

## CRTP Singleton: The Design of Clock Configuration

There is a line of code in `main.cpp` that uses a pattern we haven't discussed in detail yet:

```cpp
Clock::instance().configure(64'000'000);
```

Behind this line is a singleton pattern based on CRTP. Let's look at two source files first.

The first is `utils/singleton.hpp`, a general-purpose CRTP singleton base class:

```cpp
#ifndef UTILS_SINGLETON_HPP_
#define UTILS_SINGLETON_HPP_

template <typename T>
class Singleton {
 public:
  static T& instance() {
    static T instance;
    return instance;
  }

  Singleton(const Singleton&) = delete;
  Singleton(Singleton&&) = delete;
  Singleton& operator=(const Singleton&) = delete;
  Singleton& operator=(Singleton&&) = delete;

 protected:
  Singleton() = default;
  ~Singleton() = default;
};

#endif // UTILS_SINGLETON_HPP_
```

The second is `driver/clock.hpp`, where `Clock` gains singleton capability by inheriting from this base class:

```cpp
#ifndef DRIVER_CLOCK_HPP_
#define DRIVER_CLOCK_HPP_

#include "utils/singleton.hpp"
#include <cstdint>

class Clock : public Singleton<Clock> {
 public:
  void configure(uint32_t cpu_freq_hz) {
    // ... HAL_RCC_OscConfig ...
    // ... HAL_RCC_ClockConfig ...
  }

  [[nodiscard]] uint32_t getCpuFreqHz() const {
    return SystemCoreClock;
  }
};

#endif // DRIVER_CLOCK_HPP_
```

CRTP stands for Curiously Recurring Template Pattern. The name sounds strange, but the principle isn't complicated: the subclass `Clock` passes itself as a template parameter to the base class `Singleton`. In this way, the `instance` method in the base class returns `Clock&`, not some generic base class reference.

The benefit of this approach is that it doesn't need virtual functions. Traditional singleton patterns often provide polymorphic `instance` methods through virtual functions, but virtual functions require a vtable (virtual function table), which is unnecessary overhead in an embedded environment. CRTP determines the specific subclass type at compile time through templates, completely eliminating runtime polymorphism overhead.

The implementation of the `instance` method uses a guarantee from C++11: `static` local variables inside a function are initialized the first time execution reaches that declaration, and initialization is thread-safe. So `static T instance` will only be constructed once, even if multiple threads call `instance` simultaneously; the compiler guarantees that only one thread executes the construction, while the others wait. In bare-metal embedded environments this isn't too important (usually there's only one thread), but in more complex systems it's a valuable guarantee.

The `= delete` part of the base class deletes the copy constructor, move constructor, copy assignment operator, and move assignment operator. These four `= delete` declarations ensure the singleton cannot be accidentally copied or moved — if you write `auto led2 = led`, the compiler will directly report an error. The comment "Never Shell A Single Instance Copyable And Movable" contains a typo where "Shell" should be "Share," but the intent is clear: a singleton should never be copied.

Why does clock configuration need to be a singleton? The STM32F103 has only one clock tree, and the system clock has only one configuration. If creating multiple `Clock` instances were allowed, code like this could appear:

```cpp
Clock::instance().configure(64'000'000);
Clock::instance().configure(72'000'000); // Which one is valid?
```

Although repeatedly calling `configure` doesn't necessarily cause immediate hardware failure (HAL functions usually reconfigure registers), it is a design flaw — allowing multiple instances implies "each instance can have a different configuration," while clock configuration should be globally unique physically. The singleton pattern prevents this misuse at the type system level.

The `getCpuFreqHz` method is marked with the `[[nodiscard]]` attribute. This is a feature introduced in C++17 that tells the compiler: this return value should not be ignored. If you write `Clock::instance().getCpuFreqHz()` without receiving the return value, the compiler will issue a warning. In embedded development, querying the clock frequency is usually for subsequent calculations (such as baud rate, timer period), so ignoring the return value is almost certainly a bug.

The CRTP singleton isn't the focus of this post — it will be expanded in detail in later chapters. But you need to understand its role in `main.cpp`: providing a globally unique, thread-safe, non-copyable entry point for clock configuration. `Clock::instance()` returns a reference to the unique instance, and `.configure(...)` calls the configuration method on that instance. The entire expression is a chain call, completing clock initialization in one line.

---

## A Pitfall Experience Regarding Construction Timing

Before continuing the comparison, there is a pitfall directly related to the usage of the LED template that is worth mentioning specifically.

⚠️ **Note:** The LED template's constructor configures the GPIO immediately when the object is created. This means that if you declare an LED object in the global scope, its construction will occur before `main` (during the C++ static initialization phase), at which point the HAL may not be initialized yet. Therefore, LED objects must be declared **after** `HAL_Init` and clock configuration — that is, inside the `main` function. This order cannot be chaotic; otherwise, although the GPIO configuration doesn't report errors, register writes when the clock is not enabled will be silently ignored by the hardware.

So LED objects must be declared after `HAL_Init` and clock configuration — that is, inside the `main` function. This is exactly how we do it in our `main.cpp`: first `HAL_Init`, then `Clock::instance().configure(...)`, and finally declare `LedTemplate<Port::C, 13> led`. This order cannot be chaotic.

---

## Final Comparison with the C Macro Approach

From the first post to this one, we have undergone four refactorings. Now it's time for a thorough comparison.

### Complete Code for the C Macro Approach

A typical C macro LED driver is divided into two parts: a header file and a source file.

**led.h:**

```cpp
#ifndef LED_H_
#define LED_H_

#include "stm32f1xx_hal.h"

#define LED_PORT GPIOC
#define LED_PIN GPIO_PIN_13
#define LED_ON() HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET)
#define LED_OFF() HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)
#define LED_TOGGLE() HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

void LED_Init(void);

#endif // LED_H_
```

**led.c:**

```cpp
#include "led.h"

void LED_Init(void) {
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {
    .Pin = LED_PIN,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW
  };
  HAL_GPIO_Init(LED_PORT, &gpio);
}
```

**main.c:**

```cpp
#include "led.h"

int main(void) {
  HAL_Init();
  // ... Clock config ...
  LED_Init();

  while (1) {
    HAL_Delay(500);
    LED_ON();
    HAL_Delay(500);
    LED_OFF();
  }
}
```

About 40 lines of driver code plus 15 lines of main function in total. It looks tidy too. But the problem is — each LED needs a separate pair of header and source files.

### Complete Code for the C++ Template Approach

**device/led.hpp (LED Template, ~30 lines):**

```cpp
#ifndef DEVICE_LED_HPP_
#define DEVICE_LED_HPP_

#include "device/gpio.hpp"

template <Port PortId, uint16_t PinId, ActiveLevel Level = ActiveLevel::Low>
class LedTemplate : public GpioTemplate<PortId, PinId> {
 public:
  using Base = GpioTemplate<PortId, PinId>;

  LedTemplate() {
    Base::init(GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
  }

  void on() const {
    if constexpr (Level == ActiveLevel::Low) {
      Base::write(GPIO_PIN_RESET);
    } else {
      Base::write(GPIO_PIN_SET);
    }
  }

  void off() const {
    if constexpr (Level == ActiveLevel::Low) {
      Base::write(GPIO_PIN_SET);
    } else {
      Base::write(GPIO_PIN_RESET);
    }
  }

  void toggle() const {
    Base::toggle();
  }
};

#endif // DEVICE_LED_HPP_
```

**main.cpp:**

```cpp
#include "device/led.hpp"
#include "driver/clock.hpp"

extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
  HAL_Init();
  Clock::instance().configure(64'000'000);

  LedTemplate<Port::C, 13> led;

  while (true) {
    HAL_Delay(500);
    led.on();
    HAL_Delay(500);
    led.off();
  }
}
```

### Item-by-Item Comparison

The `main` functions of both approaches are similarly concise, both just a dozen lines. The gap doesn't seem large. But the real difference lies in extensibility — when you need to add a second LED to the project.

**C Approach to Add a Second LED (e.g., PA0):**

You need to copy `led.h` to `led2.h`, copy `led.c` to `led2.c`, then modify all macro definitions — `LED_PORT` to `GPIOA`, `LED_PIN` to `GPIO_PIN_0`, clock enable to `__HAL_RCC_GPIOA_CLK_ENABLE`. If the LED is active-high, you also need to swap `GPIO_PIN_RESET` and `GPIO_PIN_SET`. Two files, at least six modifications.

Worse, if you have 10 LEDs? 10 pairs of header and source files, each pair manually maintained. If the HAL library API changes, you have to change 10 places.

**C++ Approach to Add a Second LED (e.g., PA0, Active-High):**

You only need to add one line in `main.cpp`:

```cpp
LedTemplate<Port::A, 0, ActiveLevel::High> led2;
```

One line of code. Clock enabling, mode configuration, and level logic are all handled automatically by the template. No need to create new files, no need to copy code, no need to modify any existing code.

This is the true value of template metaprogramming in embedded systems — not to make `main.cpp` look shorter (the length of `main.cpp` is similar in both approaches), but to drive the marginal cost of extension to zero. Every time an LED is added, the cost of the C approach is linear (new files, new code, new maintenance), while the cost of the C++ approach is constant (one declaration).

### Comparison of Build Artifacts

A frequently asked question is: will the code size of the C++ template approach be larger?

The answer is no. Because all parameters of the LED template are constants at compile time, the compiler can perform complete inline optimization. The machine code generated by `led.on()` is exactly the same as directly calling `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)`. There is no vtable, no runtime polymorphism, and no extra function call overhead. This is the so-called "zero-overhead abstraction" — you pay compilation time (template instantiation requires the compiler to do more work) in exchange for zero loss of runtime performance.

If you use `objdump -d` to disassemble the final firmware, you will find that the machine code generated by the C++ template approach and the C macro approach is almost identical at the instruction level. The cost of abstraction is completely transferred to the compilation phase.

---

## Conclusion

The LED template is complete. From the initial C macro approach, to bare C++ class encapsulation, to the general GPIO template, and to today's dedicated LED template — four refactorings, each step shifting more hardware knowledge from "things developers need to remember" to "things the compiler handles automatically."

Reviewing the evolution of these four steps: First, the C macro approach centralized hardware parameters in macro definitions in the header file. Although centralized, it was still text replacement without type safety. Second, the C++ class encapsulation turned macro definitions into member functions, with scope and type checking, but could only handle specific ports and pins. Third, the GPIO template parameterized ports and pins, achieving a general-purpose GPIO abstraction, but users still needed to know how to configure LEDs. Fourth, the LED template built a domain-specific abstraction on top of the GPIO template, encapsulating all hardware knowledge of the LED — push-pull output, active-low, low speed — in 30 lines of code.

The final result is: the user only needs to write one line of declaration to obtain a fully configured LED object. The semantics of `on()`, `off()`, and `toggle()` are clear and unambiguous, requiring no concern for underlying level logic. Template parameters determine everything at compile time, with zero runtime overhead. The cost of adding a new LED is one line of code, not a pair of files.

In the next post, we will wrap up the C++23 and modern C++ features involved in this LED series, systematically sorting out the specific applications of `if constexpr`, `enum class`, `template`, `using`, and `[[nodiscard]]` in embedded scenarios, and use actual comparisons of build artifacts to prove that these abstractions are indeed zero-overhead. We will not only write elegant code but also prove that it is as efficient as hand-written register operations.
