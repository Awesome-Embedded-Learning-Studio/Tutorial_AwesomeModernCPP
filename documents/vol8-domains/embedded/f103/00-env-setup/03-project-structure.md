---
title: "项目结构:HAL 库的获取与目录搭建"
description: "HAL 库的三层架构、submodule 的嵌套陷阱、启动文件的密度命名玄学,以及 stm32f1xx_hal_conf.h 模板里埋着的暗雷"
chapter: 0
order: 3
tags:
  - stm32f1
  - beginner
  - 入门
  - 嵌入式
  - 交叉编译
difficulty: beginner
platform: stm32f1
reading_time_minutes: 14
prerequisites:
  - "Renode 先行:不买板子,先点第一盏灯"
related:
  - "CMake 配置:从零构建 STM32 构建系统"
---

# 项目结构:HAL 库的获取与目录搭建

上一篇咱们在模拟器里把灯点起来了,这篇回头补地基:HAL 库怎么来的、工程目录为什么长那样。别嫌这步琐碎——ST 的固件库有一套自己的"生态系统",CMSIS 层、HAL 驱动层、启动文件、链接脚本,必须按特定方式组织,否则编译器找不到头文件,链接器不知道代码该放哪。更麻烦的是获取方式上的坑:官方库通过 Git 仓库发布,内部还有嵌套 submodule,常规方式克隆十有八九漏文件,等编译到一半报错再回头查,非常痛苦。

## 先搞清楚 HAL 库的三层架构

下载代码之前,咱们把 ST 固件库的分层设计看明白。这能解释后面为什么要建那些目录、每个文件是谁的责任。

最底层是 **CMSIS-Core**(Cortex Microcontroller Software Interface Standard)。这是 ARM 制定的一套标准,定义 Cortex-M 内核的寄存器访问接口。简单说,CMSIS-Core 告诉您"这颗芯片有一个叫 SCB 的寄存器组,地址是 0xE000ED00",写代码时就能用 `SCB->VTOR = 0x00` 这种方式操作内核寄存器,不用记魔法数字。这一层对所有制式 Cortex-M 的芯片通用。

中间层是 **CMSIS-Device**,ST 针对 STM32F1 系列做的具体化。它定义 F103C8T6 这颗具体芯片有哪些外设、寄存器地址在哪。比如 `GPIOA` 基址 `0x40010800`,这种信息就写在这层的头文件里。您以后会频繁见到 `stm32f103xb.h` 这种名字,它们属于这层。

最上层是 **HAL 驱动层**,ST 用 C 写的外设驱动 API,`HAL_GPIO_TogglePin()`、`HAL_UART_Transmit()` 这些函数的家。它们屏蔽底层寄存器操作,让您用统一方式操作不同系列的 STM32——理论上 HAL 代码移植到 F4 只需改少量配置,这件事在后面换 F407 芯片时会真的做一次给您看。

再往上就是您的应用代码。调用链自上而下:应用 → HAL → CMSIS-Device → CMSIS-Core。这套教程的立场也在这个分层里:HAL 及以下**用官方的,只讲不造**(拆开看懂它是工程素养,自己重写一套寄存器框架属于没事找事);您的现代 C++ 武艺,全部施展在应用层。

## 获取 HAL 库:submodule 的陷阱

> PS: 笔者推介您自己来学会处理依赖，咱这里是因为源码独立到一个仓库上去了，才挂的他哈。

HAL 官方仓库在 `https://github.com/STMicroelectronics/STM32CubeF1`。本教程的仓库没有直接挂它,挂的是配套的外设库 libestdx(`third_party/libestdx`,独立公开仓),CubeF1 由 libestdx 自己作为 submodule 带着——依赖链是"教程仓 → libestdx → CubeF1"三层。咱们克隆教程仓库后执行一次:

```bash
git submodule update --init --recursive
```

一条命令,三层全下。注意结尾的 `--recursive`,这是本节的主角:少写它,您拉到的只有 libestdx 的壳,它肚子里的 CubeF1 还是个空指针。如果您用 `--depth=1` 做浅克隆:

```text
# 错误做法,不要抄
git submodule add --depth=1 https://github.com/STMicroelectronics/STM32CubeF1.git STM32F1
```

命令本身能成功,但 STM32CubeF1 内部还有一层 submodule(CMSIS 等),浅克隆会破坏嵌套 submodule 的初始化。症状特别阴险:平时毫无异常,直到某天您发现这个目录是空的:

