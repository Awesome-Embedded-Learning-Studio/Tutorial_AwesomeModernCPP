---
title: "点灯之前：Cortex-M3 这台机器长什么样"
description: "点灯之前把机器看清：ARM 卖蓝图不造芯片、Cortex-M3 是家族第一位成员的两本手册分工；核心家底——13 个通用寄存器加 SP/LR/PC、三级流水线取指、译码、执行各干什么、I-Code/D-Code/System 三条 AHB 加一条 PPB，哈佛到总线为止、编址统一到 4GB；架构手册 DDI 0403 Table B3-1 画死的地址分区（Code/SRAM/Peripheral/XN/PPB），拿 initial SP 0x20005000 和 bluepill.repl 的 nvic/rcc 逐行核对；load/store 架构的定调——运算指令只碰寄存器、访存只有 ldr/str 一族，A4.6 分章自证，全固件普查留到下一篇反汇编里兑现；一条 str 从取指到引脚全程走完，Renode LogPeripheralAccess 路由日志逐行对上，日志里的 PC 与下一篇反汇编互证——全部读数真跑可复现"
chapter: 1
order: 1
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 单片机
difficulty: beginner
platform: stm32f1
---

# 点灯之前：Cortex-M3 这台机器长什么样

好了！欢迎来到咱们的点灯环节。笔者在知乎冲浪的时候，看到一个很有意思的比喻，实在没找到它的出处，咱们就稍微借用一下。

> C 语言的入门头一件事，是您亲手写出
>
> ```c
> printf("Hello World");
> ```
>
> 单片机的入门头一件事，则是咱们亲手点亮一盏灯。

是的，点灯大概就是咱们整条 STM32F103 线的伟大征程的第一步。它标志着咱们可以用软件，近距离地控制硬件——真的，笔者没夸张！

当然，笔者不打算一上来就把灯点上。市面上主流的 32 位单片机，每一颗通电时跳动的 CPU，几乎都是从同一张设计图里出来的。就像咱们每个人都各有个性，而终归都是人，对吧。人类这个生物的谱系，决定了咱们最基本的底色。CPU 也是一样的，决定它底色的谱系，就是大名鼎鼎的 CPU 架构。咱们这段旅程要用的单片机，内核是名叫 Cortex-M3 的处理器，它照着的规范叫 ARMv7-M：v7 是 ARM 家架构的排号，-M 标的是微控制器这一路。

这套架构直接规定了咱们**能用哪些指令**、**地址怎么编号**，还有一条写指令怎么从 CPU 走到引脚。咱们要是没把这些弄清楚，点灯就只是把别人的代码原样抄一遍，真出了问题，也只能两手一摊地喊一句 Man what can i say。

## ARM 出蓝图，ST 造芯片

因为一些朋友可能真的不太知晓架构这个事情，只是沉迷于梭哈做自己喜欢的产品，笔者认为有必要介绍一下。ARM 是谁呢？其实是一家公司。它倒是不生产也不卖芯片，卖的是 CPU 的设计：实际用这个架构的芯片厂交授权费，拿到一份能直接流片的设计描述，往里加上自己的外设，贴上自己的牌子出货<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" />。（感觉这个模式有点奇妙）

这个模式的效果，咱们随手数一圈就能感受到：ST 的 STM32、NXP 的 LPC、TI 的 LM3、Microchip 的 SAM3、Nordic 的 nRF，还有树莓派 RP2040 里的那两颗 M0+，心脏统一是 Cortex-M 家族的设计。这样的厂商名单，咱们在维基百科<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" />的词条里数得更全，朋友们想数全可以自己去数。所以，咱们学它一次，就等于把一大片机器的公共心脏一起学了。

2004 年发布的 Cortex-M3，是这个家族里的第一位成员，咱们能在维基百科<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" />的词条里查到它的出生记录：目标人群就是单片机，指令集够精简了，中断延迟压到了 12 个周期，中断输入的数量最多 240 个。最早把它做成芯片的厂商里，一家叫 Luminary Micro 的小公司 2006 年抢了头筹，ST 的跟进就在次年，咱们的 STM32F103 一卖就是十几年<RefLink :id="5" preview="Power &amp; Robinson, Embedded.com, 2010" />的这段履历，十几块钱的 BluePill 上装的就是它。它出生起认的就只有 Thumb 指令集（Thumb-1 加 Thumb-2），老 ARM 内核的那套 32 位定长指令，它是压根就不执行的——维基百科<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" />的词条里也写得明明白白。所以下一篇反汇编里，指令也就有了胖瘦之分，16 位和 32 位是混着排的，这就是 Thumb-2 的模样。

