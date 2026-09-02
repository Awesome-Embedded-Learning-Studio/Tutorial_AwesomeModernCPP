---
title: "Renode 先行:不买板子,先点第一盏灯"
description: "一条命令在模拟器里跑通完整 HAL 工程,看懂 resc 脚本与板级描述,再用寄存器采样证明 LED 真的在闪——外加两个差点把笔者骗了的采样坑"
chapter: 0
order: 2
tags:
  - stm32f1
  - beginner
  - 入门
  - 嵌入式
  - renode
difficulty: beginner
platform: stm32f1
reading_time_minutes: 15
prerequisites:
  - "从零搭建 STM32 开发工具链"
related:
  - "项目结构:HAL 库的获取与目录搭建"
  - "CMake 配置:从零构建 STM32 构建系统"
---

# Renode 先行:不买板子,先点第一盏灯

工具装齐了,咱们现在回答一个现实问题:手头没有 Blue Pill,甚至压根不打算买,这套教程还能不能跟?

能,而且不是降级体验。咱们的主验证环境是 Renode,一个开源的系统级模拟器:它在您电脑上虚拟出一整块开发板,Cortex-M3 内核、GPIO、USART、定时器、中断控制器,样样有模有样。编译出来的固件直接丢进去跑,行为和实际板子高度一致。Antmicro(Renode 的开发方)、Zephyr 项目、做 Rust 嵌入式的团队,CI 里跑的都是这一套。

这一篇咱们就把第一个工程在 Renode 里跑起来,顺便把"怎么确认它真的在跑"这件事做扎实;采样路上的假象差点把笔者骗过去,那些教训实际板子调试时一样用得上。

## 一条命令,从构建到"点亮"

教程配套工程在仓库 `code/stm32f1-tutorials/0_start_our_tutorial/`,您先确保 `third_party/STM32F1` 子模块已经拉下来(上一篇环境里如果没做,回项目结构篇看补救命令),然后:

```bash
cd code/stm32f1-tutorials/0_start_our_tutorial
cmake -B build
cmake --build build --target run_in_renode
```

第一条命令配置构建,第二条直接帮咱们把"编译固件 + 在 Renode 里跑起来 + 采样验证"一条龙做完。真实输出长这样:

```text
[100%] Built target stm32_demo.elf
   text     data      bss      dec      hex  filename
   3016       12        4     3032      bd8  build/stm32_demo.elf
...
echo "PC13 翻转证明(GPIOC_ODR @0x4001100C)"
0x00000000
0x00000000
0x00002000
0x00000000
```

咱们先看构建产物:`text 3016` 是代码体积,`data 12` 是带初值的全局变量,`bss 4` 是清零段。加起来 3 KB 出头,64 KB Flash 的 C8T6 装它毫无压力。

再看下面四行十六进制数。`0x4001100C` 是 GPIOC 的 ODR 寄存器(Output Data Register,输出数据寄存器)地址,咱们每隔 70 毫秒的虚拟时间读一次它的值:`0x00000000` 和 `0x00002000` 交替出现。`0x2000` 展开成二进制是 `0010 0000 0000 0000`,第 13 位在 0 和 1 之间跳——这正是 PC13 引脚在翻转,而 PC13 接的就是 Blue Pill 的板载 LED。

一条命令,LED 在"闪"。但这背后发生了什么,值得咱们拆开看。

## 看懂 resc:Renode 的启动脚本

工程根目录下的 `renode.resc` 就是刚才那条命令的"剧本",您打开看,一共十几行:

```text
using sysbus
mach create
$plf?=$ORIGIN/../../stm32-tutorials/f103/0_blink/platform/blue_pill.repl
machine LoadPlatformDescription $plf

$bin?=$ORIGIN/build/stm32_demo.elf

macro reset
"""
    sysbus LoadELF $bin
"""
runMacro $reset

emulation RunFor "00:00:00.500000"

echo "PC13 翻转证明(GPIOC_ODR @0x4001100C)"
sysbus ReadDoubleWord 0x4001100C
emulation RunFor "00:00:00.070000"
sysbus ReadDoubleWord 0x4001100C
...
```

