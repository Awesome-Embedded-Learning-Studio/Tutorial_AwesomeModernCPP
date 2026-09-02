---
title: "CMake 配置:从零构建 STM32 构建系统"
description: "把 HAL 库、启动文件、链接脚本和您的代码串成一条流水线;template 文件过滤、generator expression、nano/nosys specs,以及 flash 与 run_in_renode 双目标"
chapter: 0
order: 4
tags:
  - stm32f1
  - beginner
  - 入门
  - CMake
  - 交叉编译
difficulty: beginner
platform: stm32f1
reading_time_minutes: 16
prerequisites:
  - "项目结构:HAL 库的获取与目录搭建"
related:
  - "WSL2 USB 透传(想用实际板子再看)"
  - "调试:从 printf 到 GDB"
---

# CMake 配置:从零构建 STM32 构建系统

零件都备齐了,现在让 CMake 把它们串成流水线。第一次做这件事的人,光是让 CMake 理解"这是裸机 ARM 工程,别试图运行测试程序"就要花掉半个下午;笔者第一次的 CMakeLists.txt,是对着 CubeIDE 生成的 Makefile 一行行"翻译"出来的。这篇把配套工程 `0_start_our_tutorial/CMakeLists.txt` 从头到尾拆开,每一段都讲清楚为什么。

## 完整文件先睹为快

```cmake
cmake_minimum_required(VERSION 3.22)

# ── 工具链(必须在 project() 之前)──
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER       arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER     arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER     arm-none-eabi-gcc)
set(CMAKE_OBJCOPY          arm-none-eabi-objcopy)
set(CMAKE_SIZE             arm-none-eabi-size)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(stm32_demo C CXX ASM)
set(CMAKE_C_STANDARD   23)
set(CMAKE_CXX_STANDARD 23)

# ── HAL / CMSIS 路径 ──
set(STM32F1_ROOT ${CMAKE_SOURCE_DIR}/../../../third_party/STM32F1/Drivers)
set(CMSIS_TMPL   ${STM32F1_ROOT}/CMSIS/Device/ST/STM32F1xx/Source/Templates)

# ── 源文件 ──
file(GLOB HAL_SRC
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/*.c
)
list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")

add_compile_options(
    -mcpu=cortex-m3 -mthumb -O2 -Wall -Wextra
    -Wno-missing-field-initializers
    -ffunction-sections -fdata-sections
    -DUSE_HAL_DRIVER -DSTM32F103xB
)
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

set(SYSTEM_MOCK_SRC
    system/hal_mock.c
    system/syscall.c
)

add_executable(${PROJECT_NAME}.elf
    main.cpp
    ${SYSTEM_MOCK_SRC}
    ${CMSIS_TMPL}/system_stm32f1xx.c
    ${CMSIS_TMPL}/gcc/startup_stm32f103xb.s
    ${HAL_SRC}
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${STM32F1_ROOT}/CMSIS/Include
    ${STM32F1_ROOT}/CMSIS/Device/ST/STM32F1xx/Include
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Inc
)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m3 -mthumb
    -T${CMAKE_SOURCE_DIR}/STM32F103C8TX_FLASH.ld
    -nostartfiles
    -specs=nano.specs
    -specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map
)
```

后面还有 POST_BUILD(生成 .bin、打印 size)和三个自定义目标,咱们放到文末再讲。现在逐段拆开看。

## 工具链设置:为什么要写在 project() 之前

CMake 的交叉编译有一条必须遵守的顺序:工具链变量必须在 `project()` 之前设置,因为 project() 一执行,CMake 就拿着编译器去做平台探测了。`CMAKE_SYSTEM_NAME Generic` 告诉 CMake 目标没有操作系统(裸机);如果手滑写成 `Linux`,CMake 会去找 Linux 头文件,然后给您一整排红色报错。`CMAKE_SYSTEM_PROCESSOR ARM` 写的是目标处理器族,给 CMake 查找库和包时当筛选条件用,和性能没有关系。

接着是五个工具变量。三个编译器好理解:C 源交给 `arm-none-eabi-gcc`,C++ 交给 `arm-none-eabi-g++`,`.s` 启动文件也交给 gcc,所以 `CMAKE_ASM_COMPILER` 还是指它。`CMAKE_OBJCOPY` 和 `CMAKE_SIZE` 是登记给文末 POST_BUILD 用的:objcopy 从 ELF 提炼 `.bin`,size 打印体积,CMake 自己不碰这俩,咱们先把名字存好,后面用变量引用。

最能救命的是这一行:

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

默认情况下,CMake 配置项目时会编译一个小程序并**尝试运行**,以验证工具链正常。但 ARM 程序在 x86-64 开发机上根本跑不起来,咱们不加这行,configure 阶段就报 `try_compile` 失败。设成 `STATIC_LIBRARY` 后只编译不运行,问题消失。

