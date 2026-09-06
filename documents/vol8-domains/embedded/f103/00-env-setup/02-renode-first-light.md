---
title: "Renode 先行:不买板子,先点第一盏灯"
description: "一条命令在模拟器里跑通 HAL 闪灯固件,看懂 resc 脚本与板级描述,再用虚拟时间采样证明 LED 真的在闪——外加两个差点把笔者骗了的采样坑"
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

能,而且不是降级体验。咱们的主验证环境是 Renode,一个开源的系统级模拟器:它在您电脑上虚拟出一整块开发板,Cortex-M3 内核、GPIO、USART、定时器、中断控制器,样样有模有样。GPIO 是通用输入输出,芯片引脚的统称,咱们点的 PC13 就是 C 组第 13 脚;USART 就是串口,芯片上收发字节的通道,嵌入式里 printf 的输出将来走的就是它,细节留给 UART 站。编译出来的固件直接丢进去跑,行为和实际板子高度一致。Antmicro(Renode 的开发方)、Zephyr 项目、做 Rust 嵌入式的团队,CI 里跑的都是这一套。

这一篇咱们把第一个固件在 Renode 里跑起来,顺便把"怎么确认它真的在跑"这件事做扎实;采样路上的假象差点把笔者骗过去,那些教训实际板子调试时一样用得上。

## 一条命令,从构建到"点亮"

第一个上机的固件用外设库自带的示例 `01_blinky`:`HAL_Init`、配置时钟、翻转 PC13,主循环里一个 `HAL_Delay(500)`,Blue Pill 板载灯的标准点法。顺带把主角请出场:HAL,ST 官方的硬件抽象层库,把"照着手册配寄存器"包成 `HAL_Init`、`HAL_Delay`、`HAL_GPIO_TogglePin` 这类函数,咱们调函数,它在底下替咱们写寄存器;怎么把它请进工程,下一篇讲。示例住在咱们挂进来的外设库 `third_party/libestdx` 里,您先确认子模块已经拉下来(没做的话,回项目结构篇看补救命令),然后:

```bash
cd third_party/libestdx
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake
cmake --build build --target sim
```

第一条命令配置构建,第二条替咱们把"编译固件 + 在 Renode 里跑起来"一条龙做完。构建阶段尾部的真实输出:

```text
[13/13] Linking CXX executable examples/01_blinky/blinky; objcopy -> bin, report size
   text    data     bss     dec     hex filename
   7536      12       4    7552    1d80 .../build/examples/01_blinky/blinky
```

咱们看构建产物:`text 7536` 是代码体积,`data 12` 是带初值的全局变量,`bss 4` 是清零段,合计 7.5 KB 出头,64 KB Flash 的 C8T6 装它毫无压力。

接着 Renode 起来,停在 monitor 提示符上,每 700 毫秒滚一行 GPIOC_ODR 的值。笔者跑的时候,滚出来的是:

```text
0x00002000
0x00000000
0x00000000
0x00002000
0x00000000
0x00000000
0x00002000
```

`0x4001100C` 是 GPIOC 的 ODR 寄存器(Output Data Register,输出数据寄存器)地址,watch 每隔 700 毫秒替咱们读一次。芯片把每个外设的控制和状态,编进地址空间里一格格的"存储":写这些地址是在配置硬件,读是在看状态,这套路数叫内存映射(memory-mapped),咱们直接读 `0x4001100C` 用的正是它。`0x2000` 展开成二进制是 `0010 0000 0000 0000`,第 13 位在 0 和 1 之间跳——这正是 PC13 引脚在翻转,而 PC13 接的就是 Blue Pill 的板载 LED(阴极接法,写 0 点亮)。

细心的您可能注意到:这串值不是干净的 `0`、`2000` 交替,偶尔连着两行相同。这个"不干净"本身就是信息,咱们采样一节揭底。退出按 Ctrl+C,或在 monitor 里敲 `quit`。

## 看懂 resc:Renode 的启动脚本

刚才那条命令的"剧本",是 `examples/01_blinky/renode.resc`,您打开看,全文如下:

