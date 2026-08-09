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
title: '**Part 11: HAL_GPIO_WritePin and TogglePin — Making Pins Move**'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/06-hal-gpio-output.md
  source_hash: bdbcf6d8feb6a72b6a1e056ee0ea9473e30501ced12d46750a4f9486bec547d8
  translated_at: '2026-06-16T10:15:24.099197+00:00'
  engine: anthropic
  token_count: 1548
---
# Part 11: HAL_GPIO_WritePin and TogglePin — Making Pins Move

> Following up on the previous part: the pins are configured, the clock is enabled, and push-pull output is ready. Now we need the final step—telling the pin to "output high" or "output low." This is the job of `HAL_GPIO_WritePin()` and `HAL_GPIO_TogglePin()`.

---

## Our Goal

Thanks to the efforts in the previous parts, the GPIOC clock is enabled, and PC13 is configured for push-pull output mode. The pin is now "standing at attention" waiting for commands. However, we haven't issued any instructions yet—so the LED remains off. In this part, we will solve this final step: how to make the pin output the logic level we want.

---

## HAL_GPIO_WritePin — Directly Controlling Pin Levels

This is the most basic pin control function provided by the HAL library. Let's first look at its full signature:

```c
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
```

We have encountered all three parameters in previous articles. Now, let's examine them together. The first parameter, `GPIO_TypeDef *GPIOx`, is the port pointer that tells the HAL which port to operate on—GPIOA, GPIOB, or GPIOC. The second parameter, `uint16_t GPIO_Pin`, is the pin bit mask that specifies the exact pin. The third parameter, `GPIO_PinState PinState`, has only two possible values: `GPIO_PIN_SET` (high level, value is 1) and `GPIO_PIN_RESET` (low level, value is 0).

For the on-board LED on our Blue Pill (PC13, active low), turning the LED on requires a low level output, while turning it off requires a high level output:

```c
// 点亮LED —— PC13输出低电平
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

// 熄灭LED —— PC13输出高电平
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
```

One point of confusion here: "turning on the LED" corresponds to `GPIO_PIN_RESET` (low level), not `GPIO_PIN_SET` as intuition might suggest. This is because the PC13 LED circuit on the Blue Pill is active-low, a detail we analyzed in depth in Part 3 (Push-Pull, Open-Drain, and PC13). If you accidentally swap SET and RESET, the LED behavior will be completely inverted—"on" becomes "off," and "off" becomes "on." That said, this doesn't affect program execution; it's just a logical inversion.

---

## BSRR Register — The Hero Behind Atomic Operations

The underlying implementation of `HAL_GPIO_WritePin` is quite elegant and worth a closer look. It doesn't operate on the ODR (Output Data Register), but rather on the BSRR (Bit Set/Reset Register). The design of the BSRR is a major highlight of the ARM Cortex-M series:

```c
// HAL_GPIO_WritePin 的实现（简化版）
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    if (PinState != GPIO_PIN_RESET) {
        GPIOx->BSRR = GPIO_Pin;                    // 低16位：设置
    } else {
        GPIOx->BSRR = (uint32_t)GPIO_Pin << 16U;   // 高16位：清除
    }
}
```

BSRR is a 32-bit write-only register with a very clever design. The lower 16 bits (bit 0 to bit 15) are used to set the corresponding ODR bits—writing 1 to bit 13 sets ODR bit 13 to 1 (output high). The upper 16 bits (bit 16 to bit 31) are used to clear the corresponding ODR bits—writing 1 to bit 29 (which is bit 13 shifted left by 16) clears ODR bit 13 to 0 (output low).

Taking PC13 as an example, the value of `GPIO_PIN_13` is `0x2000` (bit 13 is 1). When we need to output a high level, we write `GPIOC->BSRR = 0x2000`, which sets ODR bit 13 to 1. When we need to output a low level, we write `GPIOC->BSRR = 0x2000 << 16 = 0x20000000`, which clears ODR bit 13 to 0.

Why not write to ODR directly? Because ODR is a 16-bit read-write register. If we modify a specific bit using a "read-modify-write" sequence, an interrupt might occur between the read and write operations. The interrupt service routine (ISR) could modify another bit on the same port, and our subsequent write-back would overwrite the interrupt's changes. BSRR avoids this problem through its "write-1-to-activate" design: setting and clearing are two independent bit fields, and the write operation is atomic, eliminating the need for the read-modify-write sequence. This means that even if multiple interrupts operate on different pins of the same port simultaneously, they will not interfere with each other.

---

## HAL_GPIO_TogglePin — Toggling Pin Levels

Sometimes we do not need to care about the current level; we simply want to toggle it—high to low, or low to high. In such cases, using `HAL_GPIO_TogglePin` is more convenient:

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```

It takes only two parameters—the port and the pin—without needing to specify the target logic level. The underlying implementation is also straightforward:

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIOx->ODR ^= GPIO_Pin;   // 异或操作翻转对应位
}
```

