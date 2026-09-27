---
title: "把配置写进类型：Gpio 模板与编译期的验证"
description: "从 enum class 的类型革命到 Gpio<Port, Mask, Dir> 模板：端口基地址直接做枚举值、countr_zero 从掩码派生脚号、if constexpr 在编译期自动选电闸，时钟外置的问题就此解决、concept 约束把「输入脚塞给 LED」拦在编译期并附真实报错的逐行阅读、极性参数让 Blue Pill 的低电平点亮变成类型事实——最后用反汇编逐项检查：init() 展开成时钟三件套加 HAL_GPIO_Init、on() 的极性翻译零运行时开销，最后是五个问题的终局核对表"
chapter: 1
order: 8
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 模板
difficulty: beginner
platform: stm32f1
---

# 把配置写进类型：Gpio 模板与编译期的验证

其实两篇走下来，咱们心里都有数：C 宏留下的五个问题，HAL 拆掉了问题 1（枚举挡住拼错），缓解了问题 4 和问题 5，剩下的问题 2 和问题 3，时钟外置的那个原封未动，组合无约束的那个也只被枚举蹭掉半边——拼错倒是挡住了，非法组合还是没人替咱们管。07 篇末尾留过一句话："端口和它的电闸，在类型系统里没有关系"——说的就是 C 语言的上限。所以这一篇，咱们动手把剩下的尾巴收掉：**把配置从函数参数提升为类型本身**。配置一旦成了类型，配错就从"运行时怪事"变成了"编译期错误"，这也正是 00 站承诺的"现代 C++"在寄存器上真正的落点。

咱们用的素材，全部来自 `third_party/libestdx` 的源码和真实构建产物，每个数字都是真跑的。

## 起手：enum class 把端口变成类型

咱们回头看 06 篇：宏封装的死穴之一，就是端口和脚号用的都是裸数字——您把 `GPIOA` 换成 `GPIOC`，宏和编译器都被蒙在了鼓里。库作者走的第一步就是把端口收进 `enum class`（`libestdx/boards/stm32f1/gpio.hpp`）：

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,   // 0x40010800
    B = GPIOB_BASE,   // 0x40010C00
    C = GPIOC_BASE,   // 0x40011000
    // ...
};
```

这个定义值得咱们停下来细看：**枚举值直接就是外设基地址**。`GpioPort::C` 既是"C 端口"这个类型层面的身份，又随身携带了 `0x40011000` 这个数值，也就是 02 篇里 CMSIS 宏层层相加算出来的那个地址。取用的时候，一次转型就到位了：

```cpp
static inline GPIO_TypeDef* const port =
    reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
```

还记得 02 篇留的话头吗？咱们说过，`reinterpret_cast` 在嵌入式的正当用法是"把整数当地址"。CMSIS 的 `(GPIO_TypeDef*)0x40011000` 是 C 风格转型的同款写法，这里只是换上了名字更诚实、也更窄的 C++ 形式。您再留意存储类别：这里写的其实是 `inline` 加 `const`，不是 `constexpr`——`reinterpret_cast` 进不了常量表达式，真写成 `constexpr` 是编不过的，所以库作者只能退到这一步。不过结果不受影响：这个指针的值在编译期就定了，照旧凝固成字面量池里的一个 `.word 0x40011000`——运行时一行代码都不占。

拼错的代价也变了：`GpioPort::X` 根本不存在，您刚写下去，编译器就翻脸了。回想 06 篇那个幽灵 bug：端口挪了窝、时钟宏忘改，那样的代码照样编译通过。

## 四个维度写进一个类型

```cpp
template <GpioPort PORT, uint16_t MASK,
          gpio::GpioDirection DIRECTION,
          gpio::GpioPull PULL = gpio::GpioPull::NoPull>
struct Gpio {
    static inline GPIO_TypeDef* const port = /* 上节那个转型 */;
    static constexpr auto mask  = MASK;
    static constexpr auto direction = DIRECTION;
    static constexpr uint8_t pin = std::countr_zero(MASK);  // C++20 <bit>
    // ...
};
```

用的时候，咱们把配置写成一个类型别名：

```cpp
using LedPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Output>;
```

咱们注意一下模板参数的形态：`PORT` 是枚举、`MASK` 是整数，这就是所谓的**非类型模板参数（NTTP）**。配置从此不再是运行时函数的实参，而是类型的一部分：`Gpio<GpioPort::C, 0x2000, Output>` 和 `Gpio<GpioPort::A, 0x20, Input>` 是两个毫无继承关系的独立类型，编译器对它们的了解精确到每一位。

`std::countr_zero(MASK)` 帮咱们数掩码尾部零的个数：`0x2000` 尾随 13 个零，`pin == 13`。脚号从掩码**派生**而来，不用再和掩码平行地手写两遍。06 篇问题 5 那个复制粘贴的毛病，到这里就消失了：一个引脚的全部事实，咱们整个工程里只写一次。

![咱们把 GPIO 的配置事实写进类型系统，再交给 concept 的约束把关](./08-gpio-type-system.drawio)

## init()：问题 2 的终点

`Gpio` 的 `init()` 一手开钟、一手配置。咱们看开钟这段，它是全篇最要紧的一段：

```cpp
static void enable_clock() {
    if constexpr (PORT == GpioPort::A) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if constexpr (PORT == GpioPort::B) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if constexpr (PORT == GpioPort::C) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    // ...
}
```

为什么必须是 `if constexpr`，不能是运行时的 `if`？咱们看这些时钟使能宏：一个端口一个名字，这个名字在编译期就定好了。`__HAL_RCC_GPIOC_CLK_ENABLE` 展开成对固定地址的读改写（03 篇拆过它的 `tmpreg` 假读），运行时根本**没有**一个"可以按端口选择的时钟函数"存在。而 C 宏时代，这件事只能靠人肉纪律：配了 GPIOC，就得记得念对 GPIOC 的时钟宏。现在这道纪律被写进了 `GpioPort::C` 这个类型：**选了 C 端口，就自动选了 C 的电闸，您想错都错不成**。问题 2 到这儿就解决了。

到了配置那段，咱们填一个结构体、再调 HAL：

```cpp
GPIO_InitTypeDef init{
    .Pin = mask, .Mode = direction_mode(), .Speed = GPIO_SPEED_FREQ_LOW,
    .Pull = pull_mode()};
