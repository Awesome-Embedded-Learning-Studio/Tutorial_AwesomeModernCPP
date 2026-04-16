---
title: "第30篇：EXTI 中断 + 坑位与练习"
description: ""
tags:
  - cpp-modern
  - intermediate
  - stm32f1
difficulty: intermediate
platform: stm32f1
---
# 第30篇：EXTI 中断 + 坑位与练习

> 按钮教程的最后一篇。前面的 11 篇我们一直在用"轮询"方式检测按钮——主循环不断调用 `poll_events()`。这一篇介绍另一种方式：让硬件在按钮状态变化时主动通知 CPU。然后是常见坑位汇总和三个练习题。

---

## 轮询 vs 中断

轮询（polling）的方式是：CPU 在主循环中反复检查按钮状态。优点是简单、可控；缺点是如果主循环在做其他耗时操作，可能错过按钮状态变化。

中断（interrupt）的方式是：CPU 配置硬件，让硬件在引脚电平变化时自动打断当前执行流，跳转到一个预先注册的中断服务函数（ISR）中处理。处理完毕后返回被打断的位置继续执行。

两种方式不是非此即彼。我们的最终代码用的是轮询 + 状态机消抖——这对大多数按钮场景已经足够。但理解中断机制对嵌入式开发很重要，因为很多外设（UART 接收、定时器、ADC 转换完成）都是通过中断通知 CPU 的。

---

## EXTI：外部中断控制器

EXTI（External Interrupt/Event Controller）是 STM32 中专门处理外部引脚电平变化的中断控制器。

### EXTI line 映射

STM32F103 有 20 条 EXTI line（EXTI0 ~ EXTI19），其中 EXTI0 ~ EXTI15 对应 GPIO 引脚：

```text
PA0, PB0, PC0, ... → EXTI0（共享，通过 AFIO 选择哪个端口）
PA1, PB1, PC1, ... → EXTI1
...
PA4, PB4, PC4, ... → EXTI4
PA5, PB5, PC5, ... → EXTI5  ─┐
...                           ├→ EXTI9_5_IRQn（共享中断向量）
PA9, PB9, PC9, ... → EXTI9  ─┘
PA10, PB10, PC10, ... → EXTI10 ─┐
...                              ├→ EXTI15_10_IRQn（共享中断向量）
PA15, PB15, PC15, ... → EXTI15 ─┘
```text

关键规则：同一时刻，一条 EXTI line 只能连接一个端口的对应引脚。比如 EXTI0 可以连接 PA0、PB0 或 PC0，但不能同时连接多个。连接选择通过 AFIO（Alternate Function I/O）的 `EXTICR` 寄存器配置。

我们选 PA0 的一个好处：EXTI0 有独立的中断向量 `EXTI0_IRQn`，不需要和其他引脚共享。如果选 PA5，EXTI5 的中断向量 `EXTI9_5_IRQn` 被 EXTI5~9 共享——中断触发后你还需要判断具体是哪个引脚。

### 触发模式

EXTI 支持三种触发模式：

| 模式 | 含义 | HAL 常量 |
|------|------|---------|
| 上升沿触发 | 电平从低到高时触发 | `GPIO_MODE_IT_RISING` |
| 下降沿触发 | 电平从高到低时触发 | `GPIO_MODE_IT_FALLING` |
| 双边沿触发 | 任何电平变化都触发 | `GPIO_MODE_IT_RISING_FALLING` |

按钮上拉方案中，按下是下降沿（高→低），释放是上升沿（低→高）。如果只关心按下，用下降沿触发；如果按下和释放都关心，用双边沿。

---

## EXTI 配置流程

### C 语言配置

```c
/* 1. 使能 AFIO 时钟（EXTI 配置需要 AFIO） */
__HAL_RCC_AFIO_CLK_ENABLE();

/* 2. 使能 GPIOA 时钟 */
__HAL_RCC_GPIOA_CLK_ENABLE();

/* 3. 配置 PA0 为中断模式 + 上拉 */
GPIO_InitTypeDef init = {0};
init.Pin = GPIO_PIN_0;
init.Mode = GPIO_MODE_IT_FALLING;  // 下降沿触发（按下瞬间）
init.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOA, &init);
// HAL_GPIO_Init 内部会自动配置 AFIO EXTICR 寄存器

/* 4. 配置 NVIC 中断优先级和使能 */
HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
HAL_NVIC_EnableIRQ(EXTI0_IRQn);
```text

四步：使能 AFIO 时钟 → 配置 GPIO 中断模式 → 配置 NVIC。

