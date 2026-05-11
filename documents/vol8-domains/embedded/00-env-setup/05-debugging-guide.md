---
title: 第5篇：调试进阶篇 —— 从 printf 到完整 GDB 调试环境
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 14
order: 5
---
# 第5篇：调试进阶篇 —— 从 printf 到完整 GDB 调试环境

> 写给所有还在用 printf 调试 STM32 程序、想知道"为什么不能像普通程序那样单步调试"的朋友。
> 本篇记录我们从零搭建完整调试环境的全过程，包括 GDB Server 原理、命令行调试实战、VSCode 图形化配置，以及那些让你抓狂的调试问题如何排查。

---

## 为什么我一定要写调试这一篇

回想一下，当你写了一个普通的 C++ 程序，想知道某变量为什么值不对的时候，你会怎么做？直接在 IDE 里打个断点，按 F5 运行，程序停在那里，鼠标悬停在变量上就能看到值，单步几步就能定位问题。这套流程你已经用了成千上万次，根本不需要过脑子。

但当你切换到 STM32 开发时，世界突然变了。代码不在你的电脑上跑，而是在那块几块钱的板子上，你不能直接"运行"它，只能把编译好的二进制文件烧进 Flash。程序跑起来之后，你唯一能看到的反馈就是那几个 LED 的闪烁状态，或者如果你幸运的话，串口打印出来的一些字符。这时候你如果想知道某个变量的值，只能加一句 printf，重新编译、烧录、观察结果，这流程慢得让人抓狂。

更糟糕的是，printf 调试在嵌入式环境下有严重的局限性。首先它需要串口资源，如果你所有的 UART 都已经用作通信了怎么办？其次 printf 会占用代码空间和时间，时序敏感的代码可能因为加了 printf 就不工作了。最要命的是，有些 bug 只在特定条件下出现，你加了 printf 之后时序变了，bug 就消失了，这就是典型的"海森堡bug"。

我在早期折腾 STM32 的时候，就是靠这种原始方法过来的。每次改一点代码，重新烧录，盯着串口输出看半天。有次一个中断服务程序里的 bug，我加了十几条打印语句，烧了二十几次，最后发现是因为中断优先级设置错误。如果有完整的调试环境，我只需要在 ISR 里打个断点，看一眼调用栈就能定位问题。

所以这一篇，我要带你搭建一套完整的调试环境，让你能够像调试普通程序一样调试 STM32：打断点、单步执行、查看变量、监视寄存器、甚至直接修改内存里的值。这套环境一旦跑通，你的开发效率会提升一个数量级。

---

## 先搞清楚：为什么不能直接调试

在开始动手之前，我们得先理解一个核心问题：为什么 STM32 程序不能像普通程序那样直接调试？

当你调试一个普通的 x86 程序时，GDB 和被调试程序运行在同一台机器上，它们通过操作系统提供的调试接口（ptrace）通信。操作系统知道进程的所有信息：内存布局、寄存器状态、调用栈，GDB 只需要向操作系统请求这些信息就行。

但 STM32 的情况完全不同。你的程序运行在一块独立的芯片上，它的 CPU、内存、外设都和你开发机器物理隔离。GDB 无法直接访问这些资源，需要一个"中间人"来帮忙。这个中间人就是调试探针（debug probe），比如 ST-Link V2。

调试探针通过 SWD（Serial Wire Debug）协议和 STM32 通信。SWD 是 ARM 专门为调试设计的一种协议，只需要两根线（SWDIO 和 SWCLK）就能实现完整的调试功能：读写内存、设置断点、单步执行、查看寄存器。ST-Link 内部有一颗专门的芯片，它一边通过 USB 和你的电脑通信，另一边通过 SWD 和 STM32 通信，扮演着"翻译官"的角色。

但事情还没完。ST-Link 只是硬件层面的桥梁，我们还需要软件来驱动它，并且把 GDB 的调试命令"翻译"成 SWD 协议。这个软件就是 OpenOCD（Open On-Chip Debugger）。OpenOCD 可以以两种模式运行：一种是直接命令模式，用来烧录固件；另一种是 GDB Server 模式，监听一个 TCP 端口，等待 GDB 连接。

