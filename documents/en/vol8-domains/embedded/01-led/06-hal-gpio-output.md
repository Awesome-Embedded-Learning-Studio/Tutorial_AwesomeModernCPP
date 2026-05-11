# Part 11: HAL_GPIO_WritePin and TogglePin — Making Pins Move

> Picking up from the previous article: the pin is configured, the clock is enabled, and push-pull output is ready. Now we need the final step — telling the pin to "output high" or "output low." This is the job of `HAL_GPIO_WritePin()` and `HAL_GPIO_TogglePin()`.

---

## Our Goal

Thanks to our efforts in the previous articles, the GPIOC clock is enabled and PC13 is configured for push-pull output. The pin is now at attention, waiting for orders. But we haven't issued any commands yet — so the LED remains off. In this article, we tackle that final step: how to make the pin output the level we want.

---

## HAL_GPIO_WritePin — Direct Pin Level Control

This is the most basic pin control function provided by the HAL library. Let's look at its full signature:

```c
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
```

We've seen all three parameters in previous articles, but now let's understand them together. The first parameter, `GPIO_TypeDef *GPIOx`, is the port pointer, telling HAL which port to operate on — GPIOA, GPIOB, or GPIOC. The second parameter, `uint16_t GPIO_Pin`, is the pin bit mask, specifying the exact pin. The third parameter, `GPIO_PinState PinState`, has only two possible values: `GPIO_PIN_SET` (high level, value 1) and `GPIO_PIN_RESET` (low level, value 0).

For our Blue Pill onboard LED (PC13, active-low), turning on the LED requires a low level, and turning it off requires a high level:

```c
// 点亮LED —— PC13输出低电平
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

// 熄灭LED —— PC13输出高电平
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
```

Note an easy point of confusion here: "turning on the LED" corresponds to `GPIO_PIN_RESET` (low level), not the intuitive `GPIO_PIN_SET`. This is because the Blue Pill's PC13 LED circuit is active-low — we analyzed this in detail in Part 3 (Push-Pull, Open-Drain, and PC13). If you accidentally swap SET and RESET, the LED behavior will be completely inverted — "on" becomes "off," and "off" becomes "on." That said, this doesn't affect program execution; it's just a logical inversion.

---

## The BSRR Register — The Unsung Hero of Atomic Operations

The underlying implementation of `HAL_GPIO_WritePin` is quite elegant and worth a closer look. It doesn't operate on the ODR (Output Data Register), but rather on the BSRR (Bit Set/Reset Register). The BSRR design is a major highlight of the ARM Cortex-M series:

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

The BSRR is a 32-bit write-only register with a very clever design. The lower 16 bits (bit0 to bit15) set the corresponding ODR bits — writing 1 to bit13 sets ODR's bit13 to 1 (output high). The upper 16 bits (bit16 to bit31) clear the corresponding ODR bits — writing 1 to bit29 (that is, bit13 shifted left by 16) clears ODR's bit13 to 0 (output low).

Taking PC13 as an example, the value of `GPIO_PIN_13` is `0x2000` (bit 13 is 1). When we need to output a high level, we write `GPIOC->BSRR = 0x2000`, which sets ODR's bit 13 to 1. When we need to output a low level, we write `GPIOC->BSRR = 0x2000 << 16 = 0x20000000`, which clears ODR's bit 13 to 0.

Why not write to the ODR directly? Because the ODR is a 16-bit read-write register. If we use a "read-modify-write" approach to change a single bit, an interrupt might occur between the read and the write-back. The interrupt service routine (ISR) might modify another bit on the same port — and the write-back would overwrite the interrupt's changes. The BSRR avoids this problem through its "write-1-to-actuate" design: setting and clearing are two independent bit fields, and the write operation is atomic, requiring no read-modify-write three-step dance. This means that even if multiple interrupts simultaneously operate on different pins of the same port, they won't interfere with each other.

---

## HAL_GPIO_TogglePin — Toggling the Pin Level

Sometimes we don't care about the current level; we just want to flip it — high to low, low to high. In these cases, `HAL_GPIO_TogglePin` is more convenient:

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```

It takes only two parameters — port and pin — with no need to specify a target level. The underlying implementation is also straightforward:

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIOx->ODR ^= GPIO_Pin;   // 异或操作翻转对应位
}
```

The property of the XOR operation is: XOR with 0 keeps the bit unchanged, XOR with 1 flips it. So `ODR ^= GPIO_PIN_13` only flips bit 13 of the ODR, leaving all other bits unaffected.