⚠️ 第一步最容易忘记。AFIO 时钟默认是关闭的。如果你不调用 `__HAL_RCC_AFIO_CLK_ENABLE()`，EXTI 的配置寄存器写不进去，中断永远不会触发。这个 bug 不会报错——`HAL_GPIO_Init()` 不知道你有没有开 AFIO 时钟，它只是往寄存器里写值，但值写不进去它也检测不到。

### 中断回调链

硬件中断触发后的调用链：

```text
物理电平变化（下降沿）
  → EXTI 硬件检测到边沿
  → NVIC 挂起 EXTI0 中断
  → CPU 暂停当前任务
  → 跳转到 EXTI0_IRQHandler()
    → HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)
      → 清除 EXTI 挂起标志
      → 调用 HAL_GPIO_EXTI_Callback(GPIO_PIN_0)
        → 用户在这里写处理逻辑
  → 返回被中断的代码继续执行
```text

我们的 `hal_mock.c` 中已经定义了 `EXTI0_IRQHandler` 和一个弱 `HAL_GPIO_EXTI_Callback`：

```c
void EXTI0_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

__attribute__((weak)) void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    (void)GPIO_Pin;
}
```text

`__attribute__((weak))` 是 GCC 的弱符号属性——如果其他 `.c`/`.cpp` 文件中定义了同名函数，链接器会使用那个定义；如果没有，就使用这个空实现。这让你可以在任何地方覆盖回调函数，不需要修改 `hal_mock.c`。

---

## 中断驱动按钮的简单示例

```c
/* 全局变量：中断标志 */
volatile uint8_t button_pressed = 0;

/* 覆盖弱回调 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) {
        button_pressed = 1;
    }
}

int main(void) {
    HAL_Init();
    /* 系统时钟配置 */
    /* GPIO 和 NVIC 配置（如上） */

    while (1) {
        if (button_pressed) {
            button_pressed = 0;
            /* 处理按钮按下 */
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
        /* 其他任务 */
    }
}
```text

### volatile 的作用

`button_pressed` 变量被声明为 `volatile`。为什么？

编译器优化时，如果它发现主循环中 `button_pressed` 只被读取、没有被其他代码修改（编译器看不到中断的上下文），它可能会把 `button_pressed` 的值缓存到寄存器中，永远不再从内存读取。这样即使 ISR 修改了 `button_pressed`，主循环也看不到变化。

`volatile` 告诉编译器：这个变量可能被编译器看不到的方式修改（比如中断），每次读取都必须从内存重新加载，不能缓存。

⚠️ `volatile` 不保证原子性——它只保证"每次都从内存读取"。如果多个中断同时修改同一个变量，仍然需要互斥保护。不过我们的场景中只有一个 ISR 写、主循环读，不存在竞争。

### 中断消抖

上面的示例没有消抖——抖动期间 EXTI 会触发多次中断。在中断中消抖有两种方式：

1. **中断中记录时间戳，主循环中确认**：ISR 只设标志和时间戳，主循环检查时间差是否足够。
2. **直接在中断中延时**：不推荐——ISR 应该尽快返回，不能阻塞。在 ISR 中调 `HAL_Delay()` 是危险的，因为 `HAL_Delay()` 依赖 SysTick 中断，而 SysTick 优先级可能低于 EXTI，导致死锁。

推荐方案：中断设标志，主循环用状态机确认。这和我们之前的轮询方案本质一样，只是"初始触发"从轮询变成了中断。

---

## 常见坑位汇总

### 坑位 1：忘开 AFIO 时钟

**症状**：EXTI 中断不触发，`HAL_GPIO_EXTI_Callback()` 永远不被调用。
**原因**：没有调用 `__HAL_RCC_AFIO_CLK_ENABLE()`，EXTI 配置寄存器不可写。
**解决**：在 EXTI 配置之前使能 AFIO 时钟。

### 坑位 2：消抖时间设太短

**症状**：消抖后仍然有多次触发。
**原因**：`debounce_ms` 设得太小（比如 5ms），某些抖动时间长的开关来不及过滤。
**解决**：默认 20ms 对绝大多数开关够用。如果还有问题，可以调到 30-50ms。

### 坑位 3：ReadPin 返回值和上拉逻辑混淆

**症状**：按钮逻辑反转——按下反而灭灯。
**原因**：上拉方案中，按下=低电平=`GPIO_PIN_RESET`。如果你的代码把 `GPIO_PIN_RESET` 当成"松开"处理，逻辑就反了。
**解决**：记住"上拉方案，低电平=按下"。或者用 `ButtonActiveLevel` 让编译器帮你处理。

### 坑位 4：boot-lock 忘处理

