# 第1篇：从零搭建 STM32 开发工具链 —— 交叉编译原理与安装指南

> 写给所有想在 Linux 下搞 STM32、却被一堆工具链名词搞得晕头转向的朋友。
> 本篇记录我们从零开始搭建 ARM 交叉编译环境的完整过程，包括为什么要交叉编译、每个工具是干什么的，以及在 Ubuntu 和 Arch Linux 下分别如何安装。

---

## 为什么我要写这套教程

说句实话，我实在绷不住 Keil 那套老旧的工作流了。今年都 2024 年了，还在用只能跑在 Windows 上的闭源 IDE，代码提示残废，调试界面像上个世纪的软件，关键是还占了我好几 GB 的 C 盘空间。最要命的是，我已经习惯了 Linux 下的开发环境 —— Vim/Neovim 写代码，clangd 做补全，CMake 管构建，这套工具链用在任何项目上都顺手得不行。

但事情没那么简单。当我第一次尝试在 Linux 下给 STM32F103C8T6（也就是那块几块钱的 Blue Pill 开发板）烧程序时，我发现网上的教程简直是一场灾难。有的还在用 Makefile 手写编译规则，有的直接掏出 PlatformIO 这种把一切都封装好的黑盒，还有的干脆说"你就用 Keil 吧，Linux 下折腾不划算"。最离谱的是那些所谓"从零开始"的教程，上来就给你一堆命令让你复制粘贴，完全不说 arm-none-eabi-gcc 是干嘛的、newlib 又是什么、为什么需要链接脚本。你照着做确实能跑通，但只要稍微出点问题，你就完全不知道从哪下手排查。

我花了整整一个周末，把这套工具链从里到外折腾了一遍，踩了无数坑之后，终于理清了整个编译烧录的链条。现在我要把这个过程完整地记录下来，不是给你一份"复制就能跑"的 cheat sheet，而是带你真正理解每一步在做什么、为什么这么做。这样当你以后遇到报错时，能知道问题出在哪个环节，而不是像无头苍蝇一样到处搜答案。

---

## 先说清楚：什么是交叉编译

在我们开始敲命令之前，有一个概念必须先讲明白 —— 交叉编译（Cross-Compilation）。

如果你平时写的是运行在 x86-64 CPU 上的普通程序，编译过程很直接：你用 gcc 编译代码，生成的可执行文件也是在同一台机器上运行的。编译器和程序运行的目标平台是同一个，这叫"本地编译"（Native Compilation）。

但 STM32F103C8T6 用的是 ARM Cortex-M3 核心，指令集和你电脑上的 x86-64 完全不同。你在电脑上用普通的 gcc 编译出来的代码，STM32 根本读不懂，就像你对着一个只懂中文的人念阿拉伯语一样。所以我们需要一个"翻译官" —— 一个运行在 x86-64 Linux 上、但能生成 ARM 机器码的编译器。这就是交叉编译器。

那为什么叫 `arm-none-eabi-gcc` 这么一长串奇怪的名字？拆开来解释就很清楚了：

- `arm` 是目标 CPU 架构，生成的代码是给 ARM 用的
- `none` 表示没有操作系统厂商（后面会讲）
- `eabi` 是 Embedded Application Binary Interface，嵌入式应用二进制接口的缩写
- `gcc` 就是我们熟悉的 GNU Compiler Collection

这里有个细节值得展开。`none` 这个字段原本是用来标注操作系统厂商的，比如 `arm-linux-eabi` 表示给跑 Linux 的 ARM 设备编译。但我们的 STM32 是裸机程序，没有操作系统撑腰，所以这里填 `none`。而 `eabi` 和 `eabihf` 的区别在于后者支持硬件浮点，但 F103C8T6 的 Cortex-M3 只有单精度浮点单元，所以用普通的 `eabi` 就够了。

理解交叉编译之后，你就会明白为什么不能直接用系统自带的 gcc，也知道为什么需要一整套专门的工具链：编译器、链接器、调试器、_objcopy_（用来把 ELF 转成二进制）、_size_（用来查看生成的固件大小），这些工具都必须是"交叉版本"的。

---

