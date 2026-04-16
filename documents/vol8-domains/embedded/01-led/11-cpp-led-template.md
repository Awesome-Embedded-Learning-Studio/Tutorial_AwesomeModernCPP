---
title: "第16篇：第四次重构 —— LED模板，从通用GPIO到专用抽象"
description: ""
tags:
  - beginner
  - cpp-modern
  - stm32f1
difficulty: beginner
platform: stm32f1
---
# 第16篇：第四次重构 —— LED模板，从通用GPIO到专用抽象

## 前言：当通用变得不够好

上一篇里，我们完成了一件值得骄傲的事情——GPIO模板。`gpio::GPIO<PORT, PIN>` 现在是一个真正通用的GPIO抽象：你可以在任何端口、任何引脚上使用它，设置模式、读写电平、翻转状态，所有操作都通过类型安全的接口完成，编译器在背后帮你搞定了一切。

但通用并不意味着好用。

想想每次你用GPIO模板点亮一个LED，需要写多少东西：

```cpp
gpio::GPIO<GpioPort::C, GPIO_PIN_13> led;
led.setup(gpio::GPIO<GpioPort::C, GPIO_PIN_13>::Mode::OutputPP,
          gpio::GPIO<GpioPort::C, GPIO_PIN_13>::PullPush::NoPull,
          gpio::GPIO<GpioPort::C, GPIO_PIN_13>::Speed::Low);
led.set_gpio_pin_state(gpio::GPIO<GpioPort::C, GPIO_PIN_13>::State::UnSet);  // 点亮
led.set_gpio_pin_state(gpio::GPIO<GpioPort::C, GPIO_PIN_13>::State::Set);    // 熄灭
```

这段代码有四个问题。第一，`setup()` 调用必须手动传入模式、上下拉和速度——但LED的模式永远是推挽输出、无上下拉、低速，这三样东西对于LED来说是不变的事实，不应该由调用者操心。第二，`set_gpio_pin_state()` 的语义是"设置GPIO电平"，而不是"点亮LED"或"熄灭LED"——你必须知道PC13是低电平点亮的，所以点亮要传 `UnSet`，熄灭要传 `Set`，这个认知负担完全不应该存在。第三，每次引用枚举都要写一长串 `gpio::GPIO<GpioPort::C, GPIO_PIN_13>::Mode::OutputPP`，冗长且容易出错。第四，如果你有第二个LED接在别的引脚上，又要复制一套几乎一样的代码。

这些问题的根源在于：GPIO模板是"通用的"，它不知道自己驱动的是一个LED。它不知道LED应该配什么模式，不知道LED是高电平有效还是低电平有效，更不知道"点亮"和"熄灭"是什么意思。

这一篇，我们要在GPIO模板之上，构建一个LED专用的模板类。它把"推挽输出、低电平有效、低速"这些LED特有的硬件知识封装起来，对外只暴露 `on()`、`off()`、`toggle()` 三个语义清晰的接口。用户只需要告诉模板"LED在哪个端口的哪个引脚"，剩下的一切——时钟使能、模式配置、电平逻辑——全部自动完成。

这也是我们整个LED系列的第四次、也是最后一次重构。从最初的C宏方案，到裸C++类，到GPIO模板，再到今天的LED模板，每一次重构都把更多的硬件知识交给编译器，让使用者写更少、更安全的代码。

---

## LED模板的完整设计

先看完整的 `led.hpp`，一共只有30行：

```cpp
#pragma once
#include "gpio/gpio.hpp"

namespace device {

enum class ActiveLevel { Low, High };

template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
class LED : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;

  public:
    LED() {
        Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
    }

    void on() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
    }

    void off() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
    }

    void toggle() const { Base::toggle_pin_state(); }
};

} // namespace device
```

30行代码，但每一行都值得仔细推敲。接下来我们逐段拆解。

### 三个模板参数：端口、引脚、有效电平

```cpp
template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
```

前两个参数 `PORT` 和 `PIN` 直接传给基类 `GPIO<PORT, PIN>`，这一点我们在上一篇GPIO模板中已经详细讨论过——它们在编译时确定具体的端口地址和引脚编号，让编译器生成针对特定硬件的代码。

