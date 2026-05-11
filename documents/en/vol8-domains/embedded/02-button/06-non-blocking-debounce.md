# Part 24: Non-Blocking Debounce — Don't Make the CPU Wait

> Continuing from the previous article: C language polling buttons work, but bounce causes multiple triggers. Using `HAL_Delay()` for blocking debounce solves the bounce, but at the cost of freezing the CPU for 20ms. This article introduces a non-blocking approach to time management.

---

## The Cost of Blocking Debounce

At the end of the last article, we tried the simplest debounce approach:

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

This approach does eliminate most bounce issues. But its cost is that `HAL_Delay(20)` freezes the CPU for 20 milliseconds.

20ms doesn't sound like much. If you're only controlling an LED, waiting is no big deal. But in real projects, your main loop might have many things to do—reading sensor data, updating a display, handling communication protocols. If you block for 20ms every time you check a button, the real-time performance of other tasks is compromised.

Even worse is the final `while` loop—if the user holds the button down, the CPU gets stuck in this loop, and other tasks stop completely. This is no longer a "delay"; it's a "hang".

We need a way to debounce without blocking the CPU.

---

## HAL_GetTick: A Free Clock

`HAL_GetTick()` returns the number of milliseconds that have elapsed since system startup. It is a 32-bit unsigned integer that starts at 0 and increments by 1 every millisecond, wrapping around to zero after about 49.7 days (which can be safely ignored for embedded projects).

```c
uint32_t now = HAL_GetTick();  // 例如返回 12345，表示系统已运行 12.345 秒
```

The underlying implementation of `HAL_GetTick()` is in `hal_mock.c`—the `SysTick_Handler()` interrupt triggers every 1ms and calls `HAL_IncTick()` to increment a global counter. This counter is our source of time.

The core idea behind using `HAL_GetTick()` for debouncing is: **record the time when a state change occurs, and check on the next loop iteration whether enough time has passed, rather than stopping to wait.**

---

## Non-Blocking Debounce Algorithm

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

Wait, there's a problem with the code above. I recorded the timestamp but didn't actually use it to make a judgment. Let me rewrite a correct version:

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

### Line-by-Line Breakdown

**State variables:**

- `last_stable`: The last confirmed stable button state. It only updates after the raw signal has been stable for 20ms.
- `last_raw`: The most recent raw sampled value. It updates whenever a different value is sampled.
- `last_change_time`: The timestamp of the last change in the raw value.

**Core logic:**

1. Sample `current` on every loop iteration.
2. If `current` and `last_raw` are different, it means the signal is transitioning—update `last_raw` and reset the timer.
3. If more than `debounce_ms` (20ms) has passed since the last change, and the raw value differs from the stable value—confirm that the state has truly changed, update the stable value, and trigger an event.

**Why this debounces:** During a bounce, the signal transitions rapidly, and each transition resets the timer. Only when the signal remains unchanged for a continuous 20ms does the timer "expire" and the state get confirmed. The 5-20ms bouncing transitions are "filtered out" by the timer's constant resetting.

**Why it's non-blocking:** The entire logic only uses `HAL_GetTick()` for timestamp comparison (one subtraction + one comparison), with no `HAL_Delay()`. The main loop runs at full speed, spending only a few microseconds per iteration. You can easily add other tasks in the available space of the `while(1)` loop—LED blinking, sensor reading, communication handling—none of which will be interrupted by button debouncing.

---

## Overflow Safety

There is a detail worth noting: `HAL_GetTick() - last_change_time` uses unsigned integer subtraction. Even if `HAL_GetTick()` wraps around to zero, the result of this subtraction is still correct—due to the modular arithmetic properties of unsigned integer subtraction.

For example: `last_change_time = 0xFFFFFFF0`, `HAL_GetTick() = 0x00000010` (after overflow), the difference is `0x00000010 - 0xFFFFFFF0 = 0x00000020 = 32`. 32ms, correct.

So you don't need to worry about the 49.7-day overflow issue. This is much cleaner than manually handling overflow, and it's a standard trick in embedded development for calculating time differences with unsigned integers.

---

## Are There Still Problems With This Approach?

Non-blocking debounce solves the `HAL_Delay()` blocking issue, but it's still not perfect:

1. **No concept of press and release events:** The code above performs an action when the stable value changes, but there are no explicit "press event" and "release event"—you have to determine for yourself whether it changed from 0 to 1 or from 1 to 0.
2. **No handling of the startup state:** What if the button is already being held down when the system powers on? The "stable state" read during initialization is pressed, but this shouldn't trigger a "press event".
3. **State variables scattered in the main loop:** `last_stable`, `last_raw`, and `last_change_time` are tightly coupled to the button logic, yet they exist as independent local variables. As the project grows more complex, maintaining these state variables becomes a headache.

These three problems point to the same solution: **encapsulate the debounce logic into a state machine**. A state machine centrally manages all state transition rules, and each state has clear entry conditions, dwell behaviors, and exit actions. Instead of scattered `if-else`, we get a structured `switch-case`.

This is the topic of the next article—a 7-state debounce state machine, the core of our final solution.

---

## Looking Back

In this article, we did three things: explained the problem with `HAL_Delay()` blocking debounce, introduced `HAL_GetTick()` for non-blocking time management, and implemented a workable non-blocking debounce algorithm.

Key takeaways:

- `HAL_GetTick()` returns a millisecond timestamp, driven by the SysTick interrupt underneath
- The core of non-blocking debouncing: record the time of change, check if it has been stable for long enough
- Unsigned integer subtraction naturally handles overflow
- Shortcomings of the current approach: no event concept, no startup handling, scattered state variables—all pointing toward a state machine

In the next article, we'll refactor the scattered `if-else` into a rigorous state machine.
