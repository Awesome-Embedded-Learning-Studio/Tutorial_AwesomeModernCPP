---
title: "操作的本质：咱们写的是代码，动的是地址"
description: "把 GPIOC->CRH |= x 拆到不能再拆：CMSIS 的结构体和宏层层展开成 *(volatile uint32_t*)0x40011004 的一次读改写，反汇编里 ldr/orr/str 三条指令一条条对过，字面量池里收着 0x40011000 本尊；再看 0x40000000 往上不是内存是外设，写地址是下令、读地址是收报；volatile 划定编译器的知情权边界，配一个 host 可跑的在线示例看等待循环在 -O2 下被整个删掉；最后用现代 C++ 的眼睛重看 reinterpret_cast 与 uintptr_t 的正当用法；收尾把整个固件的 778 条指令做个普查，兑现上一篇“CPU 对外只有读写”的断言——278 条访存全是 ldr/str 一族零例外，本篇所有地址、指令、寄存器读数全部真跑可复现"
chapter: 1
order: 2
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 操作的本质：咱们写的是代码，动的是地址

机器看清了，该轮到代码了。咱们回头看一眼 `00_my_blinky` 的 `main.cpp`：啊哈，满眼都是 HAL 库的代码，初始化用的是 `HAL_Init`，写 GPIO 用的是 `HAL_GPIO_TogglePin`。把细节建立起来的那部分，整个被库埋起来了。

> 可能有朋友困惑了，为什么一定要把这个说清楚？会用 C++ 写不好嘛？
> 不对的，咱们的理由很简单。就是因为咱们要用 C++ 的零开销抽象，所以必须知道，咱们的世界底下发生了什么：什么可以做到编译期抽象，什么不行。这一点咱们不弄清楚，早晚有一天会被编译器的警告和报错吓退，最后只好糊弄新人：C++ 写不了单片机！

那咱们就动手把它挖出来。笔者自己编写了一份不带任何 GPIO 封装的裸寄存器点灯，它跟 `01_blinky` 点的是同一盏 PC13，`arm-none-eabi-size` 称出来的结果是：一份 2380 字节、一份 3468 字节。

```cpp
// third_party/libestdx/examples/01_register_led/main.cpp（节选）
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;  // 打开 GPIOC 的时钟开关
GPIOC->CRH &= ~(0xFu << 20);         // 清掉 PC13 的 CNF、MODE 四位
GPIOC->CRH |= (0x2u << 20);          // 配成 2MHz 输出

GPIOC->BSRR = 1u << 13;              // PC13 输出高（灯灭）
GPIOC->BRR  = 1u << 13;              // PC13 输出低（灯亮）
```

咱们捡这五行代码里最要紧的问：`RCC` 和 `GPIOC` 是什么？`->` 后面这些名字是什么？`|=` 和 `=` 落到机器上差在哪？一个一个来。

## 踢掉这些抽象！回答我！它们是什么呢

`GPIOC` 看着像个货真价实的全局对象，咱们把它拆开看，其实是个**宏**（写 C++ 的朋友，是不是已经敏锐地嗅到 `constexpr` 能在这儿大展宏图的味道了？）。追到 CMSIS 的设备头文件 `stm32f103xb.h` 里，定义链长成了这样：

```cpp
// Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h
#define PERIPH_BASE          0x40000000UL                    // L575
#define APB2PERIPH_BASE      (PERIPH_BASE + 0x00010000UL)    // L583
#define GPIOC_BASE           (APB2PERIPH_BASE + 0x00001000UL)// L604
#define GPIOC                ((GPIO_TypeDef *)GPIOC_BASE)    // L666
```

三层宏套下来的 `GPIOC`，就是 `(GPIO_TypeDef *)0x40011000`：一个指向固定地址的指针，一个 C 风格的强制转型。咱们在这里找不到任何对象、任何构造，它就是纯纯的地址。加法咱们当场验算：`0x40000000 + 0x10000 + 0x1000 = 0x40011000`。

那 `GPIO_TypeDef` 呢？咱们看它的真身，一个只有七个成员的结构体，成员的类型清一色是 `__IO uint32_t`：

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

C/C++ 这边有一条现成的规范：结构体成员的地址，就是基地址加偏移。所以 `GPIOC->CRH` 的真身是 `0x40011000 + 0x04`，`GPIOC->BSRR` 的真身是 `0x40011000 + 0x10`。起步站咱们在 Renode 里反复采样那个 `0x4001100C`，当时只报了它的名字：GPIOC 的 ODR（输出数据寄存器）。现在您能自己把它算出来了：`0x40011000 + 0x0C`，正是 ODR 的本尊。

