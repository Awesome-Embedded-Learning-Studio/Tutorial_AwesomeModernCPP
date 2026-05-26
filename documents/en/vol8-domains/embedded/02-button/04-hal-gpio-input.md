---
title: 'Part 22: HAL GPIO Input API — How to Read Button State in Code'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 4
translation:
  source: documents/vol8-domains/embedded/02-button/04-hal-gpio-input.md
  source_hash: d9beddcba6146789438c19cb77e80a3a009af89874ed226ff9c3adb560384f48
  translated_at: '2026-05-26T12:11:07.797286+00:00'
  engine: anthropic
  token_count: 1531
---
# Part 22: HAL GPIO Input API — How to Read Button State in Code

> Following up on the previous article: the hardware is ready, the wiring diagram is drawn, and bouncing is thoroughly explained. Now we finally get to write some code. This article breaks down the GPIO input interface provided by the HAL library.

---

## From Output API to Input API

In the LED tutorial, we used three HAL functions to control the LED:

| Operation | HAL Function | Register Accessed |
|-----------|-------------|-------------------|
| Initialize pin | `HAL_GPIO_Init()` | CRL/CRH |
| Write pin level | `HAL_GPIO_WritePin()` | ODR/BSRR |
| Toggle pin level | `HAL_GPIO_TogglePin()` | ODR/BSRR |

For a button, we only need two: one for initialization, and one for reading.

| Operation | HAL Function | Register Accessed |
|-----------|-------------|-------------------|
| Initialize pin | `HAL_GPIO_Init()` | CRL/CRH |
| **Read pin level** | `HAL_GPIO_ReadPin()` | **IDR** |

`HAL_GPIO_Init()` was already broken down in the LED tutorial—it translates the configuration in the `GPIO_InitTypeDef` struct into bit-field operations on the CRL/CRH registers. Button initialization uses the exact same function as LED initialization, just with different parameters.

---

## Input Mode Initialization

### Input Configuration in GPIO_InitTypeDef

The LED initialization code looks like this:

```c
GPIO_InitTypeDef init = {0};
init.Pin = GPIO_PIN_13;
init.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出
init.Pull = GPIO_NOPULL;
init.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &init);
```

For the button, we only need to change two parameters:

```c
GPIO_InitTypeDef init = {0};
init.Pin = GPIO_PIN_0;
init.Mode = GPIO_MODE_INPUT;       // 通用输入
init.Pull = GPIO_PULLUP;           // 内部上拉
init.Speed = GPIO_SPEED_FREQ_LOW;  // 输入模式下 Speed 无意义，但需要填值
HAL_GPIO_Init(GPIOA, &init);
```

There are three noteworthy points here:

**First, `Mode` changes from `GPIO_MODE_OUTPUT_PP` to `GPIO_MODE_INPUT`.** This corresponds to `MODE[1:0] = 00` (input mode) and `CNF[1:0] = 10` (pull-up/pull-down input) in the CRL register.

**Second, `Pull` changes from `GPIO_NOPULL` to `GPIO_PULLUP`.** This enables the internal pull-up resistor and writes a 1 to the corresponding bit in the ODR to select the pull-up direction (the detail mentioned in the previous article about "ODR controlling pull-up/pull-down direction in input mode").

**Third, `Speed` has no practical meaning in input mode.** Speed controls the slew rate of the output driver—in input mode, the output driver is disconnected, so this parameter doesn't affect any behavior. However, the HAL requires you to fill in a value, so just put in anything.

### Don't Forget the Clock