当你启动 OpenOCD 的 GDB Server 后，完整的调试链条是这样的：GDB（client）通过 TCP 连接到 OpenOCD（server），OpenOCD 通过 USB 和 ST-Link 通信，ST-Link 通过 SWD 和 STM32 通信。这个链条上的每一环都必不可少，任何一个环节出问题，调试就无法进行。

理解这个架构之后，你就会知道为什么调试需要这么多步骤，也知道出问题时该从哪个环节排查。默认情况下，OpenOCD 会在 localhost:3333 端口监听 GDB 连接，同时在 localhost:4444 提供 Telnet 控制台（可以用来执行 OpenOCD 命令，比如手动 halt、resume 等）。

---

## 先从命令行开始：GDB 调试实战

在配置图形化界面之前，我强烈建议你先用命令行跑一遍完整的调试流程。这样做有两个好处：一是理解底层原理，知道图形界面背后实际在做什么；二是当图形界面出问题时，你能用命令行快速定位是配置问题还是环境问题。

首先启动 OpenOCD server。打开一个终端，进入你的项目目录，执行：

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
```

这条命令的含义是：使用 stlink.cfg 作为接口配置（告诉 OpenOCD 我们用的是 ST-Link），使用 stm32f1x.cfg 作为目标配置（告诉 OpenOCD 我们要调试的是 STM32F1 系列芯片）。如果一切正常，你会看到类似这样的输出：

```text
Open On-Chip Debugger 0.12.0
Licensed under GNU GPL v2
For bug reports, read
    http://openocd.org/doc/doxygen/bugs.html
Info : Listening on port 6666 for tcl connections
Info : Listening on port 4444 for telnet connections
Info : Listening on port 3333 for gdb connections
```

最后一行告诉我们 GDB server 已经在 3333 端口准备好了。保持这个终端运行，不要关闭。

接下来打开另一个终端，启动 GDB 并连接到 OpenOCD：

```bash
arm-none-eabi-gdb build/stm32_demo.elf
```

这里我们用的是 ARM 版本的 GDB（arm-none-eabi-gdb），而不是系统自带的普通 GDB。参数是我们编译好的 ELF 文件，里面包含了调试符号信息，所以 GDB 能知道源代码行号和变量名。

进入 GDB 命令行后，你会看到 `(gdb)` 提示符。现在我们按顺序执行以下命令：

```text
(gdb) target remote localhost:3333
```

这条命令告诉 GDB 连接到本地的 3333 端口，也就是 OpenOCD 的 GDB server。如果连接成功，你会看到类似 "Remote debugging using localhost:3333" 的提示。

```text
(gdb) load
```

这条命令把 ELF 文件里的代码段和数据段烧录到 STM32 的 Flash 和 RAM 里。你会看到进度条和 "Transfer rate XXX KB/s" 的输出。如果这里报错 "target not halted"，说明芯片还在运行，需要先执行 `monitor halt` 命令让芯片停下来。

```text
(gdb) break main
```

在 main 函数入口设置一个断点。GDB 会回复 "Breakpoint 1 at 0x..."，告诉你断点设置成功以及它的地址。

```text
(gdb) continue
```

让程序继续运行。程序会立即在 main 函数的断点处停下，你会看到类似这样的输出：

```text
Continuing.