从宏到地址的路，咱们一张图就能走完——左边是三层宏的加法，下面是七个成员各自落到的地址，本篇和上一篇打过照面的几个地址都描了色：

![GPIOC 宏链与 GPIO_TypeDef 偏移映射：顶部 PERIPH_BASE 0x40000000 经 +0x10000、+0x1000 两级加法得 GPIOC_BASE 0x40011000，下方七个成员（CRL 到 LCKR，偏移 0x00–0x18）与实际地址一一对应，CRH/ODR/BSRR 三行高亮](./02-gpioc-macro-offsets.drawio)

咱们给五行代码里最重的那一行做个全等变形：

```cpp
GPIOC->CRH |= (0x2u << 20);
// 等价于：
*((volatile unsigned int *)0x40011004) |= 0x00200000;
```

**咱们写的是代码，动的是地址**。咱们在这一行里找不到函数调用、找不到抽象、也找不到魔法，就是往一个写死的地址上，做一个"读回来、改一位、写回去"的三步动作。**整个嵌入式外设编程的地基就是这一句话。够了！**

## 地址后面的不是内存

那 `0x40011004` 这个地址后面是什么？上一篇咱们看过架构把 4GB 划成的几大块，落到咱们这块 STM32F103 上，跟日常相关的三块：

| 地址范围        | 是什么 | 怎么个用法           |
| --------------- | ------ | -------------------- |
| `0x08000000` 起 | Flash  | 代码和常量，掉电不丢 |
| `0x20000000` 起 | SRAM   | 变量，掉电就没       |
| `0x40000000` 起 | 外设   | 不是存储，是"机关"   |

Flash 和 SRAM 是老实的真内存，写进去什么读出来什么。外设那块完全两样：您往这些地址写，下的就是**命令**。从这些地址读，收的就是**报告**。咱们往 `0x40011010`（BSRR）塞一个 `0x2000`，命令是"把 GPIOC 的 13 脚顶到高电平"。您再读 `0x4001100C`（ODR），报告的是"这个端口每个脚现在的输出状态"。

两种地址的性格，咱们摆在一起看最清楚：

![MMIO 与真内存的对比：左侧 SRAM 写入 0x22 原样读回（对称），右侧外设写 BSRR 是下令（PC13 顶到高）、读 ODR 是收报（各脚输出状态），底部注明两条路走的是同一套 ldr/str](./02-mmio-vs-memory.drawio)

上一篇咱们跟着一条 str 全程走过：总线矩阵按地址把这次写路由给 GPIOC 的电路，触发器按 BSRR 的位定义置位或复位。这里没有软件的参与、没有操作系统，硬件按图索骥地把事办了。这套"用统一的内存地址操作外设"的方案叫 MMIO（memory-mapped I/O，内存映射的输入输出）。x86 那边还有另一路叫端口 I/O 的独立指令通道，Cortex-M 生下来用的就只有 MMIO 这一途，对编译器倒是友好——访问外设和访问内存，用的是同一套 load/store 指令。

咱们不搞口说无凭这一套，直接在 Renode 里问这些地址要报告（`01_register_led` 跑起来之后）：

```text
sysbus ReadDoubleWord 0x40011004   →  0x00200000
sysbus ReadDoubleWord 0x40021018   →  0x00000010
```

`0x40021018` 的主人是谁？`RCC` 的基址 `0x40021000` 加上偏移 `0x18`，落到的就是 APB2ENR——管外设时钟开关的寄存器。读出来的数是 `0x10`，它的二进制是 `0001 0000`，bit 4 稳稳地站着：GPIOC 的时钟开关是合上的。`0x40011004` 的 `0x00200000`，站着的是 bit 21，CRH 里 PC13 的四个配置位（[23:20]），此刻的值是 `0010`。这些数是地址后面那套电路实时报上来的，咱们一个字都没编。

> RCC 的全称是 reset and clock control——复位和时钟控制器，整块芯片的复位和时钟都归它管。它的偏移表和 GPIO 一个道理，咱们下一篇去拆它的闸门。

## 反汇编：三条指令的读改写，一条指令的直达

变形只是咱们纸面上的推演，编译器到底真的生成了什么？咱们请 `arm-none-eabi-objdump -d` 伺候。`01_register_led` 的 `main` 里，配置那三行落成了这样：

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

