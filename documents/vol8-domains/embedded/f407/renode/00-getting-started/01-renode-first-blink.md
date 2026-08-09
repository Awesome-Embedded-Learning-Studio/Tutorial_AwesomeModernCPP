---
title: "用 Renode 跑通第一盏灯"
description: "不买板子,在电脑上用 Renode 把 STM32F407 的 PD12 点亮,嵌入式主线的入门第一篇"
chapter: 0
order: 1
tags:
  - stm32f4
  - beginner
  - 嵌入式
  - 工具链
  - CMake
  - renode
difficulty: beginner
platform: stm32f4
reading_time_minutes: 6
---

# 用 Renode 跑通第一盏灯

## 这一篇要解决什么

多数嵌入式教程第一句都是"先去买块板子"。咱们这条线换个开头:用 Renode 这个开源模拟器,在电脑上把 STM32F407 跑起来,第一盏灯就在模拟器里点亮。手头没有任何硬件,也能完整跟下来。

读完这一篇,您会有一份能在模拟器里闪烁的 F407 闪灯固件,还能用读寄存器的方式亲眼证实它真的在翻。"必须先买板子"这件事挡住了太多好奇的人,咱们先把这道门槛拆掉。

## 先准备三样东西

一台 Linux 电脑,WSL2 也行。三样工具:

- `arm-none-eabi-gcc`,交叉编译器,负责把 C/C++ 编成 F407 能跑的机器码。
- `cmake`,管构建。
- `renode`,模拟器,这一篇的主角。

前两个没什么花头,装上即可。Renode 单独说一句:官方推荐从 GitHub releases 下 `linux-portable` 包解压即用,自带运行时,不依赖系统装别的;Arch 用户也能 `yay -S renode`。装完跑 `renode --version` 确认。

WSL2 有个坑先讲清楚:Renode 自带一个 GUI 窗口,WSL2 默认没有 X server,GUI 弹不出来。但咱们全程用无头模式(`--console --disable-xwt`),不弹 GUI,所以没事。

## 拿到代码

配套工程在仓库的 `code/stm32f4-tutorials/renode/0_blink/`,结构很薄:

```text
code/stm32f4-tutorials/
├── toolchain-arm-none-eabi.cmake   共用工具链文件
└── renode/
    └── 0_blink/
        ├── CMakeLists.txt           构建脚本,含 run_in_renode 目标
        ├── src/main.c               闪灯代码(翻转 PD12)
        ├── platform/startup.s       Cortex-M4F 启动
        ├── platform/stm32f407zg.ld  链接脚本
        └── blink.resc               Renode 启动脚本
```

这一篇只跑它,不改代码。代码本身怎么点亮一盏灯,是后面的事。

## 编译

进工程目录,配置加编译:

```bash
cd code/stm32f4-tutorials/renode/0_blink
cmake -B build -DCMAKE_TOOLCHAIN_FILE=../../toolchain-arm-none-eabi.cmake
cmake --build build
```

顺利的话末尾会打印一行体积:

```text
   text    data     bss      dec    hex    filename
    576      0    1024     1600    640    build/blink.elf
```

`text 576`,整个固件的代码只有 576 字节。F407 有 1MB Flash,这点东西塞进去连个水花都没有。裸机的好处就在这:没有操作系统、没有运行时,写的每一条指令就是机器跑的每一条指令。

`-DCMAKE_TOOLCHAIN_FILE=../../toolchain-...` 这一句是告诉 cmake 用交叉编译器,别用 host 的 gcc。工具链抽成独立文件,以后每个新工程都引它,改一处全改,比每个工程各写一份编译器配置强得多。

## 跑进模拟器

精华在这一步。一条命令:

```bash
cmake --build build --target run_blink_in_renode
```

它会编译 `blink.elf`,然后调 Renode 把它载进模拟的 F407 里跑。输出大概长这样:

```text
cpu: Setting initial values: PC = 0x8000189, SP = 0x20020000.
machine-0: Machine started.
=== PD12 翻转证明(GPIOD_ODR @0x40020C14,bit12 应在 0/0x1000 跳)===
0x00000000
0x00000000
0x00001000
```

最后几行是关键。`GPIOD_ODR` 是 GPIOD 端口的输出数据寄存器,bit12 就是 PD12 这只引脚的电平。模拟器跑了 1 秒虚拟时间,然后连着读了几次这个寄存器:`0x00000000`、`0x00000000`、`0x00001000`,bit12 在 0 和 1 之间跳。PD12 在闪。

`PC = 0x8000189, SP = 0x20020000` 这行也值得看一眼。这是模拟器从固件的向量表里读出来的初始 PC(程序计数器)和 SP(栈指针):F407 上电后从 `0x08000188` 开始执行(末位 1 是 Thumb 模式标记,所以显示成 `0x...189`),栈顶在 `0x20020000`,SRAM 顶端。咱们写的启动代码、链接脚本,模拟器全认。

## 模拟器怎么知道"灯亮了"

这个问题值得讲明白,不然"PD12 在闪"就只是一行数字。

真实的 F407 Discovery 板上,PD12 接了一颗 LED。代码翻转 PD12,LED 就亮或灭。Renode 模拟器里,这份"PD12 接了 LED"的关系写在一份平台描述文件里(Renode 自带的 `stm32f4_discovery.repl`):

```text
UserLED: Miscellaneous.LED @ gpioPortD
gpioPortD:
    12 -> UserLED@0
```

三行意思:有个叫 UserLED 的 LED,挂在 gpioPortD 上;gpioPortD 的第 12 脚接到这个 LED。所以代码翻转 ODR 的 bit12,gpioPortD 模型就把变化传给 UserLED,LED 状态跟着翻。咱们读 ODR 看到 bit12 跳,等价于看到 LED 闪。

这就是模拟器的本事:它模拟 CPU 跑指令,也模拟外设(寄存器、GPIO、中断),所以咱们写的裸机寄存器代码,在模拟器里和在真板上的行为是一致的。这一点,等核心板到位会拿到真硬件上对一遍。

## 常见坑

| 现象 | 原因 | 解决 |
|---|---|---|
| Renode 弹不出 GUI | WSL2 没有 X server | 用无头模式,`run_in_renode` 目标已带 `--console --disable-xwt` |
| cmake 找不到交叉编译器 | `arm-none-eabi-gcc` 不在 PATH | `which arm-none-eabi-gcc` 检查,Arch 装在 `/usr/sbin/` |
| 改了代码没反应 | cmake 缓存住 | 清掉 `build/` 重来 |
| 仓库里看不到 `blink.elf` | `.gitignore` 挡了 `build/` | 自己跑一次 cmake 生成 |

## 灯亮了,然后呢

`main.c` 里翻转 PD12 那段是裸地址强转:

```c
#define GPIOD_ODR   (*(volatile unsigned int *)0x40020C14)
...
GPIOD_ODR ^= (1u << 12);
```

能用,但写错地址、用错类型,编译器一个都不拦。能不能既留住裸机的性能,又让编译器帮着挡掉这类低级错误?能。而且每升一档封装,反汇编都证明没多花一条指令。零开销抽象在嵌入式上不是口号,能拿 objdump 钉死。