重点是第三个参数：`ActiveLevel LEVEL`。

`ActiveLevel` 是在 `led.hpp` 中定义的一个枚举类：

```cpp
enum class ActiveLevel { Low, High };
```

它只有两个值：`Low` 表示低电平有效（LED在低电平时点亮），`High` 表示高电平有效（LED在高电平时点亮）。这个概念对应的是实际的硬件电路——Blue Pill开发板上的PC13 LED是连接到GND的，所以MCU输出低电平时LED导通点亮，输出高电平时LED截止熄灭。而如果你自己焊了一个LED接到VCC上，那就是高电平点亮、低电平熄灭。

`LEVEL` 的默认值是 `ActiveLevel::Low`，因为Blue Pill板载LED就是低电平有效的。默认模板参数是C++中一个优雅的特性：当默认值满足大多数使用场景时，使用者不需要显式提供这个参数。所以对于Blue Pill的标准用法，你只需要写：

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

第三个参数自动取 `ActiveLevel::Low`。如果你的LED是高电平有效的，只需要多写一个参数：

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> led;
```

这就是默认模板参数的设计哲学：让简单的事情保持简单，让复杂的事情成为可能。

### 继承与类型别名：站在GPIO的肩膀上

```cpp
class LED : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;
```

LED继承自GPIO模板。当LED被实例化为 `LED<GpioPort::C, GPIO_PIN_13>` 时，基类就变成了 `GPIO<GpioPort::C, GPIO_PIN_13>`——一个完整的、针对GPIOC第13引脚的GPIO模板实例。这意味着LED自动拥有了基类所有的能力：`setup()`、`set_gpio_pin_state()`、`toggle_pin_state()`、`native_port()`，以及内部的 `GPIOClock` 时钟使能逻辑。

这里有一个微妙的模板实例化机制值得注意。`gpio::GPIO<PORT, PIN>` 中的 `PORT` 和 `PIN` 不是具体的值，而是LED模板自己的模板参数。编译器在看到 `LED<GpioPort::C, GPIO_PIN_13>` 的时候，会把 `PORT` 替换为 `GpioPort::C`，`PIN` 替换为 `GPIO_PIN_13`，然后实例化基类 `GPIO<GpioPort::C, GPIO_PIN_13>`。这是一个两阶段的实例化过程：LED的模板参数先被确定，然后基类的模板跟着被实例化。

`using Base = gpio::GPIO<PORT, PIN>` 是一个类型别名。它不是定义一个新的类型，只是给一个已有的类型起了一个更短的名字。在这之后，代码中所有的 `Base::` 都等价于 `gpio::GPIO<PORT, PIN>::`。在模板编程中，基类的完整名称往往很长，类型别名几乎是必须的——否则 `Base::Mode::OutputPP` 要写成 `gpio::GPIO<PORT, PIN>::Mode::OutputPP`，既冗长又容易在维护时出错。

这是一种在C++模板代码中广泛使用的惯例。你会在任何严肃的模板库中看到类似的写法：`using Base = ...` 或 `typedef ... Base`，目的都是简化对基类成员的引用。

### 构造函数：零配置的奥秘

```cpp
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}
```

这三行是整个"零配置"设计的核心。

LED的构造函数直接调用基类的 `setup()` 方法，传入三个固定的参数：

- **`Mode::OutputPP`**：推挽输出模式。推挽输出是LED驱动的标准配置——它能主动输出高电平和低电平，有较强的驱动能力，适合直接驱动LED。与之相对的开漏输出模式只能拉低电平，需要外部上拉电阻才能输出高电平，LED驱动一般不使用。
- **`PullPush::NoPull`**：无上下拉。GPIO内部的上拉和下拉电阻对于推挽输出模式来说没有意义——推挽输出自己就能驱动电平，不需要外部帮助。另外，STM32F103的PC13引脚本身也不支持内部上下拉，所以这里填 `NoPull` 也是硬件事实的反映。
- **`Speed::Low`**：低速模式。GPIO的输出速度决定了引脚电平变化的上升沿和下降沿速度，速度越快，信号边沿越陡峭，高频性能越好，但同时也会产生更多的电磁干扰（EMI）和功耗。LED闪烁的频率只有几赫兹，对速度完全没有要求，选择低速是最合理的——既降低了功耗，又减少了不必要的信号噪声。

这三样东西对于任何LED来说几乎都是不变的——推挽输出、无上下拉、低速。把它们硬编码在LED的构造函数中，意味着使用LED模板的人永远不需要操心这三个参数。创建LED对象的那一刻，构造函数自动完成配置。这就是"零配置"的含义。

而更妙的是，`setup()` 内部会自动调用 `GPIOClock::enable_target_clock()`，后者通过 `if constexpr` 在编译时确定应该使能哪个端口的时钟。所以整个初始化链是：LED构造 -> `setup(OutputPP, NoPull, Low)` -> `GPIOClock::enable_target_clock()` -> `__HAL_RCC_GPIOC_CLK_ENABLE()` -> `HAL_GPIO_Init()`。从时钟使能到引脚配置，一气呵成。

使用者只需声明一个变量：

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

这一行就完成了所有初始化。不需要单独调用初始化函数，不需要手动配置任何参数。

### on()和off()：编译时的电平分支

```cpp
void on() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
}