这个授权模式还解释了一件咱们早该疑惑的事：手上的手册为什么有两本。内核怎么干活（寄存器有哪几个、指令集长什么样、地址怎么分区），写在 ARM 的手册里。芯片上多了什么（GPIO 有几组、USART 怎么配、RCC 在哪个地址），写在 ST 的参考手册里，它的编号是 RM0008<RefLink :id="4" preview="STMicroelectronics, RM0008" />的这本手册：RM 就是 Reference Manual（参考手册）的缩写，ST 家的文档全按这个序列排号，下面咱们偷个懒，直接叫它 RM0008 得了。下一篇的 `GPIO_TypeDef` 偏移表，就在 RM0008 的正文里。

今天咱们讲的东西，几乎全落在了 ARM 那边。ARM 还有更上一层的文档，就是编号 DDI 0403 的架构手册。DDI 也是 ARM 家技术文档的排号序列，跟 ST 的 RM 一个意思。但这本手册偏偏管得最宽：RM0008 写的是某一颗芯片，它写的却是所有 Cortex-M3 都必须遵守的规范，指令集的样子、4GB 地址的分区，也全画死在了里面，咱们马上就能见到。

## CPU 的家底：一把寄存器、三级流水线、三条总线

翻开 Cortex-M3 的技术参考手册（DDI 0337，名字太长了，下面咱们小小的偷懒一下，简称成了 TRM），直接看它的 §1.2.2<RefLink :id="2" preview="Arm Ltd., DDI 0337, §1.2.2" />的原文：官方给核心开的配置单上，处理器是 32 位的，指令集也定了 Thumb-2，单周期 32 位乘法和硬件除法都配齐了，流水线排成了三级，处理器接口做成了哈佛式。配置单上的单周期乘法、硬件除法，咱们点灯用不上，就挑马上要用的三件过：工作台、流水线、总线。

头一个要看的是工作台。咱们的 CPU 里有 13 个 32 位通用寄存器，编号从 R0 一直排到了 R12。另有三个带专职的：R13 是栈指针 SP、R14 是链接寄存器 LR，R15 干脆就是程序计数器 PC 的本尊。旁边站着的还有一位 xPSR，它负责记的就是运算标志。SP 的身份有点特殊：它其实分成了主栈 MSP 和进程栈 PSP 两个分组，这一层您现在不用管，等以后转向更有意思的 RTOS（也就是另外一套正在持续更新的 ZerOS 教程），咱们到时候再回头好好看它~。咱们手里就这一小把格子，是 CPU 的全部家当：任何运算要用的数据，咱们都得搬上桌，算完了再搬回去。搬进搬出对应的正是两种动作：读和写。这个伏笔咱们马上就用得上。

可能有朋友不太了解 CPU 是怎么干活的。没关系的，咱们简单说说。它干活走的是一条三级流水线：取指、译码、执行，一级各领了一件活（手册里管这三级叫 Fe、De、Ex，您认得一下就行，读手册的时候人脑翻译，有点太令人 emo 了）。

咱们一句话说清每级干啥：

- 取指这级从代码区把指令拿回来。粗暴点说，咱们就从存放指令的地方，一般就是咱们的 Flash 芯片，抓一条指令过来，塞到 CPU 里去
- 译码这级一边弄明白指令要干什么，一边顺手把要访问的地址算出来。访问啥呢？您想想，CPU 算 1+2 这个加法，您总得说一下这两个相加的数字从哪里来，对吧！
- 执行这级才真正做运算。咱们看着 CPU 可爱地拔了拔手指头，哦吼，是 3！太聪明了！算完了，它就把地址和数据交给总线接口丢回去，一般是写回内存里

三级其实不算深，桌面 PC 的 CPU 动辄十几级，咱们这颗不至于，主要考虑的是**行为确定、中断进出快**（也就是咱们说的**实时性**），单片机最在乎的就是这个。您只需要认准一条：地址是译码时就算好的，而真正碰内存，是执行时的事。后面咱们跟着一条写指令走全程，这两步您都会亲眼看到。

