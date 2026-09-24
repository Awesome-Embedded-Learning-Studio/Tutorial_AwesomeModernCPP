---
title: "操作的本质：咱们写的是代码，动的是地址"
description: "把 GPIOC->CRH |= x 拆到不能再拆：CMSIS 的结构体和宏层层展开成 *(volatile uint32_t*)0x40011004 的一次读改写，反汇编里 ldr/orr/str 三条指令逐条对账，字面量池里躺着 0x40011000 本尊；再看 0x40000000 往上住的不是内存是外设，写地址是下令、读地址是收报；volatile 划定编译器的知情权边界，配一个 host 可跑的在线示例看等待循环在 -O2 下被整个删掉；最后用现代 C++ 的眼睛重看 reinterpret_cast 与 uintptr_t 的正当用法——本篇所有地址、指令、寄存器读数全部真跑可复现"
chapter: 1
order: 1
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 操作的本质：咱们写的是代码，动的是地址

上一站收工的时候，咱们的观测家当已经齐了三层：Renode 采样看结果、GDB 断点看过程、真板烧录管盖章。但您回头看一眼 `00_my_blinky` 的 `main.cpp`——满眼都是 `HAL_Init`、`HAL_GPIO_WritePin` 这些库的嘴。库说什么，咱们记什么，中间每一层都是"它说是这样，那就是这样"。

这一站从库的例程堆里换一份存货：`examples/01_register_led/`，一份不带任何 GPIO 封装的裸寄存器点灯。它跟 `01_blinky` 点的是同一盏 PC13，体积表上一份 2380 字节、一份 3468 字节，差价是什么，等咱们拆 HAL 那篇再逐个函数结账。今天只干一件事：把这份例程里的每一行，拆到不能再拆。

```cpp
// third_party/libestdx/examples/01_register_led/main.cpp（节选）
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; // 打开 GPIOC 的时钟开关
GPIOC->CRH &= ~GPIO_CRH_MODE13;     // 清掉 PC13 的 MODE 位
GPIOC->CRH |= GPIO_CRH_MODE13_1;    // 配成 2MHz 输出

GPIOC->BSRR = GPIO_BSRR_BS13;       // PC13 输出高（灯灭）
GPIOC->BRR  = GPIO_BRR_BR13;        // PC13 输出低（灯亮）
```

五行代码，六个问题。`RCC` 和 `GPIOC` 是什么？`->` 后面这些名字是什么？`|=` 和 `=` 落到机器上差在哪？咱们一个一个来。

## 先把糖衣剥了

`GPIOC` 看着像个全局对象，其实是个宏。追到 CMSIS 的设备头文件 `stm32f103xb.h` 里，定义链长这样：

```cpp
// Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h
#define PERIPH_BASE          (0x40000000UL)                  // L583 附近
#define APB2PERIPH_BASE      (PERIPH_BASE + 0x00010000UL)    // L583
#define GPIOC_BASE           (APB2PERIPH_BASE + 0x00001000UL)// L604
#define GPIOC                ((GPIO_TypeDef *)GPIOC_BASE)    // L666
```

三层宏套下来，`GPIOC` 就是 `(GPIO_TypeDef *)0x40011000`——一个指向固定地址的指针，一个 C 风格的强制转型，没有任何对象、没有任何构造，纯纯的地址。加法咱们当场验算：`0x40000000 + 0x10000 + 0x1000 = 0x40011000`。

那 `GPIO_TypeDef` 呢？一个只有七个成员的结构体，每个成员都是 `__IO uint32_t`：

```cpp
// stm32f103xb.h L357-L366
typedef struct
{
  __IO uint32_t CRL;   // 偏移 0x00：低 8 脚的配置
  __IO uint32_t CRH;   // 偏移 0x04：高 8 脚的配置
  __IO uint32_t IDR;   // 偏移 0x08：输入数据
  __IO uint32_t ODR;   // 偏移 0x0C：输出数据
  __IO uint32_t BSRR;  // 偏移 0x10：原子置位/复位
  __IO uint32_t BRR;   // 偏移 0x14：原子复位
  __IO uint32_t LCKR;  // 偏移 0x18：配置锁定
} GPIO_TypeDef;
```

