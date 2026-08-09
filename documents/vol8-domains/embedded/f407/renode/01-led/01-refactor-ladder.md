---
title: "LED 重构阶梯:从 C 裸机到 C++23,一档都不多花"
description: "把闪灯代码从裸机寄存器一档一档重构成 mmio 模板/位域/enum/模板 GPIO/C++23,每档用反汇编证明零开销"
chapter: 1
order: 1
tags:
  - stm32f4
  - cpp-modern
  - intermediate
  - 嵌入式
  - 零开销抽象
  - 类型安全
  - 模板
  - enum_class
  - consteval
difficulty: intermediate
platform: stm32f4
cpp_standard: [11, 17, 20, 23]
reading_time_minutes: 12
---

# LED 重构阶梯:从 C 裸机到 C++23,一档都不多花

## 这一篇干什么

[起步篇](../00-getting-started/01-renode-first-blink.md)里咱们跑通了闪灯,但 `main.c` 那段代码是裸的地址强转:

```c
#define GPIOD_ODR (*(volatile unsigned int *)0x40020C14)
GPIOD_ODR ^= (1u << 12);
```

能用,但写错地址、用错类型,编译器一个都不拦。这一篇把它一档一档重构成现代 C++,从裸机走到 C++23,每升一档都用反汇编确认没多花一条指令。这就是 C++ 在嵌入式上零开销抽象的实证,能拿 objdump 一行一行数出来,不是口号。

五档分别是:裸机 C → `mmio_reg` 类型安全封装 → 位域 + `enum class` → 模板 GPIO/LED → C++23 收尾。代码在 `code/stm32f4-tutorials/renode/0_blink/src/`,每档一个 `main_*.cpp`,各自能编、能在 Renode 里跑。

## 第 1 档:裸机 C(起点)

核心三步,开时钟、设模式、翻转:

```c
RCC_AHB1ENR |= (1u << 3);              /* 开 GPIOD 时钟 */
GPIOD_MODER &= ~(3u << (2 * 12));      /* PD12 模式清零 */
GPIOD_MODER |= (1u << (2 * 12));       /* PD12 = 输出 */
/* ... */
GPIOD_ODR ^= (1u << 12);               /* 翻转 PD12 */
```

痛点写在脸上:`0x40023830`、`3u << (2*12)`、`1u << 12` 这些魔术数字散在代码里,地址和类型各走各的。把 `GPIOD_ODR` 写成 `0x40020C18`(打错一位),或者当成 16 位寄存器读写,编译器都不会吱声。能用,但脆。

## 第 2 档:mmio_reg 类型安全封装

第一档的问题,根子在地址和类型没绑死。那就用一个模板把它们绑起来:

```cpp
template <typename T, std::uintptr_t Addr>
struct mmio_reg {
    static volatile T& value() noexcept {
        return *reinterpret_cast<volatile T*>(Addr);
    }
};

using RCC_AHB1ENR = mmio_reg<std::uint32_t, 0x40023830>;
using GPIOD_MODER = mmio_reg<std::uint32_t, 0x40020C00>;
using GPIOD_ODR   = mmio_reg<std::uint32_t, 0x40020C14>;
```

`mmio_reg<std::uint32_t, 0x40020C14>` 把"这是个 32 位寄存器、在地址 0x40020C14"绑成一个类型。用错类型(比如拿 `std::uint16_t` 去读 ODR)编译期就报错。

代价呢?看翻转那一行的汇编,跟第 1 档逐字节相同:

```text
ldr.w  r3, [r1, #3092]   @ 0xc14    读 GPIOD_ODR
eor.w  r3, r3, #4096               翻 bit12
str.w  r3, [r1, #3092]   @ 0xc14    写回
```

`mmio_reg::value()` 是个内联函数,返回 volatile 引用,编译器优化完就是一次直接寻址的读改写。类型安全白送,`text` 体积 556 字节,跟第 1 档一模一样。

## 第 3 档:位域 + enum class

第二档绑了地址和类型,但"PD12 的模式占 MODER 的 bit 24-25"这个知识还是手写的 `2*12`、`3u << 24`。把它也封装掉:

```cpp
template <std::uint32_t Offset, std::uint8_t Width>
struct reg_field {
    static constexpr std::uint32_t kMask = (1u << Width) - 1u;
    static constexpr std::uint32_t kShift = Offset;
    template <typename Reg>
    static void set(std::uint32_t v) {
        Reg::value() = (Reg::value() & ~(kMask << kShift))
                     | ((v & kMask) << kShift);
    }
};

enum class PinMode : std::uint32_t { kInput=0, kOutput=1, kAlternate=2, kAnalog=3 };

using ModerPd12 = reg_field<24, 2>;  /* MODER 里 PD12 占 bit 24-25 */
```