Breakpoint 1, main () at main.cpp:42
42        HAL_Init();
```

现在程序已经停在了 main 函数的第一行，你可以开始单步调试了。`step` 命令会进入函数内部（如果当前行是函数调用），而 `next` 命令会执行当前行并停到下一行（不进入函数）。我个人的习惯是用 `next` 为主，只有在确实需要进入某个函数查看细节时才用 `step`。

查看变量用 `print` 命令：

```text
(gdb) print counter
```

如果变量是基本类型，GDB 会直接显示它的值。如果是数组或结构体，GDB 会显示完整的结构。你还可以用 `print/x` 以十六进制显示，或者 `print/t` 以二进制显示。

查看寄存器状态用 `info registers`：

```text
(gdb) info registers
```

这会显示所有通用寄存器（r0-r12）、sp、lr、pc 以及特殊寄存器（xPSR）的当前值。在嵌入式调试中，有时候你需要查看某个外设寄存器的值，比如想知道 GPIOC 的 ODR（Output Data Register）当前是什么状态，可以直接用 `x` 命令查看内存：

```text
(gdb) x/wx 0x4001080C
```

`x/wx` 的含义是：以十六进制（x）显示一个字（w，4字节）大小的内存内容。0x4001080C 是 GPIOC 的 ODR 寄存器地址（这个地址需要查参考手册）。GDB 会输出类似 `0x4001080c:    0x00002000` 的结果，表示这个寄存器的当前值是 0x2000，也就是第 13 位被置位（GPIOC 的 Pin 13 是板载 LED）。

如果你想直接修改变量或内存的值，可以用 `set` 命令：

```text
(gdb) set var counter = 100
```

这在测试某些边界条件时非常有用。比如你想验证当某个计数器溢出时程序的行为，可以直接把它设为接近溢出的值，而不是傻傻地单步几百次。

当你调试完毕，想退出时，用 `quit` 命令。如果芯片还在运行，GDB 会问你是否要停止它，选择 yes 即可。

---

## 好了，现在把它搬进 VSCode

命令行调试确实很酷，能让你显得像个老派黑客，但说实话，日常开发中我还是更愿意用图形界面。能看到源代码、变量列表、调用栈，能直接点击设置断点，这些便利性不是靠情怀能替代的。

VSCode 上调试 STM32 需要安装一个插件：Cortex-Debug。它是专门为 ARM Cortex 芯片设计的调试插件，支持 OpenOCD、J-Link、ST-Link 等多种调试器。安装完成后，我们需要创建一个 `.vscode/launch.json` 文件来配置调试行为。

让我先给你一个完整的配置，然后逐行解释：

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
            "serverpath": "/usr/bin/openocd",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32f1x.cfg"
            ],
            "searchDir": ["/usr/share/openocd/scripts"],
            "runToEntryPoint": "main",
            "device": "STM32F103C8T6",
            "interface": "swd",
            "serialNumber": ""
        }
    ]
}
```

`name` 字段是你在 VSCode 调试面板里看到的配置名称，可以随便改，选一个你能记住的就行。`type` 必须是 "cortex-debug"，这告诉 VSCode 用哪个插件来处理这个配置。`request` 用 "launch" 表示我们要启动调试（如果你已经有一个正在运行的 OpenOCD server，也可以用 "attach" 模式）。

`servertype` 指定我们用的 GDB server 类型，这里填 "openocd"。如果你用 J-Link，可以改成 "jlink"，但对应的配置也会不同。`cwd` 是当前工作目录，用 `${workspaceRoot}` 变量会自动设置为你的项目根目录。

`executable` 是最重要的一项，它指向你编译好的 ELF 文件。注意这里必须用 ELF 而不是 bin，因为 ELF 包含调试符号，而 bin 只是纯二进制。路径可以是相对路径（相对于 workspaceRoot），也可以是绝对路径。

`serverpath` 指定 OpenOCD 可执行文件的完整路径。在 Ubuntu 和 Arch 上，OpenOCD 通常安装在 `/usr/bin/openocd`，但如果你手动安装到其他位置，这里需要相应修改。Cortex-Debug 插件会自动启动这个 OpenOCD 实例，所以你不需要自己手动启动。

`configFiles` 数组指定 OpenOCD 的配置文件。这两个文件的路径是相对于 `searchDir` 的。`interface/stlink.cfg` 告诉 OpenOCD 我们用的是 ST-Link 调试器，`target/stm32f1x.cfg` 告诉它目标芯片是 STM32F1 系列。这些配置文件都是 OpenOCD 自带的，位于 `/usr/share/openocd/scripts` 目录下（大部分 Linux 发行版都是这个路径）。

`searchDir` 就是我刚才说的那个脚本目录。Cortex-Debug 需要知道在哪里找那些 `.cfg` 文件，所以这里要指定 OpenOCD 的脚本目录。如果你的系统上 OpenOCD 安装在其他位置（比如用源码编译安装到了 `/usr/local`），这里可能需要改成 `/usr/local/share/openocd/scripts`。

`runToEntryPoint` 是一个非常方便的选项。设为 "main" 后，调试会自动在 main 函数入口处停下，省去了手动设置断点的麻烦。如果你想从复位向量开始调试（比如想看启动文件和系统初始化过程），可以把这个选项删掉，程序会在 `Reset_Handler` 处停下。

`device` 字段指定具体的芯片型号。这个信息主要被 Cortex-Debug 用来显示正确的寄存器定义和外设信息。填 "STM32F103C8T6" 就能覆盖我们的 Blue Pill 开发板。