## 整个工具链长什么样

在正式安装之前，我想先把整体框架搭起来，让你知道我们最终要凑齐哪些零件。

编译一个 STM32 程序并烧到板子上，大概需要这么一套流水线：

首先是源代码层面。你写的 C/C++ 代码需要经过预处理、编译、汇编，变成一个个目标文件（`.o` 文件）。这一步用的是 `arm-none-eabi-gcc`（C 代码）和 `arm-none-eabi-g++`（C++ 代码）。

但光有目标文件还不行，它们需要被"胶水"粘在一起。这个胶水就是链接器（`arm-none-eabi-ld`），它的工作是把所有目标文件、库文件按照指定的规则拼成一个完整的程序。对于 STM32 来说，链接过程尤其特殊 —— 你需要告诉它 Flash 从哪个地址开始、RAM 在哪里、堆栈怎么分配，这些规则写在链接脚本（Linker Script，`.ld` 文件）里。链接器会按照脚本里的"地图"把代码段、数据段放到正确的位置。

链接完成之后，你得到的是一个 ELF 格式的文件（`.elf`），里面包含了代码、数据、符号表等一堆信息。但 STM32 的 Flash 只认纯粹的二进制数据，不需要什么符号表。所以需要用 `arm-none-eabi-objcopy` 把 ELF 文件里的"干货"提取出来，生成一个 `.bin` 二进制文件。这个文件才是真正要烧进 Flash 的东西。

烧录工具有好几种选择。最常见的是 ST-Link V2，这是 ST 官方出的调试器/烧录器，通过 SWD（Serial Wire Debug）协议和 STM32 通信。在 Linux 下，我们需要一个软件来驱动 ST-Link，这个软件就是 OpenOCD（Open On-Chip Debugger）。它能扮演两个角色：一是把固件写到 Flash 里（烧录），二是充当 GDB Server，让你用 GDB 调试板子上的程序。

说到库文件，这里有个新手容易混淆的点。ARM 裸机程序没法直接用你电脑上的 glibc（GNU C Library），因为 glibc 是给操作系统环境设计的，依赖一堆系统调用。嵌入式环境需要的是 newlib —— 一个专门为裸机/嵌入式系统设计的 C 标准库实现。更具体地说，我们用的是 newlib-nano，它是 newlib 的精简版，针对代码体积做了优化。安装 `arm-none-eabi-newlib` 之后，编译器就能找到 `<stdint.h>`、`<string.h>` 这些头文件，链接时也能拿到必要的库函数实现。

最后一环是调试。OpenOCD 可以以 GDB Server 模式运行，监听某个端口（默认 3333）。你用 `arm-none-eabi-gdb` 连上去，就能像调试普通程序一样单步执行、打断点、查看变量。VSCode 的 Cortex-Debug 插件就是把这整套流程图形化了，你不用手动敲 GDB 命令。

把这些串起来，完整的链条是：**源代码 → 交叉编译 → 链接（带链接脚本）→ objcopy 提取二进制 → OpenOCD 烧录 → GDB 调试**。理解这个链条之后，你就会知道每个工具在哪个环节起作用，出问题时能快速定位是编译、链接还是烧录阶段出了岔子。

---

## 好了，现在开始上号

前面铺垫了这么多概念，现在我们终于可以动手了。我会分 Ubuntu 和 Arch 两条线来讲，但你很快会发现命令其实差不多，都是包管理器那一套。

先说 Ubuntu。这里我用的是 22.04 LTS，但 20.04 和 24.04 的命令基本一致，毕竟是同一个软件源。打开终端，先更新一下包索引，这是个好习惯：

```bash
sudo apt update
```

然后一口气把需要的包装上：

```bash
sudo apt install -y \
    gcc-arm-none-eabi \
    gdb-arm-none-eabi \
    openocd \
    cmake \
    build-essential
```

让我解释一下这几个包都干嘛的。`gcc-arm-none-eabi` 是个大礼包，里面包含了交叉编译器、链接器、objcopy、size 等一整套工具。`gdb-arm-none-eabi` 是 ARM 版本的 GDB，用来调试嵌入式程序。`openocd` 我们前面说过了，是烧录和 GDB Server。`cmake` 和 `build-essential` 则是构建工具，后者包含了 make 等基础编译工具。

