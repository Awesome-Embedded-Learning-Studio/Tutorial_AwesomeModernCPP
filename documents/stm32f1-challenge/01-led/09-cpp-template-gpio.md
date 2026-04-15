# 第14篇：第二次重构 —— 模板登场，编译时绑定端口和引脚

> 承接上一篇：`enum class` 解决了类型安全问题，但端口和引脚仍然是运行时参数。这一篇引入C++模板的核心武器——非类型模板参数（NTTP），把端口和引脚变成编译时常量。

---

## 模板是什么——嵌入式开发者友好版

如果你之前没接触过C++模板，不要被它的语法吓到。模板本质上是一个"代码生成器"——你写一个通用的"蓝图"，编译器根据你提供的参数自动生成具体的代码。

你可以把它类比成芯片的设计图纸：你画一张GPIO端口的通用图纸，上面有"端口号"和"引脚号"两个空位。当你需要GPIOC的Pin13时，你在空位上填上"C"和"13"，编译器就帮你生成一份专门针对GPIOC Pin13的代码。如果你还需要GPIOA的Pin0，再填一次空位就行。每份生成的代码都是独立的、优化过的，就像你手写了两份不同的代码一样。

对于嵌入式开发来说，模板的威力在于：你可以在编译时就把所有"已知"的信息固化到代码中，运行时只执行"真正需要"的操作。GPIO的端口和引脚在设计时就已经确定了——你在Blue Pill板上控制PC13的LED，这个信息从项目开始到结束都不会变。既然如此，为什么不让编译器在编译时就帮你把这些常量"烧死"在代码里？

---

## 非类型模板参数——NTTP

C++模板有两种参数：类型参数和非类型参数。类型参数是我们最常见的，用 `typename` 或 `class` 声明，代表一个类型。非类型参数（NTTP）则是一个具体的值——一个整数、一个枚举值、或者一个指针。

在嵌入式开发中，NTTP特别有用，因为硬件配置参数（端口号、引脚号、地址）都是编译时常量。我们的GPIO模板正是利用了这一点：

```cpp
template <GpioPort PORT, uint16_t PIN>
class GPIO {
    // ...
};
```

这里有两个NTTP：`PORT` 是 `GpioPort` 类型的枚举值（如 `GpioPort::C`），`PIN` 是 `uint16_t` 类型的整数（如 `GPIO_PIN_13 = 0x2000`）。

当你写 `GPIO<GpioPort::C, GPIO_PIN_13>` 时，编译器会生成一个全新的类，其中 `PORT` 被替换为 `GpioPort::C`，`PIN` 被替换为 `GPIO_PIN_13`。这个类不包含任何成员变量——`PORT` 和 `PIN` 不存在于对象中，它们只存在于类型系统中。

这意味着：

```cpp
GPIO<GpioPort::C, GPIO_PIN_13> led1;
GPIO<GpioPort::A, GPIO_PIN_0> led2;
```

`led1` 和 `led2` 是完全不同的类型。它们没有共享的虚函数表，没有成员变量，`sizeof(led1) = sizeof(led2) = 1`（C++规定空类至少占1字节）。类型系统帮你在编译时就区分了不同的引脚配置，运行时不需要任何额外存储。

---

## constexpr native_port()——编译时地址转换

这是整个GPIO模板中技术含量最高的三行代码：

```cpp
static constexpr GPIO_TypeDef* native_port() noexcept {
    return reinterpret_cast<GPIO_TypeDef*>(
        static_cast<uintptr_t>(PORT)
    );
}
```

它做了三件事，每一步都有明确的理由。

第一步，`static_cast<uintptr_t>(PORT)`：从 `GpioPort` 枚举中提取底层地址值。因为 `PORT` 是 `GpioPort::C`，底层值是 `GPIOC_BASE = 0x40011000`。这个操作在编译时完成——`PORT` 是模板参数，编译器知道它的精确值。

第二步，`reinterpret_cast<GPIO_TypeDef*>(...)`：把整数地址转换为GPIO寄存器结构体指针。这告诉编译器"在地址 `0x40011000` 处有一组GPIO寄存器"。`reinterpret_cast` 是C++中表示"我知道我在干什么，请信任我"的转型——它不做任何检查，因为嵌入式开发中我们确实知道硬件寄存器的地址。

第三步，`constexpr`：整个函数可以在编译时求值。调用 `native_port()` 在概念上等同于写 `GPIOC`，但它是类型安全的、经过编译器验证的。`noexcept` 承诺这个函数不会抛出异常——在 `-fno-exceptions` 的嵌入式环境中，这是自然的保证。

---

## setup()方法——把所有转换组合起来

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIOClock::enable_target_clock();
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