C/C++ 的规矩：结构体成员的地址就是基地址加偏移。所以 `GPIOC->CRH` 的真身是 `0x40011000 + 0x04`，`GPIOC->BSRR` 的真身是 `0x40011000 + 0x10`。上一站咱们在 Renode 里反复采样那个 `0x4001100C`，现在可以对账了：`0x40011000 + 0x0C`，GPIOC 的 ODR 本尊——当时咱们说"这是灯的状态寄存器"，现在咱们能自己把它算出来了。

把五行代码里最重的一行做全等变形：

```cpp
GPIOC->CRH |= GPIO_CRH_MODE13_1;
// 等价于：
*((volatile unsigned int *)0x40011004) |= 0x00200000;
```

**咱们写的是代码，动的是地址。**这一行里没有函数调用、没有抽象、没有魔法，就是往一个写死的地址上，做一个"读回来、改一位、写回去"的三步动作。整个嵌入式外设编程的地基，就是这一句话。

## 地址后面住的不是内存

那 `0x40011004` 这个地址后面住的是什么？答案取决于地址本身。Cortex-M3 把 4GB 地址空间划成了几大块，跟咱们日常相关的三块：

| 地址范围 | 住着谁 | 怎么个用法 |
|---------|--------|-----------|
| `0x08000000` 起 | Flash | 代码和常量，掉电不丢 |
| `0x20000000` 起 | SRAM | 变量，掉电就没 |
| `0x40000000` 起 | 外设 | 不是存储，是"机关" |

前两块是真内存，写进去什么读出来什么。第三块完全两样：往这些地址写，是**下达命令**；从这些地址读，是**收取报告**。写 `0x40011010`（BSRR）一个 `0x2000`，命令是"把 GPIOC 的 13 脚顶到高电平"；读 `0x4001100C`（ODR），报告的是"这个端口每个脚现在的输出状态"。

路径上发生的事：CPU 执行到那条写指令，把地址和数据放到内部总线上；总线矩阵查了查地址——`0x40011000` 段，APB2 总线，归 GPIOC 模块管——就把这次写转发过去；GPIOC 里那组真正的触发器电路收到信号，按 BSRR 的位定义执行置位或复位。没有软件参与，没有操作系统，硬件按图索骥。这套"用统一的内存地址操作外设"的方案叫 MMIO（memory-mapped I/O，内存映射输入输出）；x86 那边还有另一路叫端口 I/O 的独立指令通道，Cortex-M 从出生就只用 MMIO 这一途，对编译器友好——访问外设和访问内存是同一套 load/store 指令。

口说无凭。咱们在 Renode 里直接问这些地址要报告（`01_register_led` 跑起来之后）：

```text
sysbus ReadDoubleWord 0x40011004   →  0x00200000
sysbus ReadDoubleWord 0x40021018   →  0x00000010
```

`0x40021018` 是谁？`RCC` 基址 `0x40021000` 加偏移 `0x18`，APB2ENR——外设时钟开关寄存器。读数 `0x10`，二进制 `0001 0000`，bit 4 站着——GPIOC 的时钟开关是合上的。`0x40011004` 的 `0x00200000`，bit 22 站着——CRH 里 PC13 那四位（[23:20]）此刻的值是 `0010`。地址后面，真的有一套电路在记账。

> RCC 是 reset and clock control，复位和时钟控制器，整块芯片的"配电房"。它的偏移表和 GPIO 一个道理，完整拆解留给下一篇。

## 反汇编：三条指令的读改写，一条指令的直达

变形归变形，到底编译器真的生成了什么？`arm-none-eabi-objdump -d` 伺候。`01_register_led` 的 `main` 里，配置那三行落成了这样：

```text
08000158: 4a0c      ldr   r2, [pc, #48]   ; r2 ← 字面量池: 0x40021000 (RCC)
0800015a: 4c0d      ldr   r4, [pc, #52]   ; r4 ← 字面量池: 0x40011000 (GPIOC)
0800015c: 6993      ldr   r3, [r2, #24]   ; r3 ← *(0x40021018)  读 APB2ENR
0800015e: f043 0310 orr.w r3, r3, #16     ; r3 |= 0x10          置 bit4
08000162: 6193      str   r3, [r2, #24]   ; *(0x40021018) ← r3  写回 APB2ENR
08000164: 6863      ldr   r3, [r4, #4]    ; r3 ← *(0x40011004)  读 CRH
08000166: f423 0370 bic.w r3, r3, #15728640 ; r3 &= ~0x00F00000 清 [23:20]
0800016a: 6063      str   r3, [r4, #4]    ; 写回 CRH
0800016c: 6863      ldr   r3, [r4, #4]    ; 再读 CRH
0800016e: f443 1300 orr.w r3, r3, #2097152 ; r3 |= 0x00200000   置 [21]
08000172: 6063      str   r3, [r4, #4]    ; 写回 CRH
```

