---
title: "第13篇：第一次重构 —— enum class取代宏，类型安全的开始"
description: ""
tags:
  - beginner
  - cpp-modern
  - stm32f1
difficulty: beginner
platform: stm32f1
---
# 第13篇：第一次重构 —— enum class取代宏，类型安全的开始

> 承接上一篇：C宏方案能跑但有问题——类型不安全、端口和时钟没有强制关联、代码无法复用。现在我们迈出C++重构的第一步：用 `enum class` 替代宏定义。

---

## 为什么要替换宏

上一篇的C宏LED驱动看起来还不错——宏定义集中了硬件参数，函数封装了操作逻辑。但问题出在宏本身：`#define LED_PORT GPIOC` 展开后就是 `((GPIO_TypeDef *)0x40011000UL)`——一个裸的整数地址。编译器不会帮你检查这个值是否合理，也不会阻止你把一个随机的整数赋给期望 `GPIO_TypeDef*` 的函数。

`enum class` 是C++11引入的特性，它把我们从"宏的海洋"带入了"类型安全的世界"。用 `enum class` 重新定义GPIO参数后，编译器会在编译时就帮你检查类型——你不可能把一个模式值传给期望上下拉参数的函数，也不可能把端口A的地址传给期望端口C的操作。

---

## GpioPort枚举——类型安全的端口地址

在 `device/gpio/gpio.hpp` 中，端口是这样定义的：

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,    // 0x40010800
    B = GPIOB_BASE,    // 0x40010C00
    C = GPIOC_BASE,    // 0x40011000
    D = GPIOD_BASE,    // 0x40011400
    E = GPIOE_BASE,    // 0x40011800
};
```

这里有几个设计决策需要解释。首先，为什么底层类型是 `uintptr_t` 而不是 `uint32_t`？因为枚举值是内存地址，`uintptr_t` 是C标准定义的"足以容纳指针的无符号整数类型"——在32位ARM上它就是 `uint32_t`，但在64位平台上它会自动变成64位。用 `uintptr_t` 比 `uint32_t` 更能表达"这是一个地址"的语义，也使代码在理论上有更好的可移植性。

其次，为什么用 `GPIOA_BASE` 而不是 `GPIOA`？`GPIOA` 是CMSIS定义的指针常量——它已经被 cast 成了 `GPIO_TypeDef*` 类型。而枚举值必须是整数常量表达式，不能是指针。`GPIOA_BASE` 是纯整数地址，可以作为枚举值。后面我们会看到 `constexpr native_port()` 如何把这个整数地址转回 `GPIO_TypeDef*` 指针。

最后，为什么用 `enum class` 而不是普通 `enum`？原因是作用域隔离。普通 `enum` 的成员会"泄漏"到外部作用域——如果你定义了两个普通枚举 `enum Color { Red, Green }` 和 `enum Pull { PullUp, PullDown }`，编译器不一定报错，但如果你在两个枚举中都定义了同名的成员，就会产生冲突。`enum class` 的成员必须通过 `GpioPort::A` 这种完整限定名来访问，不同的 `enum class` 之间绝不会冲突。

---

## Mode、PullPush、Speed——枚举化的HAL常量

GPIO的三个核心配置参数也被重新定义为 `enum class`：

```cpp
enum class Mode : uint32_t {
    Input = GPIO_MODE_INPUT,
    OutputPP = GPIO_MODE_OUTPUT_PP,
    OutputOD = GPIO_MODE_OUTPUT_OD,
    AfPP = GPIO_MODE_AF_PP,
    AfOD = GPIO_MODE_AF_OD,
    Analog = GPIO_MODE_ANALOG,
    ItRising = GPIO_MODE_IT_RISING,
    ItFalling = GPIO_MODE_IT_FALLING,
    // ... 更多模式
};

enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,
    PullUp = GPIO_PULLUP,
    PullDown = GPIO_PULLDOWN,
};

enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,
    Medium = GPIO_SPEED_FREQ_MEDIUM,
    High = GPIO_SPEED_FREQ_HIGH,
};
```

这里有一个贯穿始终的设计原则：底层类型 `uint32_t` 与HAL库的字段类型一一对应。`GPIO_InitTypeDef` 的 `Mode`、`Pull`、`Speed` 字段都是 `uint32_t` 类型，我们的枚举底层类型也用 `uint32_t`，这样 `static_cast` 提取底层值时是零开销的——没有任何类型转换的开销，编译器只是把存储的整数值"当作"另一个类型来使用。

现在想象一下，如果你写代码时不小心把模式值传给了期望上下拉参数的函数：

```cpp
// C宏风格：编译通过，运行时LED行为异常
g.Pull = GPIO_MODE_OUTPUT_PP;   // 错了！但编译器不会警告

// enum class风格：编译直接报错
setup(Mode::OutputPP, Mode::OutputPP);  // 编译错误！第二个参数期望PullPush类型
```

`enum class` 的类型安全在这里体现得淋漓尽致：`Mode` 和 `PullPush` 是完全不同的类型，编译器会阻止你混用它们。而在C宏的世界里，`GPIO_MODE_OUTPUT_PP` 和 `GPIO_PULLUP` 都是 `uint32_t` 的宏，编译器看不到任何区别。

---

## static_cast——从枚举到HAL的桥梁

`enum class` 的值不能隐式转换为整数——这是安全特性，但HAL库只认 `uint32_t`。所以我们用 `static_cast` 做显式转换：

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

`static_cast<uint32_t>(gpio_mode)` 在编译时解析——如果 `gpio_mode` 是 `Mode::OutputPP`（底层值 `0x01`），那么 `static_cast` 的结果就是 `0x01`。这个过程不产生任何运行时代码，它就是从枚举中取出底层存储的整数。

对比C风格的隐式转换：

```c
// C风格：宏展开后是裸整数，类型信息完全丢失
g.Mode = GPIO_MODE_OUTPUT_PP;  // 等价于 g.Mode = 0x01;

