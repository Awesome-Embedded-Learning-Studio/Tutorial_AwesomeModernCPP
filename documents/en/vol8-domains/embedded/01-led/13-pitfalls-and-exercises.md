---
chapter: 15
difficulty: beginner
order: 13
platform: stm32f1
reading_time_minutes: 10
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 18: Common Pitfalls and Practical Exercises — Getting Creative with LEDs'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/13-pitfalls-and-exercises.md
  source_hash: 0233448f34fd5478b734be99ee5bf30cbc0110873d2ddd223548e6c1e36a897d
  translated_at: '2026-06-16T04:10:19.271173+00:00'
  engine: anthropic
  token_count: 1928
---
# Part 18: Common Pitfalls and Practical Exercises — Getting Creative with LEDs

> Prerequisites: All principles and code have been covered, and the LED is blinking. However, when you actually start working, you will inevitably encounter various weird problems. This article marks out all the common pitfalls first, and then provides three progressive exercises to help you transform your knowledge from "understood" to "writable".

---

## Pitfall 1: Forgetting to Enable the Clock — The Silent Killer of Peripherals

This is the number one pitfall in the entire STM32 learning process. The symptoms are very weird: your code is completely "correct", `HAL_Init` returns no errors, and `HAL_GPIO_Init` is fine, but the LED just won't light up. When you check the GPIO registers with a debugger, you find that the written values haven't taken effect — the registers are still at their default reset values.

The reason is simple: The clock for the GPIO port is not enabled. To save power, all peripheral clocks are disabled by default when the STM32 powers up. Without a clock, the peripheral's registers are in a "power-off" state — the CPU's bus write operations are silently accepted by the hardware but not executed. It's like typing on a keyboard connected to a computer that is turned off — the key presses happen, but the computer doesn't react.

Troubleshooting: Your first reaction should be to check the clock. Use the debugger to read the `RCC` register (address `0x40021004` for APB2 or `0x4002101C` for APB1, depending on the port) to see if the bit for the corresponding GPIO port is set to 1. If it is 0, the clock is not enabled.

Our C++ template has eliminated this pitfall by design: the `Led` constructor automatically calls `HAL_GPIO_Init`, which internally calls `__HAL_RCC_GPIOx_CLK_ENABLE`, so you cannot forget to enable the clock. But if you bypass the template and use the HAL API directly, this pitfall still exists.

---

## Pitfall 2: Confusing Push-Pull and Open-Drain — LED Flickers or Won't Light

If you mistakenly configure the GPIO as open-drain output (`GPIO_MODE_OUTPUT_OD`), the LED's behavior will be very weird: it might not light up at all, it might be very dim, or the brightness might be unstable.

The reason is that open-drain output only has the low-side N-MOS working. When outputting a "high" level, the pin is actually in a floating state — there is no active drive to VDD. The voltage across the LED depends on whether the external circuit has a pull-up path. The Blue Pill's PC13 LED circuit has no external pull-up resistor, so when the open-drain output is "high", the LED basically won't light up.

The solution is simple: Always use push-pull output (`GPIO_MODE_OUTPUT_PP`) for LED control. Our LED template defaults to push-pull, so as long as you use the template, you won't fall into this trap.

---

## Pitfall 3: The PC13 Pull-Up/Pull-Down Trap

You might think configuring a pull-up or pull-down for PC13 is a good idea — for example, to give the pin a definite level when the LED is off. But ST's datasheet explicitly states that the internal pull-up/pull-down functionality is not available for pins PC13, PC14, and PC15. Even if you set `GPIO_PULLUP` in `GPIO_InitTypeDef`, HAL won't report an error — it will write your configuration to the register, but the hardware will silently ignore it.

So for PC13, Pull must be set to `GPIO_NOPULL`. Our LED template defaults to `NoPull`, which is both the correct choice and the only available choice on PC13.

---

## Pitfall 4: The Speed Selection Misconception — High Speed Won't Make LEDs Blink Faster

