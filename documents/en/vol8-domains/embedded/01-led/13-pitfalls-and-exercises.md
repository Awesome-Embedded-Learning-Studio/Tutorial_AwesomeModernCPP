---
title: 'Part 18: Common Pitfalls and Hands-on Practice — Having Fun with LEDs'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 15
order: 13
translation:
  source: documents/vol8-domains/embedded/01-led/13-pitfalls-and-exercises.md
  source_hash: 20e7dc756e285ab3e206f725de1e1671030edd9ad71cc5a68265e84106b1645b
  translated_at: '2026-05-26T12:08:49.663261+00:00'
  engine: anthropic
  token_count: 1922
---
# Part 18: Common Pitfalls and Practical Exercises — Getting Creative with LEDs

> Picking up where we left off: we've covered all the theory and code, and the LED blinks. But when you actually get your hands dirty, you'll run into all sorts of bizarre issues — this article first maps out all the common pitfalls, then provides three progressive exercises to help you turn "understanding" into "writing code."

---

## Pitfall 1: Forgetting to Enable the Clock — The Silent Peripheral Killer

This is the number one pitfall in the entire STM32 learning journey. The symptoms are bizarre: your code is perfectly "correct," `HAL_GPIO_Init` returns no errors, `HAL_GPIO_WritePin` checks out fine, but the LED simply won't light up. When you inspect the GPIO registers with a debugger, you'll find that the values you wrote never took effect — the registers are still at their reset defaults.

The reason is simple: the GPIO port clock is not enabled. After power-up, STM32 disables all peripheral clocks by default to save power. Without a clock, the peripheral's registers are in a "powered-down" state — the CPU's bus write operations are silently accepted by the hardware but never executed. It's like typing on a keyboard connected to a powered-off computer — the keypresses physically happen, but the computer doesn't react.

How to troubleshoot: your first instinct should be to check the clock. Use the debugger to read the `RCC_APB2ENR` register (address `0x40021018`) and see if the bit for the corresponding GPIO port is set to 1. If it's 0, the clock isn't enabled.

Our C++ template eliminates this pitfall by design: the `setup()` method automatically calls `GPIOClock::enable_target_clock()` internally, making it impossible to forget the clock. But if you bypass the template and use the HAL API directly, this pitfall still exists.

---

## Pitfall 2: Choosing Push-Pull vs. Open-Drain Incorrectly — LED Flickers Inconsistently

If you mistakenly configure the GPIO as open-drain output (`GPIO_MODE_OUTPUT_OD`), the LED will behave very strangely: it might not light up at all, it might be extremely dim, or the brightness might be unstable.

The reason is that open-drain output only has the N-MOS low-side transistor working. When outputting a "high" level, the pin is actually floating — it's not actively driven to VDD. The voltage across the LED depends on whether the external circuit has a pull-up path. The PC13 LED circuit on the Blue Pill has no external pull-up resistor, so when the open-drain output is "high," the LED basically won't light up.

The solution is simple: always use push-pull output (`GPIO_MODE_OUTPUT_PP`) for LED control. Our LED template defaults to push-pull, so as long as you use the template, you won't fall into this trap.

---

## Pitfall 3: The PC13 Pull-Up/Pull-Down Trap

You might think it's a good idea to configure a pull-up or pull-down for PC13 — for example, to give the pin a defined level when the LED is off. But ST's datasheet explicitly states that the internal pull-up and pull-down functions are not available on PC13/14/15. Even if you set `Pull=GPIO_PULLUP` in `GPIO_InitTypeDef`, HAL won't report an error — it writes your configuration to the register, but the hardware silently ignores it.

So for PC13, Pull must be set to `GPIO_NOPULL`. Our LED template defaults to NoPull, which is both the correct choice and the only viable choice on PC13.

---

## Pitfall 4: The Speed Selection Misconception — High Speed Won't Make the LED Blink Faster

Many beginners think that setting the GPIO speed to `GPIO_SPEED_FREQ_HIGH` will make the LED toggle faster. In reality, the speed setting controls the slew rate of the output signal — that is, how fast the voltage transitions from one level to another. For LED blinking (1Hz to 10Hz), there's no visible difference whether you choose low speed or high speed. High speed only makes the voltage edges steeper, generating more electromagnetic interference (EMI) and higher transient currents.

