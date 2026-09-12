---
title: "工作环境：您装的那四样东西，到底是什么"
description: "上一篇一条命令装了工具，这一篇补课：什么是交叉编译、arm-none-eabi-gcc 这串名字怎么拆、从源码到固件的完整流水线每个工具各管哪一段——全部用 ninja 实际执行的真命令逐阶段对号，cmake 和 ninja 的角色也一并说清，新手不再悬空"
chapter: 0
order: 3
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 工具链
difficulty: beginner
platform: stm32f1
reading_time_minutes: 12
related:
  - "Renode 观测课:没有板子,谁说了算"
  - "第一个自己的固件:往库里加 target"
---

# "上一章让我装了四个软件，我到现在都不知道它们是干嘛的"

是的，不是故意的，也不是不小心的。我是故意不小心的。毕竟，先让你看到能跑起来，我认为您才值得坐下来，继续漫游我们的旅程。

一个油然而生的疑问就是——不对啊，我用Keil, STM32CudeIDE或者是其他厂商带的神秘IDE，就没你这些P事情！是不是在骗我？

> 笔者这辈子不会忘记曾经混的某个群，一个人指着鼻子说我——“什么年代了，还在学什么gcc，知不知道我点一下那个按钮，代码就跑起来了，说你就是神神叨叨的，还说什么编译链接运行”

Man, what can i say。所以我每一次开启一个新的旅程的时候必然要科普一下这些东西。

## Section 1: 我们都在说编译，交叉编译又是何方神圣

咱们平时在电脑上写程序，用 `gcc` 编译，编出来的程序就在这台电脑上跑——编译它的人和将来执行它的 CPU 是同一种，这叫本地编译。但 Blue Pill 上跑的是 ARM Cortex-M3，指令集跟您电脑的 x86-64 **完全不同**：您拿系统自带的 `gcc` 编出的可执行文件，扔给 STM32 它一个字都看不懂。所以咱们需要一个特殊的东西：**一个跑在 x86 电脑上、却专门生产 ARM 机器码的编译器**。这件事就叫交叉编译（cross-compilation），这个编译器就叫交叉编译器。

那串名字咱们也就能逐段看懂了，`arm-none-eabi-gcc` 一共四段：

- `arm`：产出的机器码给 ARM 处理器用
- `none`：目标机器上没有操作系统（裸机，我们的固件自己就是"操作系统"）
- `eabi`：Embedded Application Binary Interface，嵌入式应用二进制接口，约定函数参数怎么传、栈怎么摆这类底层规矩
- `gcc`：GNU 编译器家族

四个串在一起，我们得到了`arm-none-eabi-gcc`。自然的，如果是C++编译器呢？`arm-none-eabi-g++`，如果我要cv一下二进制或者改一下格式或者看一下大小：`arm-none-eabi-objcopy`、`arm-none-eabi-size`；当然，如果是调试：`arm-none-eabi-gdb`。前缀相同，**目标机器相同**，一家子。

> Q: 我懂C语言，你怎么用`memcpy`、`printf` 这些 C 标准库函数的？却不能直接用电脑上的 glibc？
> A: 因为 glibc 是给"有操作系统"的环境设计的，一开口就是要syscall，也就是系统调用。咱们是STM32F103C8T6拿来的操作系统不是。
>
> 裸机世界用的是 **newlib**——专为嵌入式重写的 C 标准库；咱们链接时用的 `--specs=nano.specs` 选的是它更抠体积的精简版 newlib-nano。这也是为什么 00 篇那两个空函数桩（`_init`/`_fini`）会存在：newlib 的启动流程要调它们，裸机上没人提供，只能固件自己补。

## 从 main.cpp 到 my_blinky.bin

### 第一步，叫编译，而且是交叉编译

一个固件，咱们写下的时候是源码（可能有点难读），最后烧上去是二进制（这个肯定读不懂）。中间发生的过程，可以让ninja这个构建系统说：

```bash
ninja -C build -t commands blinky
```

ninja 会把它会跑的每条命令原样吐出来。咱们挑每个代表，中间漫长的 `-I` 头文件路径清单用 `...` 折叠了。你要问我这是啥，就是你配置Keil环境的时候抄的头文件的包含路径。

第一步，ninja如是说：咱们写的 `.cpp`/`.c` 变成目标文件（`.o`），每个源文件一个：

```text
arm-none-eabi-g++ ... -mcpu=cortex-m3 -mthumb -fno-exceptions -fno-rtti \
    -O3 -DNDEBUG -std=gnu++23 \
    -o examples/00_my_blinky/CMakeFiles/my_blinky.dir/main.cpp.obj \
    -c .../examples/00_my_blinky/main.cpp
```

`-c` 就是compile动作，"只编译不链接"。注意，不链接！剩下的还有

- `-mcpu=cortex-m3 -mthumb` 告诉编译器按 Cortex-M3 的指令集生成代码（这就是"交叉"落在实处的地方）
- `-O3` 是上秤那篇说好的优化级别
- `-fno-exceptions -fno-rtti` 是那两个"不用的不给钱"开关。C++ 源文件交给 `g++`，C 源文件（HAL 库的 `.c`、咱们的桩文件）交给 `gcc`，还有一份启动汇编 `startup_stm32f103xb.s` 也在这个这里编成 `.o`。