逐段对号。`mach create` 创建一台虚拟机;`machine LoadPlatformDescription` 加载板级描述文件,告诉 Renode 这台"板子"上有哪些器件;`sysbus LoadELF` 把固件加载进虚拟 Flash;`emulation RunFor "00:00:00.500000"` 让虚拟机跑 0.5 秒的**虚拟时间**——注意是虚拟时间,不是您手表上的时间,这个区别后面有坑要讲;`sysbus ReadDoubleWord 0x4001100C` 读一个 32 位寄存器,就是刚才那些十六进制数的来源。

`$plf?=` 和 `$bin?=` 是变量赋值,`?` 表示"如果还没定义就赋值"。这里有个词法坑:`$ORIGIN`(resc 文件所在目录)必须出现在变量值的最开头,写成 `../../xxx` 或绝对路径 `/home/xxx`,Renode 的命令解析器会直接报 `Could not tokenize`。笔者在这一步来回试了三种写法才踩明白。

## blue_pill.repl:给自己的板子写"户口本"

`$plf` 指向的 `blue_pill.repl` 值得单独讲,因为 Renode 的发行版里**没有**现成的 Blue Pill 板级文件。官方提供了芯片级的 `platforms/cpus/stm32f103.repl`(GPIO、USART、定时器、中断控制器都在),但"Blue Pill 这块板子上 LED 接在哪个脚"这种板级信息,官方不管。咱们的做法是自己维护一份,全文如下:

```text
// Blue Pill(STM32F103C8T6)板级描述。
// Renode 发行版只有芯片级的 platforms/cpus/stm32f103.repl,没有现成的 Blue Pill 板级文件;
// 这份是本教程自己维护的板级描述:在芯片级之上挂板载器件。
using "platforms/cpus/stm32f103.repl"

// 板载 LED:PC13。Blue Pill 的 LED 阴极接 PC13,写 0 点亮、写 1 熄灭
// (和 F4 Discovery 板的高电平点亮正好相反)。
UserLED: Miscellaneous.LED @ gpioPortC

gpioPortC:
    13 -> UserLED@0
```

`using` 一行把官方芯片级描述整个继承过来;然后咱们声明一个 LED 外设,把它挂到 GPIOC 的第 13 号引脚上。十几行,这就是一块虚拟 Blue Pill 的全部"户口"。以后按键站要加按键,也在这份文件里补一个 Button 外设,板级描述本身就是教程的一部分。

## 怎么知道它真的在闪

跑到这一步,可能有人会问:看几行十六进制就说灯在闪,是不是自欺欺人?问得好,因为笔者的第一版采样脚本,真的被"灯没闪"的假象骗过。

### 采样窗必须塞得进一次跳变

判断一个周期信号在不在翻转,直觉做法是读两次寄存器、看值变没变。但两次读取之间隔多久,咱们得讲究:间隔必须大于信号半周期,否则整个采样窗都落在同一电平里,值当然不变。

这坑笔者踩过,还是在更早的裸寄存器闪灯实验里:那边的翻转半周期约 300 毫秒(虚拟时间),第一版脚本用 50 毫秒间隔连采四次,四次全是 `0x00000000`,当时以为固件卡死了,差点回头查代码;间隔放大到 500 毫秒,`0x00000000` 和 `0x00002000` 立刻交替出现。采样窗小于半周期,"没变化"什么也证明不了。这份配套工程的半周期短得多,约 55 毫秒——为什么不是名义上的 500 毫秒,下一小节揭底;`renode.resc` 里 70 毫秒的间隔,就是按"大于半周期"挑的。

### 一秒一采,采出一个常值

更隐蔽的假象,是笔者在写这一篇的实验里现踩的。把采样间隔改成整整 1 秒,连采六次,ODR 六次全是 `0x00002000`,常值!固件确实卡死了吗?

没有。笔者连着读了几次 CPU 的 PC 寄存器,发现它正在主循环里正常往复;再看 HAL 的毫秒计数器 `uwTick`,一秒涨 9001——活得好好的。实际情况是,这个固件在模拟器里的翻转周期约 0.111 秒,而 1 秒恰好约等于 9 个完整周期。每秒采一次,每次都落在周期的同一个相位上,就像两支速度成整数倍的手表,秒针永远指着同一个位置。信号在高速翻转,采样点却纹丝不动,这个现象叫**混叠**(aliasing),数字信号处理课的老朋友。

