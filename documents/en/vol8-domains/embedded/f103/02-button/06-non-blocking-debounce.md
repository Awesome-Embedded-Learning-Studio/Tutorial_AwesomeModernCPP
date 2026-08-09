---
chapter: 16
difficulty: intermediate
order: 6
platform: stm32f1
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 24: Non-blocking Debounce — Keeping the CPU from Waiting'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/06-non-blocking-debounce.md
  source_hash: 25ccb48a6315cae61a9898b8d31b6c4156f8b8b6e9f249a1932075c08b2d3e5d
  translated_at: '2026-06-16T04:10:56.237850+00:00'
  engine: anthropic
  token_count: 1485
---
# Part 24: Non-blocking Debounce — Don't Make the CPU Wait

> Following the previous post: C language polling works, but jitter causes multiple triggers. Using `HAL_Delay` for blocking debounce solves the jitter, but at the cost of freezing the CPU for 20ms. This post introduces a non-blocking approach to time management.

---

## The Cost of Blocking Debounce

At the end of the last post, we tried the simplest debounce solution:

```c
// 阻塞式消抖
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
    HAL_Delay(20);  // 阻塞 20ms
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
        // 确认按下
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        // 等待释放，防止按住不放时重复触发
        while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {}
    }
}
```

This solution does eliminate most jitter issues. However, the cost is that `HAL_Delay` freezes the CPU for 20 milliseconds.

20ms doesn't sound long. If you are just controlling an LED, waiting is fine, it doesn't matter. But in real projects, your main loop might have many things to do—reading sensor data, updating displays, handling communication protocols. If you block for 20ms every time you check a button, the real-time performance of other tasks is compromised.

Even worse is the final `while` loop—if the user holds the button down, the CPU gets stuck in this loop, and other tasks stop completely. This is no longer just a "delay"; it is a "hang".

We need a debounce method that does not block the CPU.

---

## HAL_GetTick: A Free Clock

`HAL_GetTick` returns the number of milliseconds since the system started. It is a 32-bit unsigned integer, starting at 0 and incrementing by 1 every millisecond, overflowing back to zero after about 49.7 days (which can be basically ignored for embedded projects).

```c
uint32_t now = HAL_GetTick();  // 例如返回 12345，表示系统已运行 12.345 秒
```

The underlying implementation of `HAL_GetTick` is in `HAL_IncTick`—the `SysTick` interrupt triggers every 1ms, calling `HAL_IncTick` to increment a global counter. This counter is our source for time.

The core idea of using `HAL_GetTick` for debounce is: **Record the time when the state change occurs, and check in the next loop if enough time has passed, rather than stopping to wait.**

---

## Non-blocking Debounce Algorithm

### Basic Idea

```text
1. 每次循环采样当前引脚状态
2. 如果和上次记录的"稳定状态"不同：
   a. 记录变化发生的时间 (debounce_start)
   b. 标记"正在消抖"
3. 如果"正在消抖"且已经过了 debounce_ms：
   a. 再次采样确认
   b. 如果确认状态确实变了，更新"稳定状态"
   c. 触发事件
4. 如果在消抖期间状态又变了回来：
   a. 取消消抖（这是假信号）
```

Represented as an ASCII state diagram:

```text
    ┌──────────┐  状态变化   ┌──────────────┐  确认变化   ┌──────────┐
    │   稳定    │──────────→│   消抖中      │──────────→│  新稳定   │
    │ (高/低)   │           │ (等待时间到)  │           │ (高/低)   │
    └──────────┘←──────────└──────────────┘           └──────────┘
                  状态回弹
                  (假信号)
```

### C Language Implementation

```c
#include "stm32f1xx_hal.h"

int main(void) {
    HAL_Init();
    /* 系统时钟配置省略 */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PA0 上拉输入 */
    GPIO_InitTypeDef btn_init = {0};
    btn_init.Pin = GPIO_PIN_0;
    btn_init.Mode = GPIO_MODE_INPUT;
    btn_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &btn_init);

    /* PC13 推挽输出 */
    GPIO_InitTypeDef led_init = {0};
    led_init.Pin = GPIO_PIN_13;
    led_init.Mode = GPIO_MODE_OUTPUT_PP;
    led_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &led_init);

    /* 消抖状态变量 */
    uint8_t stable_pressed = 0;      // 当前稳定的按钮状态：0=松开，1=按下
    uint32_t debounce_start = 0;     // 状态变化时的时间戳
    const uint32_t debounce_ms = 20; // 消抖等待时间

    while (1) {
        /* 采样当前引脚状态 */
        uint8_t current = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;

        if (current != stable_pressed) {
            /* 状态发生了变化 */
            debounce_start = HAL_GetTick();
            stable_pressed = current;  // 简化处理：直接更新
        }

        /* 这里有一个问题——上面的实现并没有真正"等待确认"
         * 我们只是记录了时间戳，但没有用它来判断
         * 让我们修正 */
    }
}
```

