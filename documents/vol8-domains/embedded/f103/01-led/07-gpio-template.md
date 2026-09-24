---
title: "把配置写进类型：Gpio 模板与编译期的账"
description: "从 enum class 的类型革命到 Gpio<Port, Mask, Dir> 模板：端口基地址直接做枚举值、countr_zero 从掩码派生脚号、if constexpr 在编译期把端口与电闸焊死拆掉时钟幽灵雷、concept 约束把'输入脚塞给 LED'拦在编译期并附真实报错的逐行阅读、极性参数让 Blue Pill 的低电平点亮变成类型事实——最后反汇编对账：init() 展开成时钟三件套加 HAL_GPIO_Init、on() 的极性翻译零运行时开销，五颗雷的终局对账表收口"
chapter: 1
order: 7
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 模板
difficulty: beginner
platform: stm32f1
---

# 把配置写进类型：Gpio 模板与编译期的账

两篇下来，债台账很清楚：C 宏那五颗雷，HAL 拆了雷 1（枚举）、缓解了雷 4 和雷 5，但雷 2 的时钟幽灵、雷 3 的组合无约束原封不动——06 篇末尾那句话，"端口和它的电闸，在类型系统里没有关系"，就是 C 语言的天花板。这一篇动手拆天花板：**把配置从函数参数提升为类型本身**。配置一旦是类型，配错就从"运行时怪事"变成"编译期错误"——这正是 00 站承诺的"现代 C++"在寄存器上真正的落点。

素材全部来自 `third_party/libestdx` 的源码和真实构建产物，每个数字都是真跑的。

## 第一刀：enum class 把端口变成类型

05 篇宏封装的死穴之一，是端口和脚号都是裸数字：`GPIOA` 换成 `GPIOC`，宏不知道、编译器不知道。库的第一步是把端口收进 `enum class`（`libestdx/boards/stm32f1/gpio.hpp`）：

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,   // 0x40010800
    B = GPIOB_BASE,   // 0x40010C00
    C = GPIOC_BASE,   // 0x40011000
    // ...
};
```

这个定义有股狠劲儿：**枚举值直接就是外设基地址**。`GpioPort::C` 既是"C 端口"这个类型层面的身份，又携带 `0x40011000` 这个数值——01 篇里 CMSIS 宏层层相加算出来的那个地址。取用时一次转型：

```cpp
static constexpr auto port =
    reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
```

还记得 01 篇留的话头吗——`reinterpret_cast` 在嵌入式的正当用法是"把整数当地址"。CMSIS 的 `(GPIO_TypeDef*)0x40011000` 是 C 风格转型的同款写法，这里只是换上了名字更诚实、更窄的 C++ 形式。而这个转型发生在 `constexpr` 上下文里，编译完就凝固成字面量池里的一个 `.word 0x40011000`，运行时一行代码都不占。

拼错的代价也变了：`GpioPort::X` 不存在，编译器第一行就翻脸——对比 05 篇那个端口改 A、时钟忘改还照常编译的幽灵 bug。

## 四个维度焊进一个类型

```cpp
template <GpioPort PORT, uint16_t MASK,
          gpio::GpioDirection DIR = gpio::GpioDirection::Input,
          gpio::GpioPull PULL = gpio::GpioPull::NoPull>