`CMAKE_EXPORT_COMPILE_COMMANDS ON` 生成 `compile_commands.json`,clangd 全靠它才能看懂交叉编译的代码;您不开它,IDE 就满屏红线,第 7 篇专门讲。

工程大了以后,这段工具链设置建议抽成独立的 `toolchain.cmake` 文件,configure 时用 `-DCMAKE_TOOLCHAIN_FILE=` 传入。仓库里另一套工程(`code/stm32-tutorials/`)就是这么干的,好处是多个工程共享一份、不重复。入门阶段咱们先内联,够用。

文件第一行和 `project()` 这行,也值得咱们停一下。`cmake_minimum_required(VERSION 3.22)` 的 3.22 不是随便写的:它是 Ubuntu 22.04 仓库自带的版本号,写这个数,Ubuntu 用户 `apt` 装完 CMake 直接能配,不必再去官网下包。`project(stm32_demo C CXX ASM)` 声明三种语言,少了 `ASM`,CMake 就不认识 `.s` 启动文件;`stm32_demo` 这个名字会存进 `PROJECT_NAME`,后面目标名 `${PROJECT_NAME}.elf` 反复用它。紧跟的两行 `CMAKE_C_STANDARD/CXX_STANDARD 23` 向工具链请求 C++23,请求不等于全有,GCC 没实现的特性照样报错,但这套教程就按这个档位写,UART 站要用到的 `std::expected` 就是 C++23 的东西。

## 源文件收集:那个该死的 template 问题

```cmake
file(GLOB HAL_SRC ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/*.c)
list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")
```

第一行把 HAL 驱动的所有 `.c` 收进来,第二行把 `_template.c` 结尾的踢出去。为什么?HAL 库里有一批模板文件(`stm32f1xx_hal_msp_template.c` 之类),它们提供的是"给您抄的参考实现",不是拿来直接编译的。混进去的话,编译到一半报:

```text
multiple definition of 'HAL_MspInit'
```

正则里的 `\\.c` 要转义点号,不然 `.` 匹配任意字符。笔者第一次忘转义,连正经的 `stm32f1xx_hal.c` 都被误杀,链接器一口气甩出几百个 `undefined reference`。

两个路径变量咱们也交代一下:`STM32F1_ROOT` 从工程目录往上走三级,指向仓库根共享的 `third_party/STM32F1/Drivers`,上一篇"依赖共享一份、四个工程不重复占体积"说的就是这条相对路径;`CMSIS_TMPL` 是它下面一个目录的缩写,启动文件和 `system_stm32f1xx.c` 都住在那里。

启动文件和 `system_stm32f1xx.c` 是咱们单独点名加入的:前者上一篇讲过(`xb` 中容量);后者提供 `SystemInit()`,启动文件会调它做系统级初始化,漏了就 `undefined reference to SystemInit`。

`add_executable` 把五路源码拼成一个目标,目标名直接叫 `${PROJECT_NAME}.elf`——带 `.elf` 后缀是嵌入式圈的习惯,`cmake --build` 的目标名就是文件名,objcopy、OpenOCD、resc 引用起来都顺。五路分别是:咱们的 `main.cpp`、两个地基桩(`system/`)、CMSIS 的 `system_stm32f1xx.c` 加启动文件、整包 HAL 源。

咱们在 `target_include_directories` 里登记四个头文件目录,正好对应上一篇的三层架构加工程根:`CMSIS/Include` 和 `CMSIS/Device/.../Include` 是内核层与芯片具体化层,`STM32F1xx_HAL_Driver/Inc` 是 HAL 层,`${CMAKE_SOURCE_DIR}` 是工程根,`stm32f1xx_hal_conf.h` 就住在那儿,上一篇说过它必须能被引号形式的 `#include` 找到。

## system/:裸机环境的地基抹平

`system/hal_mock.c` 和 `system/syscall.c` 个头很小,但缺了它们,链接这一关咱们就过不去。

`syscall.c` 全文只有一个空函数:

```c
void _init() {
    // Do nothing, mock staff
}
```

它是给 newlib 的 C++ 构造机制用的桩。链接器默认会找 `_init`,裸机环境没人提供,咱们就靠这个空壳顶上。

`hal_mock.c` 里除了定义 `SystemClock_Config()`(HSI 内部振荡器二分频后乘 16,目标 64MHz 系统时钟,详细拆解留给时钟树篇),还有 `SysTick_Handler()`:SysTick 中断每毫秒触发一次,里面调 `HAL_IncTick()` 给 HAL 的毫秒计数器加一。`HAL_Delay()` 就靠这个计数器判断"够 500 毫秒了没"。没有这个中断处理函数,`HAL_Delay` 会永远等下去,程序看起来就是"卡死在第一次延时"——这个症状值得咱们记住,以后遇到了能少查半小时。

