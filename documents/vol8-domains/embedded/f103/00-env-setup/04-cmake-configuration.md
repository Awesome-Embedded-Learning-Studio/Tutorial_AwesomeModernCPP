---
title: "CMake 配置:从零构建 STM32 构建系统"
description: "把工具链文件、HAL 静态库、链接脚本和固件工程串成一条流水线;手选 HAL 源、按语言分旗标、nano/nosys specs,以及 flash 与 sim 双目标"
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

零件都备齐了,现在让 CMake 把它们串成流水线。第一次做这件事的人,光是让 CMake 理解"这是裸机 ARM 工程,别试图运行测试程序"就要花掉半个下午;笔者第一次的 CMakeLists.txt,是对着 CubeIDE(ST 官方的图形化 IDE)生成的 Makefile 一行行"翻译"出来的。这篇把 libestdx 的构建系统从头到尾拆开,每一段都讲清楚为什么。它一共四个角色:根 `CMakeLists.txt` 只做编排,`cmake/arch/` 里躺着工具链文件和链接脚本,`include/libestdx/boards/stm32f1/` 把 HAL 编成静态库,`examples/` 下每个固件是一个独立工程。咱们按固件产出的顺序挨个拆。

## 工具链文件:交叉编译的"户口"

上一篇说过,libestdx 走的是独立 toolchain 文件范式,这篇咱们把文件本体 `cmake/arch/stm32f103c8t6.cmake` 从头到尾过一遍,全文十九行:

```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m3)

set(CROSS_COMPILE arm-none-eabi-)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")

set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${MCU_FLAGS} -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 给 clangd / IDE 用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

咱们先看 `common.cmake`,它干的是"前缀到工具名"的绑定:`set(CROSS_COMPILE arm-none-eabi-)` 之后,`gcc`、`g++`、`objcopy`、`size` 全从这一个前缀拼出来。想换工具链版本或者换厂商前缀,改一处就行。它还有句注释值得原样抄来:toolchain 文件里定位同仓库的其他文件,必须用 `CMAKE_CURRENT_LIST_DIR`,不能写相对路径,因为 toolchain 文件的执行目录不是它自己所在目录,裸相对路径指哪去了说不准。

CMake 交叉编译有一条必须遵守的顺序:工具链变量必须在 `project()` 之前就位,因为 project() 一执行,CMake 就拿着编译器去做平台探测了。文件范式天然满足这条——configure 时用 `-DCMAKE_TOOLCHAIN_FILE=` 传入的文件,在读任何 CMakeLists 之前就被加载,这也是它比"把工具链 set 写进 CMakeLists"更稳的原因。`CMAKE_SYSTEM_NAME Generic` 告诉 CMake 目标没有操作系统(裸机);手滑写成 `Linux`,CMake 会去找 Linux 头文件,然后给您一整排红色报错。`CMAKE_SYSTEM_PROCESSOR` 写目标处理器族,给 CMake 查找库和包时当筛选条件用,和性能没有关系。

最能救命的是这一行:

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

默认情况下,CMake 配置项目时会编译一个小程序并**尝试运行**,以验证工具链正常。但 ARM 程序在 x86-64 开发机上根本跑不起来,咱们不加这行,configure 阶段就报 `try_compile` 失败。设成 `STATIC_LIBRARY` 后只编译不运行,问题消失。

`CMAKE_EXPORT_COMPILE_COMMANDS ON` 生成 `compile_commands.json`,clangd 全靠它才能看懂交叉编译的代码;您不开它,IDE 就满屏红线,第 7 篇专门讲。

## 旗标怎么分家:C 一套,C++ 一套

咱们先看 `MCU_FLAGS`,它是公共底仓:`-mcpu=cortex-m3` 告诉编译器目标核是谁,指令集、指令调度、软浮点 ABI 全按 M3 来;`-mthumb` 用 16 位 Thumb 指令集,代码更省,64KB Flash 能省一点是一点。底仓之上,三种语言各领各的 `*_FLAGS_INIT`:C 就用底仓,C++ 多三个 `-fno-*`,汇编再加 `-x assembler-with-cpp`(让 cpp 预处理器处理 `.s` 里的宏,启动文件里用得上)。

为什么 `-fno-exceptions`、`-fno-rtti` 只给 C++?因为它们是 C++ 专属选项,要是塞进公共旗标,HAL 的几十个 C 文件每个都报一串 `'-fno-rtti' is not valid for C` 警告,真正的错误被淹没在噪声里。咱们按语言分开的 `FLAGS_INIT` 是一种解法;另一种是在目标上用 generator expression(`$<$<COMPILE_LANGUAGE:CXX>:...>`,意思是"仅当编译 C++ 文件时应用"),效果等价,写在哪看工程组织,库这边选了前者,一处定义全库受益。

裸机为什么禁这三个,咱们数一数:异常展开表、RTTI 类型信息、线程安全的局部 static 初始化,个个都要运行时支撑,吃 Flash 吃 RAM;而嵌入式错误处理有更轻的路子,`std::expected`,UART 站正式讲。`-fno-threadsafe-statics` 关掉的是"局部 static 初始化的隐式加锁",裸机没有多线程支撑,这层锁本来就没法生效,关掉省代码。

优化档工具链文件也没锁:GCC 默认 `-O0`,上一篇看到的 7.5KB 固件里就有它的份。您要压体积就加 `-Os`,要调试舒服就配 `-Og`,这一档按场景挑,后头各站会给出各自的取舍。

## HAL 静态库:手选六个 .c,而不是 glob 一把梭

咱们再看 `include/libestdx/boards/stm32f1/CMakeLists.txt`,它把官方 HAL、CMSIS 设备层连同启动文件编成一个 `hal` 静态库:

```cmake
set(STM32F1_ROOT ${PROJECT_SOURCE_DIR}/third_party/STM32F1/Drivers)
set(CMSIS_TMPL   ${STM32F1_ROOT}/CMSIS/Device/ST/STM32F1xx/Source/Templates)