Just like with output, we must enable the corresponding clock before using any GPIO port. PA0 is on GPIOA, so:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
```

If you forget this step, the `HAL_GPIO_Init()` call won't throw an error (it doesn't know whether you've enabled the clock or not), but the written configuration won't take effect—the pin will remain in its reset state (floating input), and the read value will be indeterminate. This is one of the most common pitfalls for beginners.

In the LED tutorial, we used `if constexpr` to automatically select the clock enable macro at compile time. The Button template class in this button tutorial will reuse the same mechanism. But if you're writing in C, remember to call it manually.

---

## HAL_GPIO_ReadPin

### Function Signature

```c
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```

Two parameters: `GPIOx` specifies the port (GPIOA, GPIOB, GPIOC...), and `GPIO_Pin` specifies the pin number (`GPIO_PIN_0` ~ `GPIO_PIN_15`). The return value is the `GPIO_PinState` enum:

```c
typedef enum {
    GPIO_PIN_RESET = 0,  // 低电平
    GPIO_PIN_SET   = 1   // 高电平
} GPIO_PinState;
```

### Underlying Implementation

The HAL library's implementation of `HAL_GPIO_ReadPin()` is very concise:

```c
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
    GPIO_PinState bitstatus;
    if ((GPIOx->IDR & GPIO_Pin) != (uint32_t)GPIO_PIN_RESET) {
        bitstatus = GPIO_PIN_SET;
    } else {
        bitstatus = GPIO_PIN_RESET;
    }
    return bitstatus;
}
```

The core is a single bit operation: `GPIOx->IDR & GPIO_Pin`. `IDR` is a 16-bit read-only register where each bit corresponds to a pin. The value of `GPIO_PIN_0` is `0x0001`, so `IDR & 0x0001` simply extracts the value of bit 0. If it's not zero, the pin is high; otherwise, it's low.

This takes just a few clock cycles (LDR + AND + CMP, roughly 2-4 cycles after compiler optimization). A 72MHz CPU means reading a pin state takes only a few tens of nanoseconds.

### Comparison with WritePin

`HAL_GPIO_WritePin()` operates on the BSRR register (Bit Set/Reset Register), which is a write-only register—writing a 1 to the lower 16 bits resets (clears) the corresponding ODR bit, while writing a 1 to the upper 16 bits sets the corresponding ODR bit. This is an atomic operation that doesn't require the three-step read-modify-write process.

`HAL_GPIO_ReadPin()` operates on the IDR register, which is read-only and directly returns the pin level.

| | Output (LED) | Input (Button) |
|---|-----------|-----------|
| Initialization | `GPIO_MODE_OUTPUT_PP` | `GPIO_MODE_INPUT` |
| Core operation | `HAL_GPIO_WritePin()` → BSRR | `HAL_GPIO_ReadPin()` → IDR |
| Register attribute | BSRR write-only | IDR read-only |
| Operation time | 1 clock cycle | 1 clock cycle |

---

## read_pin_state(): Our C++ Wrapper

In `device/gpio/gpio.hpp`, we added the `read_pin_state()` method to the GPIO template class:

```cpp
[[nodiscard]] State read_pin_state() const {
    return static_cast<State>(HAL_GPIO_ReadPin(native_port(), PIN));
}
```

There are a few design decisions here that need explaining.

### Why Return a State Enum Instead of bool

You could argue that returning a `bool` is simpler—`true` is high, `false` is low. But we chose to return the `State` enum (`State::Set` and `State::UnSet`) to maintain symmetry with the output side's `set_gpio_pin_state(State)`. This way, input and output use the same set of types, keeping the code style consistent.

Furthermore, the `State` enum is less prone to misuse than `bool`. If you're working with multiple pins, the meaning of `bool`'s `true`/`false` might get confused in different contexts—does `true` mean pressed or released? It depends on whether you're using pull-up or pull-down. But `State::Set` always means the pin is high, and `State::UnSet` always means it's low, with no ambiguity.

### Why Add [[nodiscard]]

`[[nodiscard]]` tells the compiler that the return value of this function should not be ignored. If you write `button.read_pin_state();` without using the return value, the compiler will issue a warning.

The sole purpose of reading a pin state is to get the return value. If you call `read_pin_state()` and don't use the result, the call is one hundred percent a mistake—most likely a forgotten assignment statement. In embedded development, if such a low-level error isn't caught, it could lead to the button state not being detected, causing abnormal system behavior that is difficult to debug.

### Zero Overhead of static_cast

`HAL_GPIO_ReadPin()` returns a `GPIO_PinState` (0 or 1), and `static_cast<State>()` converts it to a `State::Set` or `State::UnSet`. `static_cast` conversion between enums is a purely compile-time operation—the underlying value (0 or 1) doesn't change, only the type information does. The generated machine code is exactly the same as using `GPIO_PinState` directly.

### const Member Function

`read_pin_state()` is declared as `const`—it doesn't modify any of the object's member variables. This is the standard C++ way to express a "read-only operation." In contrast, `set_gpio_pin_state()` is also declared as `const`—this is because our GPIO template class has no member variables to modify; all "state" exists in the hardware registers, not in the C++ object.

---

## A Minimal C Example

Before moving on to the complete polling program in the next article, let's first verify with a minimal C code snippet: can we read the button state?

```c
#include "stm32f1xx_hal.h"

int main(void) {
    HAL_Init();
    /* 系统时钟配置省略 */

    /* 使能 GPIOA 时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 配置 PA0 为上拉输入 */
    GPIO_InitTypeDef init = {0};
    init.Pin = GPIO_PIN_0;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &init);

    /* 同时配置 PC13 为推挽输出（控制 LED） */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef led_init = {0};
    led_init.Pin = GPIO_PIN_13;
    led_init.Mode = GPIO_MODE_OUTPUT_PP;
    led_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &led_init);

    while (1) {
        /* 读取 PA0 状态 */
        GPIO_PinState state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

        if (state == GPIO_PIN_RESET) {
            /* 按钮按下：低电平 → 点亮 LED（PC13 低电平有效） */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        } else {
            /* 按钮松开：高电平 → 熄灭 LED */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
    }
}
```

This code does four things: (1) enables the GPIOA and GPIOC clocks, (2) configures PA0 as pull-up input, (3) configures PC13 as push-pull output, and (4) reads PA0 and controls PC13 in the main loop.

⚠️ Note: this code **does not debounce**. If you quickly press the button, the LED might blink several times. In the next article, we will see a full demonstration of this problem and its solution.

If you flash this code to the board, the LED turns on when you hold the button and turns off when you release it. The most basic input-output interaction is now realized.

---

## Looking Back

This article broke down two HAL APIs: the input mode configuration of `HAL_GPIO_Init()` and the underlying implementation of `HAL_GPIO_ReadPin()`. The key takeaways are:

1. Input initialization only requires two parameters: `GPIO_MODE_INPUT` + `GPIO_PULLUP`
2. `HAL_GPIO_ReadPin()` simply reads the `IDR` register underneath, taking one clock cycle
3. Our `read_pin_state()` wrapper adds `[[nodiscard]]` and `const`, returning a type-safe `State` enum

In the next article, we'll expand this minimal code into a complete C polling program—and see firsthand what happens without debouncing.