Many beginners think that setting the GPIO speed to `GPIO_SPEED_FREQ_HIGH` will make the LED switch faster. In reality, the speed setting controls the slew rate of the output signal — that is, how fast the voltage jumps from one level to another. For LED blinking (1Hz to 10Hz), whether you choose low speed or high speed, the human eye can't see any difference. High speed only makes the voltage edges steeper, generating more electromagnetic interference (EMI) and higher transient currents.

Rule of thumb: Use low speed by default, and only increase the speed for high-speed peripherals (SPI clocks exceeding a few MHz, UART high baud rates, etc.).

---

## Exercise 1: Multi-LED Control

**Task:** Control two LEDs on the Blue Pill — the onboard LED on PC13 blinks at 1Hz, and assume an external LED is connected to PA0 blinking at 2Hz. Assume the PA0 LED is active-high (LED anode connected to PA0, cathode connected to GND).

**Complete Reference Answer:**

```cpp
// Define LED types
using BoardLed = Led<PortC, 13>;        // PC13: Active Low
using ExtLed = Led<PortA, 0, ActiveHigh>; // PA0: Active High

// Instantiate LEDs
BoardLed board_led;
ExtLed ext_led;

while (true) {
    board_led.toggle();
    HAL_Delay(500); // 1Hz period (500ms on, 500ms off)

    ext_led.toggle();
    HAL_Delay(250); // 2Hz period (250ms on, 250ms off)
}
```

**Discussion:** The two LEDs are different types — `Led<PortC, 13>` and `Led<PortA, 0, ActiveHigh>`. The compiler generates independent code for each type. The onboard LED uses the default `ActiveLow` (the third template parameter is omitted), while the external LED explicitly specifies `ActiveHigh`. Each LED's constructor automatically enables the clock for the corresponding port — `board_led` enables the GPIOC clock, `ext_led` enables the GPIOA clock, so you don't need to manage it manually.

---

## Exercise 2: Button Input + LED Interaction

**Task:** Connect a button to PA8 (connected to VDD via a 10K pull-up resistor, grounded when pressed). When the button is pressed, the PC13 LED lights up; when released, the LED turns off.

**Complete Reference Answer:**

```cpp
using BoardLed = Led<PortC, 13>;
using Button = GpioPin<PortA, 8, InputMode, PullUp>;

BoardLed led;
Button btn;

while (true) {
    if (btn.read() == false) { // Button pressed (low level)
        led.on();
    } else { // Button released (high level)
        led.off();
    }
    HAL_Delay(10); // Simple debounce
}
```

**Discussion:** Here we use the `GpioPin` template directly (instead of the `Led` template) to configure the button pin because a button is an input device. The button is configured as input mode (`InputMode`) with the internal pull-up resistor enabled (`PullUp`) — when the button is floating, PA8 is pulled high; when pressed, it connects to ground and goes low. `read()` directly reads the IDR register, returning `true` or `false`. The 10ms delay is the simplest debounce solution — actual projects might require a more complex debounce algorithm.

---

## Exercise 3: Generic GpioPin Template

**Task:** Design a more generic `GpioPin` template that decides available operation methods at compile time based on the mode parameter. Output modes have `OutputPP` and `OutputOD`, input modes have `InputMode`.

**Complete Reference Answer:**

