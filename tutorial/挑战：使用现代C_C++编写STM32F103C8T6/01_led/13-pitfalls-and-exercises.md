# 第18篇：常见坑位与实战练习 —— 把LED玩出花样来

> 承接：所有原理和代码都讲完了，LED能闪了。但实际动手时总会遇到各种诡异的问题——这一篇先把常见坑位全部标出来，然后给出三个递进式练习帮你把知识从"看懂"变成"写得出"。

---

## 坑位1：忘开时钟——外设沉默的杀手

这是整个STM32学习过程中排名第一的坑。症状非常诡异：你的代码完全"正确"，`HAL_GPIO_Init` 没有返回错误，`HAL_GPIO_WritePin` 也没有问题，但是LED就是不亮。用调试器查看GPIO寄存器，发现写入的值根本没有生效——寄存器还是复位后的默认值。

原因很简单：GPIO端口的时钟没有使能。STM32上电后，为了省电，所有外设的时钟默认都是关闭的。没有时钟，外设的寄存器就处于"断电"状态——CPU的总线写操作会被硬件默默接受但不执行。这就像你对着一个关了机的电脑按键盘——按键动作确实发生了，但电脑不会有任何反应。

排查方法：第一反应就查时钟。用调试器读 `RCC_APB2ENR` 寄存器（地址 `0x40021018`），看看对应GPIO端口的位是否为1。如果为0，说明时钟没开。

我们的C++模板已经从设计上消除了这个坑：`setup()` 方法内部自动调用 `GPIOClock::enable_target_clock()`，你不可能忘记开时钟。但如果你绕过模板直接用HAL API，这个坑依然存在。

---

## 坑位2：推挽和开漏选错——LED时亮时不亮

如果你误把GPIO配置为开漏输出（`GPIO_MODE_OUTPUT_OD`），LED的表现会非常诡异：可能完全不亮，可能非常暗，或者亮度不稳定。

原因是开漏输出只有N-MOS下管在工作。输出"高电平"时，引脚实际上是浮空状态——没有主动驱动到VDD。LED两端的电压取决于外部电路是否有上拉路径。Blue Pill的PC13 LED电路没有外部上拉电阻，所以开漏输出"高电平"时LED基本不会亮。

解决方案很简单：LED控制一律用推挽输出（`GPIO_MODE_OUTPUT_PP`）。我们的LED模板已经默认选择推挽，所以只要你使用模板就不会踩这个坑。

---

## 坑位3：PC13的上下拉陷阱

你可能觉得给PC13配置上拉或下拉是个好主意——比如在LED不亮时让引脚有个确定的电平。但ST的数据手册明确说明PC13/14/15这三个引脚的内部上下拉功能不可用。即使你在 `GPIO_InitTypeDef` 中设置 `Pull=GPIO_PULLUP`，HAL也不会报错——它会把你的配置写入寄存器，但硬件会默默忽略。

所以对于PC13，Pull必须设为 `GPIO_NOPULL`。我们的LED模板默认就是NoPull，这既是正确的选择，也是PC13上唯一可用的选择。

---

## 坑位4：速度选择误区——高速不会让LED闪得更快

很多初学者以为把GPIO速度设为 `GPIO_SPEED_FREQ_HIGH` 就能让LED切换得更快。但实际上，速度设置控制的是输出信号的压摆率（slew rate）——也就是电压从一个电平跳变到另一个电平的速度有多快。对于LED闪烁（1Hz到10Hz），无论选低速还是高速，人眼都看不出任何区别。高速只会让电压边沿更陡峭，产生更多的电磁干扰（EMI）和更高的瞬态电流。

经验法则：默认用低速，只有在高速外设（SPI时钟超过几MHz、UART高波特率等）场景下才提高速度。

---

## 练习1：多LED控制

**任务：** 在Blue Pill上控制两个LED——PC13的板载LED以1Hz闪烁，假设在PA0上外接一个LED以2Hz闪烁。假设PA0的LED是高电平有效（LED正极接PA0，负极接GND）。