安装完成之后，我们可以验证一下工具链是不是真的装上了：

```bash
arm-none-eabi-gcc --version
```

正常的话，你会看到类似这样的输出：

```text
arm-none-eabi-gcc (Ubuntu 11.3.0-1ubuntu1~22.04) 11.3.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is no warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

版本号可能不一样，但只要能打印出版本信息，就说明安装成功了。这里还有个小细节：Ubuntu 的包名是 `gcc-arm-none-eabi`，不带版本号，软件源会自动选一个"稳定且大多数人用"的版本。如果你需要特定版本（比如想用最新的 GCC 14），那就得去 ARM 官方下载预编译的工具链，手动解压到某个目录，然后把路径加到 `PATH` 环境变量里。不过对于 F103C8T6 这种老芯片，GCC 11 已经足够了，没必要折腾太新的版本。

---

## Arch Linux 用户的路线

如果你用的是 Arch Linux（或者我用的 Manjaro），包管理就更直接了。Arch 的优势是软件更新快，你能拿到比较新的工具链版本。

安装命令比 Ubuntu 简短一些：

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-gdb openocd cmake make
```

这里有个和 Ubuntu 不同的地方：Arch 把工具拆分成了多个包。`arm-none-eabi-gcc` 是编译器本身，`arm-none-eabi-binutils` 包含了 ld、objcopy、size 这些工具，`arm-none-eabi-gdb` 是调试器。Ubuntu 把这些都打包进了 `gcc-arm-none-eabi`，所以需要装的包更少。

验证一下安装是否成功：

```bash
arm-none-eabi-gcc --version
```

Arch 上你大概率会看到 GCC 13 或者 14，因为滚得快：

```text
arm-none-eabi-gcc (GCC) 13.2.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is no warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

这里有个坑需要提前预警一下。Arch 上装完 `arm-none-eabi-gcc` 之后，你可能会发现编译时找不到 `<stdint.h>` 这类头文件，或者链接时报 `cannot read spec file 'nano.specs'`。原因都是同一个 —— Arch 的 `arm-none-eabi-gcc` 包不包含 newlib，你需要额外装一个 AUR 上的包：

```bash
yay -S arm-none-eabi-newlib
```

如果你没有装 `yay`，那得先装这个 AUR helper，或者手动从 AUR 克隆 PKGBUILD 来装。这个过程我就不展开了，用 Arch 的人应该都熟。

装完 newlib 之后，`<stdint.h>`、`<string.h>` 这些头文件就有了，`nano.specs` 和 `nosys.specs` 也能正常使用。这两个 specs 文件是干嘛的？`nano.specs` 告诉链接器用 newlib-nano（精简版 C 库），`nosys.specs` 则提供一个空的系统调用实现 —— 毕竟裸机环境没有操作系统，像 `read()`、`write()` 这类函数根本没法实现，用 nosys.specs 能让链接时不报错。

---

## 到哪一步了

到这里，我们的工具链安装就算完成了。你现在的系统上应该有：

- 交叉编译器（arm-none-eabi-gcc/g++）
- 链接器和工具链（arm-none-eabi-ld, objcopy, size）
- 调试器（arm-none-eabi-gdb）
- 烧录工具（OpenOCD）
- 构建系统（CMake）
- C 标准库（newlib）

但光有工具还不够，下一篇文章我们会讲项目结构 —— 怎么获取 ST 官方的 HAL 库、那个坑人的 submodule 问题、启动文件到底选哪个、链接脚本怎么写。那部分才是真正的"踩坑集中营"，现在我们先把地基打牢。

你可以先验证一下所有工具都能正常调用：

```bash
# 验证编译器
arm-none-eabi-gcc --version

# 验证调试器
arm-none-eabi-gdb --version

# 验证烧录工具
openocd --version

# 验证 CMake
cmake --version
```

如果这些命令都能打印出版本信息，恭喜你，工具链安装这一关就算过了。下一篇文章我们会直接进入项目结构，开始搭建真正的 STM32 C++ 项目。