```cpp
enum class PinMode {
    Input,
    OutputPP,
    OutputOD
};

template <typename Port, int PinNo, PinMode Mode>
class GpioPin {
public:
    GpioPin() {
        static_assert(PinNo >= 0 && PinNo < 16, "Invalid pin number");

        GPIO_InitTypeDef init{};

        init.Pin = (1U << PinNo);
        init.Speed = GPIO_SPEED_FREQ_LOW;

        if constexpr (Mode == PinMode::Input) {
            init.Mode = GPIO_MODE_INPUT;
            init.Pull = GPIO_NOPULL; // Default to no pull, configurable via template params if needed
        } else if constexpr (Mode == PinMode::OutputPP) {
            init.Mode = GPIO_MODE_OUTPUT_PP;
            init.Pull = GPIO_NOPULL;
        } else if constexpr (Mode == PinMode::OutputOD) {
            init.Mode = GPIO_MODE_OUTPUT_OD;
            init.Pull = GPIO_NOPULL;
        }

        // Enable Clock and Init
        Port::enable();
        HAL_GPIO_Init(Port::base(), &init);
    }

    // Write method: Only available for Output modes
    void write(bool state) {
        if constexpr (Mode == PinMode::OutputPP || Mode == PinMode::OutputOD) {
            HAL_GPIO_WritePin(Port::base(), (1U << PinNo), state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }

    // Read method: Only available for Input mode
    bool read() const {
        if constexpr (Mode == PinMode::Input) {
            return HAL_GPIO_ReadPin(Port::base(), (1U << PinNo)) == GPIO_PIN_SET;
        }
        return false; // Fallback for non-input modes (should be optimized out)
    }
};
```

⚠️ **Note:** In Exercise 3's `GpioPin` template, the `write` and `read` methods become no-ops via `if constexpr` when the mode doesn't match — the compiler won't stop you from calling them, it just silently ignores them. If you want the compiler to report an error directly when calling `write` on an input pin (instead of silently ignoring it), you can use `static_assert` or C++20 Concepts to constrain method availability. This is a direction worth exploring further.

**Discussion:** This `GpioPin` template has several key differences from the previous `Led` template.

`PinMode` as a template parameter determines the pin's role. When declaring `GpioPin<PortA, 0, PinMode::OutputPP>`, the compiler knows this is an output pin, and the `write` method will work normally. The `write` and `read` methods use `if constexpr` as a compile-time guard. If you call `write` on an input pin, since the `if constexpr` condition is false, the entire call is discarded by the compiler — no code is generated. This is much more efficient than a runtime "mode check + return error code" scheme.

The constructor automatically selects the correct HAL mode based on `PinMode`. `Port::enable()` is a `constexpr` function that maps the `Port` template parameter to the HAL's `__HAL_RCC_GPIOx_CLK_ENABLE` macro at compile time. The usage is also very intuitive:

```cpp
using MyInput = GpioPin<PortA, 8, PinMode::Input>;
using MyOutput = GpioPin<PortC, 13, PinMode::OutputPP>;

MyInput button;
MyOutput led;

// ...
if (button.read()) {
    led.write(false);
}
```

There is a subtle design decision here worth pondering — the `write` and `read` methods are discarded via `if constexpr` in non-matching modes. This means the compiler won't stop you from calling a method that "logically doesn't exist"; it just silently turns the call into a no-op. For example, calling `write` on an input pin compiles, but nothing happens. If you want the compiler to report an error directly when calling `write` on an input pin (instead of silently ignoring it), you need to use `static_assert` or SFINAE/Concepts to constrain method availability. This is a direction that can be explored further.

---

## Chapter Summary

Looking back at the entire LED tutorial series, we started with the hardware principles of GPIO, learned how to use the HAL API, saw the limitations of the C macro approach, and then through four progressive refactorings (enum class → template parameters → `if constexpr` → LED template), we finally arrived at a type-safe, zero-configuration, zero-overhead LED driver abstraction.

Every step of refactoring solved a specific problem, and every C++ feature introduced had a clear purpose. This isn't using modern C++ just to show off — it's because the limitations of traditional C solutions in terms of type safety and code reuse become increasingly painful in complex projects.

You now have a set of reusable device layer code: `GpioPin`, `Led`, and `Button`. They will accompany you into subsequent tutorials — timer interrupts, UART communication, SPI drivers — where we will continue to build on the existing template foundation.

Next tutorial preview: SysTick Timer and Interrupts. We will move away from the `HAL_Delay` polling mode and enter interrupt-based LED blinking, introducing more C++23 features. Taking a photo of the board now is justified.
