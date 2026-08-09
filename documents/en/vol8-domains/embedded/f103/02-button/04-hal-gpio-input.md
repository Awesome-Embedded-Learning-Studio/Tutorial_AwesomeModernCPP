---
chapter: 16
difficulty: intermediate
order: 4
platform: stm32f1
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 22: HAL GPIO Input API — How to Read Button Status with Code'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/04-hal-gpio-input.md
  source_hash: e146e6d17a05be3c87995679da6b4f91762a24dcffd5be0a1d6e11a0840f49c5
  translated_at: '2026-06-16T04:11:03.063757+00:00'
  engine: anthropic
  token_count: 1537
---
# Part 22: HAL GPIO Input API — How to Read Button State with Code

> Following the previous post: The hardware is ready, the wiring diagram is drawn, and bouncing is explained thoroughly. Now it is finally time to write code. This post breaks down the GPIO input interfaces provided by the HAL library.

---

## From Output API to Input API

In the LED tutorial, we used three HAL functions to control the LED:

| Operation | HAL Function | Register Accessed |
|-----------|-------------|-------------------|
| Initialize pin | `HAL_GPIO_Init` | CRL/CRH |
| Write pin level | `HAL_GPIO_WritePin` | ODR/BSRR |
| Toggle pin level | `HAL_GPIO_TogglePin` | ODR/BSRR |

For buttons, we only need two: one for initialization and one for reading.

| Operation | HAL Function | Register Accessed |
|-----------|-------------|-------------------|
| Initialize pin | `HAL_GPIO_Init` | CRL/CRH |
| **Read pin level** | `HAL_GPIO_ReadPin` | **IDR** |

`HAL_GPIO_Init` was already broken down in the LED tutorial—it translates the configuration in the `GPIO_InitTypeDef` structure into bit-field operations on the CRL/CRH registers. Button initialization uses the same function as LED initialization, just with different parameters.

---

## Input Mode Initialization

### Input Configuration for GPIO_InitTypeDef

The LED initialization code looks like this:

```cpp
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.Pin = GPIO_PIN_5;
GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // Push-pull output
GPIO_InitStruct.Pull = GPIO_NOPULL;          // No pull-up/pull-down
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  // Low speed
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

For button initialization, we only need to change two parameters:

```cpp
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.Pin = GPIO_PIN_0;
GPIO_InitStruct.Mode = GPIO_MODE_INPUT;        // Input mode
GPIO_InitStruct.Pull = GPIO_PULLUP;            // Internal pull-up
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;   // Ignored in input mode
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

Three things are worth noting:

**First, `Mode` changes from `GPIO_MODE_OUTPUT_PP` to `GPIO_MODE_INPUT`.** This corresponds to the `MODE` bits (input mode) and `PUPD` bits (pull-up/pull-down input) in the CRL register.

**Second, `Pull` changes from `GPIO_NOPULL` to `GPIO_PULLUP`.** This enables the internal pull-up resistor and writes 1 to the corresponding bit in ODR to select the pull-up direction (that detail about "ODR controlling pull-up/down direction in input mode" mentioned in the last post).

**Third, `Speed` has no actual meaning in input mode.** Speed controls the slew rate of the output driver—in input mode, the output driver is disconnected, so this parameter does not affect any behavior. However, HAL requires you to fill in a value; just pick anything.

### Don't Forget the Clock

Just like with output, we must enable the corresponding clock before using any GPIO port. PA0 is on GPIOA, so:

```cpp
__HAL_RCC_GPIOA_CLK_ENABLE();
```

If you forget this step, the `HAL_GPIO_Init` call won't error out (it doesn't know if you enabled the clock), but the written configuration won't take effect—the pin stays in reset state (floating input), and the read value will be indeterminate. This is one of the most common pitfalls for beginners.

In the LED tutorial, we used `RCC_ClkEnable` in the Button template class to automatically select the clock enable macro at compile time. But if you are writing in C, remember to call it manually.

---

## HAL_GPIO_ReadPin

### Function Signature

```cpp
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```

Two parameters: `GPIOx` specifies the port (GPIOA, GPIOB, GPIOC...), and `GPIO_Pin` specifies the pin number (`GPIO_PIN_0` ~ `GPIO_PIN_15`). The return value is a `GPIO_PinState` enum:

```cpp
typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;
```

### Underlying Implementation

The HAL library's implementation of `HAL_GPIO_ReadPin` is very concise:

```cpp
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
    return (GPIO_PinState)((GPIOx->IDR & GPIO_Pin) != 0U);
}
```

The core is a single bit operation: `GPIOx->IDR & GPIO_Pin`. `IDR` is a 16-bit read-only register where each bit corresponds to a pin. `GPIO_Pin`'s value is `0x0001` (for Pin 0), so `IDR & 0x0001` extracts the value of bit 0. If it's not 0, the pin is high; otherwise, it's low.

It takes just a few clock cycles (LDR + AND + CMP, about 2-4 cycles after compiler optimization). For a 72MHz CPU, this means reading pin state takes only a few tens of nanoseconds.