`|=` 和 `&=` 果然是"读、改、写"三条指令一组，两组下来 CRH 被完整地配成了 `0x00200000`——和咱们刚才在 Renode 里读到的值一字不差。而翻转那两行是另一个待遇：

```text
08000178: 6125      str   r5, [r4, #16]   ; *(0x40011010) ← 0x2000  写 BSRR
0800017a: f000 f887 bl    800028c <HAL_Delay>
0800017e: f44f 70fa mov.w r0, #500
08000182: 6165      str   r5, [r4, #20]   ; *(0x40011014) ← 0x2000  写 BRR
```

单条 `str`，一步到位。为什么 BSRR 敢这么直，ODR 那边就得绕三步？这个问题的完整答案（读改写在并发下的竞争、BSRR 的原子性设计）是第四篇的正菜，这里先把现象记下：**同一个寄存器组里，不同地址有不同的"脾气"，这脾气是硬件设计者定的，不是软件能选的**。

细心的您可能还注意到 `ldr r2, [pc, #48]` 这个奇怪的寻址。Thumb 指令一律 16 或 32 位，塞不下 `0x40021000` 这种 32 位常数，编译器就把这些常数安放在函数代码后面的"字面量池"（literal pool）里，用 PC 相对寻址去取。`main` 结尾那两行 `.word 0x40021000`、`.word 0x40011000` 就是池子本尊——您要找"程序里到底用了哪些外设地址"，直接翻字面量池，一抓一个准。

## volatile：划定编译器的知情权

回头看结构体定义里那个 `__IO`。追到 `core_cm3.h`（CMSIS 内核头）：

```cpp
#define     __IO    volatile
```

就是 `volatile`。它在这儿的身份不是"优化提示"，而是一份**知情权声明**：这个地址的内容，会在你看不见的时刻、以你看不见的方式变化——硬件改的，中断改的，反正不是当前代码流改的。编译器对这个地址的每次读写，都必须动真格，不许缓存、不许合并、不许凭"刚才读过"就自作主张。

没有它会出什么事？咱们不上板子，host 上就能复现。下面这个示例里有两份"状态寄存器"，一份普通变量，一份 volatile 变量，各等它变 1 再往下走：

<OnlineCompilerDemo allow-run
  title="volatile 与等待循环：-O2 下的两种命运"
  source-path="code/examples/vol8/01_mmio_volatile.cpp"
  description="两份状态变量各等一次置位：点运行程序正常结束；点开汇编对比——普通版等待循环被编译器整个删掉（它看着上面的赋值断定条件恒假，一次内存都不读），volatile 版老老实实保留 load 循环。对外设状态的知情权，就差这一个关键字"
/>

编译器不是在使坏，它是在按规矩办事：**普通变量的变化必须由代码流造成**，它看得见的赋值就是全部真相，所以循环可以被证明为死代码。volatile 把这个"看得见"的边界重新划定：这个地址的真相在硬件手里，你的每一次读都是采风。

两个常被搅在一起的边界也顺手划清。`volatile` 只约束"访问必须真实发生"，**不提供原子性**——`CRH |= x` 那三条指令中间来一个中断，照样翻车，这个坑第四篇专门埋专门挖；多核同步那是 `<atomic>` 的地盘，volatile 管不了也不该管。C++ 标准里 volatile 的完整语义辨析，咱们在 C 教程那篇[《嵌入式 C 编程模式》](../../../../vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md)里盘过，这里不重讲。

## 现代 C++ 的眼睛重看这件事

把糖衣剥到底之后，回头看 `GPIOC` 那个 C 风格强转 `(GPIO_TypeDef *)0x40011000`。日常 C++ 里强转是被嫌弃的惯用法，但这里是它少数的正当场景：**地址是硬件手册写死的，不是运行时算出来的**。现代 C++ 只求把这件事写得更诚实：