void off() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
}
```

这是整个LED模板中最精巧的部分，也是最能体现模板参数威力的一段代码。

让我们逐步拆解。

`LEVEL` 是模板参数，在编译时就已经确定了具体的值——要么是 `ActiveLevel::Low`，要么是 `ActiveLevel::High`。因此 `LEVEL == ActiveLevel::Low` 是一个编译时常量表达式，对于任何给定的模板实例化，它的结果只有两种可能：`true` 或 `false`。

编译器在优化时（哪怕是 `-O0` 级别），都能直接根据这个常量表达式的结果选择对应的分支，生成没有任何条件判断的机器码。不存在运行时的if-else开销。

对于Blue Pill的PC13 LED（`LEVEL = ActiveLevel::Low`）：

`on()` 的分支判断结果为 `true`，所以 `on()` 最终等价于：

```cpp
void on() const {
    Base::set_gpio_pin_state(Base::State::UnSet);
    // 展开 -> HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)
    // 物理效果：输出低电平 -> LED导通 -> 点亮
}
```

`off()` 的分支判断结果也为 `true`（因为LEVEL仍然是Low），所以 `off()` 最终等价于：

```cpp
void off() const {
    Base::set_gpio_pin_state(Base::State::Set);
    // 展开 -> HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET)
    // 物理效果：输出高电平 -> LED截止 -> 熄灭
}
```

而对于一个高电平有效的LED（`LEVEL = ActiveLevel::High`），情况正好反过来：

`on()` 的分支判断结果为 `false`，选择 `Base::State::Set`：

```cpp
void on() const {
    Base::set_gpio_pin_state(Base::State::Set);
    // 展开 -> HAL_GPIO_WritePin(GPIOx, GPIO_PIN_x, GPIO_PIN_SET)
    // 物理效果：输出高电平 -> LED导通 -> 点亮
}
```

`off()` 的分支判断结果也为 `false`，选择 `Base::State::UnSet`：

```cpp
void off() const {
    Base::set_gpio_pin_state(Base::State::UnSet);
    // 展开 -> HAL_GPIO_WritePin(GPIOx, GPIO_PIN_x, GPIO_PIN_RESET)
    // 物理效果：输出低电平 -> LED截止 -> 熄灭
}
```

这就是模板参数的威力——一份源代码，两种硬件配置，编译器自动生成正确的电平操作，运行时零开销。`on()` 就是"点亮"，`off()` 就是"熄灭"，无论你的LED电路是怎么接的。语义上的正确性由模板保证，使用者不需要关心底层的电平逻辑。

还有一个细节值得注意：这两个方法都被声明为 `const`。因为它们只调用基类的 `set_gpio_pin_state()`，而 `set_gpio_pin_state()` 本身也是 `const` 的——它只是调用 `HAL_GPIO_WritePin()` 写寄存器，不修改任何成员变量。在C++中，不修改对象逻辑状态的方法应该声明为 `const`，这是一种良好的编程习惯，也使得 `const LED&` 引用也能调用这些方法。

### toggle()：委托给基类的翻转

```cpp
void toggle() const { Base::toggle_pin_state(); }
```

`toggle()` 的实现最为简单——直接委托给基类的 `toggle_pin_state()`。

为什么它不需要关心 `ActiveLevel`？因为翻转操作是无条件的：无论当前引脚输出的是高电平还是低电平，`toggle()` 都会把它变成相反的状态。如果LED当前是点亮的（低电平），翻转后就变成熄灭的（高电平），反之亦然。翻转本身不关心"哪个电平代表点亮"，它只关心"变成和现在相反的状态"。

所以 `toggle()` 的行为对于低电平有效和高电平有效的LED都是一致的——翻转当前状态。底层调用的 `HAL_GPIO_TogglePin()` 会读取当前输出数据寄存器（ODR）的对应位，取反后写回。

---

## main.cpp的使用：一切化繁为简

现在让我们看看完整的 `main.cpp`：

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
    /* led setups! */
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;

    while (1) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

逐行来看。

**第一行：`#include "device/led.hpp"`**

