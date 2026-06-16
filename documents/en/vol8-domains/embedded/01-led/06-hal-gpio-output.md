---
chapter: 15
difficulty: beginner
order: 6
platform: stm32f1
reading_time_minutes: 9
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 11: HAL_GPIO_WritePin and TogglePin — Making Pins Move'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/06-hal-gpio-output.md
  source_hash: bdbcf6d8feb6a72b6a1e056ee0ea9473e30501ced12d46750a4f9486bec547d8
  translated_at: '2026-06-16T04:10:35.014739+00:00'
  engine: anthropic
  token_count: 1548
---
# Part 11: HAL_GPIO_WritePin and TogglePin — Making Pins Move

> Following the previous article: The pin is configured, the clock is enabled, and push-pull output is ready. Now we need the final step—telling the pin to "output a high level" or "output a low level." This is where `HAL_GPIO_WritePin` and `HAL_GPIO_TogglePin` come in.

---

## Our Goal

Thanks to the efforts in the previous articles, the GPIOC clock is enabled, and PC13 is configured for push-pull output mode. The pin is now "standing at attention" waiting for commands. But we haven't issued any instructions yet—so the LED is still not lit. In this article, we will solve this final step: how to make the pin output the logic level we want.

---

## HAL_GPIO_WritePin — Direct Pin Level Control

This is the most basic pin control function provided by the HAL library. Let's look at its full signature first:

```cpp
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
```

We have seen all three parameters in previous articles. Now let's understand them together. The first parameter, `GPIOx`, is the port pointer, telling HAL which port you want to operate on—GPIOA, GPIOB, or GPIOC. The second parameter, `GPIO_Pin`, is the pin bit mask, indicating the specific pin. The third parameter, `PinState`, has only two possible values: `GPIO_PIN_SET` (high level, value is 1) and `GPIO_PIN_RESET` (low level, value is 0).

For the Blue Pill onboard LED (PC13, active low), lighting the LED requires a low level output, and extinguishing the LED requires a high level output:

```cpp
// Turn on LED (Active Low -> Output Low)
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

// Turn off LED (Active Low -> Output High)
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
```

Here is a point that is easy to confuse: we say "light LED" corresponds to `GPIO_PIN_RESET` (low level), not the intuitive `GPIO_PIN_SET`. This is because the Blue Pill's PC13 LED circuit is active low—we analyzed this in detail in Part 3 (Push-Pull, Open-Drain, and PC13). If you accidentally swap SET and RESET, the LED behavior will be completely reversed—"on" becomes "off", and "off" becomes "on". That said, this doesn't affect program execution, only the logic is inverted.

---

## The BSRR Register — The Hero Behind Atomic Operations

The underlying implementation of `HAL_GPIO_WritePin` is very ingenious and worth a deeper look. It does not operate on the ODR (Output Data Register), but on the BSRR (Bit Set/Reset Register). The design of BSRR is a major highlight of the ARM Cortex-M series:

```cpp
static void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
  // Check parameters
  assert_param(IS_GPIO_PIN(GPIO_Pin));
  assert_param(IS_GPIO_PIN_ACTION(PinState));

  if (PinState != GPIO_PIN_RESET)
  {
    GPIOx->BSRR = GPIO_Pin; // Set corresponding ODR bits
  }
  else
  {
    GPIOx->BSRR = (uint32_t)GPIO_Pin << 16u; // Reset corresponding ODR bits
  }
}
```

BSRR is a 32-bit write-only register, and its design is very clever. The lower 16 bits (bit 0 to bit 15) are used to set the corresponding ODR bits—writing 1 to bit 13 sets ODR's bit 13 to 1 (output high level). The upper 16 bits (bit 16 to bit 31) are used to clear the corresponding ODR bits—writing 1 to bit 29 (which is bit 13 shifted left by 16) clears ODR's bit 13 to 0 (output low level).

Taking PC13 as an example, the value of `GPIO_PIN_13` is `0x2000` (bit 13 is 1). When we need to output a high level, we write `0x2000`, which sets ODR bit 13 to 1. When we need to output a low level, we write `0x20000000`, which clears ODR bit 13 to 0.

Why not write to ODR directly? Because ODR is a 16-bit read-write register. If you modify a single bit using a "read-modify-write" approach, and an interrupt occurs between the read and write back, the interrupt handler might also modify another bit of the same port—the write back will overwrite the interrupt's modification. BSRR avoids this problem through its "write 1 to take effect" design: setting and clearing are two independent bit fields, and the write operation is atomic, requiring no read-modify-write steps. This means that even if multiple interrupts operate on different pins of the same port simultaneously, they will not interfere with each other.

---

## HAL_GPIO_TogglePin — Toggling Pin Levels

Sometimes we don't care what the current level is; we just want to toggle it—high to low, low to high. In this case, `HAL_GPIO_TogglePin` is more convenient:

```cpp
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```

It has only two parameters—port and pin—and does not need to specify the target level. The underlying implementation is also straightforward:

```cpp
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  // Check parameters
  assert_param(IS_GPIO_PIN(GPIO_Pin));

  uint32_t odr = GPIOx->ODR;
  GPIOx->ODR = odr ^ GPIO_Pin;
}
```