## 编译选项:generator expression 隔离 C++ 专属项

公共选项一视同仁。`-mcpu=cortex-m3` 告诉编译器目标核是谁:指令集、指令调度、软浮点 ABI 全按 M3 来;`-mthumb` 用 16 位 Thumb 指令集,代码更省,咱们这 64KB Flash 能省一点是一点;`-O2` 是发布档优化,调试时换 `-Og`(调试篇讲过为什么);`-Wall -Wextra` 是两组常用告警,常开,图的就是替咱们盯哑弹。`-ffunction-sections` 配 `-fdata-sections` 把每个函数、每个数据对象放进独立段,配合链接期 `--gc-sections`,没用到的代码整段丢弃。

唯一被点名关掉的告警是 `-Wno-missing-field-initializers`:HAL 源码和咱们自己的 `hal_mock.c` 里都有 `= {0}` 这种只初始化第一个字段的写法,`-Wextra` 会为它整屏告警。咱们只关这一条,别图省事把整个 `-Wextra` 关了,那等于把烟雾报警器的电池抠了。

`-fno-exceptions` 和 `-fno-rtti` 是 C++ 专属选项,咱们得用 generator expression 把它们包住:

```cmake
$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
```

这个 `$<...>` 语法的意思是"仅当编译 C++ 文件时应用"。直接扔进公共选项的话,HAL 的几十个 C 文件每个都报一串 `'-fno-rtti' is not valid for C` 警告,真正的错误被淹没在噪声里。咱们为什么在裸机环境禁异常和 RTTI?两者都需要运行时支撑(展开表、类型信息),吃 Flash 吃 RAM,而嵌入式错误处理有更轻的路子:`std::expected`,UART 站会正式讲。

宏定义 `-DSTM32F103xB`(中容量密度代码,上一篇讲过)和 `-DUSE_HAL_DRIVER`(告诉头文件走 HAL 而不是 LL),这两个咱们必须配到位。

## 链接选项:nano 与 nosys

```cmake
-nostartfiles
-specs=nano.specs
-specs=nosys.specs
-Wl,--gc-sections
-T${CMAKE_SOURCE_DIR}/STM32F103C8TX_FLASH.ld
```

先说 `-specs=xxx.specs` 这对。specs 文件是 GCC 工具链自带的一个文本小脚本,内容是"驱动程序最终调链接器时,该拼上哪些库、哪些对象"的规则;`-specs=` 就是让 gcc 在默认规则之外再加载一份。空口无凭,咱们真跑看效果:给 gcc 加 `-###`(只打印要执行的命令,不真链接),把驱动拼出来的库单摘出来对比:

```text
默认:                -lc -lgcc -lm
-specs=nano.specs:   -lc_nano -lgcc -lm
-specs=nosys.specs:  -lc -lgcc -lm -lnosys
两个都带:            -lc_nano -lgcc -lm -lnosys
```

一行 specs 就改了整张库单。`nano.specs` 把 `libc` 整个换成 `libc_nano`:精简版 C 库,printf 一族默认砍掉浮点支持这类重量级功能,固件能小好几个 KB。`nosys.specs` 则是往库里添一个 `-lnosys`——它全文只有十几行,咱们直接看核心:

```text
*nosys_libgloss:
-lnosys

*nosys_libc:
%{!specs=nano.specs:-lc} %{specs=nano.specs:-lc_nano}

*link_gcc_c_sequence:
%(nosys_link_gcc_c_sequence) --start-group %G %(nosys_libc) %(nosys_libgloss) --end-group
```

它把 `-lnosys` 塞进链接序列,顺手兼容了 nano(第二段的意思是:如果同时带了 nano.specs,libc 就用 `-lc_nano`)。`libnosys` 里装的是 `write()`、`sbrk()`、`read()` 这些系统调用的空桩:符号存在,链接不再报 undefined,但被调用时只会返回失败。裸机没有操作系统,这些调用本来也没人能真实现,一个"占位不干活"的桩刚好够用;真要 printf 出点东西,咱们得自己写 `_write` 把字符送进外设,UART 站做这件事。

`-nostartfiles` 管的是另一摊:不让 gcc 链入标准库自带的启动文件(crt0 那一套),因为咱们有自己的启动文件。它和两个 specs 一个管启动、一个管库,互不替代,裸机工程三个都得要。`-T` 指定链接脚本,内存的"户口本"。