引入LED模板。`led.hpp` 内部已经 `#include "gpio/gpio.hpp"`，所以不需要额外引入GPIO头文件。LED模板是使用者唯一需要关心的入口，它封装了对GPIO模板的所有依赖。这是好的模块设计——每一层只暴露必要的接口，内部的实现细节不泄漏给上层。

**第二行：`#include "system/clock.h"`**

引入时钟配置。`clock.h` 中定义了 `ClockConfig` 类，负责将STM32的系统时钟配置到目标频率（64MHz）。

**第三到五行：`extern "C" { #include "stm32f1xx_hal.h" }`**

HAL头文件必须用 `extern "C"` 包裹。这是因为 `stm32f1xx_hal.h` 是一个纯C语言头文件，里面的函数声明使用的是C语言的名称修饰（name mangling）规则。C++编译器默认使用C++的名称修饰规则，两者不兼容。如果不加 `extern "C"`，链接器会找不到HAL函数的定义，报出"undefined reference"错误。

`extern "C"` 告诉C++编译器：花括号内的所有声明都使用C语言的链接规范，不要对函数名进行C++风格的名称修饰。这是C++项目中调用C库的标准做法，在嵌入式开发中极为常见。

**第七行：`HAL_Init()`**

初始化HAL库。这个函数做了几件重要的事情：配置Flash预取指缓冲区、配置SysTick定时器为1ms中断周期、初始化HAL的内部状态机。后续所有HAL函数（包括 `HAL_Delay()`、`HAL_GPIO_Init()` 等）都依赖于这个初始化。

**第八行：`clock::ClockConfig::instance().setup_system_clock()`**

通过单例模式获取时钟配置实例，然后配置系统时钟。这一行涉及两个设计模式的组合使用——CRTP单例和硬件初始化封装。我们会在下一节专门讨论这个设计。

**第十行：`device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led`**

这一行做了所有事情。让我列出它触发的完整操作链：

1. 编译器实例化 `LED<GpioPort::C, GPIO_PIN_13>`，`LEVEL` 取默认值 `ActiveLevel::Low`
2. 实例化基类 `GPIO<GpioPort::C, GPIO_PIN_13>`
3. 调用LED构造函数
4. 构造函数调用 `Base::setup(OutputPP, NoPull, Low)`
5. `setup()` 内部调用 `GPIOClock::enable_target_clock()`
6. `GPIOClock::enable_target_clock()` 中，`if constexpr (PORT == GpioPort::C)` 匹配成功，调用 `__HAL_RCC_GPIOC_CLK_ENABLE()`
7. `setup()` 构造 `GPIO_InitTypeDef` 结构体，填入 Pin=GPIO_PIN_13, Mode=OutputPP, Pull=NoPull, Speed=Low
8. 调用 `HAL_GPIO_Init(GPIOC, &init_types)` 完成引脚配置