我们逐行拆解。`GPIOClock::enable_target_clock()` 首先使能时钟——下一篇会详细讲它的 `if constexpr` 实现。`GPIO_InitTypeDef init_types{}` 用聚合初始化把所有字段清零。`init_types.Pin = PIN` 中 `PIN` 是模板参数，编译时已知，编译器会直接把 `GPIO_PIN_13` 嵌入到指令中。三个 `static_cast<uint32_t>()` 从 `enum class` 提取底层值传给HAL。最后 `HAL_GPIO_Init(native_port(), &init_types)` 调用HAL初始化——`native_port()` 在编译时返回 `GPIOC`。

注意 `PullPush` 和 `Speed` 参数有默认值，这意味着你可以只传 `Mode`：

```cpp
gpio.setup(Mode::OutputPP);                                 // 默认NoPull, 默认High
gpio.setup(Mode::OutputPP, PullPush::PullUp);               // 指定PullPush, 默认High
gpio.setup(Mode::OutputPP, PullPush::NoPull, Speed::Low);   // 全部指定
```

函数默认参数是C++的便利特性——在保持API灵活性的同时简化了最常见的调用方式。

---

## set_gpio_pin_state()和toggle_pin_state()

```cpp
enum class State { Set = GPIO_PIN_SET, UnSet = GPIO_PIN_RESET };

void set_gpio_pin_state(State s) const {
    HAL_GPIO_WritePin(native_port(), PIN, static_cast<GPIO_PinState>(s));
}

void toggle_pin_state() const {
    HAL_GPIO_TogglePin(native_port(), PIN);
}
```

`State` 枚举封装了引脚状态——`Set` 对应高电平，`UnSet` 对应低电平。`static_cast<GPIO_PinState>(s)` 把我们的 `State` 转换回HAL的 `GPIO_PinState`。`const` 修饰表示这些方法不修改对象状态——虽然对象本来就没有成员变量。

`native_port()` 和 `PIN` 在编译时已知，编译器会在 `-O2` 优化下把这两个函数完全内联。最终生成的机器码与直接调用 `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)` 完全一致。

---

## 零开销抽象的证明

当你写：

```cpp
GPIO<GpioPort::C, GPIO_PIN_13> led;
led.set_gpio_pin_state(GPIO<GpioPort::C, GPIO_PIN_13>::State::UnSet);
```

编译器在 `-O2` 优化下生成的代码与直接写：

```c
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
```

完全一致。模板参数在编译时已经被替换为具体值，`native_port()` 在编译时返回 `GPIOC`，`PIN` 在编译时替换为 `GPIO_PIN_13`。没有运行时查找，没有虚函数调用，没有额外的存储开销。

说到零开销，有一个模板的"隐性成本"值得提前了解——代码膨胀（code bloat）。如果你用10种不同的模板参数组合实例化GPIO类，编译器会为每种组合生成一份独立的代码。在我们的场景中这不是问题，通常只有2-3个不同的GPIO配置。但如果你在大型项目中大量使用模板，要注意检查最终的Flash使用量。`arm-none-eabi-size` 是你的好朋友，编译后跑一下就能看到各段的大小。

这就是"零开销抽象"（zero-overhead abstraction）的含义：你用C++的高级特性写了更安全、更可维护的代码，但编译出的机器码与手写C代码一模一样。C++的创始人Bjarne Stroustrup说过："你不使用的东西，你不应该为它付出代价。"我们的GPIO模板完美地践行了这一原则——模板的"代价"只体现在编译时间上，不在STM32的64KB Flash上。

⚠️ 注意：模板的一个常见陷阱是"代码膨胀"——如果你用10种不同的模板参数组合实例化GPIO类，编译器会生成10份独立的代码。在我们的场景中这不是问题（通常只有2-3个不同的GPIO配置），但如果你在大型项目中大量使用模板，要注意检查最终的Flash使用量。`arm-none-eabi-size` 是你的好朋友。

---

## 与C宏方案的对比

C宏方案中，端口和引脚通过 `#define` 定义，分散在头文件中。模板方案中，端口和引脚通过模板参数在编译时绑定到类型中。关键差异在于：C++方案中，端口和引脚是类型的一部分。你不可能"忘记"指定端口或引脚——编译器会强制你在声明变量时提供所有模板参数。而C宏方案中，如果你忘了 `#include "led.h"` 或者 `LED_PORT` 宏没有被定义，编译错误信息会非常晦涩。

---

## 我们走到了哪一步

GPIO模板的骨架搭好了，但还有一个关键功能没有实现：时钟使能。`setup()` 方法调用了 `GPIOClock::enable_target_clock()`，但我们还没讲它是怎么工作的。下一篇我们就来揭开这个谜底——`if constexpr` 如何在编译时自动选择正确的时钟使能宏。这是整个模板设计中最优雅的部分。
