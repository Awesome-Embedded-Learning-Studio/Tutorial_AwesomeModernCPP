---
title: "C 宏时代的 LED 驱动：能跑，但代价要摆上桌"
description: "裸寄存器代码管理问题的第一代答案：C 宏。把地址和位装进 LED_ 一族宏，调用处确实清爽了；然后亲手加第二盏灯体验整套复制的重量、亲手挪端口而忘改时钟使能，复现那个编译全过、灯不亮的幽灵 bug（正是 03 篇说过的那个问题的宏版本）；最后在 GDB 里直面宏展开的层层嵌套，数出五项代价——也说句公道话，一个人一片芯片的场合它完全够用。三步重构的路线从这里起步，通往 08 篇的模板"
chapter: 1
order: 6
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# C 宏时代的 LED 驱动：能跑，但代价要摆上桌

地砖下面这三层，咱们都看透了：地址走的是 MMIO，时钟的电闸在 RCC，配置和翻转靠的是七个寄存器。现在咱们开始往上爬。要爬的第一级楼梯，就是 C 程序员造了几十年的轮子：**用宏把寄存器代码包起来**。

场景很朴素：手边没有 HAL 的库，没有 C++ 的模板，就您一个人、一份纯 C 的工程、一盏要闪的灯。裸寄存器代码长这样（02 到 04 篇攒下的存货）：

```c
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x2u << 20);
GPIOC->BSRR = 1u << 13;
GPIOC->BRR  = 1u << 13;
```

能跑倒是能跑，但魔法数字撒了一地。`0xF << 20` 是什么？`13` 是谁？`0x2` 呢？三个月后您自己回来看，就得从头考古了。于是有了第一版宏。

## 第一版：把地址和位装进名字里

```c
// led.h —— C 宏时代的第一版 LED 封装
#define LED_PORT        GPIOC
#define LED_PIN         13
#define LED_CLK_EN()    do { RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; } while (0)

#define LED_MODE_OUT()  do { LED_PORT->CRH = \
        (LED_PORT->CRH & ~(0xFu << 20)) | (0x2u << 20); } while (0)

#define LED_ON()        do { LED_PORT->BRR  = (1u << LED_PIN); } while (0)
#define LED_OFF()       do { LED_PORT->BSRR = (1u << LED_PIN); } while (0)
#define LED_TOGGLE()    do { LED_PORT->ODR ^= (1u << LED_PIN); } while (0)
```

咱们再看调用处，这下焕然一新了：

```c
LED_CLK_EN();
LED_MODE_OUT();
for (;;) {
    LED_ON();
    LED_DELAY_MS(500);
    LED_OFF();
    LED_DELAY_MS(500);
}
```

`LED_DELAY_MS` 管的是延时，led.h 里没有它的定义，干的活儿就是 02 篇反汇编里见过的 `HAL_Delay(500)`，咱们这版不重造它。

诚实地讲，这一版**确实好了**：意图有了名字，改引脚只动一处就够了，`do {} while (0)` 的写法也够规范。编译出来的产物，一个字节都不比手写的多。宏是预处理阶段的文本替换，运行时的开销为零。所以拿 C 宏当稻草人打并不公平，它是几十年工业固件的正统开局。能跑倒是真的，可**代价**也实打实地存在，咱们现在就一项项把它摆清。

## 第二盏灯：复制粘贴的重量

需求来了：再加一个 LED2 在 PA5。宏的问题立刻现形：这套宏**生来只服务一盏灯**。您只有两条路：

```c
// 路线 A:整套复制改名
#define LED2_PORT       GPIOA
#define LED2_PIN        5
#define LED2_CLK_EN()   do { RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; } while (0)
#define LED2_MODE_OUT() do { LED2_PORT->CRL = \
        (LED2_PORT->CRL & ~(0xFu << 20)) | (0x2u << 20); } while (0)   // ← 偏移 20 是照抄的
#define LED2_ON()       do { LED2_PORT->BRR  = (1u << LED2_PIN); } while (0)
// ...以下五条同理复制
```

路线 A 复制了十来行，毛病就出在标注的那行：偏移 `20` 是从 PC13 那套原样复制过来的（`(13-8)×4=20`）。PA5 的偏移恰好也是 `(5-0)×4=20`，这次蒙对了，纯属两个算式撞出同一个数的侥幸。哪天 LED2 挪到了 PA6（`6×4=24`），复制来的 `<< 20` 就配错了脚，编译器还一声不吭地放行，咱们只能眼睁睁看着 04 篇的翻车换了个样子再来一遍。

路线 B 是把宏改成带参数的 `LED_ON(port, pin)`，可咱们在它身上同样走不通：port 是个指针、pin 是个数、配置偏移还跟着 pin 走，这几件事没法全塞进一个类型安全的调用里。宏的参数又没有类型，拼错了照样编译。

## 幽灵 bug：编译器看不见的错

再演一出更阴的。老板发话了：LED2 挪到 PB12。您麻利地改了：

```c
#define LED2_PORT       GPIOB
#define LED2_PIN        12
// 改完了,编译,烧录,运行 —— LED2 不亮
```

为啥？`LED2_CLK_EN()` 里那句 `IOPAEN` 忘了改成 `IOPBEN`：GPIOA 的电闸开着，GPIOB 的还关着。这段代码**在编译器眼里无懈可击**：宏展开成合法的 C 表达式，咱们查类型、查语法、数警告，结果样样挑不出毛病。可它运行起来是错的，而且错得无声：不崩溃、不报错、不打印，就是灯不理您。

咱们第三篇说过：外设不工作，第一个要查的永远是时钟。现在咱们补上后半句：**C 宏把"开钟"和"用脚"拆在两处，一致靠的是人肉纪律，而人肉是会忘的**。端口和时钟是一对必须同步的配置，咱们从语言上却看不出半点联系，这就是幽灵 bug 的病根。