HAL_GPIO_Init(port, &init);
```

`direction_mode()` 和 `pull_mode()` 的内部也是 `if constexpr`，干的就是把枚举翻译成 HAL 常量的活。咱们拿到的是**编译期**的翻译：函数体按类型实例化成直接 `return GPIO_MODE_OUTPUT_PP`，没有运行时的 switch。HAL 的通用机器照常运转（07 篇那 1060 字节），但喂给它的参数，已经不可能错了。

## concept：配错的编译期报错

库的约束体系放在 `libestdx/gpio/gpio_base.hpp`，一共分成了三段，咱们挨段看：

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

`GPIOPin` 说的是"是根引脚"，`GPIOOutputPin` 在它之上加了一条**值约束**：`direction == Output`。这里头藏着一次真实的翻车（源码注释里的原话）：这个相等判断，必须以合取的方式放在 `requires {}` 块**外面**。而写进块内的话，`Concrete::direction == Output` 验证的只是"这个表达式合法"，至于"它为真"是不验证的，配成 Input 的引脚照样通过约束。**`requires` 块查的是"能不能编译"，块外的 `&&` 查的是"值是多少"**。concept 语义里最微妙的这层分界，库作者已经替咱们探明白了。

而消费这些约束的活，最后落到了 `LED` 头上（`libestdx/device/led.hpp`），咱们看它怎么用：

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

现在咱们做一个 06 篇想都不敢想的实验：故意把输入脚塞给 LED。

```cpp
using BadPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Input>;
using Led = estdx::device::LED<BadPin>;   // ← 输入脚点灯?
```

咱们把上面几行存成 `/tmp/concept_fail.cpp`、用 `arm-none-eabi-g++` 一编，编译器当场就报了错。真跑的报错原文给您（节选自 arm-none-eabi-g++ 16.2）：

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

咱们读这类报错有窍门：从 `template constraint failure` 往下找 **`evaluated to 'false'`** 那一行，它说的全是人话：这个 `Pin` 的 `direction` 是 `Input` 而不是 `Output`，所以不满足 `GPIOOutputPin`。哪个类型挂了、哪个约束没过、哪条子表达式出了问题，一屏之内就全部交代清楚了。对比一下：06 篇的宏拼错参数照样编译，07 篇的 `assert_param` 默认不干活，到了这一步，咱们连 `main` 都还没写完。问题 3 到这儿也解决了。

## 极性进类型：电路知识兑现

咱们再看 `LED` 的第二个模板参数 `POLARITY`：默认的就是 `ActiveHigh`，这块 Blue Pill 的 PC13 得写 `ActiveLow`，05 篇的电路课（LED 阳极接 VCC、灌电流导通），到这里就变成了一个类型参数。`on()` 里的 `if constexpr` 在实例化时二选一：`ActiveLow` 的 `on` 编译成的是 `Pin::reset()`，`off` 编译成的是 `Pin::set()`。**低电平点亮的这个硬件事实，从此写进类型里了**。写应用的人不用记得 PC13 的电路拓扑。改板子挪了 LED 接法，您改一个枚举字面量，`on`/`off` 就全部自动对调了。

## 反汇编核对：编译期都付了什么、没付什么

咱们真跑的固件是 `examples/03_led`（全文就是上面那个 `LedPin` 加 `Led`，再配上标准的时钟配置），`-O3` 编出来的产物是 3476 字节 text，对照裸寄存器版的 2380。接下来看 `main` 里 `LedPin::init()` 展开成了什么（`arm-none-eabi-objdump -d` 真跑节选）：

```text
8000154: ldr   r3, [pc, #80]    @ 0x40021000  ← RCC 基地址进了字面量池
8000158: ldr   r2, [r3, #24]                   ← APB2ENR
800015c: orr.w r2, r2, #16                     ← |= IOPCEN
8000160: str   r2, [r3, #24]
8000162: ldr   r3, [r3, #24]                   ← tmpreg 假读(03 篇讲过的讲究)
        ...  填栈上的 GPIO_InitTypeDef ...
800017a: bl    HAL_GPIO_Init
```

咱们看：时钟三件套、`tmpreg` 假读、结构体填充，**全部内联进了 `main`，一次函数调用都没了**。`enable_clock()` 作为一个函数消失了，这就是 `if constexpr` 的编译期语义：不成立的分支根本不实例化，成立的分支内联以后，就是 02 篇手工写的那几条。

咱们再来看运行路径上的 `Led::on()` / `Led::off()`：

```text
8000180: mov.w r1, #8192        @ GPIO_PIN_13
8000184: ldr   r0, [pc, #36]    @ 0x40011000  ← GPIOC
8000186: bl    HAL_GPIO_WritePin ← r2=0 (on) / r2=1 (off)
```

调用 `on()` 的时候，`r2` 装的是 `0`（`GPIO_PIN_RESET`），`off()` 装的则是 `1`：**极性的翻译在编译期就完成了，运行时是没有 if 的**。不过同时有件事，笔者得诚实交代：estdx 的 `set`/`reset` 是对 `HAL_GPIO_WritePin` 的薄转发，没开 LTO 的时候，它是没有被内联的。`bl` 那一跳是还在的，WritePin 内部的 `PinState` 分支（07 篇拆过）也还在。对比裸寄存器版的单条 `str r5, [r4, #16]`：**模板机制本身零开销（类型消失于编译期，地址也直接落进了字面量池），但实现路径选了 HAL 函数，就诚实地背着 HAL 的调用成本**。这是"零开销抽象"四个字准确的读法：您不用的抽象，咱们一个字节都不为它出。您选的实现，出的每个字节都看得见落在哪。

## 五个问题的终局核对

| 问题 | C 宏（06 篇） | HAL（07 篇） | 模板 + concept（本篇） |
|----|--------------|--------------|----------------------|
| 1. 无类型 | 全裸数字 | 枚举挡拼错 | **类型即配置**，拼错不存在 |
| 2. 时钟外置 | 人肉纪律 | 宏照旧外置 | **选端口即选电闸** |
| 3. 组合无约束 | 运行时都不会炸 | 运行时才不对劲 | **编译期报错** |
| 4. 不可调试 | 宏展开读不懂 | 真函数真符号 | 类型有名字，实例化可追 |
| 5. 复制粘贴 | 加脚全靠抄 | 结构体复用 | **一个类型一个引脚，派生不复制** |

C 语言走过的这一路，咱们从位宏爬到 SPL、再爬到 HAL，结构体抽象吃掉了一千多字节，所以问题 2 和问题 3 始终没解决。为什么？您看："端口配了、电闸没开"和"输入脚当输出用"，毛病出在同一个地方：**配置之间的一致性没人负责**。而 C 的函数参数各管各的、没有谁替它们把关。类型系统管的正是这件事：配置一旦成了类型，一致性就变成"这个类型存不存在"的问题。咱们这个站从 `Led::on()` 一路下到 BSRR 的电线，正题到这儿就走完了，还剩收尾的一篇：`toggle` 里的讲究、攒下的验证工具，都归置到那篇里了，咱们下一篇见。

## 您来动手

1. 您对着 `03_led` 编出来的 `led_example` 跑 `arm-none-eabi-objdump -d`，找到 `main` 里时钟三件套和两次 `bl HAL_GPIO_WritePin`，核对 `r2` 的值与 `ActiveLow` 的语义。
2. 您把 `03_led` 的极性改成 `ActiveHigh` 重新编译，到反汇编里看 `on`/`off` 的 `r2` 怎么对调，体会一下"改一个字面量，电路知识自动重接线"。
3. 您可以自己造一次编译期翻车：把 `GpioPort::C` 换成不存在的 `GpioPort::X`，再看报错和 `direction` 那次的差别。
4. 您在 `gpio_base.hpp` 里把 `Concrete::direction == GpioDirection::Output` 挪进 `requires {}` 块里，重新编译 `/tmp/concept_fail.cpp`，亲眼看看"表达式合法"和"表达式为真"的差别，亲手复现库注释里提醒的那个陷阱。

## 欸欸！自查一下再走

- 您来说说，`GpioPort::C` 是怎么同时携带"端口身份"和"基地址"的？运行时它占多少代码空间？
- 为什么时钟使能必须 `if constexpr`，不能运行时 `if`？宏的哪条性质决定了这一点，您能指出来吗？
- concept 的 `requires {}` 块内写 `direction == Output`，为什么挡不住输入脚？正确写法在哪，您还有印象吗？
- `ActiveLow` 的 `on()` 编译成 `Pin::set()` 还是 `Pin::reset()`？咱们在反汇编里看到的 `r2`，又是多少？
- "零开销抽象"的准确说法是什么？estdx 的哪一项开销不是模板带来的、是哪项选择带来的，您能把这两件事分开吗？

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