### 第二步是链接，零碎的二进制产物需要按照正确的方式组合起来

咱们把所有 `.o` 加上 `hal` 静态库，拼成一个完整的 ELF：

```text
arm-none-eabi-g++ -mcpu=cortex-m3 -mthumb \
    -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
    -T.../cmake/arch/stm32f103c8t6.ld \
    -Wl,-Map=.../my_blinky.map \
    examples/00_my_blinky/CMakeFiles/my_blinky.dir/main.cpp.obj \
    examples/00_my_blinky/CMakeFiles/my_blinky.dir/stm32f1xx_it.c.obj \
    examples/00_my_blinky/CMakeFiles/my_blinky.dir/syscalls.c.obj \
    -o examples/00_my_blinky/my_blinky \
    include/libestdx/boards/stm32f1/libhal.a
```

链接器（真正的干活的叫 `ld`，`g++` 在这当调度）回答的问题是"谁挨着谁、各住哪个地址"。`-T` 给它的那张内存地图（64K Flash、20K SRAM 的布局）您在 00 篇见过

`--gc-sections` 是那个"没人引用的函数直接扔掉"的清洁工，他的意思是Garbage Collections。又从带有gc语言的朋友来嘛？一个意思！不用就扔掉！

`-Map` 让它把搬家明细写成 `.map` 文件，这样我们做CI审计，或者是就是纯想看看啥情况的时候，可以来确认哪个家伙占用的体积过大！

### 第三步，提取真正的有用代码，把烧录的产物从ELF中剥离出来

别看用的是 obj-copy，其实他做的事情是这个。`my_blinky`这家伙，是个 ELF。里面除了代码和数据，还带着符号表、调试信息这些"给人看的附件"。Flash 不认这些，它只要干货：

```text
arm-none-eabi-objcopy -O binary .../my_blinky my_blinky.bin
```

`objcopy` 把 ELF 里的加载镜像原样剥出来，交给咱们一个可以逐字节倒进 Flash 的 `.bin`。恭喜，这就是我们之前手动验证的时候，使用的工具~

**工位四：报告**：

```text
arm-none-eabi-size --format=berkeley .../my_blinky
```

就是那张您已经看过三轮的 `text/data/bss` 三列表。

事已至此，已成为艺术。我们走通了从源文件到可以直接烧录的binary二进制的全部流程。请~。

## 喂喂，你刚刚介绍那一坨工具链，很开心嘛！那 cmake 和 ninja 是干嘛的呢

看完全流水线，一个自然的疑问冒出来了：既然最后就是这几条命令，**为什么咱们要写 CMakeLists.txt，不能直接敲命令？**

可以敲，一次可以。但真实工程是十几个源文件、两套编译旗子（C 一套、C++ 一套）、六七个固件目标，每次改一个文件只想重编它自己，敲命令的方案第一天就崩了。所以需要两层分工：**cmake 是设计师**，读您的 `CMakeLists.txt`，算出"要编哪些目标、每个目标哪些源文件、带什么旗子"，把结论写成一份施工图纸（`build.ninja`）

那我们的**ninja 是包工头**，拿着图纸调度上面那条流水线，谁改了就重做谁。您敲的 `cmake -B build` 是请设计师出图，`cmake --build build` 是让包工头开工——刚那个 `ninja -t commands` 就是把包工头的施工清单偷来看了一眼。

为什么 CMakeLists 里要写 `PROJECT`、`add_executable` 这种"图纸语言"而不是直接写命令？因为图纸是跨工地通用的：换个芯片，改一份工具链文件（`cmake/arch/stm32f103c8t6.cmake`，它告诉 cmake"编译器叫 arm-none-eabi-gcc、链接器选项是什么"），图纸本身一个字不用动。这套设计到咱们换 F407 芯片的那一站会直接派上用场。可以说一行都不咋改！偷懒嘛！

## 验收：亲手摸一下流水线

光看不过瘾，咱们亲手把流水线摸一遍。在您 clone 的库的构建目录里，用编译的产物开头：

```bash
file build/examples/00_my_blinky/CMakeFiles/my_blinky.dir/main.cpp.obj
```

```text
main.cpp.obj: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped
```

`file` 告诉您这个 `.o` 是"可重定位的 ELF"（relocatable）——机器码已经是 ARM 的了，但地址还没定，所以叫可重定位，等链接器分配。再摸下面的几个产物！

```bash
file build/examples/00_my_blinky/my_blinky build/examples/00_my_blinky/my_blinky.bin
```

```text
my_blinky:     ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
               statically linked, with debug_info, not stripped
my_blinky.bin: ARM Cortex-M firmware, initial SP at 0x20005000,
               reset at 0x080001a4, NMI at 0x080001ec, ...
```

`my_blinky` 升级成了 executable（地址已定，还带着调试信息）；而 `.bin` 这份"纯数据"有个离谱的事情：`file` 直接把它的 Cortex-M 向量表解出来了，**initial SP 在 0x20005000，恰好是 20K SRAM 的顶**（链接脚本写的内存地图，在这里被验证，有点小帅）。其他的部分，您可以展开慢慢研究，刚好也算是嵌入式C++旅程中arm架构知识的热身环节了！
