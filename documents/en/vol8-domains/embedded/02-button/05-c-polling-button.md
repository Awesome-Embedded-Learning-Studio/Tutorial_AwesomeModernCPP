---
chapter: 16
difficulty: intermediate
order: 5
platform: stm32f1
reading_time_minutes: 10
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part篇 23: Polling Buttons in C — Your First Time Controlling an LED with a
  Button'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/02-button/05-c-polling-button.md
  source_hash: e0b865c83a896f18c013c16013f418aecdd2aa531cc5438c68a323a299953b9c
  token_count: 1844
  translated_at: '2026-05-26T12:12:14.955131+00:00'
description: ''
---
# Part 23: C Language Button Polling — Making a Button Control an LED for the First Time

In the previous four articles, we covered everything from circuit principles to the HAL library's GPIO input APIs. Now it is time to tie all that knowledge together and write a program that actually runs.

The goal of this article is straightforward: **write a complete button-controlled LED program in pure C, flash it to the board, and see firsthand just how severe mechanical bounce really is.** We add no debounce, use no clever tricks—just the most basic "read pin → write pin" approach. Only by seeing the problem first will we understand why we need to solve it later.

---

## 1. The Complete C Code

Let's not worry about debouncing or state machines for now—our goal today is simply to wire up the circuit, write the code correctly, and make the LED follow the button. We get things moving first, and optimize later.

### Hardware Wiring Recap

| Pin  | Function     | Connection                                       |
|------|--------------|--------------------------------------------------|
| PA0  | Button input | One end to GND, the other end to PA0             |
| PC13 | LED output   | Onboard LED (active low)                         |

PA0 is configured in **pull-up input** mode. When the button is not pressed, a pull-up resistor holds PA0 high; when the button is pressed, PA0 is shorted directly to GND, and we read a low level.

### Complete Code

Below is a complete, compilable, and flashable `main.c`. Every line is commented so that you know exactly what each step does.

```c
#include "stm32f1xx_hal.h"

/* ============================================
 * 按钮控制 LED —— 纯 C 轮询版本（无消抖）
 * PA0  : 按钮输入（上拉，按下为低电平）
 * PC13 : 板载 LED（推挽输出，低电平点亮）
 * ============================================ */

/**
 * @brief 系统时钟配置
 *        STM32F103C8T6 外部晶振 8MHz，倍频到 72MHz
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef        RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef        RCC_ClkInitStruct = {0};

    /* 开启外部高速晶振 (HSE) */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;   /* 8MHz × 9 = 72MHz */
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /* 配置系统时钟来源为 PLL */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK
                                     | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1
                                     | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK  = 72MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;     /* PCLK1 = 36MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;     /* PCLK2 = 72MHz */
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

/**
 * @brief GPIO 初始化
 *        PA0  -> 上拉输入（按钮）
 *        PC13 -> 推挽输出（LED）
 */
void GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 第一步：使能 GPIOA 和 GPIOC 的时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* 第二步：配置 PA0 为上拉输入 */
    GPIO_InitStruct.Pin   = GPIO_PIN_0;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;     /* 输入模式 */
    GPIO_InitStruct.Pull  = GPIO_PULLUP;         /* 内部上拉电阻 */
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 第三步：配置 PC13 为推挽输出 */
    GPIO_InitStruct.Pin   = GPIO_PIN_13;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP; /* 推挽输出 */
    GPIO_InitStruct.Pull  = GPIO_NOPULL;         /* 无上下拉 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; /* 低速就够了 */
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
 * @brief 主函数
 */
int main(void)
{
    /* HAL 库初始化（必须放在最前面） */
    HAL_Init();

    /* 配置系统时钟到 72MHz */
    SystemClock_Config();

    /* 初始化 GPIO */
    GPIO_Init();

    /* ====== 主循环：轮询按钮状态 ====== */
    while (1)
    {
        /* 读取 PA0 的电平
         * 按钮按下 -> PA0 为低电平 -> GPIO_PIN_RESET
         * 按钮松开 -> PA0 为高电平 -> GPIO_PIN_SET
         */
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
        {
            /* 按钮按下：点亮 LED（PC13 输出低电平） */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        }
        else
        {
            /* 按钮松开：熄灭 LED（PC13 输出高电平） */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        }
    }
}
```

The code structure is very clear, focusing on three things: **initialize the clock, configure the pins, and repeatedly read the button state in the main loop.** There is no debouncing logic whatsoever—just the most straightforward polling.

> If you are still not entirely familiar with parameters like `HAL_GPIO_ReadPin` and `GPIO_PULLUP`, go back and review the API details in [Part 04](./04-hal-gpio-input.md), where every parameter is explained.

---

## 2. Flashing and Running: It Looks Normal... Or Does It?

Compile and flash the code to the board. Press and hold the button—the LED turns on. Release the button—the LED turns off. Looks like everything is working fine?

Don't celebrate just yet. Try this: **press the button as quickly as you can and release it immediately.**

You will most likely notice that sometimes the LED state is wrong—you clearly intended to press it only once, but the LED behaves as if you pressed it several times. Sometimes it turns on and then off, off and then on again, or it doesn't react at all.

### Quantifying the Problem with a Counter

Talk is cheap, so let's use a counter to quantify just how severe the bounce is. Add one line inside the `if` branch:

```c
/* 在 main() 开头添加一个计数器 */
uint32_t press_count = 0;

/* 修改主循环 */
while (1)
{
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
    {
        /* 每次检测到"按下"，计数器加 1 */
        press_count++;
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
}
```