struct Gpio {
    static constexpr auto port  = /* 上节那个转型 */;
    static constexpr auto mask  = MASK;
    static constexpr auto direction = DIR;
    static constexpr uint8_t pin = std::countr_zero(MASK);  // C++20 <bit>
    // ...
};
```

用法是把配置写成一个类型别名：

```cpp
using LedPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Output>;
```

注意模板参数的形态：`PORT` 是枚举、`MASK` 是整数——**非类型模板参数（NTTP）**。配置不再是运行时函数的实参，而是类型的一部分：`Gpio<GpioPort::C, 0x2000, Output>` 和 `Gpio<GpioPort::A, 0x20, Input>` 是两个毫无继承关系的独立类型，编译器对它们的了解精确到每一位。

`std::countr_zero(MASK)` 数掩码尾部零的个数：`0x2000` 尾随 13 个零，`pin == 13`——脚号从掩码**派生**而来，而不是和掩码平行地手写两遍。05 篇雷 5 的复制粘贴问题，从这里开始失去土壤：一个引脚的全部事实只有一个来源。

## init()：雷 2 的葬礼

`Gpio` 的 `init()` 干两件事：开钟、配置。开钟这段是全篇的戏眼：

```cpp
static void enable_clock() {
    if constexpr (PORT == GpioPort::A) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if constexpr (PORT == GpioPort::B) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if constexpr (PORT == GpioPort::C) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    // ...
}
```

为什么必须 `if constexpr`，不能运行时 `if`？因为这些时钟使能宏的名字本身就是编译期的——`__HAL_RCC_GPIOC_CLK_ENABLE` 展开成对固定地址的读改写（06 篇拆过它的 `tmpreg` 假读），运行时根本**没有**一个"可以按端口选择的时钟函数"存在。C 宏时代只能靠人肉纪律保证"配 GPIOC 就念 GPIOC 的咒"，现在这条纪律被焊进了 `GpioPort::C` 这个类型——**选了 C 端口，就自动选了 C 的电闸，想错都错不成**。雷 2，拆。

配置那段是填结构体加调 HAL：

```cpp
GPIO_InitTypeDef init{
    .Pin = mask, .Mode = direction_mode(), .Speed = GPIO_SPEED_FREQ_LOW,
    .Pull = pull_mode()};
HAL_GPIO_Init(port, &init);
```

`direction_mode()` 和 `pull_mode()` 内部也是 `if constexpr` 翻译枚举到 HAL 常量——**编译期**的翻译，函数体按类型实例化成直接 `return GPIO_MODE_OUTPUT_PP`，没有运行时 switch。HAL 的通用机器照常运转（06 篇那 1060 字节），但喂给它的参数已经不可能错了。

## concept：配错的编译期讣告

库的约束体系在 `libestdx/gpio/gpio_base.hpp`，三段式：

```cpp
template <typename Concrete>
concept GPIOPin = requires {
    { Concrete::mask } -> std::convertible_to<uint32_t>;
    { Concrete::direction } -> std::convertible_to<GpioDirection>;
    Concrete::port;
    Concrete::pin;
};

template <typename Concrete>
concept GPIOOutputPin =
    GPIOPin<Concrete> && Concrete::direction == GpioDirection::Output && requires {
        Concrete::set();
        Concrete::reset();
        Concrete::toggle();
    };

template <typename Concrete>
concept GPIOInputPin =
    GPIOPin<Concrete> && Concrete::direction == GpioDirection::Input && requires {
        { Concrete::level() } -> std::convertible_to<bool>;
    };
```

`GPIOPin` 说"是根引脚"，`GPIOOutputPin` 在其上加一条**值约束**：`direction == Output`。这里藏着一段真实的踩坑（源码注释里的原话）：这个相等判断必须放在 `requires {}` 块**外面**做合取——写进块内的话，`Concrete::direction == Output` 只验证"这个表达式合法"，不验证"它为真"，配成 Input 的引脚照样通过约束。**`requires` 块查的是"能不能编译"，块外的 `&&` 查的是"值是多少"**——concept 语义里最微妙的一道分界，库作者替咱们踩过了。

这些概念由 `LED` 消费（`libestdx/device/led.hpp`）：

```cpp
template <gpio::GPIOOutputPin Pin,
          gpio::GpioPolarity POLARITY = gpio::GpioPolarity::ActiveHigh>
struct LED {
    static void on() {
        if constexpr (POLARITY == gpio::GpioPolarity::ActiveHigh) { Pin::set(); }
        else { Pin::reset(); }
    }
    // off() 对称;极性反过来选
    static void toggle() { Pin::toggle(); }  // Take it easy :)
};
```

现在做 05 篇不敢想的实验：故意把输入脚塞给 LED。

```cpp
using BadPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Input>;
using Led = estdx::device::LED<BadPin>;   // ← 输入脚点灯?
```

编译器当场出讣告（arm-none-eabi-g++ 16.2，真跑原文节选）：

```text
error: template constraint failure for 'template<class Pin, ...> requires
       GPIOOutputPin<Pin> struct estdx::device::LED'
  7 | using Led = estdx::device::LED<BadPin>;
    |                                      ^