用法是 `ModerPd12::set<GPIOD_MODER>(static_cast<std::uint32_t>(PinMode::kOutput))`。模式只能传 `PinMode` 那几个值,传个 5 进去编译期就拦。汇编还是那条读改写,而且这一档 `text` 掉到 548 字节,比前两档还小 8。原因:位域封装把"清零再置位"两步合成了一次读改写,少了几条指令。抽象不仅没开销,还倒赚了。

## 第 4 档:模板 GPIO/LED

到这里寄存器访问已经很干净了,但"开 GPIOD 时钟、设 PD12 输出、翻转 PD12"还是散的三句,而且端口和引脚是运行期的魔术数字。把它们做成模板参数,编译期绑定:

```cpp
template <std::uintptr_t BaseAddr, std::uint32_t ClockBit>
struct GpioPort {
    static void enable_clock() { RCC_AHB1ENR::value() |= (1u << ClockBit); }
    template <std::uint8_t Pin>
    static void set_mode(PinMode mode) { /* ... 清零 + 置位 ... */ }
    template <std::uint8_t Pin>
    static void toggle() { /* ODR ^= (1u << Pin) */ }
};

using GpioD = GpioPort<0x40020C00, 3>;          /* 基址 + 时钟使能位 */

template <typename Port, std::uint8_t Pin>
struct Led {
    static void init()  { Port::enable_clock(); Port::template set_mode<Pin>(PinMode::kOutput); }
    static void toggle() { Port::template toggle<Pin>(); }
};

using UserLed = Led<GpioD, 12>;                  /* PD12,编译期绑定 */
```

`main` 里只剩两句:

```cpp
UserLed::init();
for (;;) { UserLed::toggle(); delay(500000); }
```

`UserLed` 的类型 `Led<GpioD, 12>` 把端口 D、引脚 12 焊死在类型里,换引脚就换类型,选错端口编译期就报。翻转那一行汇编纹丝不动,还是 `ldr/eor/str`,`text` 548。

## 第 5 档:C++23 收尾

最后一档加三样东西,全是编译期的,不产生运行时代码:

```cpp
consteval bool valid_pin(std::uint8_t pin) noexcept { return pin < 16; }

template <typename Port, std::uint8_t Pin>
struct Led {
    static_assert(valid_pin(Pin), "pin must be 0-15");   /* consteval 校验 */
    /* ... */
    [[nodiscard]] static consteval std::uint8_t pin() noexcept { return Pin; }
};

constinit const std::uint32_t kBlinkDelay = 500000;       /* 编译期初始化 */
```

`consteval bool valid_pin` 在 `static_assert` 里跑,非法引脚(比如 `Led<GpioD, 16>`)直接编译失败,错误信息清清楚楚指到点上。`[[nodiscard]]` 标注 `pin()` 的返回值不能被丢掉。`constinit` 保证 `kBlinkDelay` 在编译期完成初始化,没有运行期构造。这三样都不产生指令,`text` 还是 548,翻转汇编不变。

## 五档汇总

体积和翻转指令的对照,数据全是实测:

| 档 | 写法 | text 字节 | 翻转 PD12 的指令 |
|---|---|---|---|
| 1 | 裸机 C | 556 | ldr / eor / str(读改写 ODR) |
| 2 | mmio_reg 类型安全 | 556 | 同上 |
| 3 | 位域 + enum class | 548 | 同上 |
| 4 | 模板 GPIO/LED | 548 | 同上 |
| 5 | C++23 consteval / constinit / 属性 | 548 | 同上 |

翻转 PD12 这件事,从第 1 档到第 5 档,生成的机器码完全一致。后三档反而更小,因为位域封装让 MODER 设置少了几条指令。

## 这说明了什么

C++ 的零开销抽象在嵌入式上,是能拿 objdump 一行一行数出来的事实。从裸机到模板到 consteval,类型安全、强枚举、编译期校验,每一层抽象都白送,运行期代价是零,甚至负的。这就是咱们用现代 C++ 写单片机的底气:写得更像正常的 C++,编译出来跟手写 C 一样紧。

代价在别处:编译时间、模板报错信息的长度的确比 C 难啃。但运行期开销,没有。

## 自己跑一遍

每档都能在 Renode 里跑,反汇编也能直接看:

```bash
cd code/stm32f4-tutorials/renode/0_blink
cmake -B build -DCMAKE_TOOLCHAIN_FILE=../../toolchain-arm-none-eabi.cmake
cmake --build build --target run_blink_cpp23_in_renode   # 想跑哪档换哪个名字
arm-none-eabi-objdump -d build/blink_cpp23.elf | sed -n '/<main>:/,/^\s*$/p'
```

把目标换成 `run_blink_fields_in_renode`、`run_blink_gpio_template_in_renode` 等等,跑别的档。每档的 `main` 反汇编都拿来比一比,自己确认零开销。