`interface` 指定调试接口类型，STM32 上一般都是 "swd"（Serial Wire Debug），只需要两根线。老一点的调试器可能用 "jtag"，但现在很少见了。`serialNumber` 用来指定特定的调试器（如果你同时连接了多个 ST-Link），大部分情况下留空即可。

配置完成后，回到 VSCode 主界面，按 F5 或者点击左侧的"运行和调试"面板，选择"STM32 Debug"，调试就会启动。你会看到底部的"调试控制台"输出 OpenOCD 的启动信息，然后程序会在 main 函数处停住。

---

## 完整调试 workflow：验证一切就绪

现在我们有了配置，是时候验证整个流程是否真的能跑通了。我会带着你走一遍完整的调试流程，确保每一步都按预期工作。

首先，确保你的 STM32 板子已经通过 ST-Link 连接到电脑，并且 OpenOCD 有权限访问 USB 设备（WSL 用户记得用 usbipd attach 转发）。然后在 VSCode 里按 F5 启动调试。

如果一切顺利，你应该会看到调试控制台输出类似这样的信息：

```text
Open On-Chip Debugger 0.12.0
Info : Listening on port 3333 for gdb connections
...
Info : stm32f1x.cpu: hardware has 6 breakpoints, 4 watchpoints
```

最后一行告诉你芯片支持 6 个硬件断点和 4 个观察点，这是 Cortex-M3 的标准配置。几秒钟后，编辑器会自动跳到 main 函数的第一行，左侧会显示一个黄色箭头指示当前执行位置。

现在试试单步执行。按 F10（Step Over）会执行当前行并停到下一行。如果你的 main 函数第一行是 `HAL_Init()`，按 F10 后黄色箭头会移到下一行，但不会进入 HAL_Init 函数内部。如果你想进入函数内部，按 F11（Step Into）。

左侧的"变量"面板会自动显示当前作用域内的所有局部变量和它们的值。如果变量显示 `<optimized out>`，说明编译器把它优化掉了，你需要在 CMakeLists.txt 里把优化级别改成 `-O0` 或 `-Og`（调试优化）。

在"监视"面板里，你可以手动输入想要监视的表达式。比如输入 `*GPIOC`，就能看到 GPIOC 外设的所有寄存器值；输入 `SystemCoreClock`，就能看到当前系统时钟频率。这在调试时钟配置时非常有用。

现在来试一个实战场景：监视 GPIO 寄存器。假设你的程序在闪烁 LED，你想知道 GPIOC 的 ODR 寄存器什么时候发生变化。在"监视"面板里输入 `*(volatile uint32_t*)0x4001080C`（这是 ODR 寄存器的地址），然后按 F5（Continue）让程序运行。你会发现监视值会随着 LED 状态改变而改变，从 0x2000 变成 0x0000 再变回来。

如果你想直接修改变量的值来测试某个条件，可以在"变量"面板里右键点击变量，选择"设置值"，或者在"调试控制台"里输入 GDB 命令：

```text
-exec set var counter = 1000
```

`-exec` 前缀告诉 VSCode 把后面的内容传递给 GDB 执行。这个技巧在你想测试边界条件时特别有用。

调试过程中，你可能会想查看调用栈。比如程序停在了某个中断服务程序里，你想知道是从哪里被触发的。左侧的"调用堆栈"面板会显示完整的调用链，从当前函数一直追溯到 `Reset_Handler`。点击任意一层，编辑器就会跳到对应的源代码位置，并且上下文变量也会切换到那一层。

当你调试完毕，按 Shift+F5 停止调试。VSCode 会自动关闭 OpenOCD server 并断开与 ST-Link 的连接。到这里，你的调试环境就完全验证完毕了。从编译、烧录到调试，整个工具链已经就绪，你可以开始专心写代码，而不是被环境问题困扰。

---

## 高级调试技巧：硬件断点与内存查看

上面的内容已经覆盖了 90% 的日常调试需求，但有些时候你会遇到更棘手的情况，这时候需要一些高级技巧。