链接选项里再写一遍 `-mcpu`/`-mthumb` 不是重复劳动:链接器要按它们挑 libgcc 的 ARM 变体、决定重定位方式,编译期那套旗标传不到链接这一步。最后那行 `-Wl,-Map=...` 让链接器顺手吐一张地图文件:每个符号落在哪个地址、哪些段被 `--gc-sections` 删掉了,全在里面;哪天固件体积不对劲,翻 `.map` 文件是咱们的第一反应。

## 链接脚本:内存地图

配套工程的 `STM32F103C8TX_FLASH.ld`,核心就是这块内存定义:

```text
MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
}
```

`xrw`/`rx` 是权限(RAM 可读写执行,Flash 只读可执行),ORIGIN 是起始地址,LENGTH 是大小。**C8T6 是 64KB Flash、20KB SRAM**,这个数咱们要是抄错了(网上抄来的脚本经常写 128K,那是 CB 系列的),程序小没感觉,等固件长过 64K 就神秘跑飞。

SECTIONS 这边咱们看几个关键点。向量表段用了 `KEEP`:

```text
.isr_vector :
{
  KEEP(*(.isr_vector))
} >FLASH
```

不加 KEEP 的话,链接器认为向量表"没人引用"(代码里确实不直接访问它),`--gc-sections` 一开心就把它当垃圾收了——芯片复位找不到向量表,程序直接跑飞。笔者第一次没加 KEEP,烧进去芯片毫无反应,排查了一整晚。

`.data` 段的 `>RAM AT >FLASH` 是双地址魔法:变量运行时在 RAM,初始值存在 Flash,启动代码负责把初始值搬过去。咱们要是漏写 `AT > FLASH`,全局变量的初值断电就没了,上电全是随机数。

C++ 程序员最该认识的是这两段:

```text
.init_array :
{
  PROVIDE_HIDDEN(__init_array_start = .);
  KEEP(*(SORT(.init_array.*)))
  KEEP(*(.init_array*))
  PROVIDE_HIDDEN(__init_array_end = .);
} >FLASH
```

**全局对象的构造函数指针就登记在这里**。启动代码扫一遍 `__init_array_start` 到 `__init_array_end`,逐个调用,然后才进 `main()`。这意味着:您在全局作用域写一个带构造函数的对象,它的构造发生在 `main` 之前;如果构造函数碰了还没初始化的外设,炸得无声无息。启动链条的完整拆解放在 LED 站的"地砖下面"篇,这里先埋个锚。

## 构建之后的去路

POST_BUILD 用 objcopy 从 ELF 提炼纯二进制(`.bin`,烧录格式)并打印体积——这份输出咱们得会读:`text + data` 就是 Flash 占用,`bss` 是 RAM 占用。再往下是一组自定义目标,对应两条验证路线:

```cmake
# 实际板子路线
add_custom_target(flash
    COMMAND openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
            -c "program ${PROJECT_NAME}.bin verify reset exit 0x08000000"
    DEPENDS ${PROJECT_NAME}.elf)
add_custom_target(erase ...)

# 模拟器路线
find_program(RENODE_BIN renode)
if(RENODE_BIN)
    add_custom_target(run_in_renode
        COMMAND ${RENODE_BIN} --console --disable-xwt
                ${CMAKE_SOURCE_DIR}/renode.resc -e quit
        DEPENDS ${PROJECT_NAME}.elf)
endif()
```

`flash`/`erase` 走 OpenOCD 烧录实际板子,细节在讲 WSL2 USB 透传的那篇。`run_in_renode` 就是上一篇那条一条龙命令。Renode 目标用 `find_program` 探测,您装了它才生成——没装 Renode 的环境(比如某些 CI 机器)构建依然完整可用,不会因为缺模拟器而 configure 失败。

## 常见编译错误速查

`startup_stm32f103x8.s: No such file or directory`:启动文件名写错,C8T6 用 `xb`。

`'LSI_VALUE' undeclared`:`stm32f1xx_hal_conf.h` 缺失或时钟宏没配,配法在上一篇频率宏那一节,咱们翻回去补。

`multiple definition of 'HAL_MspInit'`:`_template.c` 混进编译了,咱们去查 FILTER 正则。

`undefined reference to '__libc_init_array'`:链接顺序或 specs 问题,`nano.specs`/`nosys.specs` 得都在;`_init` 桩(syscall.c)有没有被编入,您也一并看一眼。

`ignoring option '-fno-rtti' for C`:您八成漏了给 C++ 选项包 generator expression。

到这里,咱们敲 `cmake -B build && cmake --build build` 应该就能稳定产出 `stm32_demo.elf` 和 `.bin`,`--target run_in_renode` 一条命令看到灯闪。环境四章的最后一公里是 IDE 体验和调试,分别在 clangd 篇和调试篇。