```text
# 01_blinky 的上机脚本。相对路径以仓库根解析(CMake target 的
# WORKING_DIRECTORY 钉在仓库根)。
# 启动:cmake --build build --target sim
# 固件重编后在 monitor 里敲 runMacro $reset 一条命令重载续跑。
# 注意:.resc 是 monitor 脚本,注释符是 #(不是 .repl 的 //)。

using sysbus

mach create
machine LoadPlatformDescription @sim/stm32f1/bluepill.repl

$bin?=@build/examples/01_blinky/blinky

macro reset
"""
    pause
    sysbus LoadELF $bin
    start
"""

# 诊断快照(活性对拍):GPIOC_ODR + HAL uwTick(虚拟 ms),固定顺序。
# watch 里 ODR 不动时用它分辨:uwTick 涨 = 固件活着只是没翻转,
# uwTick 停 = 真死了。
macro status
"""
    pause
    sysbus ReadDoubleWord 0x4001100C
    sysbus ReadDoubleWord 0x2000000C
    start
"""

runMacro $reset

# LED 直播:monitor 空闲(停在提示符)时每 700ms 打一行 GPIOC_ODR,
# 0x00002000=灭 / 0x00000000=亮,滚动翻转 = 灯在闪。
# watch 靠 monitor 空闲驱动,连轴命令(如脚本里的 sleep)期间不触发。
watch "sysbus ReadDoubleWord 0x4001100C" 700
```

咱们逐段对号。`mach create` 创建一台虚拟机;`machine LoadPlatformDescription` 加载板级描述文件,告诉 Renode 这台"板子"上有哪些器件;`sysbus LoadELF $bin` 把固件加载进虚拟 Flash。加载被包在 `$reset` 宏里,前后各垫了 `pause`/`start`——所以固件重编之后,monitor 里敲一句 `runMacro $reset` 就能重载续跑,不必退出重来。

`@` 开头的路径有个讲究:它按 Renode 进程的当前目录解析,不按 resc 文件所在目录。所以 CMake 的 sim 目标把 `WORKING_DIRECTORY` 固定在 libestdx 仓库根,`@sim/...` 和 `@build/...` 才都对得上;您要是从别的目录自己起 Renode 去 include 这份脚本,路径就全断了。(Renode 另有 `$ORIGIN` 写法可以锚定脚本自身所在目录,但它必须出现在变量值的最开头,写成 `../../xxx` 之类解析器直接报 `Could not tokenize`。两种写法各管一头,这份脚本选的是固定 cwd 那种。)

`$bin?=` 是条件赋值,`?` 表示"如果还没定义就赋值"。`$status` 宏是诊断快照:咱们连读 GPIOC_ODR 和 HAL 的毫秒计数器 `uwTick`,固定顺序。它管什么用,采样一节见。

最底下一行 `watch` 让 monitor 在空闲时每 700 毫秒替咱们执行一次读寄存器,就是前面那串值的来源。注意"空闲"两个字:watch 靠 monitor 空闲驱动,连轴命令(比如脚本里一路 sleep)期间它一声不吭。

## blue_pill.repl:给自己的板子写"户口本"

`@sim/stm32f1/bluepill.repl` 值得咱们单独讲,因为 Renode 的发行版里没有现成的 Blue Pill 板级文件。官方提供的是芯片级的 `platforms/cpus/stm32f103.repl`,GPIO、USART、定时器、中断控制器都在,但有两件事官方不管。一件是"Blue Pill 这块板上 LED 接在哪个脚"这种板级信息;另一件更狠:官方描述里的 RCC(时钟控制器)没有行为模型,只有 tag 预置的就绪位,固件想把时钟从 HSI(芯片内部自带的振荡器,上电默认时钟)切到 PLL(把频率倍频上去的电路),`HAL_RCC_ClockConfig` 读回的 CFGR 永远是 0,等来的只有 `HAL_TIMEOUT`。

咱们库里这份 repl 是自己维护的:从官方芯片级描述裁起,按 C8T6 的真实配置(64K flash / 20K SRAM / GPIO A-C / TIM1-4 / USART1-3)收窄,再把官方欠的两件事补上。两百多行,骨架截选如下:

```text
// flash 同时映射在真地址 0x08000000 和启动别名 0x0(复位时从 0x0 向量表取 SP/PC)
flash: Memory.MappedMemory @ {
    sysbus 0x08000000;
    sysbus 0x00000000
}
    size: 0x10000

sram: Memory.MappedMemory @ sysbus 0x20000000
    size: 0x5000

nvic: IRQControllers.NVIC @ sysbus 0xE000E000
    priorityMask: 0xF0
    // SysTick 参考频率对齐固件 HCLK(64M);构造参数不可运行时改,固件换时钟时同步改
    systickFrequency: 64000000
    IRQ -> cpu@0

cpu: CPU.CortexM @ sysbus
    cpuType: "cortex-m3"
    nvic: nvic

// 板载 LED:PC13,低电平点亮
led: Miscellaneous.LED @ gpioPortC 13

// RCC —— 官方无 F1 RCC 真模型,这份用 PythonPeripheral 补 RM0008 的握手协议
// (就绪位跟随使能位、SWS 镜像 SW、其余 read-as-written;脚本体见原文件)
rcc: Python.PythonPeripheral @ sysbus 0x40021000
    size: 0x100

// 外设位带别名区:HAL 开 PLL 走位带写,别名区不映射会静默丢失
bitBand: Miscellaneous.BitBanding @ sysbus <0x42000000, +0x2000000>
    peripheralBase: 0x40000000
```

三处增量值得咱们点名。flash 同时映射在 `0x08000000` 和 `0x0`:复位时 CPU 从地址 0 取初始栈指针和复位向量,没有这个别名,启动文件(链接进每个固件的一小段汇编,复位后先装栈指针、再跳进 `main`,长什么样下一篇讲)第一步就找不到门。`systickFrequency: 64000000` 把 SysTick(Cortex-M 内核自带的小定时器,每毫秒发一次中断当系统心跳,`HAL_Delay` 计时靠的就是它)的参考频率,对齐固件配好的 64 MHz HCLK(喂给 CPU 的那条主时钟线);它是构造期参数,固件改时钟树时这里得跟着改。RCC 那段 Python 脚本是核心增量,合成规则就三条:就绪位跟随使能位(`HSIRDY=HSION` 这类)、`SWS` 镜像 `SW`(时钟切换零延迟回执)、其余寄存器写什么读什么。三条把握手协议演全,"PLL 锁定到 64MHz"在虚拟世界里就真的发生了。位带别名区也映射了——Cortex-M3 给外设寄存器的每一位都发了一个别名地址,写那个地址等于只动这一位,HAL 开 PLL 改 RCC 的开关位用的就是这种写法;BitBanding 外设负责把别名写翻译回真实地址,这些写不会再静默丢失。

您翻原文件会发现,每一行注释都在讲为什么,这份 repl 本身就是教程的一部分。以后按键站加按键、UART 站看波特率,都在它上面长。

## 怎么知道它真的在闪

跑到这一步,可能有人会问:看几行十六进制就说灯在闪,是不是自欺欺人?问得好——判据选错的话,笔者真能被"灯没闪"的假象骗过去。咱们把判据一条条立起来。

固件的节拍咱们先说清楚。`main.cpp` 主循环是 `TogglePin` 加 `HAL_Delay(500)`,名义半周期 500 毫秒、周期 1 秒,这是纸面值,模拟器里实际是不是这个数,咱们得测。测要用虚拟时间:`pause` 之后用 `RunFor` 推进固定的虚拟时长,再读寄存器,一拍不掺水分。间隔取 250 毫秒(小于半周期),连读六次:

```text
0x00002000 0x00000000 0x00000000 0x00002000 0x00002000 0x00000000
```

相邻两个采样时常落在同一电平里,所以成对出现;但整条流里两个值都在滚,灯确实在翻。这反过来提醒咱们:只看相邻两个采样"没变化",什么也证明不了。采样窗塞不进一次跳变时,"没变化"是必然输出,和固件死没死无关。

纸面半周期靠不靠得住,咱们再拿 HAL 的毫秒计数器验一遍。`uwTick` 由 SysTick 中断每毫秒加一,让机器先跑满 1.1 秒虚拟时间再读它:

```text
sysbus ReadDoubleWord 0x2000000C → 0x00000459
```