```cpp
#include <cstdint>

// 地址作为编译期常量,类型转型用显式的那个
constexpr auto gpio_c =
    reinterpret_cast<volatile std::uint32_t *>(0x4001'1000u);

gpio_c[1] &= ~0x00F0'0000u;  // CRH: 基址 + 4 字节 = 偏移 0x04
```

`reinterpret_cast` 比括号转型好在哪：全文可搜索、意图自声明、不会跟函数风格转型一样顺手把 const 也剥了。`std::uintptr_t` 则是"把指针当整数算"时的合法渡口——地址加减偏移的中间态用它装，不会像拿 `int` 装那样在窄平台上溢出踩坑。

再往前一步，把"地址加偏移"也收进一个函数：

```cpp
constexpr auto &reg32(std::uintptr_t base, unsigned offset) {
    return *reinterpret_cast<volatile std::uint32_t *>(base + offset);
}

reg32(0x4001'1000u, 0x10) = 0x2000;  // GPIOC->BSRR = BS13
```

到这一步，抽象还非常薄：一个函数、两行语义，换来的是所有 MMIO 访问有了统一的入口。那再往前呢？——把地址和偏移装进类型、让"GPIO 输出脚"和"UART 波特率寄存器"在类型上就是两种东西、配错脚位编译期就报错？这是本站第七篇的主场，`estdx::Gpio<Port, Pin, Dir>` 模板把刚才这一步走到头。今天先记住方向：**从裸地址到强类型，中间每一层都该是可验证的薄层，而不是一堵黑墙**。

## 到这儿，链路通了

把今天拆开的零件按顺序串起来：代码里一行 `GPIOC->BSRR = 0x2000`，经过宏展开是固定地址 `0x40011010`，编译成一条 `str`，总线把这次写路由到 GPIOC 模块，触发器电路把 13 脚的电平顶起来——Renode 里读 `0x4001100C`，报告 `0x00002000`；半秒后 BRR 写入，读数归零。**从一行代码到一个电平，中间没有不可知的环节**。这就是本站的地基，后面七篇都在这块地基上盖楼。

不过您可能已经憋了一个问题：`main` 里第一行为什么是开时钟？那行 `RCC->APB2ENR |= 0x10` 要是不写，后面全是什么光景？下一篇咱们把 RCC 这个配电房整个逛一遍——顺便看看"外设没供电时写寄存器"这个新手坟场Top1的坑，在 Renode 里长什么样。

## 您来动手

1. 把 `third_party/libestdx` 构建出来（起步站的流程原样能跑），`cmake --build build --target register_led`；
2. `arm-none-eabi-objdump -d build/examples/01_register_led/register_led | sed -n '/<main>:/,/^$/p'`，亲手找到字面量池里的 `0x40011000`；
3. 拿 `main` 里 BSRR 那条 `str` 的偏移，对 `GPIO_TypeDef` 的偏移表，验一遍 `0x10` 的来历；
4. 把 `01_mmio_volatile.cpp` 在 Compiler Explorer 上换成 `-O0` 再看汇编，两个循环的区别还剩多少——想想为什么。

## 欸欸！自查一下再走

- `GPIOC` 的地址 `0x40011000`，您能不查表、从 `PERIPH_BASE` 一路加出来吗？
- `GPIOC->CRH |= x` 落到指令层是几条？`GPIOC->BSRR = x` 呢？
- `0x4001100C` 是谁？为什么上一站采样宏读的就是它？
- `volatile` 在这里保护的到底是什么——是速度，还是正确性？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="2 Memory map; 9 GPIO and AFIO registers"
  />
  <ReferenceItem
    :id="2"
    author="Arm Ltd."
    title="Arm Cortex-M3 Processor Technical Reference Manual (DDI 0337)"
    :year="2024"
    url="https://developer.arm.com/documentation/ddi0337"
    chapter="3 System address map; 3.3 Peripheral memory map"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f103xb.h — CMSIS Device Header"
    :year="2016"
    url="https://github.com/STMicroelectronics/cmsis_device_f1/blob/master/Include/stm32f103xb.h"
    chapter="L357 GPIO_TypeDef; L583-L666 Peripheral memory map"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="cv (const and volatile) type qualifiers"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/cv"
    chapter="Uses of volatile"
  />
</ReferenceCard>