```bash
ls third_party/libestdx/third_party/STM32F1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

正常应该有一排启动文件(`startup_stm32f103xb.s` 之类),浅克隆下这里是空的,编译时报 `cannot find 'startup_stm32f103xb.s'`,而 submodule 明明"已经加进来了",咱们查起来一头雾水。

原因在 Git submodule 机制本身:克隆含 submodule 的仓库时,Git 只拉外层内容,子目录里放的只是一个"指针"(指向另一个仓库的某个 commit),必须 `update --init --recursive` 才真正拉取嵌套内容。已经掉坑的朋友,您跑这条命令补救:

```bash
cd third_party/libestdx
git submodule update --init --recursive
```

跑完再用上面的 `ls` 验证,您看到一排 `.s` 文件就说明齐了。

## 启动文件的命名玄学

文件都到位了,新问题来了:`gcc/` 目录下一排启动文件,咱们该用哪个?

网上不少教程写 `startup_stm32f103x8.s`,但您 `ls` 一下会发现**根本没有这个文件**。官方文件名是 `startup_stm32f103xb.s`。这个差异背后是 ST 的密度命名规则。F103C8T6 型号里的"C8":C 代表 48 脚封装,8 代表 64KB Flash。而启动文件按"密度等级"命名:

| 后缀 | 密度     | Flash 容量 |
| ---- | -------- | ---------- |
| `x6` | 小容量   | 16-32KB    |
| `xB` | 中容量   | 64-128KB   |
| `xE` | 大容量   | 256-512KB  |
| `xG` | 超大容量 | 768KB-1MB  |

C8T6 的 64KB 属于中容量,所以咱们用 `startup_stm32f103xb.s`。这个"B"容易让人当成 8 的十六进制,其实它是 ST 的密度代码。对应到编译宏是 `-DSTM32F103xB`(大写 B),写错成 `x8` 会让头文件条件编译选错分支,编出来的代码和硬件对不上。

启动文件本身是干嘛的?它是芯片复位后执行的第一段代码。上电或复位时,CPU 从地址 0x00000000 读初始堆栈指针,从 0x00000004 读复位向量,跳过去执行。启动文件定义了整张向量表(所有中断和异常的入口地址),还负责把 `.data` 段从 Flash 搬到 RAM、清零 `.bss` 段,最后跳进您的 `main()`。C++ 程序员还得关心一件事:全局对象的构造函数也在这一步被调用。没有启动文件,芯片复位后不知道该干什么。这份文件咱们在 CMake 篇还会再见到它。

## 工程目录:教程仓库里的真实样子

咱们直接看外设库 `third_party/libestdx/`,剪掉构建产物后长这样:

```text
libestdx/
├── CMakeLists.txt               # 根构建:只做编排(下一篇的主角之一)
├── cmake/
│   ├── arch/stm32f103c8t6.cmake # 工具链文件:交叉编译器全在这儿
│   ├── arch/stm32f103c8t6.ld    # 链接脚本:64KB Flash / 20KB RAM 的内存地图
│   └── common.cmake             # 编译器前缀到工具名的绑定
├── include/libestdx/
│   ├── gpio/gpio_base.hpp       # 概念层:GPIOOutputPin 这些 concept
│   ├── boards/stm32f1/          # 家族实现层:F1 的 GPIO 与 HAL 分片封装
│   └── device/                  # 器件层:LED、Button
├── examples/                    # 固件工程:01_blinky 到 04_button
│   └── 01_blinky/
│       ├── main.cpp             # 应用入口:HAL_Init → 时钟 → 翻转循环
│       ├── stm32f1xx_it.c       # 中断服务程序(SysTick_Handler 在这)
│       ├── syscalls.c           # newlib 运行时桩
│       └── renode.resc          #上一篇跑的 Renode 剧本
├── sim/stm32f1/bluepill.repl    # 板级描述(上一篇的"户口本")
└── third_party/STM32F1/         # CubeF1:HAL 与 CMSIS 的真身
```

对照着讲几个要点。`include/libestdx` 是库的三层骨架:概念层(`gpio_base.hpp`)只认标准库,家族实现层(`boards/stm32f1`)把 concept 落到 F1 的 HAL 上,器件层(`device`)再用它们拼出 LED、Button 这类常见器件。""库里的轮子只讲不造"那条立场,落进目录就是这三层,LED 站咱们再逐层拆。`examples/` 下的固件工程消费这套库:每个示例一个独立可执行目标,上一篇跑的 `01_blinky` 是里面最小的一个。HAL 的真身 `third_party/STM32F1` 在库自己肚子里,CMake 从这儿引用,同时把分片化的 hal_conf(本篇末尾细看)一并落地——具体怎么落,下一篇全是这件事。

## stm32f1xx_hal_conf.h:模板里的暗雷

ST 官方**不**提供现成的 `stm32f1xx_hal_conf.h`,只有 `stm32f1xx_hal_conf_template.h` 模板。您得复制一份改名再用。用 CubeMX(ST 官方的图形化配置工具,点选芯片和外设就能生成初始化代码)的朋友会被自动生成,咱们手写路线,手动来:

```bash
cp third_party/libestdx/third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_conf_template.h \
   stm32f1xx_hal_conf.h