The XOR operation has the property that XORing with 0 leaves a bit unchanged, while XORing with 1 flips it. Therefore, `ODR ^= GPIO_PIN_13` only flips bit 13 of the ODR, leaving other bits unaffected.

⚠️ **Note:** Unlike BSRR, the "read-modify-write" operation of `TogglePin` is not atomic. If an interrupt occurs between the read and write of the ODR, and the interrupt service routine (ISR) modifies other pins on the same port, issues could theoretically arise. However, for simple scenarios like LED blinking, there is no need to worry—LEDs do not require atomicity guarantees.

---

## HAL_Delay — The Source of Time

LED blinking requires a delay, so we use `HAL_Delay()`:

```c
HAL_Delay(500);   // 延时500毫秒
```

The implementation of `HAL_Delay` relies on the SysTick timer. SysTick is a built-in 24-bit decrementing counter in the Cortex-M3 core, clocked by HCLK (64 MHz in our configuration). `HAL_Init()` configures SysTick to generate an interrupt every 1 ms, incrementing a global counter named `uwTick` on each interrupt. `HAL_Delay()` determines if the specified number of milliseconds has elapsed by polling this counter.

This is why we must call `HAL_Init()` first in `main.cpp`—without it, SysTick is not configured, `HAL_Delay()` will not work at all, and your program will hang inside the delay function forever.

---

## Complete C-Style LED Blinking Program

Now, let's combine all the HAL APIs we discussed and write a complete C-style LED blinking program. This serves as a full demonstration of the "pure HAL approach" in this series and acts as the starting point for our subsequent C++ refactoring:

```c
#include "stm32f1xx_hal.h"

/* 时钟配置：HSI -> PLL -> 64MHz */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL = RCC_PLL_MUL16;
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

/* LED初始化：使能时钟 + 配置PC13为推挽输出 */
void led_init(void) {
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_13;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
}

/* LED点亮：PC13输出低电平 */
void led_on(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
}

/* LED熄灭：PC13输出高电平 */
void led_off(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

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

Let's walk through this program section by section. First, `SystemClock_Config()` configures the system clock to 64 MHz. The HSI (8 MHz internal oscillator) is multiplied by the PLL (/2 × 16 = 64 MHz) to serve as SYSCLK. Then, the AHB bus runs without division, APB1 is divided by two to 32 MHz, and APB2 remains undivided at 64 MHz. This code corresponds to the `setup_system_clock()` method in `system/clock.cpp` in our project.

Next is `led_init()`, which does two things: first, it calls `__HAL_RCC_GPIOC_CLK_ENABLE()` to enable the clock for GPIOC (this is the first major pitfall discussed in Article 4), and then it configures PC13 as push-pull output, without pull-up or pull-down resistors, and at low speed. This function does exactly the same thing as the `setup()` method in `gpio.hpp` in our project.

Finally, `led_on()` and `led_off()` call `HAL_GPIO_WritePin` to output a low level and a high level, respectively. Note that `led_on()` passes `GPIO_PIN_RESET` (low level) because the PC13 LED on the Blue Pill is active-low.

The logic of the `main()` function is straightforward: initialize the HAL library and the clock, initialize the LED pin, and then toggle the LED on and off in an infinite loop with a 500 ms interval.

---

## Compiling and Flashing

If you have followed the env_setup series, compiling and flashing should be very familiar by now:

```bash
mkdir build && cd build
cmake ..
make
make flash
```

If you use the `CMakeLists.txt` from our project, the firmware size will be displayed automatically after compilation:

```text
   text    data     bss     dec     hex filename
   1234     120       4    1358     54e stm32_demo.elf
```

After flashing successfully, you should see the LED on the Blue Pill board blinking steadily with a period of one second (500 ms on + 500 ms off).

If the LED does not respond at all, follow this troubleshooting sequence: first, verify that the ST-Link connection is normal (SWDIO, SWCLK, and GND lines); second, confirm that the clock configuration is correct (use the debugger to read the `RCC_CFGR` register); third, ensure that the GPIOC clock is enabled (read bit 4 of `RCC_APB2ENR`); and fourth, verify that PC13 is configured as an output (read bits [23:20] of `GPIOC_CRH`).

---

## Where We Are Now

At this point, we have mastered the three core GPIO APIs of the HAL library: `__HAL_RCC_GPIOx_CLK_ENABLE()` to enable the clock, `HAL_GPIO_Init()` to configure the pin, and `HAL_GPIO_WritePin()`/`HAL_GPIO_TogglePin()` to control the logic level. These three APIs are sufficient to control the LED blinking.

However, if you look back at the code above, you will notice a problem: this code is hard-bound to PC13. The constants `GPIOC`, `GPIO_PIN_13`, and `__HAL_RCC_GPIOC_CLK_ENABLE()` are scattered across three different functions. If you want to move the LED to PA0, you need to modify three places—and you must get all three right; missing just one will cause it to fail.

In the next article, we will analyze the problems with this C-style coding approach, see how it gradually leads to "unmaintainable" code, and lay the groundwork for the subsequent C++ refactoring.