note: constraints not satisfied
required for the satisfaction of 'GPIOOutputPin<Pin>'
    [with Pin = estdx::stm32f1::Gpio<GpioPort::C, 8192,
                   GpioDirection::Input, GpioPull::NoPull>]
the expression '(Concrete::direction) == estdx::gpio::GpioDirection::Output'
evaluated to 'false'
```

读报错有窍门：从 `template constraint failure` 往下找 **`evaluated to 'false'`** 那一行——它说的全是人话：这个 `Pin` 的 `direction` 是 `Input`，不等于 `Output`，所以不满足 `GPIOOutputPin`。整条证据链（哪个类型、哪个约束、哪条子表达式挂了）一屏写尽。对比一下账：05 篇的宏拼错参数照样编译，06 篇的 `assert_param` 默认是空气，这里连 `main` 都还没写完。雷 3，拆。

## 极性进类型：电路知识兑现

`LED` 的第二个模板参数 `POLARITY` 默认 `ActiveHigh`，Blue Pill 的 PC13 要写 `ActiveLow`——04 篇的电路课（LED 阳极接 VCC、灌电流导通）在这里变成一个类型参数。`on()` 里的 `if constexpr` 在实例化时二选一：`ActiveLow` 的 `on` 编译成 `Pin::reset()`，`off` 编译成 `Pin::set()`。**低电平点亮这个硬件事实，从此写在类型里**，写应用的人不需要记得 PC13 的电路拓扑，改板子挪了 LED 接法，改一个枚举字面量，`on`/`off` 全部自动对调。

## 反汇编对账：编译期都付了什么、没付什么

真跑的固件是 `examples/03_led`（全文就是上面那个 `LedPin` 加 `Led` 加标准时钟配置），`-O3` 产物 3476 字节 text，对照裸寄存器版 2380。看 `main` 里 `LedPin::init()` 展开成什么（`arm-none-eabi-objdump -d` 真跑节选）：

```text
8000154: ldr   r3, [pc, #80]    @ 0x40021000  ← RCC 基地址进了字面量池
8000158: ldr   r2, [r3, #24]                   ← APB2ENR
800015c: orr.w r2, r2, #16                     ← |= IOPCEN
8000160: str   r2, [r3, #24]
8000162: ldr   r3, [r3, #24]                   ← tmpreg 假读(06 篇讲过的讲究)
        ...  填栈上的 GPIO_InitTypeDef ...
800017a: bl    HAL_GPIO_Init
```

时钟三件套、`tmpreg` 假读、结构体填充——**全部内联进 `main`，没有一次函数调用**。`enable_clock()` 作为函数消失了，这就是 `if constexpr` 的编译期语义：不成立的分支根本不实例化，成立的分支内联后就是 06 篇手工写的那几条。

再看运行路径，`Led::on()` / `Led::off()`：

```text
8000180: mov.w r1, #8192        @ GPIO_PIN_13
8000184: ldr   r0, [pc, #36]    @ 0x40011000  ← GPIOC
8000186: bl    HAL_GPIO_WritePin ← r2=0 (on) / r2=1 (off)
```

`on()` 那次调用 `r2` 装的是 `0`（`GPIO_PIN_RESET`），`off()` 是 `1`——**极性的翻译在编译期完成，运行时没有 if**。同时要诚实记一笔：estdx 的 `set`/`reset` 是对 `HAL_GPIO_WritePin` 的薄转发，没开 LTO 时它没被内联，`bl` 那一跳和 WritePin 内部的 `PinState` 分支（06 篇拆过）都在。对比裸寄存器版的单条 `str r5, [r4, #16]`：**模板机制本身零开销（类型消失于编译期，地址直接落字面量池），但实现路径选了 HAL 函数，就诚实背着 HAL 的调用成本**。抽象不要钱，实现选择要——这是"零开销抽象"四个字准确的理解方式：你不用的抽象，你不出钱；你选的实现，你出的每一分钱都看得见发票。

## 五颗雷的终局对账

| 雷 | C 宏（05 篇） | HAL（06 篇） | 模板 + concept（本篇） |
|----|--------------|--------------|----------------------|
| 1. 无类型 | 全裸数字 | 枚举挡拼错 | **类型即配置**，拼错不存在 |
| 2. 时钟外置 | 人肉纪律 | 宏照旧外置 | **if constexpr 焊死** |
| 3. 组合无约束 | 运行时都不会炸 | 运行时才不对劲 | **编译期讣告** |
| 4. 不可调试 | 宏展开无字天书 | 真函数真符号 | 类型有名字，实例化可追 |
| 5. 复制粘贴 | 加脚全靠抄 | 结构体复用 | **一个类型一个引脚，派生不复制** |

C 语言从位宏爬到 SPL 再到 HAL，爬了两千多字节的结构体抽象，雷 2 和雷 3 始终在——因为"端口配了、电闸没开"和"输入脚当输出用"都是**配置之间的一致性问题**，C 的函数参数各管各的，没人看管彼此。类型系统看管：当配置成为类型，一致性就是"这个类型存不存在"的问题。这一站从 `Led::on()` 一路下到 BSRR 的电线爬完了；往上一站，轮到"输入"的世界——配成输入只是第一步，读引脚、消抖、认事件，02-button 见。

## 您来动手

1. 对着 `led_example` 跑 `arm-none-eabi-objdump -d`，找到 `main` 里时钟三件套和两次 `bl HAL_GPIO_WritePin`，核对 `r2` 的值与 `ActiveLow` 的语义；
2. 把 `03_led` 的极性改成 `ActiveHigh` 重新编译，反汇编看 `on`/`off` 的 `r2` 如何对调——体验"改一个字面量，电路知识自动重接线"；
3. 自己造一次编译期翻车：把 `GPIO_PIN_13` 换成一个不存在的端口 `GpioPort::X`，再看报错和 `direction` 那次的差别；
4. 在 `gpio_base.hpp` 里把 `Concrete::direction == GpioDirection::Output` 挪进 `requires {}` 块里，重新编译 `/tmp/concept_fail.cpp`，亲眼看看"表达式合法"和"表达式为真"的差别——库注释里那颗坑的复现。

## 欸欸！自查一下再走

- `GpioPort::C` 怎么同时携带"端口身份"和"基地址"？运行时它占多少代码空间？
- 为什么时钟使能必须 `if constexpr`，不能运行时 `if`？宏的哪条性质决定了这一点？
- concept 的 `requires {}` 块内写 `direction == Output` 为什么挡不住输入脚？正确写法在哪？
- `ActiveLow` 的 `on()` 编译成 `Pin::set()` 还是 `Pin::reset()`？反汇编里 `r2` 是多少？
- "零开销抽象"的准确说法是什么？estdx 的哪笔开销不是模板带来的、是哪项选择带来的？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="cppreference"
    title="constraints and concepts — requires clause vs requires-expression"
    :year="2025"
    url="https://en.cppreference.com/w/cpp/language/constraints"
    chapter="Atomic constraints; requires-expression semantics"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="if statement — constexpr if"
    :year="2025"
    url="https://en.cppreference.com/w/cpp/language/if"
    chapter="Constexpr if; discarded statements are not instantiated"
  />
  <ReferenceItem
    :id="3"
    author="Stroustrup, B."
    title="The Design and Evolution of C++ — zero-overhead principle"
    :year="1994"
    url="https://www.stroustrup.com/dne.html"
    chapter="Zero-overhead rule: What you don't use, you don't pay for"
  />
  <ReferenceItem
    :id="4"
    author="cppreference"
    title="std::countr_zero — count trailing zeros"
    :year="2025"
    url="https://en.cppreference.com/w/cpp/numeric/countr_zero"
    chapter="<bit> library, C++20"
  />
</ReferenceCard>