### Comparison with WritePin

`HAL_GPIO_WritePin` operates on the BSRR register (Bit Set/Reset Register), which is write-only—writing 1 to the lower 16 bits resets (clears) the corresponding ODR bit, and writing 1 to the upper 16 bits sets (assigns 1 to) the corresponding ODR bit. This is an atomic operation that doesn't require the read-modify-write process.

`HAL_GPIO_ReadPin` operates on the IDR register, which is read-only, directly returning the pin level.

| | Output (LED) | Input (Button) |
|---|-----------|-----------|
| Initialization | `HAL_GPIO_Init` | `HAL_GPIO_Init` |
| Core Operation | `HAL_GPIO_WritePin` → BSRR | `HAL_GPIO_ReadPin` → IDR |
| Register Attribute | BSRR Write-Only | IDR Read-Only |
| Operation Time | 1 Clock Cycle | 1 Clock Cycle |

---

## read_pin_state(): Our C++ Wrapper

In `gpio.hpp`, we added a `read_pin_state` method to the GPIO template class:

```cpp
enum class State { Low = 0, High = 1 };

[[nodiscard]] State read_pin_state() const {
    return static_cast<State>(
        HAL_GPIO_ReadPin(GPIOx, GPIO_Pin_x)
    );
}
```

Here are a few design decisions to explain.

### Why Return a State Enum Instead of bool

You could argue that returning `bool` is simpler—`true` is high, `false` is low. But we choose to return a `State` enum (`Low` and `High`), keeping symmetry with the output side's `write`. This way, input and output use the same set of types, and the code style remains consistent.

Also, the `State` enum is less prone to misuse than `bool`. If you have multiple pins to operate, the `true`/`false` meaning of `bool` can be confusing in different contexts—is `true` pressed or released? It depends on whether it's pull-up or pull-down. But `High` always means the pin is at a high electrical level, and `Low` always means low, without ambiguity.

### Why Add [[nodiscard]]

`[[nodiscard]]` tells the compiler: the return value of this function should not be ignored. If you write `read_pin_state()` but don't use the return value, the compiler will issue a warning.

The sole purpose of reading pin state is to get the return value. If you call `read_pin_state()` and don't use the result, that call is 100% wrong—you likely forgot the assignment statement. In embedded development, if such a basic error isn't caught, it could lead to button states not being detected, causing abnormal system behavior that is hard to debug.

### Zero-Overhead of static_cast

`HAL_GPIO_ReadPin` returns `GPIO_PinState` (0 or 1), and `static_cast<State>` converts it to `State::Low` or `State::High`. `static_cast` between enums is a pure compile-time operation—the underlying value (0 or 1) doesn't change, only the type information does. The generated machine code is exactly the same as using the raw value directly.

### const Member Function

`read_pin_state` is declared as `const`—it doesn't modify any member variables of the object. This is the standard C++ way to express a "read-only operation." In contrast, `write` is also declared as `const`—this is because our GPIO template class has no member variables to modify; all "state" exists in the hardware registers, not in the C++ object.

---

## Minimal C Example

Before moving on to the complete polling program in the next post, let's verify with a minimal C code snippet: can we read the button state?

```cpp
#include "stm32f1xx_hal.h"

int main(void) {
    // 1. Initialize System Clock
    HAL_Init();
    SystemClock_Config();

    // 2. Enable Clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();  // For Button (PA0)
    __HAL_RCC_GPIOC_CLK_ENABLE();  // For LED (PC13)

    // 3. Initialize Button (PA0) as Input with Pull-up
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // 4. Initialize LED (PC13) as Output
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 5. Main Loop
    while (1) {
        // Read button state
        GPIO_PinState button_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

        // Control LED based on button (Active Low logic)
        // Button Pressed (Low) -> LED ON (Low)
        // Button Released (High) -> LED OFF (High)
        if (button_state == GPIO_PIN_RESET) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // LED ON
        } else {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   // LED OFF
        }
    }
}
```

This code does four things: (1) enables GPIOA and GPIOC clocks, (2) configures PA0 as pull-up input, (3) configures PC13 as push-pull output, (4) reads PA0 and controls PC13 in the main loop.

⚠️ **Note:** This code **does not debounce**. A quick press of the button might cause the LED to flash several times. In the next post, we will see a full demonstration of this problem and its solution.

If you flash this code to the board, the LED turns on when the button is held down and turns off when released. The most basic input-output interaction is now realized.

---

## Looking Back

This post broke down two HAL APIs: the input mode configuration of `HAL_GPIO_Init` and the underlying implementation of `HAL_GPIO_ReadPin`. Key points:

1. Input initialization only needs `Mode` + `Pull` parameters.
2. `HAL_GPIO_ReadPin` is essentially reading the `IDR` register, taking one clock cycle.
3. Our `read_pin_state` wrapper adds `[[nodiscard]]` and `const`, returning a type-safe `State` enum.

In the next post, we will extend this minimal code into a complete C polling program—and then see firsthand what happens without debouncing.