一个塞不进跳变,一个每次都落在同一个相位,病根都是采样间隔没选对。要遵守的就一条:**采样间隔既要大于半周期,又要避开周期的整数倍**。咱们工程里 `renode.resc` 用的 70 毫秒,就是这么挑的。以后按键站做消抖时序验证,同样的挑选还得再做一次。

::: warning 实际板子上一样会中招
这两个坑不是模拟器特产。逻辑分析仪采样率不够、示波器时基设错,咱们看到的都是"假静止"。在模拟器里学会怀疑自己的观测手段,是便宜的学费。
:::

## GUI 给人看,无头给机器跑

`renode.resc` 是无头模式(`--console --disable-xwt`),没有窗口,输出全在终端里,适合脚本和后续的 CI 自动化。如果您想"亲眼看见",同一份固件可以开 GUI 模式跑:

```bash
renode renode.resc
```

WSL2 下 WSLg 直接把窗口送到 Windows 桌面。Monitor 窗口里您能看到机器树和外设状态;而更符合嵌入式日常的体验是串口窗口(`showAnalyzer`),从 UART 站开始,固件的 printf 会像真实终端一样滚动输出,那才是"板子活了"的感觉,到时候再细讲。

## 模拟器的边界:那些 WARNING 在说什么

您第一次跑时可能会被满屏 WARNING 吓到,比如:

```text
[WARNING] sysbus: [cpu: 0x800027C] ReadDoubleWord from an unimplemented register
FLASH:ACR (0x40022000), returning a value from SVD: 0x30
[WARNING] sysbus: [cpu: 0x800027C] (tag: 'RCC_CR') ReadDoubleWord from non
existing peripheral at 0x40021000, returning 0x0A020083.
```

这不是您的固件出错了。Renode 对 STM32F1 的 RCC(时钟控制器)和 Flash 配置寄存器**没有实现行为模型**,固件读写这些地址时,Renode 从芯片描述文件(SVD)里找一个"哑寄存器"顶着:写进去的值存着,读出来原样奉还,但不会有任何真实副作用。HAL 配置时钟树的代码写了一堆 RCC 寄存器,在模拟器里等于对着空气比划——功能不受影响(GPIO、SysTick、USART 这些真正干活的外设都有真模型),但"PLL 锁定到 64MHz"这件事在虚拟世界里并没有发生。

这背后是 Renode 的自我定位:**功能级仿真器,不是周期精确仿真器**。它保证"程序逻辑走对了",不保证"每条指令走了几个周期、频率精确到多少兆"。所以咱们给这套教程立的验证纪律就一句话:功能看模拟器,时序看实际板子。LED 闪不闪、串口说什么、按键响不响应,Renode 说了算;PWM 占空比准不准、波特率误差多大,留给实际板子。

::: details 另一种 WARNING:位带写丢了也不要慌
WARNING 里偶尔混着 `WriteDoubleWord to non existing peripheral at 0x42420060` 这种更吓人的。`0x42420060` 是 F1 位带区(bit-band)的别名地址。HAL 通过"写一个地址的某一位"的技巧操作 PLL 开关位,而官方芯片级描述没挂位带外设,这次写同样落空。无功能影响,您读得懂 WARNING 就不会慌。
:::

## 练习

练习两道,咱们手头有 `renode.resc` 就能做:

1. 把 `main.cpp` 里的 `HAL_Delay(500)` 改成 `HAL_Delay(1000)`,重跑 `run_in_renode`,先预测采样输出会变成什么样,再验证您的预测。提示:翻转周期变了,70 毫秒的采样间隔还合适吗?
2. 试着把采样间隔改成一个"坏值"(比如恰好等于翻转周期),复现一次混叠假象,亲眼看一次"灯假死"。

下一篇咱们回到工程本身,看 HAL 库怎么获取、目录怎么搭——那边的 submodule 陷阱和启动文件命名玄学,坑一点不比这边少。