第一个要讲的是硬件断点 vs 软件断点。你可能听说过，Cortex-M3 只支持 6 个硬件断点，但软件断点可以设置无数个。这是什么区别呢？软件断点是通过在目标地址写入一条特殊指令（BKPT）来实现的，当 CPU 执行到这条指令时会触发调试异常。但 Flash 是只读存储器，你无法在运行时修改它的内容，所以软件断点只能用在 RAM 里运行的代码。硬件断点则是通过 CPU 内部的比较电路来实现，不需要修改代码，所以可以设在 Flash 的任何位置，但数量受硬件限制（Cortex-M3 是 6 个）。

在实践中，这意味着当你设置第 7 个断点时，GDB 会报错 "cannot set breakpoint" 或者断点根本不生效。解决方法有两种：一是删掉不需要的断点，保持活动断点在 6 个以内；二是在 RAM 里运行一段代码（比如把某个频繁调试的函数复制到 RAM 执行），这样就可以用软件断点了。

在 GDB 里，你可以用 `info breakpoints` 查看当前所有断点的状态：

```text
(gdb) info breakpoints
Num     Type           Disp Enb Address            What
1       hw breakpoint  keep y   0x080001a8 in main at main.cpp:42
```

注意 `Type` 列，如果显示 `hw breakpoint`，说明用的是硬件断点；`breakpoint` 则是软件断点。

第二个高级技巧是内存查看。有时候你想查看一大片连续内存的内容，比如整个 DMA 缓冲区，或者某个结构体数组。用 `x` 命令可以实现：

```text
(gdb) x/10wx 0x20000000
```

这条命令会从 0x20000000 开始，以十六进制显示 10 个字（每个字 4 字节）的内容。`x/10gx` 则可以显示 64 位整数（8 字节），这在查看双精度浮点数数组时很有用。

在 VSCode 里，你可以在"监视"面板输入数组名称来查看数组内容，但如果你想查看原始内存，可以在"调试控制台"执行：

```text
-exec x/32xb 0x20000000
```

这会以字节为单位显示 32 字节的内存内容，`b` 表示 byte。这在调试内存对齐问题、DMA 传输问题时非常有用。

第三个技巧是关于 RTOS 调试。如果你用了 FreeRTOS 之类的 RTOS，你会发现调用栈里充满了 `xTaskResumeAll`、`vTaskSwitchContext` 之类的函数，很难找到当前任务的真正入口。Cortex-Debug 插件支持 RTOS 感知调试，但需要额外配置。在 `launch.json` 里添加：

```json
"rtos": "FreeRTOS",
"rtosConfigFile": "${workspaceRoot}/third_party/FreeRTOS/FreeRTOS/Source/include/FreeRTOS.h"
```

配置后，调试面板会显示一个"线程"下拉框，里面列出所有当前创建的任务，你可以像调试多线程程序一样在不同任务之间切换。

最后一个要讲的技巧是 SWO（Serial Wire Output）。SWO 是 ARM Cortex-M 的一种特性，可以通过 SWD 接口的高速通道输出调试信息，不需要占用 UART 资源，而且比 printf 快得多。但 SWO 的配置相对复杂，需要设置波特率、配置 TRACETCK 引脚，而且不是所有 ST-Link 都支持（ST-Link V2 才支持）。这块内容比较独立，我计划在后续文章里单独讲一篇。

---

## 常见调试问题排查

就算你照着上面的步骤一步步来，也难免会遇到各种奇奇怪怪的问题。调试环境涉及的环节多，任何一个地方出问题都会导致调试失败。我把我踩过的坑整理了一下，按症状分类，希望能帮你快速定位。

最常见的问题是 `Error: target not halted`。这个错误通常出现在你执行 `load` 命令的时候，原因是 OpenOCD 无法在芯片运行时烧录 Flash。解决方法是在 load 前先执行 `monitor halt`：

```text
(gdb) monitor halt
(gdb) load
```

`monitor` 前缀告诉 GDB 把后面的命令传递给 OpenOCD 而不是自己执行。`halt` 命令会让 CPU 停下来，进入调试模式。如果 halt 也报错，可能是芯片处于低功耗模式，需要更长时间才能唤醒，或者 SWD 连接不稳定。

第二个常见错误是 `Error: undefined debug reason 8`。这个错误我遇到时也是一头雾水，最后查资料发现是因为芯片处于睡眠或停止模式（Sleep/Stop Mode），调试器无法正常唤醒它。解决方法是在进入低功耗模式前禁用调试器睡眠，或者按复位按钮强制芯片退出低功耗状态。