从C宏版本的30多行代码，到这一行声明。这就是抽象的力量。

**第十二到十七行：主循环**

```cpp
while (1) {
    HAL_Delay(500);
    led.on();
    HAL_Delay(500);
    led.off();
}
```

主循环的逻辑清晰得不能再清晰了：等500毫秒，点亮LED，等500毫秒，熄灭LED，周而复始。`HAL_Delay()` 基于SysTick中断实现毫秒级延时，精度取决于系统时钟配置。`led.on()` 和 `led.off()` 的语义一目了然，不需要任何注释解释它们做了什么。

如果你想在另一个引脚上加一个LED呢？只需要一行声明：

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0> led2;
```

然后在循环里调用 `led2.on()`、`led2.off()`。不需要复制任何头文件或源文件，不需要修改任何宏定义，不需要手动配置GPIO。每个LED就是一个对象，创建即用，各司其职。

---

## CRTP单例：时钟配置的设计

在 `main.cpp` 中有一行代码用到了一个我们还没详细讨论过的模式：

```cpp
clock::ClockConfig::instance().setup_system_clock();
```

这行代码的背后是一个基于CRTP的单例模式。让我们先看两个源文件。

第一个是 `base/simple_singleton.hpp`，这是一个通用的CRTP单例基类：

```cpp
#pragma once

namespace base {
template <typename SingletonClass> class SimpleSingleton {
  public:
    SimpleSingleton() = default;
    ~SimpleSingleton() = default;

    static SingletonClass& instance() {
        static SingletonClass _instance;
        return _instance;
    }

  private:
    /* Never Shell A Single Instance Copyable And Movable */
    SimpleSingleton(const SimpleSingleton&) = delete;
    SimpleSingleton(SimpleSingleton&&) = delete;
    SimpleSingleton& operator=(const SimpleSingleton&) = delete;
    SimpleSingleton& operator=(SimpleSingleton&&) = delete;
};
} // namespace base
```

第二个是 `system/clock.h`，`ClockConfig` 通过继承这个基类来获得单例能力：

```cpp
#pragma once
#include "base/simple_singleton.hpp"
#include <cstdint>

namespace clock {
class ClockConfig : public base::SimpleSingleton<ClockConfig> {
  public:
    /* Setup the System clocks */
    void setup_system_clock();

    [[nodiscard("You should accept the clock frequency, it's what you request!")]]
    uint64_t clock_freq() const noexcept;
};
} // namespace clock
```

CRTP的全称是 Curiously Recurring Template Pattern——奇怪递归模板模式。名字听起来很奇怪，但原理并不复杂：子类 `ClockConfig` 把自己作为模板参数传给基类 `SimpleSingleton<ClockConfig>`。这样一来，基类中的 `instance()` 方法返回的就是 `ClockConfig&`，而不是某个通用的基类引用。

这种写法的好处是不需要虚函数。传统的单例模式往往通过虚函数来提供多态的 `instance()` 方法，但虚函数需要虚函数表（vtable），在嵌入式环境中这是不必要的开销。CRTP通过模板在编译时确定了具体的子类类型，完全消除了运行时的多态开销。

`instance()` 方法的实现使用了C++11的一个保证：函数内部的 `static` 局部变量在首次执行到该声明时初始化，且初始化是线程安全的。所以 `static SingletonClass _instance` 只会构造一次，即使多个线程同时调用 `instance()`，编译器也会保证只有一个线程执行构造，其余线程等待。在裸机嵌入式环境中这不太重要（通常只有一个线程），但在更复杂的系统中这是一个有价值的保证。

基类的 `private` 部分删除了复制构造函数、移动构造函数、复制赋值运算符和移动赋值运算符。这四个 `= delete` 声明确保单例不会被意外复制或移动——如果你写了 `auto copy = ClockConfig::instance()`，编译器会直接报错。注释 "Never Shell A Single Instance Copyable And Movable" 中的 "Shell" 应该是 "Share" 的笔误，但意图很明确：单例永远不应该被复制。

为什么时钟配置需要是单例？STM32F103只有一个时钟树，系统时钟只有一套配置。如果允许创建多个 `ClockConfig` 实例，就可能出现这样的代码：

```cpp
clock::ClockConfig config1;
config1.setup_system_clock();  // 配置为64MHz