Cortex-M3 的处理器接口是哈佛式的：**取指和取数据各走各的口，而这两件事能同时进行**。但“哈佛”的作用只到总线这一层，所以咱们程序员看到的地址，是完整统一的 4GB。可能有朋友在这儿犯嘀咕：“欸？那我是不是得搞点特殊操作？一套地址编码拿数据，另一套地址编码拿指令？”这里不存在的。咱们都是直接用一个 0~4GB 的映射空间拿东西，只是头 512MB 给了代码区放指令，后面 512MB 给了 SRAM 放数据，就是类似的划分，不存在“指令一套编号、数据另一套编号”的事。

这是怎么做到的？咱们数一数它对外的接口就明白了：Cortex-M3 往外伸了三条 32 位 AHB 总线，外加的还有一条私有外设总线。

咱们把口子挨个报上名：I-Code 专门从代码区取指令，D-Code 专门管代码区的数据读写，System 负责其余空间的指令、数据和向量，PPB 则专管内核自用的外设，挂在它身上的还有中断控制器 NVIC 和 SysTick 定时器。

听着倒是四通八达，落到咱们头上的好处其实就一条：路可以分着修，地址必须一起编。

这套家底值得咱们配一张图，把寄存器、流水线、总线各就各位地摆进去：

![Cortex-M3 内核结构：左侧是寄存器组（R0–R12 通用，SP/LR/PC/xPSR 带专职），中间 Fe/De/Ex 三级流水线，右侧 I-Code/D-Code/System/PPB 四条总线出口](./01-cortex-m3-core.drawio)

## 您可能要催了：ARMv7-M 到底怎么规定咱们的 4GB 地址空间？

您别急，咱们这就翻开 ARMv7-M 架构手册（DDI 0403）的 B3.1 节。

咱们有一张值得整篇看一遍的表：架构把 4GB 空间划成几大块，每块放什么、带什么属性，全都写死了，而<RefLink :id="1" preview="Arm Ltd., DDI 0403E.e, Table B3-1" />的 Table B3-1，节录跟咱们相关的行：

| 地址范围                  | 名字       | 规划用途                                     |
| ------------------------- | ---------- | -------------------------------------------- |
| `0x00000000`–`0x1FFFFFFF` | Code       | 通常接 Flash，放代码和常量                   |
| `0x20000000`–`0x3FFFFFFF` | SRAM       | 片上内存                                     |
| `0x40000000`–`0x5FFFFFFF` | Peripheral | 片上外设，不许执行（XN）                     |
| `0x60000000`–`0x9FFFFFFF` | RAM        | 外部内存                                     |
| `0xA0000000`–`0xDFFFFFFF` | Device     | 外部设备，同样不许执行                       |
| `0xE0000000`–`0xFFFFFFFF` | System     | 头 1MB 是 PPB（NVIC、SysTick），往后是厂商区 |

分区表是架构规定的，咱们拿到哪家厂商的芯片，它对谁都是一视同仁的。并不是说 ST 用的是一套，NXP 又换了一套。不会的！

ST 做的事是往格子里填：`0x08000000` 起放 64K Flash，`0x20000000` 起放的就是 20K SRAM，`0x40000000` 起一个个地挂外设。填成什么样，查 RM0008 第 2 章的 memory map，那是下一篇的主场。表里连"外设区不许当代码执行"都标了：XN，也就是 eXecute Never 的缩写。外设区那些地址背后的电路，架构给它们的定位就是：永远只能读写、不能跳过去执行。架构替咱们把一类事故提前堵死。

哈？这表咱们真用得上吗？用得上的。您可能只是抄数字的时候没留意。咱们在[起步站](../00-env-setup/03-toolchain-anatomy.md)里，让 `file` 命令从 `.bin` 里嗅出过 initial SP 的位置：`0x20005000`，它就是 0x20000000 加 20K 的结果，正好顶到 SRAM 区的最高处。咱们自己的 bluepill.repl 平台文件里，`nvic @ sysbus 0xE000E000` 落在了 PPB 格，`rcc @ sysbus 0x40021000` 落在了外设格。Renode 的平台文件，就是一行行照着架构分区表写的，咱们这一路对得这么齐，靠的正是架构手册定下的分区。