咱们把这个失配画成了下面一张图：
![端口与时钟宏失配形成的幽灵 bug](./06-macro-coupling-bug.drawio)

## 调试器里读不懂的宏

第三个代价在调试的时候现身。咱们把 GDB 连上、`step` 进 `LED_MODE_OUT()`，您猜进的是哪？宏没有函数实体，一行 `step` 直接落在展开后的表达式上，断点行号在预处理后的行和原始行之间乱跳。咱们想 `print` 一下 `LED_PORT`？它可不是什么变量，只是编译期的文本替换，您在调试器里找不到它。反汇编倒是对得上（第二篇练过：字面量池里找 `0x40011000`），可"源码级调试"的待遇，宏时代是没有的。要是三处 `LED_ON()` 调用点出了问题，您在反汇编里只能一个一个对地址。

## 五个问题，一起点数

咱们把代价归总，数一数这版能跑的宏封装带着的五个问题：

1. **无类型**：`pin` 是裸数字，您把 `13` 写成 `0x13` 编译照过，运行错位。
2. **时钟外置**：端口与电闸的配对，全靠咱们自己守纪律，可纪律总有守不住的一天。
3. **正交维度全靠人肉**：复用、速度、上下拉这些 04 篇组合表里的维度，加上极性，每个维度都是一行独立代码，相互的合法性，没有谁替咱们背书。
4. **不可调试**：预处理把函数边界和符号名都吞了，咱们在调试器里抓不住它。
5. **一致性靠复制粘贴**：从第二盏灯开始，咱们每一处修改都得乘以灯数。

## 说句公道话

咱们别急着把 C 宏说成草包。一个人维护、需求固定、片子单一的项目，您拿这版宏撑十年都不成问题：按钮、蜂鸣器、继电器，每个外设配上自己的一套宏、文件分清楚，比裸寄存器强出的可是一个身位。嵌入式工业史上一半的固件就是这么写的，包括今天还在产线上跑的那些。宏的问题只在**规模**和**协作**：等灯从一盏变八盏、维护的人从一个变一组，五个问题就一个个暴露出来了。

> 位操作宏这一族的完整用法（置位、清位、翻转、判位，还有 C 语境下的陷阱），咱们在 C 教程里已经系统盘过一遍了，您要细看，翻[《嵌入式 C 编程模式》](../../../../vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md)那一篇就够了，这儿就不重讲了。

## 出路：三步重构的路线图

咱们就顺着刚点破的病根一条条来治：

1. **上类型**：咱们把端口、引脚、极性从裸数字变成 `enum class`，拼错直接编译失败，问题 1 解决。
2. **绑时钟**：咱们把"这个脚的时钟"做成类型的属性，初始化时自动合闸，问题 2 解决。
3. **约束组合**：咱们让 concept 把非法组合（输入脚当输出用）挡在编译期，问题 3 解决。

咱们这三步走下去，终点就是 08 篇的 `estdx::Gpio<Port, Pin, Dir>`。到了下一篇，咱们歇一脚，看看 HAL 的官方路线：五个问题它解决了几个、这套方案值多少字节，咱们上秤称。

## 您来动手

1. 您把第一版宏抄进 `01_register_led` 改造一份，编译后 `arm-none-eabi-size` 对比，验证"宏零开销"不是空话。
2. 您亲手试试幽灵 bug 在模拟器里露不露馅：把 `LED_PORT` 改成 `GPIOA`、`LED_PIN` 改成 `5`，但故意不改 `IOPCEN`，Renode 里看 PA5 的 ODR 有没有动静（提示：02 篇教过用 `sysbus ReadDoubleWord` 读外设地址，GPIOA 的基址是 `0x40010800`）。动手前您不妨想想：03 篇实测过，Renode 的 GPIO 模型压根不模拟时钟门控，04 篇的翻车实录里，不配 CRH 直接写 BSRR，ODR 也纹丝不动。猜完了再看结果。
3. 您拿 `gcc -E` 对一个只有宏的 .c 文件跑一遍预处理，肉眼看 `LED_MODE_OUT()` 展开成什么，体会"调试器读不懂宏"的原因。
4. 您想想第 4、5 个问题（不可调试、复制粘贴）为什么"上类型"治不了，得靠什么治。

## 欸欸！自查一下再走

- 幽灵 bug（忘改时钟）为什么编译器抓不到？您说说，"类型正确但语义错误"中间隔着什么？
- `LED_ON` 用 BRR、`LED_OFF` 用 BSRR，您对着 Blue Pill 的接法（05 篇）说一遍，为什么不是反过来？
- 这五个问题，哪几个是跟着规模来的，咱们只有一盏灯时还不存在？哪几个从第一天就在？
- 您来对一对：三步重构各自解决哪个问题？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="ISO/IEC"
    title="N2176 — C Preprocessor Working Draft (宏语义)"
    :year="2007"
    url="https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1425.pdf"
    chapter="6.10 Preprocessing directives"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="stm32f10x_std_periph_lib — STM32F10x Standard Peripheral Library"
    :year="2011"
    url="https://www.st.com/en/embedded-software/stsw-stm32054.html"
    chapter="stm32f10x_gpio.c — C 函数式封装的官方前辈"
  />
  <ReferenceItem
    :id="3"
    author="Hunt, Andrew & Thomas, David"
    title="The Pragmatic Programmer — DRY 原则出处"
    :year="1999"
    url="https://pragprog.com/titles/tpp20/the-pragmatic-programmer-20th-anniversary-edition/"
    chapter="Ch.2 A Pragmatic Approach"
  />
</ReferenceCard>