clock::ClockConfig config2;
config2.setup_system_clock();  // 又配置一次——可能中断正在使用时钟的外设
```

虽然重复调用 `setup_system_clock()` 不一定立刻导致硬件故障（HAL函数通常会重新配置寄存器），但它是一种设计上的缺陷——允许多实例暗示着"每个实例可以有不同的配置"，而时钟配置在物理上应该是全局唯一的。单例模式在类型系统层面杜绝了这种误用。

`clock_freq()` 方法上标注了 `[[nodiscard("You should accept the clock frequency, it's what you request!")]]` 属性。这是C++17引入的特性，告诉编译器：这个返回值不应该被忽略。如果你写了 `config.clock_freq()` 而没有接收返回值，编译器会发出警告。在嵌入式开发中，查询时钟频率通常是为了后续计算（比如波特率、定时器周期），忽略了返回值几乎一定是bug。

CRTP单例不是本篇的重点——它会在后续章节中详细展开。但你需要理解它在 `main.cpp` 中的作用：提供一个全局唯一的、线程安全的、不可复制的时钟配置入口点。`ClockConfig::instance()` 返回唯一实例的引用，`.setup_system_clock()` 在这个实例上调用配置方法。整个表达式链式调用，一行代码完成时钟初始化。

---

## 一个关于构造时机的踩坑经验

在继续对比之前，有一个与LED模板使用方式直接相关的坑值得专门说一说。

⚠️ 注意：LED模板的构造函数在对象创建时立即配置GPIO。这意味着如果你在全局作用域声明LED对象，它的构造会发生在`main()`之前（在C++静态初始化阶段），此时HAL可能还没初始化。所以LED对象必须在 `HAL_Init()` 和时钟配置之后声明——也就是在 `main()` 函数内部。这个顺序不能乱，否则GPIO配置虽然不报错，但时钟未使能时寄存器写入会被硬件默默忽略。

所以LED对象必须在 `HAL_Init()` 和时钟配置之后声明——也就是在 `main()` 函数内部。我们的 `main.cpp` 中就是这样做的：先 `HAL_Init()`，再 `clock::ClockConfig::instance().setup_system_clock()`，最后才声明 `device::LED<...> led`。这个顺序不能乱。

---

## 与C宏方案的最终对比

从第一篇到这一篇，我们经历了四次重构。现在是时候做一次彻底的对比了。

### C宏方案的完整代码

典型的C宏LED驱动分为头文件和源文件两部分。

**led.h：**

```c
#ifndef LED_H
#define LED_H

#include "stm32f1xx_hal.h"

#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13
#define LED_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#define LED_ON_LEVEL    GPIO_PIN_RESET   /* 低电平点亮 */
#define LED_OFF_LEVEL   GPIO_PIN_SET     /* 高电平熄灭 */

void led_init(void);
void led_on(void);
void led_off(void);
void led_toggle(void);

#endif
```

**led.c：**

```c
#include "led.h"

void led_init(void) {
    LED_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);
}

void led_on(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_ON_LEVEL);
}

void led_off(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_OFF_LEVEL);
}

void led_toggle(void) {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}
```

**main.c：**

```c
#include "led.h"

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

总共约40行驱动代码加15行main函数。看起来也算整洁。但问题在于——每个LED需要单独的一对头文件和源文件。

### C++模板方案的完整代码

**device/led.hpp（LED模板，约30行）：**