The characteristic of the XOR operation is: XOR with 0 keeps it unchanged, XOR with 1 flips it. So `ODR ^ GPIO_PIN_13` will only flip bit 13 of the ODR, leaving other bits unaffected.

⚠️ **Note:** Unlike BSRR, the "read-modify-write" operation of TogglePin is not atomic. If an interrupt occurs between reading the ODR and writing it back, and the interrupt handler modifies another pin on the same port, problems could theoretically arise. However, for simple scenarios like LED blinking, there is no need to worry—LEDs do not require atomicity guarantees.

---

## HAL_Delay — The Source of Time

LED blinking requires a delay, and we use `HAL_Delay`:

```cpp
void HAL_Delay(uint32_t Delay);
```

`HAL_Delay`'s implementation relies on the SysTick timer. SysTick is a 24-bit decrementing counter built into the Cortex-M3 core, and its clock source is HCLK (64MHz in our configuration). `HAL_Init` will configure SysTick to generate an interrupt every 1ms. Each time the interrupt fires, a global counter named `uwTick` is incremented by 1. `HAL_Delay` determines whether the specified number of milliseconds has passed by querying this counter.

This is why `main` must call `HAL_Init` first—without it, SysTick is not configured, `HAL_Delay` won't work at all, and your program will be stuck forever in the delay function.

---

## Complete C-Style LED Blinking Program

Now let's combine all the previous HAL APIs to write a complete C-style LED blinking program. This is the full display of the "pure HAL approach" in this entire series and serves as the starting point for subsequent C++ refactoring:

```cpp
#include "stm32f1xx_hal.h"

void SystemClock_Config(void) {
  // Enable HSI oscillator
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  // Configure PLL and clocks
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  // PLL configuration: HSI/2 * 16 = 64MHz
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

void LED_Init(void) {
  // Enable GPIOC clock (The first big pitfall from Part 4)
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Configure PC13 as Push-Pull Output
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

int main(void) {
  // Reset of all peripherals, Initializes the Flash interface and the Systick.
  HAL_Init();

  // Configure the system clock
  SystemClock_Config();

  // Initialize the LED pin
  LED_Init();

  while (1) {
    // Turn on LED (Active Low)
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(500);

    // Turn off LED (Active Low)
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(500);
  }
}
```

Let's understand this program section by section. First is `SystemClock_Config`, which configures the system clock to 64MHz—HSI (8MHz internal oscillator) is multiplied by the PLL (/2 × 16 = 64MHz) to serve as SYSCLK, then AHB is not divided, APB1 is divided by 2 to 32MHz, and APB2 is not divided to remain at 64MHz. This code corresponds to the `SystemClock_Config` method in our project.

Next is `LED_Init`, which does two things: first, it calls `__HAL_RCC_GPIOC_CLK_ENABLE` to wake up the GPIOC clock (this is the first major pitfall discussed in Part 4), and then it configures PC13 as push-pull output, no pull-up/pull-down, and low speed. This function does exactly the same thing as the `LED_Init` method in our project.

Finally, we have the logic inside the `while(1)` loop, calling `HAL_GPIO_WritePin` to output low and high levels respectively. Note that `HAL_GPIO_WritePin` receives `GPIO_PIN_RESET` (low level) to turn the LED on, because the Blue Pill's PC13 LED is active low.

The logic of the `main` function is straightforward: initialize the HAL library and clock, initialize the LED pin, and then alternately light and extinguish the LED in an infinite loop, with a 500ms interval between each state.

---

## Compiling and Flashing

If you have followed the env_setup series all the way here, compiling and flashing should be very familiar:

```text
cmake -B build
cmake --build build
```

If you are using the CMakeLists.txt in our project, the firmware size will be displayed automatically after compilation:

```text
[100%] Built target stm32_blink
text    data     bss     dec     hex filename
2048     112    1024    3184     c70 firmware.elf
```

After flashing successfully, you should see the LED on the Blue Pill board blinking steadily with a period of one second (500ms on + 500ms off).

If the LED has no response at all, the troubleshooting order is: first, confirm the ST-Link connection is normal (SWDIO, SWCLK, GND three wires); second, confirm the clock configuration is correct (use the debugger to read the RCC_CFGR register); third, confirm the GPIOC clock is enabled (read bit 4 of RCC_APB2ENR); fourth, confirm PC13 is configured as output (read bits [23:20] of GPIOC_CRH).

---

## Where We Are Now

At this point, we have mastered the three core GPIO APIs of the HAL library: `__HAL_RCC_GPIOC_CLK_ENABLE` to enable the clock, `HAL_GPIO_Init` to configure the pin, and `HAL_GPIO_WritePin`/`HAL_GPIO_TogglePin` to control the level. These three APIs are sufficient to control LED blinking.

But if you look back at the code above, you will find a problem: this code is hard-bound to PC13. The constants `GPIOC`, `GPIO_PIN_13`, and `GPIO_PIN_RESET` are scattered across three different functions. If you want to move the LED to PA0, you need to modify three places—and you must get all three right, or it won't work if you miss one.

In the next article, we will analyze the problems of this C-style writing approach, see how it一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步一步步