Rule of thumb: stick with low speed by default, and only increase the speed for high-speed peripherals (SPI clocks exceeding a few MHz, high UART baud rates, etc.).

---

## Exercise 1: Multiple LED Control

**Task:** Control two LEDs on the Blue Pill — the onboard LED on PC13 blinks at 1Hz, and assume an external LED on PA0 blinks at 2Hz. Assume the PA0 LED is active-high (LED anode connected to PA0, cathode connected to GND).

**Full reference solution:**

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    // 板载LED：PC13，低电平有效（默认）
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> board_led;

    // 外接LED：PA0，高电平有效
    device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> ext_led;

    uint32_t counter = 0;
    while (1) {
        HAL_Delay(250);  // 250ms为一个节拍
        counter++;

        // PC13 LED：每4个节拍切换一次 = 1Hz
        if (counter % 4 == 0) {
            board_led.toggle();
        }

        // PA0 LED：每2个节拍切换一次 = 2Hz
        if (counter % 2 == 0) {
            ext_led.toggle();
        }
    }
}
```

**Discussion:** The two LEDs are different types — `LED<GpioPort::C, GPIO_PIN_13, ActiveLevel::Low>` and `LED<GpioPort::A, GPIO_PIN_0, ActiveLevel::High>`. The compiler generates independent code for each type. The onboard LED uses the default `ActiveLevel::Low` (the third template parameter is omitted), while the external LED explicitly specifies `ActiveLevel::High`. Each LED's constructor automatically enables the clock for its corresponding port — board_led enables the GPIOC clock, ext_led enables the GPIOA clock, so you don't need to manage them manually.

---

## Exercise 2: Button Input + LED Interaction

**Task:** Connect a button to PA8 (wired to VDD through a 10K pull-up resistor, grounded when pressed). When the button is pressed, the PC13 LED turns on; when released, the LED turns off.

**Full reference solution:**

```cpp
#include "device/gpio/gpio.hpp"
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    // LED输出：PC13，低电平有效
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;

    // 按钮输入：PA8，上拉（按下为低电平）
    using BtnGPIO = device::gpio::GPIO<device::gpio::GpioPort::A, GPIO_PIN_8>;
    BtnGPIO button;
    button.setup(BtnGPIO::Mode::Input, BtnGPIO::PullPush::PullUp);

    while (1) {
        // 读取按钮状态：按下时为低电平（GPIO_PIN_RESET）
        GPIO_PinState state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);

        if (state == GPIO_PIN_RESET) {
            led.on();   // 按钮按下，LED点亮
        } else {
            led.off();  // 按钮松开，LED熄灭
        }

        HAL_Delay(10);  // 简单去抖延时
    }
}
```

**Discussion:** Here we directly use the GPIO template (rather than the LED template) to configure the button pin, because the button is an input device. The button is configured as input mode (`Mode::Input`) with the internal pull-up resistor enabled (`PullPush::PullUp`) — when the button is floating, PA8 is pulled high, and when pressed, it's grounded and goes low. `HAL_GPIO_ReadPin` directly reads the IDR register, returning either `GPIO_PIN_SET` or `GPIO_PIN_RESET`. The 10ms delay is the simplest debounce approach — in real projects, you might need a more sophisticated debounce algorithm.

---

## Exercise 3: Generalized GpioPin Template

**Task:** Design a more generic `GpioPin` template that determines the available operation methods at compile time based on a mode parameter. Output modes have `write()` and `toggle()`, while input modes have `read()`.

**Full reference solution:**

```cpp
#pragma once

extern "C" {
#include "stm32f1xx_hal.h"
}

#include <cstdint>

namespace device::gpio {

enum class GpioPort : uintptr_t {
    A = GPIOA_BASE, B = GPIOB_BASE, C = GPIOC_BASE,
    D = GPIOD_BASE, E = GPIOE_BASE,
};

enum class PinMode { Input, Output, Alternate, Analog };

template <GpioPort PORT, uint16_t PIN, PinMode MODE>
class GpioPin {
    static constexpr GPIO_TypeDef* port() noexcept {
        return reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
    }