⚠️ Warning: Unlike the BSRR, TogglePin's "read-modify-write" operation is not atomic. If an interrupt occurs between reading the ODR and writing it back, and the ISR also modifies another pin on the same port, problems could theoretically arise. However, for a simple scenario like LED blinking, there's no need to worry — LEDs don't require atomicity guarantees.

---

## HAL_Delay — The Source of Time

LED blinking requires a delay, and we use `HAL_Delay()`:

```c
HAL_Delay(500);   // 延时500毫秒
```

The implementation of `HAL_Delay` relies on the SysTick timer. SysTick is a 24-bit down-counting timer built into the Cortex-M3 core, clocked by HCLK (64MHz in our configuration). `HAL_Init()` configures SysTick to generate an interrupt every 1ms, and a global counter named `uwTick` is incremented on each interrupt. `HAL_Delay()` simply polls this counter to determine whether the specified number of milliseconds has elapsed.

This is why `main.cpp` must call `HAL_Init()` first — without it, SysTick isn't configured, `HAL_Delay()` won't work at all, and your program will be stuck in the delay function forever.

---

## The Complete C-Style LED Blink Program

Now let's combine all the HAL APIs we've covered and write a complete C-style LED blink program. This is the full "pure HAL approach" demonstration in the entire series, and it serves as the starting point for our upcoming C++ refactoring:

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

Let's understand this program section by section. First is `SystemClock_Config()`, which configures the system clock to 64MHz — the HSI (8MHz internal oscillator) is multiplied by the PLL (/2 × 16 = 64MHz) to serve as SYSCLK, then the AHB is undivided, APB1 is divided by two to 32MHz, and APB2 remains undivided at 64MHz. This code corresponds to the `setup_system_clock()` method in `system/clock.cpp` in our project.

Next is `led_init()`, which does two things: it first calls `__HAL_RCC_GPIOC_CLK_ENABLE()` to wake up the GPIOC clock (the first major pitfall we discussed in Part 4), and then configures PC13 as push-pull output, with no pull-up or pull-down, at low speed. This function does exactly the same thing as the `setup()` method in `gpio.hpp` in our project.

Finally, we have `led_on()` and `led_off()`, which call `HAL_GPIO_WritePin` to output low and high levels respectively. Note that `led_on()` passes `GPIO_PIN_RESET` (low level) because the Blue Pill's PC13 LED is active-low.

The logic of the main function `main()` is straightforward: initialize the HAL library and clocks, initialize the LED pin, and then alternate between turning the LED on and off in an infinite loop, with a 500ms interval between each change.

---

## Building and Flashing

If you've been following along with the env_setup series, building and flashing should be quite familiar by now:

```bash
mkdir build && cd build
cmake ..
make
make flash
```

If you're using the CMakeLists.txt from our project, the firmware size will be displayed automatically after a successful build:

```text
   text    data     bss     dec     hex filename
   1234     120       4    1358     54e stm32_demo.elf
```

After flashing successfully, you should see the LED on the Blue Pill board blinking steadily with a one-second period (500ms on + 500ms off).

If the LED doesn't respond at all, the troubleshooting order is: first, confirm the ST-Link connection is normal (the three wires: SWDIO, SWCLK, GND); second, confirm the clock configuration is correct (use the debugger to read the RCC_CFGR register); third, confirm the GPIOC clock is enabled (read bit4 of RCC_APB2ENR); fourth, confirm PC13 is configured as output (read bits [23:20] of GPIOC_CRH).

---

## Where We Are Now

At this point, we've mastered the three core GPIO APIs of the HAL library: `__HAL_RCC_GPIOx_CLK_ENABLE()` to enable the clock, `HAL_GPIO_Init()` to configure the pin, and `HAL_GPIO_WritePin()`/`HAL_GPIO_TogglePin()` to control the level. These three APIs are sufficient for controlling an LED blink.

But if you look back at the code above, you'll notice a problem: this code is hard-bound to PC13. The three constants `GPIOC`, `GPIO_PIN_13`, and `__HAL_RCC_GPIOC_CLK_ENABLE()` are scattered across three different functions. If you want to move the LED to PA0, you need to change three places — and you must get all three right; missing even one will break it.

In the next article, we'll analyze the problems with this C-style approach, see how it一步步 leads to "hard to maintain" code, and lay the groundwork for our upcoming C++ refactoring.