Wait, the code above has a problem. I only recorded the timestamp but didn't use it to make a judgment. Let me rewrite a correct version:

```c
    /* 消抖状态变量 */
    uint8_t last_stable = 0;         // 上次确认的稳定状态
    uint8_t last_raw = 0;            // 上次原始采样值
    uint32_t last_change_time = 0;   // 原始值最后一次变化的时间
    const uint32_t debounce_ms = 20;

    /* 初始化采样 */
    last_raw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;
    last_stable = last_raw;

    while (1) {
        uint8_t current = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;

        if (current != last_raw) {
            /* 原始值变了，重置计时器 */
            last_raw = current;
            last_change_time = HAL_GetTick();
        }

        /* 检查原始值是否已经稳定了足够长时间 */
        if ((HAL_GetTick() - last_change_time) >= debounce_ms) {
            if (last_raw != last_stable) {
                /* 确认状态变化 */
                last_stable = last_raw;

                if (last_stable) {
                    /* 按钮按下 */
                    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  // LED 亮
                } else {
                    /* 按钮松开 */
                    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    // LED 灭
                }
            }
        }

        /* 这里可以做其他任务 —— CPU 没有被阻塞！ */
    }
```

### Line-by-line Interpretation

**State Variables:**

- `stable_state`: The last confirmed stable button state. It is updated only after the raw signal has been stable for 20ms.
- `raw_state`: The most recent raw sample value. Updated whenever a different value is sampled.
- `last_change_time`: The timestamp when the raw value last changed.

**Core Logic:**

1. Sample `raw_state` every loop.
2. If `raw_state` and `stable_state` are different, it means the signal is jumping—update `last_change_time` and reset the timer.
3. If `DEBOUNCE_TIME` (20ms) has passed since the last change, and the raw value differs from the stable value—confirm the state has really changed, update the stable value, and trigger the event.

**Why this debounces:** During jitter, the signal jumps rapidly, resetting the timer on every jump. Only when the signal remains unchanged for a continuous 20ms will the timer "expire" and the state be confirmed. The 5-20ms jumps during jitter are "filtered out" by the constant resetting of the timer.

**Why it's non-blocking:** The entire logic only uses `HAL_GetTick` for timestamp comparison (one subtraction + one comparison), there is no `HAL_Delay`. The main loop runs at full speed, spending only a few microseconds per loop. You can completely insert other tasks—LED blinking, sensor reading, communication processing—into the empty spaces of the `while` loop without being interrupted by button debouncing.

---

## Safety of Overflow

There is a detail worth noting: `HAL_GetTick` uses unsigned integer subtraction. Even if `HAL_GetTick` overflows and wraps to zero, the result of this subtraction is still correct—because of the modular arithmetic property of unsigned integer subtraction.

For example: `current = 100`, `last = 0xFFFFFFF0` (after overflow), the difference is `0x110` (272). 272ms, correct.

So you don't need to worry about the 49.7-day overflow issue. This is much more concise than manually handling overflow and is a standard trick in embedded development for using unsigned integers for time differences.

---

## Does This Solution Still Have Problems?

Non-blocking debounce solves the blocking problem of `HAL_Delay`, but it is not yet perfect:

1. **No concept of Press and Release events**: The code above performs an action when the stable value changes, but there are no clear "Press Event" and "Release Event"—you need to judge yourself whether it's going from 0 to 1 or 1 to 0.
2. **No handling of startup state**: What if the button is already held down when the system powers up? The "stable state" read at initialization is "pressed", but this should not trigger a "press event".
3. **State variables scattered in the main loop**: `stable_state`, `raw_state`, `last_change_time`—these variables are tightly coupled to the button logic but exist as independent local variables. As the project grows complex, maintaining these state variables will be a headache.

These three problems point to the same solution: **Encapsulate the debounce logic into a state machine**. A state machine manages all state transition rules centrally, with clear entry conditions, resident behaviors, and exit actions for each state. No longer scattered `if` statements, but a structured `switch`.

This is the topic of the next post—the 7-state debounce state machine, the core of our final solution.

---

## Looking Back

In this post, we did three things: explained the problem with `HAL_Delay` blocking debounce, introduced `HAL_GetTick` for non-blocking time management, and implemented a working non-blocking debounce algorithm.

Key takeaways:

- `HAL_GetTick` returns a millisecond timestamp, driven by the SysTick interrupt underneath.
- Core of non-blocking debounce: record the change time, check if it has been stable for long enough.
- Unsigned integer subtraction naturally handles overflow.
- Shortcomings of the current solution: no event concept, no startup handling, scattered state variables—all pointing to a state machine.

In the next post, we will refactor the scattered `if` statements into a rigorous state machine.