`|=` 和 `&=` 果然走的是"读、改、写"三条指令一组的路子，两组下来 CRH 被完整地配成了 `0x00200000`，和咱们刚才在 Renode 里读到的值一字不差。而翻转那两行是另一个待遇：

```text
08000178: 6125      str   r5, [r4, #16]   ; *(0x40011010) ← 0x2000  写 BSRR
0800017a: f000 f887 bl    800028c <HAL_Delay>
0800017e: f44f 70fa mov.w r0, #500
08000182: 6165      str   r5, [r4, #20]   ; *(0x40011014) ← 0x2000  写 BRR
```

单条 `str` 一步就到位了。为什么 BSRR 敢这么直，ODR 那边就得绕三步？咱们把这个问题的完整答案（读改写在并发下的竞争、BSRR 的原子性设计）留给第四篇专门讲，咱们眼下只把现象摆出来：**同一个寄存器组里，不同地址有不同的"脾气"，这脾气是硬件设计者定的，不是软件能选的**。

笔者把两种待遇做成了动画，上面一排是 `|=` 的读、改、写三连，下面是 `=` 的一条直达，您步进着看它们各自怎么走：

<Anim id="f103-rmw-vs-direct" />

细心的您可能还注意到 `ldr r2, [pc, #48]` 这个奇怪的寻址。Thumb 指令一律只有 16 或 32 位的宽度，塞不下 `0x40021000` 这样的 32 位常数，编译器就把这些常数安放在函数代码后面的"字面量池"（literal pool）里，用 PC 相对寻址的方式去取。`main` 结尾那两行 `.word 0x40021000`、`.word 0x40011000` 就是池子本尊：您要找"程序里到底用了哪些外设地址"，咱们直接翻字面量池，想找的地址一抓一个准。

`main` 的戏份是特写，咱们把镜头拉远。笔者说：**CPU 对外只有读和写**。现在您读得懂反汇编了，咱们当场验证一回，把整个固件的指令做个普查。命令是三段的接力：objdump 吐出反汇编，awk 按制表符切出助记符那一列（反汇编行天然是"地址、编码、助记符、操作数"四列，凑不满三列的节名和函数标签被滤掉），咱们让 `sort | uniq -c | sort -rn` 数个数、按多少排：

```text
$ arm-none-eabi-objdump -d build/examples/01_register_led/register_led \
    | awk -F'\t' 'NF>=3 {print $3}' | awk '{print $1}' \
    | sort | uniq -c | sort -rn | head -12
    185 ldr
     56 str
     45 cmp
     45 bl
     40 .word
     38 b.n
     35 movs
     35 lsls
     27 mov
     21 bic.w
     19 subs
     19 orr.w
```

咱们再拿关键字筛出碰内存的那一族：

```text
$ arm-none-eabi-objdump -d build/examples/01_register_led/register_led \
    | awk -F'\t' 'NF>=3 {print $3}' | awk '{print $1}' \
    | grep -cE '^(ldr|str|ldrb|strb|ldrh|strh|ldm|stm|push|pop)'
278
```

筛出来的这 278 条，咱们每一条都见过，全是 ldr/str 一族：字节版的 ldrb/strb，栈版的 push/pop，多寄存器版的 ldm/stm，都算上了。整个固件 778 条指令（直方图里那 40 个 `.word` 是字面量池的数据，不占指令的名额），剩下的 500 条全是纯寄存器运算和跳转。零例外：上一篇立下的那句"CPU 对外只有读和写"，在 778 条指令里站住了。

## volatile：划定编译器的知情权

咱们回头看结构体定义里那个 `__IO`，一路追到 `core_cm3.h`（CMSIS 的内核头）：

```cpp
#define     __IO    volatile
```

它的真身就是 `volatile`。它在这儿的身份是一份**知情权声明**，不是什么"优化提示"：这个地址的内容，会在咱们看不见的时刻、以咱们看不见的方式变化。谁改的？硬件改的，中断改的，反正不是当前代码流改的。所以编译器对这个地址的每次读写，都必须是动真格的——不许缓存、不许合并、也不许凭"刚才读过"就自作主张。

没有它会出什么事？这一回咱们连实际板子都不用，在 host 上就把它复现了。咱们在示例里备了两份"状态寄存器"，一份是没加 volatile 的普通变量，另一份是 volatile 修饰的变量，各自等它变 1 的信号再往下走：

