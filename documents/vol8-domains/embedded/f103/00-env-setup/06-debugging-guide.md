---
title: "调试:从 printf 到 GDB,模拟器与真板两条路"
description: "先在 Renode 里用 GDB 打断点、看调用栈——不需要任何硬件;真板在手的朋友,后半篇是 OpenOCD 加 VSCode 的完整方案"
chapter: 0
order: 6
tags:
  - stm32f1
  - beginner
  - 入门
  - 工具链
  - renode
difficulty: beginner
platform: stm32f1
reading_time_minutes: 8
prerequisites:
  - "Renode 先行:不买板子,先点第一盏灯"
related:
  - "WSL2 USB 透传(真板选修)"
  - "嵌入式 clangd:让 vscode 看懂交叉编译的代码"
---

# 调试:从 printf 到 GDB,模拟器与真板两条路

回想一下调试普通 C++ 程序:打个断点按 F5,程序停住,悬停看变量,单步几下一目了然。切到 STM32,世界变了:代码跑在一块独立的芯片上(或一个模拟器进程里),您不能"运行"它,只能把编译好的固件喂进去。想看某个变量?加一句 printf,重新编译、烧录、盯串口,一轮下来几分钟没了。

更糟的是 printf 调试本身有硬伤。它占串口资源;它占代码空间和执行时间,时序敏感的代码可能因为加了一行打印就不工作了;最气人的是有些 bug 只在特定时序下出现,printf 一加,时序变了,bug 消失——海森堡 bug,观测行为改变了被观测系统。笔者早期靠这原始方法过日子,一个中断优先级 bug 加了十几条打印、烧了二十几次才定位;后来学会用断点,一眼看调用栈的事。

这一篇把完整的 GDB 调试环境搭起来。先走模拟器路线,零硬件要求,所有人都能跟着练;真板的 OpenOCD 方案放在后面,和上一篇的 USB 透传衔接。

## 为什么需要 GDB Server 这一层

先理解架构。调试普通程序时,GDB 和被调试程序在同一台机器上,靠操作系统提供的调试接口(ptrace)通信。STM32 的程序跑在独立芯片或模拟器里,GDB 够不着,需要一个"中间人"把 GDB 的调试命令翻译成目标系统能懂的操作。

真板路线上,这个中间人由两件东西组成:硬件的调试探针(ST-Link,通过 SWD 协议用两根线连芯片)加软件的 OpenOCD(驱动探针,监听 TCP 端口等 GDB 连接)。

模拟器路线上简单得多:Renode 内置 GDB Server,一条命令开启,不需要任何硬件。完整链条是 GDB(client)连 Renode(内置 server),server 直接操作虚拟 CPU——链条短,环节少,排错也简单。

## 模拟器路线:Renode + GDB 实战

开场前把上一节的架构落成命令。给 Renode 开 GDB 服务的是 `machine` 对象上的方法(注意不是 `sysbus.cpu` 上的,笔者第一次就敲错了地方):

```text
machine StartGdbServer 3333
```

配套工程的做法是准备一个调试用的 resc(`renode_gdb.resc`),加载平台和固件后开 GDB 服务,然后停在那等连接:

```text
using sysbus
mach create
$plf?=$ORIGIN/../../stm32-tutorials/f103/0_blink/platform/blue_pill.repl
machine LoadPlatformDescription $plf
$bin?=$ORIGIN/build/stm32_demo.elf
sysbus LoadELF $bin
machine StartGdbServer 3333
```

跑起来(注意 `--console` 模式下 Renode 会跟着终端 stdin 一起退出,保持终端开着即可):

```bash
renode --console --disable-xwt renode_gdb.resc
```

Renode 那头确认出现这行日志,GDB 服务就绪:

```text
machine-0: GDB server with all CPUs started on port :3333
```

另开一个终端,启动交叉 GDB 并连接。这里加载的是 **ELF** 而不是 bin——ELF 带着符号表和行号信息,GDB 靠它把机器码对回您的源代码:

```bash
arm-none-eabi-gdb build/stm32_demo.elf
```

连上之后,有一个必须记住的顺序问题:**先 `monitor start` 再 `continue`**。`monitor` 前缀把命令转发给 Renode 的 Monitor,`start` 启动仿真;直接 continue 而 CPU 还没开跑,GDB 会锁死在等待里。

```text
(gdb) target remote localhost:3333
(gdb) monitor start
(gdb) break HAL_Delay
(gdb) continue
```