Then, set a breakpoint in debug mode and read the value of `press_count`.

**You clearly pressed the button only once, but `press_count` might show 3, 5, or even more than 8.**

This is the direct manifestation of mechanical bounce at the software level. To the naked eye, you only pressed once, but the MCU sampled multiple press-release oscillations.

---

## 3. Why Does It Trigger Multiple Times?

Do you remember the bounce waveform diagram from [Part 03](./03-button-hardware-and-bounce.md)? The moment the button is pressed or released, the contacts do not cleanly transition "from 0 to 1" or "from 1 to 0." Instead, they bounce back and forth between high and low levels for approximately **5 to 20 milliseconds**.

The problem lies right in this time difference.

Let's do the math:

- The `SystemClock_Config` above configures a 72MHz system clock (note: the `clock.cpp` in the project template uses HSI multiplied to 64MHz; here we use the more common HSE 72MHz approach for demonstration, but the calculation principle is the same).
- The work done in the main loop is simple: read a pin, evaluate a condition, and write a pin. The entire loop body consumes roughly **a few dozen clock cycles**—let's estimate 100.
- Therefore, the main loop executes approximately once every **1.4 microseconds** (about 1.6 microseconds at 64MHz, same order of magnitude).
- During a 10-millisecond bounce period, the CPU can run approximately **7,000 loop iterations**.

Among these 7,000 samples, every "false transition" generated by the bounce—even if it only lasts a few microseconds—will be faithfully captured by `HAL_GPIO_ReadPin`. If your code in the `if` branch toggles the LED instead of simply setting it high or low, the multiple toggles caused by the bounce will be directly reflected on the LED: you press once, and the LED blinks three or four times.

```text
理想信号：  ─────────────┐             ┌──────────────
                         │             │
                         └─────────────┘
                        按下           松开

实际信号：  ─────────────┐ ┌┐┌┐┌┐     ┌┌┌┐┌──────────
                         │ ││││││     │││││
                         └─┘└┘└┘└─────┘└┘└┘
                      ↑                ↑
                   按下瞬间抖动      松开瞬间抖动
                   持续 5~20ms      持续 5~20ms
```

The MCU's sampling speed is simply too fast—fast enough to read the pin thousands of times within a few milliseconds of bounce. **There is nothing wrong with our code; the problem lies in the physical characteristics of the button itself.** Therefore, debouncing is not a "nice-to-have" but an absolute necessity for button inputs.

---

## 4. The Simplest Debounce Attempt: HAL_Delay

Since the problem is that "sampling is too fast and false transitions during the bounce period are captured multiple times," the most direct approach is: **after detecting a press, wait a while and read again, confirming the level has stabilized before deciding if it is a real press.**

The simplest way to "wait a while" is `HAL_Delay`:

```c
/* 带 HAL_Delay 消抖的版本 */
while (1)
{
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
    {
        /* 第一次检测到低电平，等待 20ms 消抖 */
        HAL_Delay(20);

        /* 再读一次，确认电平仍然是低 */
        if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
        {
            /* 确认：按钮确实按下了 */
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        }
    }
    else
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
}
```

The logic is clear:

1. First read a low level → it might be bounce, or it might be a real press, so don't rush.
2. Wait 20 milliseconds → the bounce has long since ended.
3. Read again → if it is still low, it is a real press; if it has returned to high, then the previous reading was just bounce.

Flash and try it—sure enough, a quick press of the button now only turns the LED on once. The counter is normal too. Problem solved?

**Only half of it.**

### The Problem with This Approach

The essence of `HAL_Delay` is making the CPU spin empty in a `while` loop, repeatedly checking whether the SysTick timer has reached the target time. During these 20 milliseconds, the CPU cannot do any meaningful work—it is "blocked."

If your project only has one button and one LED, blocking for 20ms might not be a big deal. But imagine these scenarios:

- You also need to read a temperature sensor in the main loop, and the sampling interval must be precise to 1ms.
- You are receiving data over a serial port, and the buffer might overflow during these 20ms.
- You have an OLED screen refreshing at 60fps, and a 20ms stutter will cause screen tearing.

In a slightly more complex project, **blocking debounce is a ticking time bomb.** It makes the entire system's response unpredictable.

> ⚠️ **Warning**: In production projects, never use blocking debounce in the main loop. It looks simple and effective, but as features increase, it will become the biggest source of instability in the system.

### So, What Do We Do?

The idea is simple: **don't block the CPU; record the time instead.** Each time a level change is detected, instead of waiting, we note the current moment. The next time the loop reads a change, we check "how long has passed since the last change." Only if it has been more than 20ms do we consider the level to be truly stable.

This is the idea behind **non-blocking debounce**—it requires using the SysTick timer or a hardware timer, and we will save the detailed implementation for the next article.

---

## Summary

In this article, we did three things:

1. **Wrote our first complete button-controlled LED program**, going from clock configuration and GPIO initialization to main loop polling in one go.
2. **Saw the harm of mechanical bounce with our own eyes**—a single press was sampled as multiple triggers, and we quantified this problem with a counter.
3. **Tried the simplest debounce approach** (`HAL_Delay`), understood that it solves the problem but blocks the CPU, which led to the need for non-blocking debounce.

Now you know the "why" behind button debouncing and the "simplest how." In the next article, we will implement a truly engineering-grade non-blocking debounce solution—one that doesn't block the CPU, doesn't sacrifice real-time performance, and doesn't require as much code as you might think.