    static void enable_clock() {
        if constexpr (PORT == GpioPort::A) __HAL_RCC_GPIOA_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::B) __HAL_RCC_GPIOB_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::C) __HAL_RCC_GPIOC_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::D) __HAL_RCC_GPIOD_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::E) __HAL_RCC_GPIOE_CLK_ENABLE();
    }

    static constexpr uint32_t mode_to_hal() {
        if constexpr (MODE == PinMode::Input)      return GPIO_MODE_INPUT;
        else if constexpr (MODE == PinMode::Output) return GPIO_MODE_OUTPUT_PP;
        else if constexpr (MODE == PinMode::Alternate) return GPIO_MODE_AF_PP;
        else return GPIO_MODE_ANALOG;
    }

public:
    GpioPin() {
        enable_clock();
        GPIO_InitTypeDef init{};
        init.Pin = PIN;
        init.Mode = mode_to_hal();
        init.Pull = GPIO_NOPULL;
        init.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(port(), &init);
    }

    void write(bool high) const {
        if constexpr (MODE == PinMode::Output) {
            HAL_GPIO_WritePin(port(), PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }

    void toggle() const {
        if constexpr (MODE == PinMode::Output) {
            HAL_GPIO_TogglePin(port(), PIN);
        }
    }

    bool read() const {
        if constexpr (MODE == PinMode::Input) {
            return HAL_GPIO_ReadPin(port(), PIN) == GPIO_PIN_SET;
        }
        return false;
    }
};

} // namespace device::gpio
```

⚠️ Note: In the `GpioPin` template for Exercise 3, the `write()` and `read()` methods become no-ops under non-matching modes via `if constexpr` — the compiler won't stop you from calling them, it just silently ignores them. If you want the compiler to throw an error when `write()` is called on an input pin (rather than silently ignoring it), you can use `static_assert` or C++20 Concepts to constrain method availability. This is a direction worth further exploration.

**Discussion:** This `GpioPin` template has several key differences from the previous `GPIO` template.

`PinMode` as a template parameter determines the pin's role. When declaring `GpioPin<GpioPort::C, GPIO_PIN_13, PinMode::Output>`, the compiler knows this is an output pin, and the `write()` and `toggle()` methods will work normally. The `write()` and `read()` methods use `if constexpr` internally as compile-time guards. If you call `write()` on an input pin, because the `if constexpr` condition is false, the entire call is discarded by the compiler — no code is generated. This is far more efficient than a runtime "mode check + return error code" approach.

The constructor automatically selects the correct HAL mode based on `PinMode`. `mode_to_hal()` is a `constexpr` function that maps the `PinMode` enum to HAL's `GPIO_MODE_xxx` macro at compile time. The usage is also very intuitive:

```cpp
// 输出引脚
GpioPin<GpioPort::C, GPIO_PIN_13, PinMode::Output> led;
led.write(false);  // 输出低电平，LED点亮
led.toggle();

// 输入引脚
GpioPin<GpioPort::A, GPIO_PIN_8, PinMode::Input> button;
bool pressed = button.read();
```

There's a subtle design decision here worth pondering — the `write()` and `read()` methods are discarded via `if constexpr` in non-matching modes, meaning the compiler won't stop you from calling a method that "logically doesn't exist"; it just silently turns the call into a no-op. For example, calling `write()` on an input pin will compile fine, but nothing will happen. If you want the compiler to throw an error when `write()` is called on an input pin (rather than silently ignoring it), you need to use `static_assert` or SFINAE/Concepts to constrain method availability. This is a direction worth further exploration.

---

## Chapter Summary

Looking back at the entire LED tutorial series, we started from the hardware principles of GPIO, learned to use the HAL API, saw the limitations of the C macro approach, and then through four progressive refactorings (enum class → template parameters → if constexpr → LED template), we finally arrived at a type-safe, zero-configuration, zero-overhead LED driver abstraction.

Each refactoring step solved a specific problem, and each C++ feature introduced had a clear purpose. We didn't use modern C++ just to show off — it's because the limitations of traditional C approaches in type safety and code reuse become increasingly painful in complex projects.

You now have a set of reusable device-layer code: `gpio.hpp`, `led.hpp`, `simple_singleton.hpp`. They will accompany you into the upcoming tutorials — timer interrupts, UART communication, SPI drivers — where we'll continue to build on the existing templates step by step.

Next tutorial preview: SysTick timer and interrupts. We'll move away from the `HAL_Delay` polling model, enter interrupt-based LED blinking, and introduce more C++23 features. Taking a photo of your board is not too much to ask.