**症状**：系统上电时如果按钮被按住，释放后 LED 状态异常。
**原因**：没有 boot-lock 机制，系统把"上电时按钮已按住"当成了正常事件。
**解决**：我们的状态机已经处理了——`BootSync` 和 `BootPressed` 状态确保上电时的按钮状态不触发事件。

### 坑位 5：ISR 中做耗时操作

**症状**：系统卡死或响应异常。
**原因**：ISR 中调用了 `HAL_Delay()`、打印函数、复杂计算等耗时操作。ISR 应该尽快返回——通常在几微秒内。
**解决**：ISR 中只设标志和时间戳，所有逻辑处理放到主循环。

### 坑位 6：轮询间隔太长

**症状**：快速按下释放，状态机漏检。
**原因**：主循环中有长时间阻塞操作（比如 `HAL_Delay(500)` 闪烁 LED），导致 `poll_events()` 调用间隔超过了按钮按下的持续时间。
**解决**：避免在主循环中使用长时间阻塞。用非阻塞方式管理所有定时任务。

---

## 练习题

### 练习 1：调整消抖时间

修改 `poll_events()` 的 `debounce_ms` 参数为 50ms，观察按钮响应有什么变化。再改为 5ms，又有什么变化？

**目标**：理解消抖时间对响应延迟和可靠性的权衡。时间越长越可靠但响应越迟钝；时间越短响应越快但可能滤不干净。

### 练习 2：改用 PB5 按钮

把按钮从 PA0 改为 PB5。你需要修改哪些地方？

**提示**：
- 模板参数改为 `GpioPort::B, GPIO_PIN_5`
- EXTI line 变为 EXTI5
- 中断向量变为 `EXTI9_5_IRQn`（共享向量）
- `hal_mock.c` 中需要添加 `EXTI9_5_IRQHandler`
- 共享向量中需要检查具体是哪个引脚触发

**目标**：理解 EXTI 共享向量的处理方式，以及模板参数修改的零代码改动（只需要改类型参数）。

### 练习 3：混合方案——中断触发 + 状态机确认

实现一个方案：EXTI 中断触发时唤醒状态机，状态机在主循环中完成消抖和事件确认。

**提示**：
- ISR 中设置 `volatile bool exti_triggered = true` 和时间戳
- 主循环检查 `exti_triggered`，如果为 true 调用 `poll_events()`
- `poll_events()` 正常工作，不需要知道触发来自中断还是轮询

**目标**：理解中断和轮询可以混合使用——中断负责"通知有变化"，状态机负责"确认和消抖"。

---

## 按钮教程回顾

12 篇文章走完了。回顾一下我们的学习路径：

**阶段一：硬件基础（01-03）**
- 从输出到输入的范式转换
- GPIO 输入模式内部电路：上拉/下拉/浮空、施密特触发器、IDR 寄存器
- 按钮接线（PA0 上拉接 GND）和机械抖动物理原理

**阶段二：HAL + C 实战（04-06）**
- `HAL_GPIO_ReadPin()` 的底层实现
- 纯 C 轮询按钮，亲眼看到抖动问题
- `HAL_GetTick()` 非阻塞消抖

**阶段三：状态机（07）**
- 7 状态消抖状态机的完整解读
- Boot-lock 边界处理

**阶段四：C++ 重构（08-12）**
- `enum class`：`ButtonActiveLevel` 和私有 `State`
- `std::variant` + `std::visit`：类型安全的事件系统
- Button 模板类：NTTP 四参数、`if constexpr`、`static_assert`
- Concepts：`requires std::invocable` 约束回调
- EXTI 中断：配置流程、回调链、volatile 语义

用到的 C++ 特性总结：
- `enum class`（C++11）— LED 教程引入，按钮教程扩展
- 非类型模板参数 NTTP（C++11）— LED 教程引入，按钮教程增加参数
- `if constexpr`（C++17）— LED 教程引入，按钮教程新场景
- `static_assert`（C++11）— 按钮教程新增
- `[[nodiscard]]`（C++17/23）— LED 教程引入，按钮教程扩展
- `std::variant` + `std::visit`（C++17）— 按钮教程新增
- Concepts `std::invocable`（C++20）— 按钮教程新增
- 转发引用 `Callback&&`（C++11）— 按钮教程引入

每个特性都不是"花里胡哨的语法糖"——它们在嵌入式按钮控制这个具体场景中都有实际解决的问题。这就是现代 C++ 在嵌入式领域的价值：用编译器的能力替代人脑的警惕性，在不付出运行时代价的前提下写出更安全、更可维护的代码。
