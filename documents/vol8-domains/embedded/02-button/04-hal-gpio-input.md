---
title: 第22篇：HAL GPIO 输入 API —— 怎么用代码读到按钮状态
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 4
---
# 第22篇：HAL GPIO 输入 API —— 怎么用代码读到按钮状态

> 承接上一篇：硬件准备好了，接线图画了，抖动也讲透了。现在终于要写代码了。这一篇拆解 HAL 库提供的 GPIO 输入接口。

---

## 从输出 API 到输入 API

LED 教程中，我们用了三个 HAL 函数来控制 LED：

| 操作 | HAL 函数 | 操作的寄存器 |
|------|---------|------------|
| 初始化引脚 | `HAL_GPIO_Init()` | CRL/CRH |
| 写引脚电平 | `HAL_GPIO_WritePin()` | ODR/BSRR |
| 翻转引脚电平 | `HAL_GPIO_TogglePin()` | ODR/BSRR |

按钮只需要两个：一个初始化，一个读取。

| 操作 | HAL 函数 | 操作的寄存器 |
|------|---------|------------|
| 初始化引脚 | `HAL_GPIO_Init()` | CRL/CRH |
| **读引脚电平** | `HAL_GPIO_ReadPin()` | **IDR** |

`HAL_GPIO_Init()` 在 LED 教程中已经拆解过了——它把 `GPIO_InitTypeDef` 结构体中的配置翻译成 CRL/CRH 寄存器的位域操作。按钮初始化和 LED 初始化用的是同一个函数，只是参数不同。

---

## 输入模式初始化

### GPIO_InitTypeDef 的输入配置

LED 的初始化代码是这样的：

```c
GPIO_InitTypeDef init = {0};
init.Pin = GPIO_PIN_13;
init.Mode = GPIO_MODE_OUTPUT_PP;   // 推挽输出
init.Pull = GPIO_NOPULL;
init.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &init);
```text

按钮的初始化只需要改两个参数：

```c
GPIO_InitTypeDef init = {0};
init.Pin = GPIO_PIN_0;
init.Mode = GPIO_MODE_INPUT;       // 通用输入
init.Pull = GPIO_PULLUP;           // 内部上拉
init.Speed = GPIO_SPEED_FREQ_LOW;  // 输入模式下 Speed 无意义，但需要填值
HAL_GPIO_Init(GPIOA, &init);
```text

三个值得注意的地方：

**第一，`Mode` 从 `GPIO_MODE_OUTPUT_PP` 变成了 `GPIO_MODE_INPUT`。** 这对应 CRL 寄存器中 `MODE[1:0] = 00`（输入模式）和 `CNF[1:0] = 10`（上拉/下拉输入）。

**第二，`Pull` 从 `GPIO_NOPULL` 变成了 `GPIO_PULLUP`。** 这启用内部上拉电阻，同时在 ODR 对应位写 1 来选择上拉方向（上一篇讲过的那个"输入模式下 ODR 控制上下拉方向"的细节）。

**第三，`Speed` 在输入模式下没有实际意义。** Speed 控制输出驱动器的翻转速率——输入模式下输出驱动器是断开的，所以这个参数不影响任何行为。但 HAL 要求你填一个值，随便填就行。

### 别忘了时钟

和输出一样，使用任何 GPIO 端口之前必须先使能对应的时钟。PA0 在 GPIOA 上，所以：

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
```text

如果你忘了这一步，`HAL_GPIO_Init()` 调用不会报错（它不知道你有没有开时钟），但写入的配置不会生效——引脚保持复位状态（浮空输入），读出来的值是不确定的。这是新手最常见的坑之一。

LED 教程中我们用 `if constexpr` 在编译时自动选择时钟使能宏，按钮教程的 Button 模板类会复用同样的机制。但如果你用 C 语言写，记得手动调用。

---

## HAL_GPIO_ReadPin

### 函数签名

```c
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```text

两个参数：`GPIOx` 指定端口（GPIOA、GPIOB、GPIOC...），`GPIO_Pin` 指定引脚编号（`GPIO_PIN_0` ~ `GPIO_PIN_15`）。返回值是 `GPIO_PinState` 枚举：

```c
typedef enum {
    GPIO_PIN_RESET = 0,  // 低电平
    GPIO_PIN_SET   = 1   // 高电平
} GPIO_PinState;
```text

### 底层实现

HAL 库的 `HAL_GPIO_ReadPin()` 实现非常简洁：

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
```text

核心就是一个位操作：`GPIOx->IDR & GPIO_Pin`。`IDR` 是 16 位只读寄存器，每个 bit 对应一个引脚。`GPIO_PIN_0` 的值是 `0x0001`，所以 `IDR & 0x0001` 就是取 bit 0 的值。如果不为 0，引脚是高电平；否则是低电平。