```

这份文件里埋着四个坑,咱们挨个点名。头一个是开头的模块开关:一长排 `#define HAL_XXX_MODULE_ENABLED`,模板默认全开,编译时把所有 HAL 驱动都编进去,固件虚胖。LED 闪烁只需要四个:

```c
#define HAL_MODULE_ENABLED         // HAL 核心
#define HAL_GPIO_MODULE_ENABLED    // GPIO(控制 LED)
#define HAL_RCC_MODULE_ENABLED     // 时钟配置
#define HAL_CORTEX_MODULE_ENABLED  // Cortex-M3 内核函数
```

其余咱们全注释掉。配合链接时的垃圾回收,没用到的外设代码一段不留。

往下翻,如果编译器某天甩给您一句 `'LSI_VALUE' undeclared`,别慌,病根就在这排频率宏里。`HSE_VALUE`、`HSI_VALUE`、`LSI_VALUE` 这一组,HAL 的 RCC 模块靠它们计算时钟,写错一处,波特率跟着错,串口出来就是乱码;而 `LSI_VALUE` 在模板里是条件定义,最容易漏。Blue Pill 的参考值:

```c
#define HSE_VALUE    8000000U   // 8MHz 外部晶振
#define HSI_VALUE    8000000U   // 8MHz 内部高速振荡器
#define LSI_VALUE    40000U     // 40kHz 内部低速振荡器
#define LSE_VALUE    32768U     // 32.768kHz 外部低速晶振
```

文件尾部还藏着参数检查宏 `assert_param`,默认展开为空。如果哪天定义了 `USE_FULL_ASSERT`,断言失败会跳进 `assert_failed()`——这个函数得您自己实现,否则链接报 undefined。日常保持默认(空宏)即可,知道这开关在哪就行。

再往后,`USE_HAL_XXX_REGISTER_CALLBACKS` 这串宏控制 HAL 的"运行时回调注册"机制,默认 0(用弱符号回调:给函数标上 `__weak`,链接时只要有别处的同名定义,默认版自动让位——HAL 靠它给回调留默认空实现,想接管就写个同名函数)。咱们保持 0 就好,改成 1 会要求每个外设手工注册回调,复杂度不划算。

最后一条要单独说:`stm32f1xx_hal_conf.h` 必须出现在头文件搜索路径里,因为 HAL 的头文件用 `#include "stm32f1xx_hal_conf.h"`(引号形式)引用它。咱们把它放在工程根目录,再把工程根加进 include 路径,最省心。

咱们仓库里 libestdx 没有照抄"一整份大配置"的写法,而是拆成了分片:`hal/stm32f1xx_hal_conf.h` 只剩五行聚合,开关和宏按模块住进 `hal/gpio.hpp`、`hal/rcc.hpp`、`hal/cortex.hpp`、`hal/clock.hpp`(频率宏在这份里)、`hal/kernel.hpp`(HAL 核心开关和 assert_param 在这份里)。每个分片自己开自己的 `HAL_XXX_MODULE_ENABLED`,再统一先引 kernel 分片。这么拆的动机,和"哪些 HAL 的 .c 该进编译"直接挂钩,CMake 篇咱们看着实物讲。

下一篇咱们写构建系统,把工具链文件、HAL 库、链接脚本和固件工程串成一条能产出 `blinky` 的流水线——那里还有为什么不 glob 全量 HAL、nano/nosys 两个 specs、`__libc_init_array` 未定义(启动代码靠它调全局构造,就是上文启动文件干的那步)这几位老朋友等着。