// C++风格：枚举类型在编译时验证，然后零开销地提取底层值
init_types.Mode = static_cast<uint32_t>(gpio_mode);  // gpio_mode必须是Mode类型
```

不过，`static_cast` 的这种"零开销"安全性有一个值得注意的边界。虽然它不会在运行时检查值的合法性——如果你在 `enum class Mode` 中添加了一个新的枚举值但忘记在HAL库对应的宏中定义它，`static_cast` 不会报错，它只是忠实地把底层值传过去。这就是为什么我们的枚举值必须与HAL宏一一对应，这份对应关系需要开发者自己维护。

---

## ActiveLevel——应用层概念的枚举

```cpp
enum class ActiveLevel { Low, High };
```

注意这个枚举没有指定底层类型——它的默认底层类型是 `int`。这是有意为之的。`Low` 和 `High` 不是HAL宏的值，而是我们自己定义的应用层概念——它表达的是"这个LED电路是低电平有效还是高电平有效"。这个概念跟HAL库完全无关，是LED驱动层面的抽象。

`enum class` 的默认底层类型是 `int`，在C++中这没什么问题——嵌入式环境也完全支持 `int` 类型。如果你想要更精确地控制大小，可以显式指定 `enum class ActiveLevel : uint8_t`，但对只有两个值的枚举来说，这点存储优化完全不值得增加代码复杂度。

---

## State枚举——封装引脚状态

```cpp
enum class State { Set = GPIO_PIN_SET, UnSet = GPIO_PIN_RESET };
```

`GPIO_PIN_SET` 的值是1，`GPIO_PIN_RESET` 的值是0。`Set` 表示引脚为高电平，`UnSet` 表示引脚为低电平。这个枚举把HAL的 `GPIO_PinState` 类型包装成了类型安全的版本——跟前面的 `Mode` 和 `PullPush` 一样，你不可能把 `State::Set` 传给期望 `Mode` 参数的函数。

---

## C++23的 std::to_underlying —— 未来的优雅替代

我们当前代码中使用 `static_cast<uint32_t>(value)` 从枚举提取底层值。C++23引入了一个更优雅的工具函数 `std::to_underlying(enum_value)`，它是 `static_cast<std::underlying_type_t<E>>(e)` 的简写：

```cpp
// 当前写法（C++11兼容）
init_types.Mode = static_cast<uint32_t>(gpio_mode);

// C++23的std::to_underlying写法（未来目标）
init_types.Mode = std::to_underlying(gpio_mode);
```

`std::to_underlying` 更简洁，也不需要你手动写出底层类型——编译器会自动推导。但我们的代码目前没有使用它，原因是 `arm-none-eabi-g++` 搭配 `newlib-nano` 标准库可能还没有完整支持C++23的 `<utility>` 头文件。`static_cast` 是C++11就有的特性，兼容性更好。

当你确认你的工具链支持C++23的完整标准库后，可以安全地把所有 `static_cast<uint32_t>(xxx)` 替换为 `std::to_underlying(xxx)`。这是一个纯机械式的替换，不涉及任何逻辑变更。

---

## 重构到这里的效果

经过 `enum class` 重构后，我们的GPIO配置代码已经比纯C宏版本安全了很多。端口只能是 `GpioPort::A` 到 `GpioPort::E` 之一，不可能传入无效地址。模式只能是 `Mode` 枚举的成员，不可能传入随机的 `uint32_t`。而且 `Mode` 和 `PullPush` 是不同的类型，编译器会阻止你混用。

但还有问题没有解决：端口和引脚仍然是运行时传递的参数，不是编译时绑定的常量。时钟使能仍然是手动的——你得记得调用 `__HAL_RCC_GPIOx_CLK_ENABLE()`。这些问题要等到引入模板才能解决——那就是下一篇的主题了。

---

⚠️ 注意：虽然 `enum class` 解决了类型安全问题，但它也带来了一个新问题——不能隐式转换为整数。每次传递给HAL API都需要 `static_cast<uint32_t>(value)`。如果你觉得这个转换写起来繁琐，C++23提供了 `std::to_underlying(enum_value)` 作为更优雅的替代——但由于我们的arm-none-eabi工具链可能不支持完整的C++23标准库，所以暂时使用 `static_cast` 是最稳妥的选择。

---

## 我们回头看

这一篇我们做了三件事：用 `enum class` 替代 `#define` 获得类型安全，用 `static_cast` 在枚举和HAL之间做零开销转换，用 `ActiveLevel` 表达应用层概念。这些都是为后续的模板重构做准备——模板参数需要编译时常量，而 `enum class` 的成员恰好就是编译时常量表达式。

下一篇我们将引入C++模板的核心武器——非类型模板参数（NTTP），把端口和引脚从运行时参数变成编译时类型的一部分。这是整个系列中最重要的重构步骤。