几个时钟周期就能完成（LDR + AND + CMP，编译器优化后约 2-4 个周期）。72MHz 的 CPU 意味着读引脚状态只需要约数十纳秒。

### 和 WritePin 的对比

`HAL_GPIO_WritePin()` 操作的是 BSRR 寄存器（Bit Set/Reset Register），这是一个只写的寄存器——写 1 到低 16 位会复位（清零）对应的 ODR bit，写 1 到高 16 位会置位（设一）对应的 ODR bit。这是一种原子操作，不需要读-改-写的三步过程。

`HAL_GPIO_ReadPin()` 操作的是 IDR 寄存器，只读，直接返回引脚电平。

| | 输出 (LED) | 输入 (按钮) |
|---|-----------|-----------|
| 初始化 | `GPIO_MODE_OUTPUT_PP` | `GPIO_MODE_INPUT` |
| 核心操作 | `HAL_GPIO_WritePin()` → BSRR | `HAL_GPIO_ReadPin()` → IDR |
| 寄存器属性 | BSRR 只写 | IDR 只读 |
| 操作耗时 | 1 个时钟周期 | 1 个时钟周期 |

---

## read_pin_state()：我们的 C++ 封装

在 `device/gpio/gpio.hpp` 中，我们给 GPIO 模板类新增了 `read_pin_state()` 方法：

```cpp
[[nodiscard]] State read_pin_state() const {
    return static_cast<State>(HAL_GPIO_ReadPin(native_port(), PIN));
}
```text

这里有几个设计决策需要解释。

### 为什么返回 State 枚举而不是 bool

你可以争论说返回 `bool` 更简单——`true` 是高电平，`false` 是低电平。但我们选择返回 `State` 枚举（`State::Set` 和 `State::UnSet`），和输出端的 `set_gpio_pin_state(State)` 保持对称。这样输入和输出用的是同一套类型，代码风格一致。

而且 `State` 枚举比 `bool` 更不容易被误用。如果你有多个引脚要操作，`bool` 的 `true`/`false` 含义在不同上下文中可能混淆——`true` 是按下还是松开？取决于上拉还是下拉。但 `State::Set` 永远表示引脚为高电平，`State::UnSet` 永远表示低电平，不含歧义。

### 为什么加 [[nodiscard]]

`[[nodiscard]]` 告诉编译器：这个函数的返回值不应该被忽略。如果你写了 `button.read_pin_state();` 但没有使用返回值，编译器会发出警告。

读引脚状态的唯一目的就是获取返回值。如果你调用了 `read_pin_state()` 却不使用结果，那这个调用百分之百是写错了——多半是忘写赋值语句了。在嵌入式开发中，这类低级错误如果不被抓出来，可能导致按钮状态没被检测到，系统行为异常且难以调试。

### static_cast 的零开销

`HAL_GPIO_ReadPin()` 返回 `GPIO_PinState`（0 或 1），`static_cast<State>()` 把它转成 `State::Set` 或 `State::UnSet`。`static_cast` 在枚举之间的转换是纯编译时操作——底层值（0 或 1）不变，只是类型信息变了。生成的机器码和直接用 `GPIO_PinState` 完全一样。

### const 成员函数

`read_pin_state()` 被声明为 `const`——它不修改对象的任何成员变量。这是"只读操作"在 C++ 中的标准表达方式。对比 `set_gpio_pin_state()` 也被声明为 `const`——这是因为我们的 GPIO 模板类没有成员变量需要修改，所有的"状态"都存在于硬件寄存器中，而不是 C++ 对象里。

---

## 最小的 C 语言示例

在进入下一篇的完整轮询程序之前，先用一个最小的 C 代码片段验证一下：能不能读到按钮状态？

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
```text

这段代码做了四件事：(1) 使能 GPIOA 和 GPIOC 时钟，(2) 配置 PA0 为上拉输入，(3) 配置 PC13 为推挽输出，(4) 主循环中读取 PA0 并控制 PC13。

⚠️ 注意：这段代码**没有消抖**。快速按一下按钮，LED 可能会闪好几次。下一篇我们会看到这个问题的完整演示和解决方案。

如果你把这段代码烧到板子上，按住按钮时 LED 亮，松开时 LED 灭。最基本的输入输出交互就这样实现了。

---

## 我们回头看

这一篇拆解了两个 HAL API：`HAL_GPIO_Init()` 的输入模式配置和 `HAL_GPIO_ReadPin()` 的底层实现。关键点：

1. 输入初始化只需要 `GPIO_MODE_INPUT` + `GPIO_PULLUP` 两个参数
2. `HAL_GPIO_ReadPin()` 底层就是读 `IDR` 寄存器，一个时钟周期
3. 我们的 `read_pin_state()` 封装加了 `[[nodiscard]]` 和 `const`，返回类型安全的 `State` 枚举

下一篇我们把这段最小代码扩展成完整的 C 语言轮询程序——然后亲眼看到没有消抖会发生什么。