**完整参考答案：**

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    // 板载LED：PC13，低电平有效（默认）
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> board_led;

    // 外接LED：PA0，高电平有效
    device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> ext_led;

    uint32_t counter = 0;
    while (1) {
        HAL_Delay(250);  // 250ms为一个节拍
        counter++;

        // PC13 LED：每4个节拍切换一次 = 1Hz
        if (counter % 4 == 0) {
            board_led.toggle();
        }

        // PA0 LED：每2个节拍切换一次 = 2Hz
        if (counter % 2 == 0) {
            ext_led.toggle();
        }
    }
}
```

**讨论：** 两个LED是不同的类型——`LED<GpioPort::C, GPIO_PIN_13, ActiveLevel::Low>` 和 `LED<GpioPort::A, GPIO_PIN_0, ActiveLevel::High>`。编译器为每个类型生成独立的代码。板载LED用默认的 `ActiveLevel::Low`（省略了第三个模板参数），外接LED显式指定 `ActiveLevel::High`。每个LED的构造函数自动使能对应端口的时钟——board_led使能GPIOC时钟，ext_led使能GPIOA时钟，你不需要手动管理。

---

## 练习2：按钮输入+LED联动

**任务：** 在PA8上接一个按钮（通过10K上拉电阻接到VDD，按下时接地）。当按钮按下时PC13的LED点亮，松开时LED熄灭。

**完整参考答案：**

```cpp
#include "device/gpio/gpio.hpp"
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    // LED输出：PC13，低电平有效
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;

    // 按钮输入：PA8，上拉（按下为低电平）
    using BtnGPIO = device::gpio::GPIO<device::gpio::GpioPort::A, GPIO_PIN_8>;
    BtnGPIO button;
    button.setup(BtnGPIO::Mode::Input, BtnGPIO::PullPush::PullUp);

    while (1) {
        // 读取按钮状态：按下时为低电平（GPIO_PIN_RESET）
        GPIO_PinState state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);

        if (state == GPIO_PIN_RESET) {
            led.on();   // 按钮按下，LED点亮
        } else {
            led.off();  // 按钮松开，LED熄灭
        }

        HAL_Delay(10);  // 简单去抖延时
    }
}
```

**讨论：** 这里直接使用了GPIO模板（而不是LED模板）来配置按钮引脚，因为按钮是输入设备。按钮配置为输入模式（`Mode::Input`），启用内部上拉电阻（`PullPush::PullUp`）——按钮悬空时PA8被拉到高电平，按下时接地变低电平。`HAL_GPIO_ReadPin` 直接读取IDR寄存器，返回 `GPIO_PIN_SET` 或 `GPIO_PIN_RESET`。10ms延时是最简单的去抖方案——实际项目中可能需要更复杂的去抖算法。

---

## 练习3：GpioPin泛化模板

**任务：** 设计一个更通用的 `GpioPin` 模板，根据模式参数在编译时决定可用的操作方法。输出模式有 `write()` 和 `toggle()`，输入模式有 `read()`。

**完整参考答案：**

```cpp
#pragma once

extern "C" {
#include "stm32f1xx_hal.h"
}

#include <cstdint>

namespace device::gpio {

enum class GpioPort : uintptr_t {
    A = GPIOA_BASE, B = GPIOB_BASE, C = GPIOC_BASE,
    D = GPIOD_BASE, E = GPIOE_BASE,
};

enum class PinMode { Input, Output, Alternate, Analog };

template <GpioPort PORT, uint16_t PIN, PinMode MODE>
class GpioPin {
    static constexpr GPIO_TypeDef* port() noexcept {
        return reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
    }

    static void enable_clock() {
        if constexpr (PORT == GpioPort::A) __HAL_RCC_GPIOA_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::B) __HAL_RCC_GPIOB_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::C) __HAL_RCC_GPIOC_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::D) __HAL_RCC_GPIOD_CLK_ENABLE();
        else if constexpr (PORT == GpioPort::E) __HAL_RCC_GPIOE_CLK_ENABLE();
    }

    static constexpr uint32_t mode_to_hal() {
        if constexpr (MODE == PinMode::Input)      return GPIO_MODE_INPUT;
        else if constexpr (MODE == PinMode::Output) return GPIO_MODE_OUTPUT_PP;
        else if constexpr (MODE == PinMode::Alternate) return GPIO_MODE_AF_PP;
        else return GPIO_MODE_ANALOG;
    }