第三种情况是断点打上了但程序不停在那里。这有几个可能原因。一是你确实超过了硬件断点限制（6 个），删掉几个没用的断点试试。二是代码可能根本没被加载到那个地址，检查 `load` 命令的输出，确保确实写入了正确的 Flash 区域。三是代码被优化掉了，优化器可能把你打断点的代码整个删除了，把编译优化改成 `-O0` 再试试。

第四个问题是变量显示 `<optimized out>` 或者显示的值明显不对。这几乎都是编译优化导致的。你在调试版本里应该用 `-Og`（专门为调试优化的模式）或者 `-O0`（完全关闭优化），而不是 `-O2` 或 `-O3`。在 CMakeLists.txt 里，你可以为 Debug 配置单独设置优化级别：

```cmake
add_compile_options(
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Release>:-O2>
)
```

还有一种情况是内联函数里的变量，因为代码被内联了，原来的"局部变量"可能已经被优化到寄存器里或者彻底消失了，GDB 无法追踪。这种情况下，你可以用 `-fno-inline` 禁止内联，或者干脆在更高一层打断点。

第五种问题是 VSCode 无法连接到 OpenOCD。错误信息可能是 "Failed to connect to GDB" 或者 "Could not connect to localhost:3333"。首先确认 OpenOCD 没有在其他地方运行（比如你之前手动启动的实例还没关闭），然后用 `netstat -tlnp | grep 3333` 检查端口是否被占用。如果端口被占用，要么关掉占用进程，要么在 `launch.json` 里改用其他端口（但 OpenOCD 默认就是 3333，改端口需要额外配置，不推荐）。

如果 OpenOCD 根本没启动，检查 `serverpath` 是否正确。在终端里直接执行 `/usr/bin/openocd --version`，如果命令不存在，说明 OpenOCD 没安装或者安装在其他位置。用 `which openocd` 找到正确路径，然后更新 `launch.json`。

WSL 用户还有一个特殊问题：USB 权限。错误信息通常是 `LIBUSB_ERROR_ACCESS` 或者 `could not open device`。首先确认 ST-Link 已经被 usbipd 转发到 WSL（`lsusb | grep -i stlink` 应该能看到设备），然后用我之前提到的脚本修复权限：

```bash
sudo chmod 666 /dev/bus/usb/001/XXX
```

最后的救命招数是查看 OpenOCD 的详细日志。在 `launch.json` 里添加：

```json
"openOCDLaunchCommands": ["debug_level 3"]
```

这会让 OpenOCD 输出最详细的调试信息，虽然看不懂大部分内容，但至少能知道它在哪一步卡住了。你也可以在终端手动启动 OpenOCD 并观察输出，很多错误信息只有在那里才会显示。

---

## 到这里就大功告成了

如果你跟着前面几篇文章一路走来，到现在应该已经拥有了一套完整的 STM32 开发工具链：交叉编译器、CMake 构建系统、HAL 库、OpenOCD 烧录工具，以及现在刚刚配置好的 GDB 调试环境。从编译、烧录到调试，整个流程都能在 Linux 下完成，不再依赖 Keil 这种 Windows 专属的 IDE。

当你第一次在 VSCode 里按 F5，看着程序在 main 函数断点处停下，然后单步几行、修改一个变量的值、看着 LED 随之改变闪烁频率，那种掌控感是无与伦比的。你不再是盲目地烧录、猜测、再烧录，而是能精确地观察程序的每一步执行，这才是嵌入式开发应该有的体验。

从 Keil 迁移到这套工具链，除了跨平台的优势之外，还有很多实实在在的好处。你可以用 Vim/Neovim 写代码，用 clangd 获得比任何商业 IDE 都强大的代码补全，用 Git 管理版本（不用再应付那些奇怪的工程文件），用 CTest 运行自动化测试。更重要的是，这套工具链完全开源、完全可定制，遇到问题时你可以阅读源码、修改配置，而不是被困在一个黑盒子里。

下一步，我们终于可以开始讲现代 C++ 在嵌入式中的应用了。模板、RAII、lambda 表达式、constexpr，这些 C++ 特性如何在资源受限的 STM32 上发挥作用？如何写出既现代又高效的嵌入式代码？这才是这套教程的真正核心，前面的工具链搭建都只是在做准备。但现在有了这套工具链，我们可以专心于代码本身，而不是被环境问题分心。