<OnlineCompilerDemo allow-run
  title="volatile 与等待循环：-O2 下的两种命运"
  source-path="code/examples/vol8/01_mmio_volatile.cpp"
  description="两份状态变量各等一次置位：点运行程序正常结束；点开汇编对比——普通版等待循环被编译器整个删掉（它看着上面的赋值断定条件恒假，一次内存都不读），volatile 版老老实实保留 load 循环。对外设状态的知情权，就差这一个关键字"
/>

咱们替编译器说句公道话：它是在照章办事。**普通变量的变化必须由代码流造成**，它看得见的赋值就是全部真相，所以循环可以被证明为死代码。volatile 把这个"看得见"的边界重新划了一遍：这个地址的真相在硬件手里，您每次去读，拿到的都是硬件当下的真实状态。

还有两个常被搅在一起的边界，咱们也划清楚。`volatile` 管的只是"访问必须真实发生"，**不提供原子性**：`CRH |= x` 的三条指令中间来一个中断就照样翻车，这组竞争的来龙去脉，咱们留到第四篇专门拆。多核同步那是 `<atomic>` 的地盘，volatile 管不了也不该管。C++ 标准里 volatile 的完整语义辨析，咱们在 C 教程那篇[《嵌入式 C 编程模式》](../../../../vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md)里盘过，这里就不重讲了。

## 现代 C++ 的眼睛重看这件事

糖衣剥到了底之后，咱们回头看 `GPIOC` 那个 C 风格强转 `(GPIO_TypeDef *)0x40011000`。放在日常的 C++ 里，这样的强转一向是被嫌弃的写法，但这里是它少数的正当场景：**地址是硬件手册写死的，不是运行时算出来的**。现代 C++ 求的只是把这件事写得更诚实：

```cpp
#include <cstdint>

// 地址作为编译期常量,类型转型用显式的那个
constexpr auto gpio_c =
    reinterpret_cast<volatile std::uint32_t *>(0x4001'1000u);

gpio_c[1] &= ~0x00F0'0000u;  // CRH: 基址 + 4 字节 = 偏移 0x04
```

咱们看 `reinterpret_cast` 比括号转型好在哪里。它是全文可搜索的，意图也是自声明的，不会跟函数风格转型一样顺手把 const 也剥了。`std::uintptr_t` 则是"把指针当整数算"时的正确容器：地址加减偏移的中间态用它装，不会像拿 `int` 硬装的那样，在窄平台上就溢出了。

咱们再往前一步，把"地址加偏移"也收进一个函数：

```cpp
constexpr auto &reg32(std::uintptr_t base, unsigned offset) {
    return *reinterpret_cast<volatile std::uint32_t *>(base + offset);
}

reg32(0x4001'1000u, 0x10) = 0x2000;  // GPIOC->BSRR = BS13
```

走到了这一步，咱们攒下的抽象还非常薄：一个函数、两行语义，换来的是所有 MMIO 访问有了统一的入口。那咱们还能不能再往前走一层？把地址和偏移装进类型，让"GPIO 输出脚"和"UART 波特率寄存器"在类型上就是两种不同的东西，配错脚位编译期就报错？这是本站第八篇的主场，`estdx::stm32f1::Gpio<Port, Mask, Dir>` 模板把刚才这一步走到了头。

## 到这儿，链路通了

咱们把今天拆开的零件按顺序串一遍：代码里一行 `GPIOC->BSRR = 1u << 13`，经过宏展开是固定地址 `0x40011010`，编译出来的只有一条 `str`，总线把这次的写路由到 GPIOC 模块，触发器电路把 13 脚的电平顶起来——Renode 里读 `0x4001100C`，收到的报告是 `0x00002000`。半秒后 BRR 也写入了，读数跟着归了零。**从一行代码到一个电平的全程，中间没有不可知的环节**。这就是本站的地基，后面七篇都在这块地基上盖楼。

不过您可能已经憋了一个问题：`main` 里开时钟的那一行为什么非写不可？那行 `RCC->APB2ENR |= 0x10` 要是不写，后面全是什么光景？下一篇咱们把 RCC 的闸门和时钟树过一遍，顺便看看"外设没供电时写寄存器"这个新手最容易中招的问题，在 Renode 里长什么样。

## 欸欸！自查一下再走

- `GPIOC` 的地址 `0x40011000`，您能不查表、从 `PERIPH_BASE` 一路加出来吗？
- `GPIOC->CRH |= x` 落到指令层是几条？`GPIOC->BSRR = x` 呢？您现在能脱口而出吗？
- `0x4001100C` 是谁？咱们起步站采样宏读的，为什么就是它？
- `volatile` 在这里保护的到底是什么：是速度，还是正确性？您会怎么答？

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
