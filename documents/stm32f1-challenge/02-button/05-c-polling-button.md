# 第23篇：C 语言轮询按钮 —— 第一次亲手让按钮控制 LED

前面的四篇文章里，我们从电路原理聊到了 HAL 库的 GPIO 输入 API。现在是时候把所有知识串起来，写一个真正能跑的程序了。

这篇文章的目标很纯粹：**用纯 C 语言写一个完整的按钮控制 LED 程序，烧录到板子上，亲眼看看机械抖动到底有多严重。** 不加任何消抖，不做任何技巧，就是最朴素的"读引脚 → 写引脚"。只有先看到问题，后面才知道为什么要解决它。

---

## 1. 完整的 C 代码

先别管什么消抖、什么状态机——我们今天的目标就是把电路连好、把代码写对、让 LED 跟着按钮走。先让东西动起来，再谈优化。

### 硬件连线回顾

| 引脚 | 功能 | 连接 |
|------|------|------|
| PA0  | 按钮输入 | 一端接 GND，另一端接 PA0 |
| PC13 | LED 输出 | 板载 LED（低电平点亮） |

PA0 配置为**上拉输入**模式。按钮没按下时，上拉电阻把 PA0 拉到高电平；按钮按下时，PA0 被直接短接到 GND，读到低电平。

### 完整代码

下面是一个完整可编译、可烧录的 `main.c`。每一行都有注释，确保你清楚每一步在做什么。

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
```text

代码的结构非常清晰，就三件事：**初始化时钟、配置引脚、在主循环里反复读按钮状态**。没有任何消抖逻辑，就是最直白的轮询。

> 如果你对 `HAL_GPIO_ReadPin`、`GPIO_PULLUP` 这些参数还不太熟悉，回头看看[第 04 篇](./04-hal-gpio-input.md)的 API 详解，那里有每个参数的说明。

---

## 2. 烧录运行：看起来正常……真的吗？

把代码编译烧录到板子上，按住按钮——LED 亮了。松开按钮——LED 灭了。看起来一切正常？

别急着庆祝。试试这个操作：**用最快的速度按一下按钮然后立刻松开。**

你大概率会发现，有时候 LED 灯的状态不对——你明明只想按一次，但 LED 的表现像是你按了好几次。有时候亮了又灭、灭了又亮，或者干脆没反应。

### 用计数器量化问题

口说无凭，我们用一个计数器来量化抖动到底有多严重。在 `if` 分支里加上一行：

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
```text

然后在线调试模式下设置断点，读 `press_count` 的值。

**你明明只按了一次按钮，但 `press_count` 可能显示 3、5、甚至 8 以上。**

这就是机械抖动在软件层面的直接体现。肉眼看来你只按了一下，但 MCU 采样到的却是多次按下-松开的来回跳变。

---

## 3. 为什么会多次触发？

还记得[第 03 篇](./03-button-hardware-and-bounce.md)里那张抖动波形图吗？按钮按下和松开的瞬间，触点之间不是干净利落地"从 0 变到 1"或"从 1 变到 0"，而是在高电平和低电平之间反复弹跳，持续大约 **5 到 20 毫秒**。

问题就出在这个时间差上。

让我们算一笔账：

- 上面的 `SystemClock_Config` 配置了 72MHz 系统时钟（注意：项目模板中的 `clock.cpp` 使用 HSI 倍频到 64MHz，这里为了演示用了更常见的 HSE 72MHz 方案，计算原理一样）。
- 主循环里做的事情很简单：读一个引脚、判断条件、写一个引脚。整个循环体大约消耗 **几十个时钟周期**，我们取 100 个来估算。
- 所以主循环大约每 **1.4 微秒** 就执行一次（64MHz 下约 1.6 微秒，量级相同）。
- 在 10 毫秒的抖动期间，CPU 可以跑大约 **7,000 次循环**。

在这 7,000 次采样中，抖动产生的每一个"假跳变"——哪怕只持续几微秒——都会被 `HAL_GPIO_ReadPin` 忠实地捕捉到。如果你的代码在 `if` 分支里做的是翻转 LED（Toggle）而不是简单地设高设低，那抖动带来的多次翻转会直接反映到 LED 上：你按一次，LED 闪了三四下。

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
```text

MCU 的采样速度实在太快了，快到它能在几毫秒的抖动里读几千次引脚。**我们的代码没有任何问题，问题在于按钮本身的物理特性。** 所以，消抖不是"锦上添花"，而是按钮输入的刚需。

---

## 4. 最简单的消抖尝试：HAL_Delay

既然问题是"采样太快、抖动期间的假跳变被多次捕获"，那最直接的思路就是：**检测到按下之后，等一会儿再读一次，确认电平稳定了再决定是不是真的按下了。**

"等一会儿"最简单的实现方式就是 `HAL_Delay`：

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
```text

逻辑很清楚：

1. 第一次读到低电平 → 可能是抖动，也可能是真按下，先不急。
2. 等 20 毫秒 → 抖动早就结束了。
3. 再读一次 → 如果还是低电平，那就是真按下；如果变回高电平了，说明刚才就是抖动。

烧录试试——果然，快速按一下按钮，LED 只会亮一次。计数器也正常了。问题解决了？

**只解决了一半。**

### 这个方案的问题

`HAL_Delay` 的本质是让 CPU 在一个 `while` 循环里空转，反复检查 SysTick 定时器有没有到时间。在这 20 毫秒里，CPU 什么正经事都干不了——它被"阻塞"了。

如果你的项目里只有一个按钮和一个 LED，那阻塞 20ms 可能没什么大不了。但想象一下这些场景：

- 你还需要在主循环里读取一个温度传感器，采样间隔要求精确到 1ms。
- 你在用串口接收数据，缓冲区可能在这 20ms 里溢出。
- 你有一个 OLED 屏幕在以 60fps 刷新，20ms 的卡顿会导致画面撕裂。

在稍微复杂一点的项目里，**阻塞式消抖就是一个定时炸弹。** 它让整个系统的响应变得不可预测。

> ⚠️ **警告**：在正式项目中，永远不要在主循环里使用阻塞式消抖。它看起来简单有效，但随着功能增加，会变成系统最大的不稳定因素。

### 那怎么办？

思路很简单：**不阻塞 CPU，而是记录时间。** 每次检测到电平变化时，不等待，而是记下当前时刻；下一次循环再读到变化时，检查"距离上次变化过了多久"。只有超过 20ms 了，才认为电平真正稳定了。

这就是**非阻塞消抖**的思路——它需要用到 SysTick 定时器或者硬件定时器，我们留到下一篇来详细实现。

---

## 小结

这篇我们做了三件事：

1. **写了第一个完整的按钮控制 LED 程序**，从时钟配置、GPIO 初始化到主循环轮询，一气呵成。
2. **亲眼看到了机械抖动的危害**——一次按压被采样成多次触发，用计数器量化了这个问题。
3. **尝试了最简单的消抖方案**（`HAL_Delay`），理解了它能解决问题但会阻塞 CPU，引出了非阻塞消抖的需求。

现在你已经知道按钮消抖的"为什么"和"最简单的怎么办"。下一篇，我们来实现真正工程级别的非阻塞消抖方案——不需要阻塞 CPU，不需要牺牲实时性，而且代码量也没有你想的那么多。