```cpp
#pragma once
#include "gpio/gpio.hpp"

namespace device {

enum class ActiveLevel { Low, High };

template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
class LED : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;
  public:
    LED() {
        Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
    }
    void on() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
    }
    void off() const {
        Base::set_gpio_pin_state(
            LEVEL == ActiveLevel::Low ? Base::State::Set : Base::State::UnSet);
    }
    void toggle() const { Base::toggle_pin_state(); }
};

} // namespace device
```

**main.cpp：**

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    while (1) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

### 逐项对比

两者的 `main` 函数都差不多简洁，都只有十几行。看起来差距不大。但真正的差异在于扩展性——当你需要在项目中加第二个LED的时候。

**C方案加第二个LED（比如PA0）：**

你需要复制 `led.h` 为 `led2.h`，复制 `led.c` 为 `led2.c`，然后修改所有的宏定义——`LED_PORT` 改为 `GPIOA`，`LED_PIN` 改为 `GPIO_PIN_0`，时钟使能改为 `__HAL_RCC_GPIOA_CLK_ENABLE()`。如果LED是高电平有效的，还要交换 `LED_ON_LEVEL` 和 `LED_OFF_LEVEL`。两个文件，至少修改六处。

更糟糕的是，如果你有10个LED呢？10对头文件和源文件，每对都要手动维护。如果HAL库的API发生了变化，你要改10个地方。

**C++方案加第二个LED（比如PA0，高电平有效）：**

只需要在 `main.cpp` 中加一行：

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> led2;
```

一行代码。时钟使能、模式配置、电平逻辑全部由模板自动处理。不需要新建文件，不需要复制代码，不需要修改任何已有代码。

这就是模板元编程在嵌入式中的真正价值——不是让 `main()` 看起来更短（`main()` 的长度在两种方案中差不多），而是让扩展的边际成本趋近于零。每增加一个LED，C方案的成本是线性的（新文件、新代码、新维护），而C++方案的成本是常数级的（一行声明）。

### 编译产物对比

一个经常被问到的问题是：C++模板方案的代码体积会不会更大？

答案是不会。因为LED模板的所有参数在编译时都是常量，编译器能够进行完整的内联优化。`led.on()` 最终生成的机器码和直接调用 `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)` 是完全相同的。没有虚函数表，没有运行时多态，没有额外的函数调用开销。这就是所谓的"零开销抽象"——你付出的是编译时间（模板实例化需要编译器做更多工作），换回的是运行时性能的零损失。

如果你用 `arm-none-eabi-objdump -d` 反汇编最终固件，会发现C++模板方案和C宏方案生成的机器码在指令层面几乎一模一样。抽象的代价被完全转移到了编译期。

---

## 收尾

LED模板完成了。从最初的C宏方案，到裸C++类封装，到GPIO通用模板，再到今天的LED专用模板——四次重构，每一步都把更多的硬件知识从"需要开发者记住的事情"变成了"编译器自动处理的事情"。

回顾这四步的演进脉络：第一步，C宏方案把硬件参数集中在头文件的宏定义中，虽然集中但仍然是文本替换，没有类型安全。第二步，C++类封装把宏定义变成了成员函数，有了作用域和类型检查，但只能处理特定的端口和引脚。第三步，GPIO模板把端口和引脚参数化，实现了通用的GPIO抽象，但使用者仍然需要知道LED该怎么配置。第四步，LED模板在GPIO模板之上构建了领域专用的抽象，把LED的所有硬件知识——推挽输出、低电平有效、低速——封装在了30行代码中。

最终的成果是：使用者只需要写一行声明，就能获得一个完整配置好的LED对象。`on()`、`off()`、`toggle()` 的语义清晰明确，不需要关心底层的电平逻辑。模板参数在编译时确定一切，运行时没有任何额外开销。新增LED的成本是一行代码，而不是一对文件。

下一篇，我们将收尾这个LED系列中涉及的C++23和现代C++特性，系统性地梳理 `constexpr`、`if constexpr`、`enum class`、`[[nodiscard]]`、`extern "C"` 等特性在嵌入式场景中的具体应用，并用编译产物的实际对比来证明：这些抽象确实是零开销的。我们不止要写出优雅的代码，还要证明它和手写寄存器操作一样高效。