public:
    GpioPin() {
        enable_clock();
        GPIO_InitTypeDef init{};
        init.Pin = PIN;
        init.Mode = mode_to_hal();
        init.Pull = GPIO_NOPULL;
        init.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(port(), &init);
    }

    void write(bool high) const {
        if constexpr (MODE == PinMode::Output) {
            HAL_GPIO_WritePin(port(), PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }

    void toggle() const {
        if constexpr (MODE == PinMode::Output) {
            HAL_GPIO_TogglePin(port(), PIN);
        }
    }

    bool read() const {
        if constexpr (MODE == PinMode::Input) {
            return HAL_GPIO_ReadPin(port(), PIN) == GPIO_PIN_SET;
        }
        return false;
    }
};

} // namespace device::gpio
```

⚠️ 注意：练习3的 `GpioPin` 模板中，`write()` 和 `read()` 方法通过 `if constexpr` 在不匹配的模式下变成空操作——编译器不会阻止你调用它们，只是静默忽略。如果你希望编译器在输入引脚上调用 `write()` 时直接报错（而不是静默忽略），可以用 `static_assert` 或C++20 Concepts来约束方法的可用性。这是一个值得进一步探索的方向。

**讨论：** 这个 `GpioPin` 模板与之前的 `GPIO` 模板有几个关键区别。

`PinMode` 作为模板参数决定了引脚的角色。声明 `GpioPin<GpioPort::C, GPIO_PIN_13, PinMode::Output>` 时，编译器就知道这是一个输出引脚，`write()` 和 `toggle()` 方法会正常工作。`write()` 和 `read()` 方法内部使用了 `if constexpr` 做编译时守卫。如果你在一个输入引脚上调用 `write()`，由于 `if constexpr` 的条件为假，整个调用会被编译器丢弃——不会产生任何代码。这比运行时的"模式检查+返回错误码"方案高效得多。

构造函数根据 `PinMode` 自动选择正确的HAL模式。`mode_to_hal()` 是一个 `constexpr` 函数，在编译时把 `PinMode` 枚举映射到HAL的 `GPIO_MODE_xxx` 宏。使用方式也很直观：

```cpp
// 输出引脚
GpioPin<GpioPort::C, GPIO_PIN_13, PinMode::Output> led;
led.write(false);  // 输出低电平，LED点亮
led.toggle();

// 输入引脚
GpioPin<GpioPort::A, GPIO_PIN_8, PinMode::Input> button;
bool pressed = button.read();
```

这里有一个微妙的设计决策值得深思——`write()` 和 `read()` 方法在非匹配模式下通过 `if constexpr` 被丢弃，这意味着编译器不会阻止你调用一个"逻辑上不存在"的方法，它只是默默地把调用变成空操作。比如在输入引脚上调用 `write()`，代码能编译通过，但什么都不会发生。如果你希望编译器在输入引脚上调用 `write()` 时直接报错（而不是静默忽略），你需要使用 `static_assert` 或者 SFINAE/Concepts 来约束方法的可用性。这是一个可以进一步探索的方向。

---

## 本章小结

回顾整个LED教程系列，我们从GPIO的硬件原理出发，学会了HAL API的使用，看到了C宏方案的局限，然后通过四次渐进式重构（enum class → 模板参数 → if constexpr → LED模板），最终得到了一个类型安全、零配置、零开销的LED驱动抽象。

每一步重构都解决了一个具体问题，每引入一个C++特性都有明确的目的。这不是为了炫技而使用现代C++——而是因为传统C方案在类型安全和代码复用方面的局限性，在复杂项目中会越来越痛。

你现在有了一套可复用的device层代码：`gpio.hpp`、`led.hpp`、`simple_singleton.hpp`。它们将陪伴你进入后续教程——定时器中断、UART通信、SPI驱动——每一步都会在现有的模板基础上继续构建。

下一篇教程预告：SysTick定时器与中断。我们将脱离 `HAL_Delay` 的轮询模式，进入基于中断的LED闪烁，并引入更多的C++23特性。给板子拍张照不过分。
