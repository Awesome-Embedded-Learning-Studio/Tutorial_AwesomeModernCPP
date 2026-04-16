---
title: 第24篇：非阻塞消抖 —— 不让 CPU 停下来等
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 6
---
# 第24篇：非阻塞消抖 —— 不让 CPU 停下来等

> 承接上一篇：C 语言轮询按钮能跑，但抖动导致多次触发。用 `HAL_Delay()` 阻塞消抖能解决抖动，但代价是 CPU 被冻结 20ms。这一篇引入非阻塞的时间管理方式。

---

## 阻塞消抖的代价

上一篇最后我们试了一个最简单的消抖方案：

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
```text

这个方案确实能消除大部分抖动问题。但它的代价是 `HAL_Delay(20)` 把 CPU 冻结了 20 毫秒。

20ms 听起来不长。如果你只是控制一盏 LED，等就等了，无所谓。但真实项目中，你的主循环可能要做很多事情——读取传感器数据、更新显示、处理通信协议。如果你在每次检测按钮时都阻塞 20ms，其他任务的实时性就被破坏了。

更糟的是最后的 `while` 循环——如果用户一直按住按钮不放，CPU 就一直卡在这个循环里，其他任务完全停止。这已经不是"延迟"了，这是"挂死"。

我们需要一种不阻塞 CPU 的消抖方式。

---

## HAL_GetTick：免费的时钟

`HAL_GetTick()` 返回自系统启动以来经过的毫秒数。它是一个 32 位无符号整数，从 0 开始每毫秒加 1，大约 49.7 天后溢出归零（对于嵌入式项目来说基本可以忽略）。

```c
uint32_t now = HAL_GetTick();  // 例如返回 12345，表示系统已运行 12.345 秒
```text

`HAL_GetTick()` 的底层实现在 `hal_mock.c` 中——`SysTick_Handler()` 中断每 1ms 触发一次，调用 `HAL_IncTick()` 递增一个全局计数器。这个计数器就是我们获取时间的来源。

用 `HAL_GetTick()` 做消抖的核心思想是：**记录状态变化发生的时间，下次循环时检查是否已经过了足够长的时间，而不是停下来等。**

---

## 非阻塞消抖算法

### 基本思路

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
```text

用 ASCII 状态图表示：

```text
    ┌──────────┐  状态变化   ┌──────────────┐  确认变化   ┌──────────┐
    │   稳定    │──────────→│   消抖中      │──────────→│  新稳定   │
    │ (高/低)   │           │ (等待时间到)  │           │ (高/低)   │
    └──────────┘←──────────└──────────────┘           └──────────┘
                  状态回弹
                  (假信号)
```text

### C 语言实现

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
```text

等等，上面的代码有问题。我只记录了时间戳但没有用它来做判断。让我重新写一个正确的版本：

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
```text

### 逐行解读

**状态变量：**
- `last_stable`：上次确认的稳定按钮状态。只有在原始信号稳定了 20ms 之后才会更新。
- `last_raw`：最近的原始采样值。每次采到不同的值就更新。
- `last_change_time`：原始值最后一次变化的时间戳。

**核心逻辑：**
1. 每次循环采样 `current`。
2. 如果 `current` 和 `last_raw` 不同，说明信号在跳变——更新 `last_raw` 并重置计时器。
3. 如果距离上次变化已经过了 `debounce_ms`（20ms），且原始值和稳定值不同——确认状态真的变了，更新稳定值并触发事件。

**为什么这能消抖：** 抖动期间信号快速跳变，每次跳变都重置计时器。只有当信号连续 20ms 保持不变时，计时器才会"到期"，状态才被确认。抖动的 5-20ms 跳变会被计时器的不断重置"过滤掉"。

**为什么不阻塞：** 整个逻辑只用了 `HAL_GetTick()` 做时间戳比较（一次减法 + 一次比较），没有 `HAL_Delay()`。主循环以全速运行，每次循环只花几个微秒。你完全可以在 `while(1)` 循环的空余位置加入其他任务——LED 闪烁、传感器读取、通信处理——都不会被按钮消抖打断。

---

## 溢出的安全性

有一个细节值得注意：`HAL_GetTick() - last_change_time` 使用的是无符号整数减法。即使 `HAL_GetTick()` 溢出归零了，这个减法的结果仍然正确——因为无符号整数减法的模运算性质。

例如：`last_change_time = 0xFFFFFFF0`，`HAL_GetTick() = 0x00000010`（溢出后），差值是 `0x00000010 - 0xFFFFFFF0 = 0x00000020 = 32`。32ms，正确。

所以你不需要担心 49.7 天的溢出问题。这比手动处理溢出要简洁得多，也是嵌入式开发中使用无符号整数做时间差的一个标准技巧。

---

## 这个方案还有问题吗？

非阻塞消抖解决了 `HAL_Delay()` 的阻塞问题，但还不够完善：

1. **没有按下和释放的事件概念**：上面的代码在稳定值变化时做操作，但没有明确的"按下事件"和"释放事件"——你需要自己判断是从 0 变到 1 还是 1 变到 0。
2. **没有处理启动时的状态**：如果系统上电时按钮已经被按住了呢？初始化时读到的"稳定状态"是按下，但这不应该触发"按下事件"。
3. **状态变量散落在主循环里**：`last_stable`、`last_raw`、`last_change_time` 这些变量和按钮逻辑紧密耦合，却作为独立的局部变量存在。随着项目变复杂，维护这些状态变量会很头疼。

这三个问题指向同一个解决方案：**把消抖逻辑封装成一个状态机**。状态机把所有的状态转换规则集中管理，每个状态有明确的进入条件、驻留行为和退出动作。不再是散落的 `if-else`，而是一个结构化的 `switch-case`。

这就是下一篇的主题——7 状态消抖状态机，我们最终方案的核心。

---

## 我们回头看

这一篇做了三件事：解释了 `HAL_Delay()` 阻塞消抖的问题，引入了 `HAL_GetTick()` 做非阻塞时间管理，实现了一个可用的非阻塞消抖算法。

关键收获：
- `HAL_GetTick()` 返回毫秒时间戳，底层由 SysTick 中断驱动
- 非阻塞消抖的核心：记录变化时间，检查是否稳定了足够长时间
- 无符号整数减法天然处理溢出
- 当前方案的不足：没有事件概念、没有启动处理、状态变量散落——都指向状态机

下一篇我们把散落的 `if-else` 重构成一个严谨的状态机。