笔者实测的完整输出:

```text
0x080001d8 in Reset_Handler ()
Starting emulation...
Breakpoint 1 at 0x80002c4

Breakpoint 1, 0x080002c4 in HAL_Delay ()
#0  0x080002c4 in HAL_Delay ()
#1  0x0800016c in main ()
```

断点命中,`HAL_Delay` 的入口是 `0x80002c4`,调用栈清清楚楚:`main()` 调进来的。单步(`step` 进入函数,`next` 跳过)、看变量(`print`)、看寄存器(`info registers`)、看内存(`x/wx 0x4001100C` 直接读 GPIOC 的 ODR)全套可用。上一篇"混叠疑云"里笔者判断"程序在正常循环",靠的就是连着读 PC 和 `uwTick`——GDB 是把观测做扎实的终极手段。

::: tip 模拟器调试的独门优势
Renode 里您可以随时暂停整个世界、检查任意外设寄存器、甚至手动改寄存器值再继续——真板上这些操作要经过探针,慢且有副作用。练熟 GDB 这套动作,模拟器是零成本的靶场。
:::

## 真板路线:OpenOCD + GDB

真板的流程结构一样,只是 GDB Server 换成 OpenOCD。一个终端起 server:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
```

正常输出会列出监听端口:6666(tcl)、4444(telnet)、3333(gdb)。另一个终端的 GDB 操作和模拟器路线完全一致,只有一处不同——`load` 命令把固件写进真 Flash(先 `monitor halt` 停机再写):

```text
(gdb) target remote localhost:3333
(gdb) monitor halt
(gdb) load
(gdb) break main
(gdb) continue
```

`monitor` 转发的对象换成了 OpenOCD(halt/resume/reset 这些命令是它的)。

## 搬进 VSCode

命令行玩明白之后,图形界面的价值是日常效率。VSCode 装 Cortex-Debug 插件,配套工程的 `.vscode/launch.json` 长这样(真板版):

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "STM32 Debug",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "cwd": "${workspaceRoot}",
            "executable": "build/stm32_demo.elf",
            "configFiles": ["interface/stlink.cfg", "target/stm32f1x.cfg"],
            "searchDir": ["/usr/share/openocd/scripts"],
            "runToEntryPoint": "main",
            "device": "STM32F103C8T6",
            "interface": "swd"
        }
    ]
}
```

字段速览:`servertype` 选 openocd(Cortex-Debug 会自动把它拉起来);`executable` 指向 ELF(不是 bin,调试要符号);`configFiles` 就是手动命令里那两个配置;`runToEntryPoint: main` 让每次调试自动停在 main 入口,想从 `Reset_Handler` 开始看启动过程,把这行删掉即可。

模拟器路线想用 VSCode 也行:自己起 Renode(开好 3333),launch.json 里用 `request: attach` 型配置连 localhost:3333。配置细节不展开,命令行那套通了,这只是一层皮。

## 硬件断点与软件断点

Cortex-M3 有 6 个硬件断点(比较器实现,可断 Flash 里任何地址)和理论上无限的软件断点(改写指令为 BKPT,只能用于 RAM 代码)。设第 7 个 Flash 断点时 GDB 会报错或静默失效——`info breakpoints` 里 `hw breakpoint` 字样说明走的是硬件通道。日常习惯:断点用完就删,别攒。

## 优化与调试的矛盾

变量显示 `<optimized out>`、断点位置飘,都是 `-O2` 的锅。CMake 里给 Debug 配置单独设优化:

```cmake
add_compile_options(
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Release>:-O2>
)
```

`-Og` 是"对调试友好"的优化档,保留大部分优化又不至于把变量全优化飞。

## 排错速查

`target not halted`(真板):load 前忘了 `monitor halt`。

断点不命中:超了 6 个硬件断点;或代码没加载到那个地址;或被优化器删了(`-O0` 重试)。

变量 `<optimized out>`:换 Debug + `-Og`。

连不上 3333:模拟器路线查 Renode 活着没、`machine StartGdbServer` 执行了没(日志里找 `GDB server ... started`);真板路线查 OpenOCD 进程和端口占用(`netstat -tlnp | grep 3333`)。

continue 后 GDB 卡死:模拟器路线九成是没先 `monitor start`——前面反复强调的那条顺序。

调试环境到这算齐了。环境篇最后一站是 clangd:让编辑器看懂这套交叉编译的代码,跳转补全一条龙,告别"满屏红波浪线"。