`0x459` 是 1113,和 1100 对上了(多出的十几毫秒,是脚本里 `pause` 之前机器已经先跑了一小段)。虚拟毫秒和虚拟时间对齐,"周期 1 秒"的纸面值这才落了地。这一条笔者要专门记一笔:上一版平台描述里 RCC 还是哑巴,时钟切不过去,毫秒计数和虚拟时间能差出九倍,当时的采样数字全带着这份失真;模型修好,这个验证才真正通过。这也是 `$status` 宏存在的意义:watch 里 ODR 不动时跑一下它,`uwTick` 涨着就是"固件活着,只是没翻转",停了才是真死。

最后是那个更隐蔽的假象,笔者在写这一篇的实验里现踩的。把采样间隔改成整整 1 秒,正好一个周期,咱们连采五次:

```text
0x00002000 0x00002000 0x00002000 0x00002000 0x00002000
```

常值!可咱们心里清楚,这五秒里 LED 一刻没停地翻。每次采样都落在周期的同一个相位上,就像两支速度完全相同的表,秒针永远指着同一个位置。信号在翻转,采样点纹丝不动,这个现象叫**混叠**(aliasing),数字信号处理课的老朋友。

一个塞不进跳变,一个冻结在同一个相位,病根都是采样间隔没选对,这两个坑咱们都亲眼见过了。要遵守的就一条:**采样间隔既要大于半周期,又要避开周期的整数倍**。resc 里 watch 用的 700 毫秒就是这么挑的:大于 500,又离 1000 的整数倍远。开头那串"偶尔连着两行相同"也有了下文:watch 按 monitor 的真实时间走,固件按虚拟时间跑,两个钟不严格同步,采样点在周期里慢慢滑,连同行就是滑过半周期边界的瞬间;值还在滚,灯就在闪。

::: warning 实际板子上一样会中招
这两个坑不是模拟器特产。逻辑分析仪采样率不够、示波器时基设错,咱们看到的都是"假静止"。在模拟器里学会怀疑自己的观测手段,是便宜的学费。
:::

## GUI 给人看,无头给机器跑

sim 目标跑的是无头模式(`--console --disable-xwt`),没有窗口,输出全在终端里,适合脚本和后续的 CI 自动化。如果您想"亲眼看见",从 libestdx 仓库根目录起 GUI 模式:

```bash
renode examples/01_blinky/renode.resc
```

WSL2 下 WSLg 直接把窗口送到 Windows 桌面。Monitor 窗口里您能看到机器树和外设状态;而更符合嵌入式日常的体验是串口窗口(`showAnalyzer`),从 UART 站开始,固件的 printf 会像真实终端一样滚动输出,那才是"板子活了"的感觉,到时候再细讲。

## 模拟器的边界

这一版跑下来,WARNING 一条都没有:repl 把最容易报警的三处(RCC、Flash 配置寄存器 ACR、位带别名区)都建了模型。但模拟器的边界仍在,咱们心里得有数。头一条:Renode 是**功能级仿真器,不是周期精确仿真器**,它保证"程序逻辑走对了",不保证"每条指令走了几个周期、频率精确到多少兆"。所以咱们这套教程的验证纪律不变,功能看模拟器,时序看实际板子。LED 闪不闪、串口说什么、按键响不响应,Renode 说了算;PWM 占空比(PWM 靠极快地通断引脚,拿"通"的时间占比拼出调光这类中间效果,占空比就是这个占比)准不准、波特率(串口约定的传输速度)误差多大,留给实际板子。第二条记在 repl 注释里:DMA(直接内存访问)让外设和内存不经 CPU 直接互传数据,"请求脚"是外设发起传输的那根信号线,Renode 1.16.1 的 UART、Timer 模型不暴露这根脚,外设触发的 DMA 传输暂时跑不了,要等 Renode 升级才能补,将来 DMA 相关的站,这个口子得记着。第三条刚才见过:`systickFrequency` 是构造参数,固件时钟树一改,它得同步改,不然毫秒计数又要失真。

下一篇咱们把镜头转回工程本身,看 HAL 库怎么获取、目录怎么搭——那边的 submodule 陷阱和启动文件命名玄学,坑一点不比这边少。