这表再配一张条带图正合适：左边是架构画死的六格，右边是 ST 往格子里填的东西，咱们核对过的、待会儿还要打交道的几个地址，全都标在了上面：

![ARMv7-M 4GB 地址分区条带：从 0x00000000 的 Code 区到 0xFFFFFFFF 的 System 区六大块，右侧标注 ST 的填充（64K Flash、20K SRAM、RCC、GPIOC、NVIC）与 XN 属性](./01-armv7m-memory-map.drawio)

## CPU 对外的动作，只有读和写

地址有了，数据怎么拿、怎么放？这就轮到两个顶重要的指令了，笔者必须把它们请出来：管读的 load，还有管写的 store，第三种是不存在的。ARM 是真 · 教科书级的 load/store 架构：所有运算指令的操作数全是寄存器，mov、cmp、add、orr、bic、mul、udiv 这一串里的名字，谁都没有直接指到内存的本事。咱们想碰内存或外设，能用的只有 ldr/str 一族：字节版 ldrb/strb、栈版 push/pop、一次搬多个的 ldm/stm，全是这对搭档的变体。架构手册的 A4 章把指令集分门别类，A4.6 的名字就叫 Load and store instructions，所以<RefLink :id="1" preview="Arm Ltd., DDI 0403E.e, A4.6" />的目录里：运算一章、读写一章，本身就成了答案。

咱们把这个断言立在这儿。验证它的事得交给反汇编，那正是下一篇的主场：到时候您刚学会读指令，咱们顺手把整个固件 778 条指令做个普查，看碰内存的 278 条，是不是真的全在这一族里。您带着这个悬念往下走，咱们下一篇见分晓。

这就是"访问外设不需要特殊指令"的根源。外设在地址表里有地址，咱们拿 load/store 就能碰。在 CPU 和编译器的眼里，在咱们写的每一行代码里，`0x40011010` 和 `0x20000000` 就是一对没有本质区别的地址。C 的指针能直接指到外设，下一篇的 reinterpret_cast 为什么合法，volatile 为什么必要，根都在同一件事上：指令只认地址，不问它指的到底是内存还是外设。

## 一条 str 的全程：从取指到引脚

现在咱们把镜头对准一条指令。例程的循环体里就有这么一行 `GPIOC->BSRR = 1u << 13`，它在起步站 00 篇的代码里亮过相，所在的 01_register_led 例程，咱们下一篇整篇拆。1 左移 13 位的结果，编译期就定成了 0x2000，落到机器手里的就是一条 str。咱们跟着它从 PC 走到引脚，一站一站地过。取指：PC 停在了 Flash 里（代码区，I-Code 总线的地盘），把 str 的编码取回来。译码：这一级认出的是条写指令，把目标地址 `0x40011010` 算好了（GPIOC 基址 0x40011000 加 BSRR 偏移 0x10，这道算式咱们下一篇亲手验）。执行：这一级把地址和数据 0x2000 发上 System 总线。路由：总线矩阵收到了这次访问，按地址查表——`0x40011000` 段归 APB2 上的 GPIOC，转发了过去。矩阵里还备着一个单入口的写缓冲，总线忙的时候，它替 CPU 把这次写接了下来，也就不用干等了（TRM 14.11 节）。生效：GPIOC 里 BSRR 背后的电路收到信号，把 13 脚的输出顶起来，引脚电平变了，LED 就灭了。

这一路的五站，笔者做成了一部动画，您可以按着步进键一帧一帧过，每一站是谁在干活、手上拿着什么数，都写在卡片上了：

<Anim id="f103-str-journey" />

整条链路上发生了什么，咱们不靠想象，Renode 里全程都录了下来。把 GPIOC 的外设访问日志打开，跑一个完整的亮灭周期：

```text
sysbus LogPeripheralAccess sysbus.gpioPortC
（start 后 pause，再 emulation RunFor "1.1"，日志节选）

gpioPortC: [cpu: 0x8000164] ReadUInt32 from 0x4 (ConfigurationHigh), returned 0x0
gpioPortC: [cpu: 0x800016A] WriteUInt32 to 0x4 (ConfigurationHigh), value 0x0
gpioPortC: [cpu: 0x800016C] ReadUInt32 from 0x4 (ConfigurationHigh), returned 0x0
gpioPortC: [cpu: 0x8000172] WriteUInt32 to 0x4 (ConfigurationHigh), value 0x200000
gpioPortC: [cpu: 0x8000178] WriteUInt32 to 0x10 (BitSetReset), value 0x2000
gpioPortC: [cpu: 0x8000182] WriteUInt32 to 0x14 (BitReset), value 0x2000
gpioPortC: [cpu: 0x8000178] WriteUInt32 to 0x10 (BitSetReset), value 0x2000
```