# 源文件与 hal/stm32f1xx_hal_conf.h 引入的分片一一对应:
# hal_conf 加一个分片,这里加对应 .c,不要 glob 全量(会编出一堆空 obj)。
set(HAL_SRC
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c        # kernel.hpp
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c # cortex.hpp
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c    # rcc.hpp
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c   # gpio.hpp
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
)

add_library(hal STATIC
    ${CMSIS_TMPL}/gcc/startup_stm32f103xb.s
    ${CMSIS_TMPL}/system_stm32f1xx.c
    ${HAL_SRC}
)

target_include_directories(hal PUBLIC
    ${STM32F1_ROOT}/CMSIS/Include
    ${STM32F1_ROOT}/CMSIS/Device/ST/STM32F1xx/Include
    ${STM32F1_ROOT}/STM32F1xx_HAL_Driver/Inc
    ${PROJECT_SOURCE_DIR}/include   # libestdx/xxx.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hal # stm32f1xx_hal_conf.h 随分片住 hal/
)

target_compile_definitions(hal PUBLIC
    STM32F103xB
    USE_HAL_DRIVER
)
```

这里最能体现上一篇 hal_conf 分片的用意:源文件清单和配置分片一一对应,`hal_conf` 里开了哪个模块,这边就编哪个 `.c`,GPIO 用到就这六个,一个不多。要是图省事 `file(GLOB)` 把 `Src/*.c` 全收呢?两个后果。一个是注释里写的:HAL 一两百个源文件每个都编出个几乎全空的 obj,构建时间白烧;另一个更疼,GLOB 会把 `stm32f1xx_hal_msp_template.c` 这类模板文件也卷进来,它们提供的是"给您抄的参考实现",不是拿来直接编译的,混进去链接期报:

```text
multiple definition of 'HAL_MspInit'
```

真要 glob,得跟一个 `list(FILTER ... EXCLUDE REGEX ".*_template\\.c$")` 把模板踢出去,正则里 `\\.c` 还得转义点号,笔者第一次忘转义,连正经的 `stm32f1xx_hal.c` 都被误杀,链接器一口气甩出几百个 `undefined reference`。手选清单没这些戏,代价是每开一个新外设要记得两边同步,这个约束正好逼着咱们想清楚"到底用了哪些轮子"。

启动文件和 `system_stm32f1xx.c` 是咱们单独点名加进来的:前者上一篇讲过(`xb` 中容量),后者提供 `SystemInit()`,启动文件会调它做系统级初始化,漏了就 `undefined reference to SystemInit`。

`STM32F1_ROOT` 指的是库自己肚子里的 CubeF1,就是咱们上一篇目录树里那位 `third_party/STM32F1` 房客,依赖链的最后一跳在这儿落地,库内所有 examples 共享这一份,不重复占体积。咱们接着看 `target_include_directories` 登记的四个目录,正好对应上一篇的三层架构加分片目录:`CMSIS/Include` 和 `CMSIS/Device/.../Include` 是内核层与芯片具体化层,`STM32F1xx_HAL_Driver/Inc` 是 HAL 层,`hal/` 里住着分片化的 `stm32f1xx_hal_conf.h`,上一篇说过它必须能被引号形式的 `#include` 找到。两个宏也在这落位:`STM32F103xB`(中容量密度代码)和 `USE_HAL_DRIVER`(走 HAL 而不是 LL——LL 是 ST 另一套更贴寄存器的薄 API,咱们全程只走 HAL),`PUBLIC` 让链接 hal 的固件自动继承,不用每个工程再写一遍。

## 固件工程:桩为什么住在固件这边

`examples/01_blinky/CMakeLists.txt`,一个最小固件的全貌:

```cmake
# 运行时桩(SysTick handler、newlib _init/_fini)直接编进固件:
# 中断向量表和 newlib 对这些符号的引用在链接后期才出现,放库里会因
# "没人引用就不拉"被丢,owning 它们的是固件工程,不是库。
add_executable(blinky
    main.cpp
    stm32f1xx_it.c # 中断服务程序:哪些 handler 存在由这份固件决定
    syscalls.c # newlib 桩
)

target_link_libraries(blinky PRIVATE hal)

target_link_options(blinky PRIVATE
    -T${CMAKE_SOURCE_DIR}/cmake/arch/stm32f103c8t6.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/blinky.map
)

add_custom_command(TARGET blinky POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:blinky> blinky.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:blinky>
    COMMENT "objcopy -> bin, report size"
)
```

开头那段注释值得咱们单独讲,它回答了一个反直觉的问题:SysTick 的中断处理、newlib 的桩,这些"人人都需要"的东西,为什么不放进 hal 库大家共享?因为静态库的拉入规则是"有符号被引用才链接对应的成员",而中断向量表、`__libc_init_array` 对这些符号的引用,要到链接后期才浮现——放库里,`--gc-sections` 一开心就把"没人引用"的它们当垃圾收了,烧上去芯片毫无反应。所以桩的归属权在固件工程,每个示例自带自己那份。

`stm32f1xx_it.c` 全文就一个 `SysTick_Handler`,里面调 `HAL_IncTick()`。`HAL_Init()` 把 SysTick 配成 1ms 一次中断,`HAL_Delay`/`HAL_GetTick` 全指着这个计数器喂数,咱们不写这个 handler,所有阻塞 API 永远卡死,症状是"程序卡在第一次延时",值得咱们记住,遇到了能少查半小时。`syscalls.c` 两个空函数 `_init`/`_fini` 是给 newlib 的构造机制用的桩:`-nostartfiles` 丢了 crt(C runtime,gcc 默认自动链入的那组启动文件,crt0、crtbegin 都属这族)默认的这对,而 `__libc_init_array`(全局构造的发起者)会调用它们,不给桩直接链接失败。以后 `_sbrk`、`_write` 之类的桩也放这里,UART 站的 printf 就靠其中的 `_write`。

您翻 `main.cpp` 还会看到时钟配置 `SystemClock_Config`,它没有独立文件,就写在 `main.cpp` 里:HSI 8M 二分频进 PLL 倍乘 16,目标 64MHz 系统时钟,详细拆解留给时钟树篇。

## 链接选项:nano 与 nosys

咱们接着看链接。公共旗标住在工具链文件的 `CMAKE_EXE_LINKER_FLAGS_INIT` 里:

```cmake
-nostartfiles
--specs=nano.specs
--specs=nosys.specs
-Wl,--gc-sections
```

先说 `-specs=xxx.specs` 这对。specs 文件是 GCC 工具链自带的一个文本小脚本,内容是"驱动程序最终调链接器时,该拼上哪些库、哪些对象"的规则;`-specs=` 就是让 gcc 在默认规则之外再加载一份。空口无凭,咱们真跑看效果:给 gcc 加 `-###`(只打印要执行的命令,不真链接),把驱动拼出来的库单摘出来对比:

```text
默认:                -lc -lgcc -lm
-specs=nano.specs:   -lc_nano -lgcc -lm
-specs=nosys.specs:  -lc -lgcc -lm -lnosys
两个都带:            -lc_nano -lgcc -lm -lnosys
```

一行 specs 就改了整张库单。`nano.specs` 把 `libc` 整个换成 `libc_nano`:精简版 C 库,printf 一族默认砍掉浮点支持这类重量级功能,固件能小好几个 KB。`nosys.specs` 则是往库里添一个 `-lnosys`——它全文只有十几行,把 `-lnosys` 塞进链接序列,顺手兼容了 nano。`libnosys` 里装的是 `write()`、`sbrk()`、`read()` 这些系统调用的空桩:符号存在,链接不再报 undefined,但被调用时只会返回失败。裸机没有操作系统,这些调用本来也没人能真实现,一个"占位不干活"的桩刚好够用;真要 printf 出点东西,咱们得自己写 `_write` 把字符送进外设,UART 站做这件事。

`-nostartfiles` 管的是另一摊:不让 gcc 链入标准库自带的启动文件(crt0 那一套),因为咱们有自己的启动文件。它和两个 specs 一个管启动、一个管库,互不替代,裸机工程三个都得要。

链接选项里再写一遍 `-mcpu`/`-mthumb`,咱们别当成重复劳动:链接器要按它们挑 libgcc(GCC 自带的辅助运行时库,除法、软浮点这类编译器替您生成的帮助函数住在里面)的 ARM 变体、决定重定位方式,编译期那套旗标传不到链接这一步。`--gc-sections` 配合编译期的 `-ffunction-sections -fdata-sections`……等等,这两个编译旗标工具链文件里并没有写,`--gc-sections` 还能干活吗?能,只是粒度变粗:每个 `.o` 按段粒度被收走,函数级裁剪享受不到。库现在是"手选源文件"的路线,粗粒度够用,这就是取舍,真到挤 Flash 的时候把两个旗标补进 `MCU_FLAGS`,裁剪立刻细到函数。

最后,每个固件自己在 `target_link_options` 里补两件私事:`-T` 指定链接脚本(内存的"户口本",下一节),`-Wl,-Map=...` 让链接器顺手吐一张地图文件——每个符号落在哪个地址、哪些段被 `--gc-sections` 删掉了,全在里面;哪天固件体积不对劲,翻 `.map` 文件是咱们的第一反应。

## 链接脚本:内存地图

`cmake/arch/stm32f103c8t6.ld`,核心是这块内存定义:

```text
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
```

咱们逐字段看:`rx`/`rwx` 是权限(Flash 只读可执行,RAM 可读写),ORIGIN 是起始地址,LENGTH 是大小。**C8T6 是 64KB Flash、20KB SRAM**,这个数要是抄错了(网上抄来的脚本经常写 128K,那是 CB 系列的),程序小没感觉,等固件长过 64K 就神秘跑飞。脚本开头那句注释把态度也写明白了:C8T6 名义 64K,民间实测一般能多写一点,但别依赖——镜像超了 64K,让链接器在这时候报错,总好过烧录时默默翻车。

SECTIONS 这边,咱们看几个关键点。向量表段用了 `KEEP`:

```text
.isr_vector :
{
  KEEP(*(.isr_vector))
} > FLASH
```

不加 KEEP 的话,链接器认为向量表"没人引用"(代码里确实不直接访问它),`--gc-sections` 一开心就把它当垃圾收了——芯片复位找不到向量表,程序直接跑飞。笔者第一次没加 KEEP,烧进去芯片毫无反应,排查了一整晚。

咱们看 `.data` 段,`> RAM AT > FLASH` 是双地址魔法:变量运行时在 RAM,初始值存在 Flash,启动代码负责把初始值搬过去,装载地址交给 `_sidata = LOADADDR(.data)` 记着。`_sdata`/`_edata`/`_sbss`/`_ebss` 这排符号同理,启动文件靠这些**名字**找段,链接脚本和启动文件是一对签了合同的搭档,谁改名谁负责。

C++ 程序员最该认识的是这两段:

```text
.init_array :
{
  PROVIDE_HIDDEN(__init_array_start = .);
  KEEP(*(SORT(.init_array.*)))
  KEEP(*(.init_array*))
  PROVIDE_HIDDEN(__init_array_end = .);
} > FLASH
```

**全局对象的构造函数指针就登记在这里**(前面还有一段同构的 `.preinit_array`,后面有 `.fini_array`,全带 KEEP,少了哪个构造析构就丢)。启动代码扫一遍 `__init_array_start` 到 `__init_array_end`,逐个调用,然后才进 `main()`。这意味着:您在全局作用域写一个带构造函数的对象,它的构造发生在 `main` 之前;如果构造函数碰了还没初始化的外设,炸得无声无息。启动链条的完整拆解放在 LED 站的"地砖下面"篇,这里先埋个锚。

## 构建之后的去路

POST_BUILD 用 objcopy 从 ELF 提炼纯二进制(`.bin`,烧录格式)并打印体积,这份输出咱们得会读:`text + data` 就是 Flash 占用,`bss` 是 RAM 占用。再往下是三个自定义目标,对应两条验证路线:

```cmake
add_custom_target(sim
    DEPENDS blinky
    COMMAND renode --console --disable-xwt -e "include @${CMAKE_SOURCE_DIR}/examples/01_blinky/renode.resc"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
)
```

`sim` 就是咱们上一篇跑的那条一条龙命令:起无头 Renode,include 那份 resc,`WORKING_DIRECTORY` 固定在仓库根(`@` 路径按它解析,上一篇讲过),`USES_TERMINAL` 让它占住终端跟 monitor 交互。这个目标没做 `find_program` 探测,Renode 没装的机器上敲它会失败,但只构建 `blinky` 固件完全不受影响,目标各自独立,想更讲究可以拿 `find_program(RENODE_BIN renode)` 包一层,没装就不生成这个目标。

`flash`/`erase` 替咱们走 OpenOCD 烧录实际板子(`interface/stlink.cfg` 管探针侧,`target/stm32f1x.cfg` 管芯片侧,program 到 0x08000000),细节在讲 WSL2 USB 透传的那篇。

## 常见编译错误速查

`startup_stm32f103x8.s: No such file or directory`:启动文件名写错,C8T6 用 `xb`。

`'LSI_VALUE' undeclared`:hal_conf 缺失或时钟宏没配,您要是手写大配置,翻回上一篇频率宏那节,配法在那;用库的话检查 `hal/clock.hpp` 分片在不在引用里。

`multiple definition of 'HAL_MspInit'`:`_template.c` 混进编译了,十有八九是您刚把 GLOB 打开,去查 FILTER 正则,或者干脆学库这边手选源文件。

`undefined reference to '__libc_init_array'`:链接顺序或 specs 问题,`nano.specs`/`nosys.specs` 得都在;`_init` 桩(`syscalls.c`)有没有被编入,您也一并看一眼。

`ignoring option '-fno-rtti' for C`:您八成把 C++ 专属旗标漏进了 C 的旗标组,分语言的 `FLAGS_INIT` 写岔了,或者 generator expression 忘包了。

到这里,咱们 `cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake` 再 `cmake --build build`,就能稳定产出 `blinky` 和 `.bin`,`--target sim` 一条命令看到灯闪。环境篇的最后一公里是 IDE 体验和调试,分别在 clangd 篇和调试篇。