每行都有三个要素：CPU 发起访问时的 PC，寄存器名（Renode 按手册的寄存器描述翻译的），读到的或写进的值。开头四行是 CRH 的读改写：读了 0x4、写回，再读、又写回了 0x200000。为什么是这个节奏、各落成几条指令，下一篇反汇编数给您看。后三行是循环体干的事：写 0x10（BSRR）灯灭、写 0x14（BRR）灯亮、再写 0x10，半秒一步地走，凑成了一个完整周期。日志里的 PC `0x8000178`、`0x8000182`，下一篇咱们会跟它们正式见面，地址一字不差。

> 当然，咱们看到的日志，是外设模型收到访问时打的记录。它转发的路径，就是架构和总线规定的路：模型收到 `0x40011010` 的写，对应实际芯片上 GPIO 模块收到的那次总线事务。

咱们再把日志挂到 RCC 上，还能看见另一个地址的去向：main 里开 GPIOC 时钟的那一行 `RCC->APB2ENR |= ...` 的那次读，日志记的是 `rcc: [cpu: 0x800015C] ReadUInt32 from 0x18`。在咱们这同一次执行流里，`0x40021000` 段的访问路由给 RCC，`0x40011000` 段的交给 GPIOC。按地址分发的逻辑，就是总线矩阵那"查表"的活。时钟的这套机制，咱们第三篇整篇讲。

## 地基的地基，打完了

今天咱们跟着一条 str，从取指一路走到了引脚，整条路由日志都逐行验过了，连日志里的 PC，都和下一篇的反汇编对得上。

攒下的这套家底，咱们下一篇马上用上：CMSIS 那些宏层层剥到最后，就是架构分区表上的地址。`|=` 和 `&=` 落成的指令，就是咱们说过的那套读改写：译码时算好地址、执行时碰内存。volatile 划的是编译器对"地址内容会自己变"的知情权。机器看清了，该轮到代码了。

## 欸欸！自查一下再走

- 您说说，ARM 和 ST 各自出了什么手册？GPIO 偏移表和 4GB 分区表，分别在哪一本？
- `0x20000000`、`0x40000000`、`0xE0000000` 各是什么？您顺带想一想，这是谁规定的：架构，还是 ST？
- 您要是想碰内存或外设，指令只有哪一族能干这活？运算指令的操作数放在哪？
- 一条 str 从 PC 到引脚经过哪几站？Renode 日志里，咱们凭什么说"路由真的发生了"？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Arm Ltd."
    title="ARMv7-M Architecture Reference Manual (DDI 0403E.e)"
    :year="2021"
    url="https://developer.arm.com/documentation/ddi0403/latest/"
    chapter="B3.1 The system address map (Table B3-1); A4.6 Load and store instructions"
  />
  <ReferenceItem
    :id="2"
    author="Arm Ltd."
    title="Arm Cortex-M3 Processor Technical Reference Manual (DDI 0337)"
    :year="2024"
    url="https://developer.arm.com/documentation/ddi0337"
    chapter="1.2.2 Processor core; 14.11 Write buffer"
  />
  <ReferenceItem
    :id="3"
    author="Wikipedia"
    title="ARM Cortex-M"
    :year="2026"
    url="https://en.wikipedia.org/wiki/ARM_Cortex-M"
    chapter="Licensing model; Cortex-M3 (2004); MCU vendor list"
  />
  <ReferenceItem
    :id="4"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="2 Memory map"
  />
  <ReferenceItem
    :id="5"
    author="Power, Liam & Robinson, Shane"
    title="The ARM Cortex-M3 and the convergence of the MCU market — Embedded.com"
    :year="2010"
    url="https://www.embedded.com/the-arm-cortex-m3-and-the-convergence-of-the-mcu-market/"
    chapter="Cortex-M3 launched 2004; Luminary Micro first MCU 2006; STM32 2007"
  />
</ReferenceCard>
