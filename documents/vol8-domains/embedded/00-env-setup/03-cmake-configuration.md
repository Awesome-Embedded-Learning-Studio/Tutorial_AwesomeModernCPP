# CMake 配置篇 —— 从零构建 STM32 构建系统

我现在正盯着屏幕上的 CMakeLists.txt，手里的咖啡已经凉了。如果你跟着前两篇文章一路折腾过来，现在应该已经有了交叉编译工具链，也把 STM32 的固件库下载好了。但真正的问题才刚刚开始：怎么让这一切东西乖乖地编译链接成一个能烧进芯片的 .bin 文件？我第一次做这件事的时候，光是让 CMake 理解"这是一个裸机 ARM 项目，不要尝试运行测试程序"就花了半个下午。今天我们就来把这个构建系统从头到尾理清楚。

## 先看完整的 CMakeLists.txt

不废话，先把完整的配置放出来，我们再逐段拆解。这个文件放在项目根目录下，和 build.sh 在同一个位置：

```cmake
cmake_minimum_required(VERSION 3.20)

project(STM32F103C8T6_Project C CXX ASM)

# ========== 交叉编译设置 ==========
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

# 指定交叉编译工具链前缀
set(CROSS_COMPILE arm-none-eabi-)
set(CMAKE_C_COMPILER ${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
set(CMAKE_ASM_COMPILER ${CROSS_COMPILE}gcc)
set(CMAKE_OBJCOPY ${CROSS_COMPILE}objcopy)
set(CMAKE_SIZE ${CROSS_COMPILE}size)

# 防止 CMake 尝试运行测试程序（裸机环境无法运行）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 导出 compile_commands.json 给 clangd/VSCode 用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ========== 项目路径设置 ==========
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(STM32_HAL_ROOT ${PROJECT_ROOT}/third_party/STM32F1/Drivers)
set(STM32_CMSIS_ROOT ${STM32_HAL_ROOT}/CMSIS)
set(STM32_HAL_DRIVER_ROOT ${STM32_HAL_ROOT}/STM32F1xx_HAL_Driver)

# ========== 源文件收集 ==========
# 启动文件
file(GLOB STARTUP_SRC
    ${STM32_CMSIS_ROOT}/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s
)

# system_stm32f1xx.c（系统初始化，包含 SystemInit 函数）
list(APPEND STARTUP_SRC
    ${STM32_CMSIS_ROOT}/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c
)

# HAL 库源文件（全量加入，稍后排除 template 文件）
file(GLOB HAL_SRC
    ${STM32_HAL_DRIVER_ROOT}/Src/*.c
)

# 排除所有 _template.c 文件（会导致 multiple definition 错误）
list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")

# 用户代码（目前先放一个占位文件）
set(USER_SRC
    ${PROJECT_ROOT}/src/main.cpp
)

# ========== 编译选项（公共部分） ==========
add_compile_options(
    -mcpu=cortex-m3          # STM32F103 的核心是 Cortex-M3
    -mthumb                  # 使用 Thumb 指令集（更省空间）
    -O2                      # 优化级别
    -g3                      # 生成详细的调试信息
    -Wall                    # 开启所有警告
    -Wextra                  # 开启额外警告
    -ffunction-sections      # 每个函数放一个段（便于链接时 GC）
    -fdata-sections          # 每个数据对象放一个段
)

# ========== 编译选项（语言特定）==========
# 使用 generator expression 区分 C 和 C++ 选项
add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-std=c11>"
    "$<$<COMPILE_LANGUAGE:CXX>:-std=c++17>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"  # 裸机环境没有异常支持
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"        # 不需要 RTTI
)

# ========== 宏定义 ==========
add_definitions(
    -DSTM32F103xB            # 芯片型号（很重要！）
    -DUSE_HAL_DRIVER         # 使用 HAL 库
    -DHSE_VALUE=8000000      # 外部晶振频率（8MHz）
)

# ========== 包含路径 ==========
include_directories(
    ${STM32_CMSIS_ROOT}/Include
    ${STM32_CMSIS_ROOT}/Device/ST/STM32F1xx/Include
    ${STM32_HAL_DRIVER_ROOT}/Inc
    ${PROJECT_ROOT}/include
)

# ========== 链接选项 ==========
add_link_options(
    -mcpu=cortex-m3
    -mthumb
    -nostartfiles            # 不使用标准库的启动文件
    -specs=nano.specs        # 使用 newlib-nano（精简版 C 库）
    -specs=nosys.specs       # 不提供系统调用实现（我们需要自己提供）
    -Wl,--gc-sections        # 链接时删除未使用的段
    -Wl,-Map=${CMAKE_BINARY_DIR}/output.map  # 生成 map 文件
    -T${PROJECT_ROOT}/ld/STM32F103XB_FLASH.ld  # 指定链接脚本
)

# ========== 可执行文件 ==========
add_executable(${PROJECT_NAME}
    ${STARTUP_SRC}
    ${HAL_SRC}
    ${USER_SRC}
)

# ========== 后处理步骤 ==========
# 生成 .bin 文件
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Generating ${PROJECT_NAME}.bin"
)

# 显示固件大小信息
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Firmware size:"
)

# ========== 自定义目标 ==========
# 烧录目标（调用 flash.sh）
add_custom_target(flash
    COMMAND ${PROJECT_ROOT}/scripts/flash.sh ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing firmware to STM32..."
)

# 擦除目标
add_custom_target(erase
    COMMAND ${PROJECT_ROOT}/scripts/erase.sh
    COMMENT "Erasing STM32 flash..."
)
```

好了，我知道这个文件看起来有点吓人。我第一次写的时候也是对着 STM32CubeIDE 生成的 Makefile 一行一行"翻译"过来的。我们把它拆开来看，你会发现每个部分都有它存在的道理。

## 交叉编译基础设置

最前面这几行是 CMake 交叉编译的"标准姿势"：

```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)
```

`CMAKE_SYSTEM_NAME` 设置为 `Generic` 是告诉 CMake：这不是一个 Linux/Windows/macOS 程序，而是一个裸机环境。如果你把它设成 `Linux`，CMake 会尝试去找 Linux 头文件，然后你就得一整排红色波浪线等着你。

`CMAKE_SYSTEM_PROCESSOR = ARM` 主要是给一些检测 CPU 架构的脚本看的，我们的场景下不设也行，但设上总没错。

接下来是指定工具链。注意这里的 `${CROSS_COMPILE}` 前缀，加上 `arm-none-eabi-` 之后，CMake 会自动推导出完整的工具链路径。如果你用 `where` 或 `which` 命令能看到 `arm-none-eabi-gcc`，那这里就能工作。

最关键的是这一行：

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

这个设置救了我一命。默认情况下，CMake 在配置项目时会编译一个小程序并尝试运行它，用来测试工具链是否正常工作。但问题是：我们编译的是 ARM 程序，在 x86_64 的开发机上根本跑不起来！如果不加这一行，CMake 会报 `try_compile` 失败的错误。把它设成 `STATIC_LIBRARY` 后，CMake 只会编译测试程序但不尝试链接运行，问题就解决了。

最后一行的 `CMAKE_EXPORT_COMPILE_COMMANDS` 虽然不是必需的，但强烈建议开启。它会生成一个 `compile_commands.json` 文件，clangd 和 VSCode 的 C++ 插件会读取这个文件来获取正确的编译选项。没有它，你的 IDE 会找不到 STM32 的头文件，所有 `HAL_GPIO_WritePin` 这种调用都会被标成"未定义符号"。

## 源文件收集 —— 那个该死的 template 问题

接下来我们来把所有需要的源文件收集起来。首先是启动文件：

```cmake
file(GLOB STARTUP_SRC
    ${STM32_CMSIS_ROOT}/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s
)
```

注意这里的文件名：`startup_stm32f103xb.s`。如果你用 Blue Pill，那芯片型号是 STM32F103C8T6，对应的启动文件就是 `xb` 后缀（表示 medium-density devices，64KB~128KB Flash）。我第一次手滑写成了 `startup_stm32f103x8.s`，结果 CMake 找不到文件，报了一个很晦涩的错误。记住：C8T6 用 `xb`。

除了启动文件，我们还需要 `system_stm32f1xx.c`。这个文件包含了 `SystemInit()` 函数，在启动文件里会被调用，用来设置系统时钟和 Flash 配置。如果不加这个文件，链接器会报 `undefined reference to SystemInit`，然后你会花一个小时去找这个函数到底在哪里。

然后是 HAL 库的源文件。我一开始很天真，以为直接 `GLOB` 所有 `.c` 文件就行：

```cmake
file(GLOB HAL_SRC
    ${STM32_HAL_DRIVER_ROOT}/Src/*.c
)
```

如果你也这么写，编译到一半会看到这么一个错误：

```text
multiple definition of 'HAL_InitTick'
hal/src/hal_timebase_tim.c:123: first defined here
hal/src/hal_timebase_tim_template.c:98: also defined here
```

问题出在 STM32 HAL 库里有一堆 `_template.c` 文件，比如 `stm32f1xx_hal_timebase_tim_template.c`。这些模板文件提供了某些函数的默认实现，但它们不应该和普通的 HAL 文件一起被编译进去。解决方案是加一个过滤器：

```cmake
list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")
```

这行代码会把所有匹配 `*_template.c` 的文件从 `HAL_SRC` 列表里踢出去。那个正则表达式的 `\\.c` 需要转义点号，否则 `.` 会匹配任意字符，可能会误删正常文件。我第一次写的时候忘了转义，结果连 `stm32f1xx_hal.c` 都被排除了，链接器报了几百个 `undefined reference`。

最后是用户代码的源文件。目前我们只有一个空的 `main.cpp`，但你可以用 `GLOB` 或者手动添加更多文件。

## 编译选项 —— 小心 C++ 专属选项

公共编译选项部分没什么好说的，主要是一些 ARM 特定的选项：

```cmake
add_compile_options(
    -mcpu=cortex-m3
    -mthumb
    -O2
    -g3
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
)
```

`-mthumb` 是很重要的。Thumb 指令集是 ARM 的 16 位精简指令集，生成的代码更小，对于 Flash 只有 64KB 的 Blue Pill 来说能省一点是一点。`-ffunction-sections` 和 `-fdata-sections` 会把每个函数和数据对象放到独立的段里，配合链接时的 `--gc-sections` 选项，可以删除所有没被用到的代码。如果你不加这两个选项，最终的固件可能会大得离谱。

接下来是语言特定的选项，这里是新手最容易踩坑的地方：

```cmake
add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-std=c11>"
    "$<$<COMPILE_LANGUAGE:CXX>:-std=c++17>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
)
```

这个 `$<$<COMPILE_LANGUAGE:CXX>:...>` 语法叫做 **generator expression**，是 CMake 的条件表达式，表示"只有在编译 C++ 文件时才应用这些选项"。你可能会问：为什么不直接把这些选项和公共选项放一起？

问题在于：`-fno-exceptions` 和 `-fno-rtti` 是 C++ 特定的选项，GCC 在编译 C 文件时会警告这些选项对 C 语言无效。虽然只是 warning 不会导致编译失败，但看满屏的黄色警告会强迫症发作。更严重的是，某些工具链（比如某些版本的 ARM GCC）会在遇到这些选项时直接报错。

我一开始图省事，把 `-fno-exceptions` 直接加到公共选项里，结果编译 HAL 库的 C 文件时每个文件都报 warning。足足有五十多个 warning，把真正的错误信息淹没了。后来才知道可以用 generator expression 来按语言区分选项，这才清静了。

## 链接选项 —— 为什么需要 nosys.specs

链接选项部分有几个关键点需要解释：

```cmake
add_link_options(
    -mcpu=cortex-m3
    -mthumb
    -nostartfiles
    -specs=nano.specs
    -specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/output.map
    -T${PROJECT_ROOT}/ld/STM32F103XB_FLASH.ld
)
```

`-nostartfiles` 告诉链接器不要使用标准库的启动文件（比如 `crt0.o`）。我们有自己专门为 STM32 写的启动文件，标准库的那个会用错内存布局。

`-specs=nano.specs` 会链接 `newlib-nano`，这是 newlib C 标准库的精简版本。它去掉了浮点数格式化支持、线程安全等在嵌入式场景用不到的功能，能显著减小代码体积。如果你不加这个选项，最终固件可能会大上好几 KB。

`-specs=nosys.specs` 比较有意思。它告诉链接器："不要提供系统调用的实现"。在 Linux 上，C 标准库的函数比如 `printf` 会通过系统调用来操作文件描述符。但在裸机环境下没有操作系统，所以我们需要自己实现这些系统调用（比如 `write()`、`read()` 等）。`nosys.specs` 提供了一套空的系统调用存根，避免链接器报 `undefined reference`。我们稍后会在 `syscalls.c` 文件里提供自己的实现（这部分内容在下一篇文章里会详细讲）。

`-Wl,--gc-sections` 是链接时垃圾回收。配合编译时的 `-ffunction-sections` 和 `-fdata-sections`，它会删除所有没被引用的段。如果你只用了 GPIO 和 UART，那 SPI、I2C、ADC 的代码都会被丢掉，最终固件会小很多。

最后一行的 `-T` 指定了链接脚本文件。这个文件定义了 Flash 和 RAM 的布局，我们稍后会详细分析。

## 链接脚本详解

链接脚本是个很多工程师都搞不明白的东西，我第一次接触的时候也是一脸懵。简单来说，它告诉链接器：哪些代码放 Flash 里，哪些变量放 RAM 里，堆栈多大，从哪个地址开始执行。下面是一个简化版的 STM32F103C8T6 链接脚本，我们把关键部分拆开来看。

首先是 MEMORY 定义：

```ld
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 20K
}
```

这里的 `rx` 和 `rwx` 是权限标志：`r` = 可读，`w` = 可写，`x` = 可执行。Flash 是只读的（烧进去后就不能改），所以只有 `rx`；RAM 是可读可写可执行的，所以是 `rwx`。`ORIGIN` 是起始地址，`LENGTH` 是大小。STM32F103C8T6 有 128KB Flash 和 20KB RAM，这些数据可以在芯片的 datasheet 里找到。

接下来是 SECTIONS 定义，这是最关键的部分：

```ld
ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector :
  {
    KEEP(*( .isr_vector ))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
  } > FLASH

  .data :
  {
    *(.data*)
  } > RAM AT > FLASH
}
```

`ENTRY(Reset_Handler)` 指定了程序的入口点。`Reset_Handler` 是启动文件里的一个函数，它会在芯片复位时被执行。

`.isr_vector` 段存放中断向量表，这是 STM32 启动时第一件要读的东西。注意这里用了 `KEEP(...)` 指令。如果你不加 `KEEP`，链接器可能会认为向量表没有被引用（因为代码里没有直接访问它），然后在 `--gc-sections` 时把它删掉。结果就是芯片复位后找不到向量表，程序直接跑飞。我第一次编译时就忘了加 `KEEP`，烧进去后芯片一点反应都没有，排查了一整晚。

`.text` 段存放所有代码和只读数据（比如字符串字面量）。它们都放在 Flash 里。

`.data` 段存放已初始化的全局变量和静态变量，比如 `int count = 0;`。这里有个很关键的语法：`> RAM AT > FLASH`。它的意思是：这些变量最终要放在 RAM 里（因为运行时需要修改），但它们的初始值存放在 Flash 里。为什么？因为 Flash 里的内容断电后不会丢失，而 RAM 断电后数据就没了。启动代码会在 `Reset_Handler` 里把 Flash 里的初始值复制到 RAM 里，这个过程叫"data 段初始化"。

如果忘记加 `AT > FLASH`，链接器会认为初始值就放在 RAM 里，但 RAM 里断电后是空的，结果就是所有变量初始值都是错的。我见过有人在调试时发现全局变量总是随机值，最后查出来是链接脚本写错了。

最后是堆栈设置：

```ld
_stack_start = ORIGIN(RAM) + LENGTH(RAM);
_stack_end = _stack_start - 0x400;  /* 1KB stack */

_heap_start = _ebss;
_heap_end = _stack_start;
```

栈从 RAM 的末尾开始向下生长，堆从 BSS 段的末尾开始向上生长。这里留了 1KB 给栈，如果你的函数调用层次很深或者用了大量局部数组，可能需要增大这个值。如果栈溢出了，程序行为会完全不可预测，可能死机，可能跳到随机地址执行。

## 后处理和自定义目标

编译链接完成后，我们需要把 ELF 文件转换成原始二进制格式，这样才能用 st-flash 或者 OpenOCD 烧录：

```cmake
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Generating ${PROJECT_NAME}.bin"
)
```

`objcopy` 会把 ELF 文件的所有段（包括 .text、.data、.rodata 等）按地址顺序拼接成一个纯二进制文件，去掉所有 ELF 元数据。最终得到的 .bin 文件可以直接烧进 Flash 里。

`size` 命令会显示各个段的大小，帮助你判断固件有没有超出 Flash 容量：

```text
text    data     bss     dec     hex filename
 4512     124    1024    5660    161c stm32f103c8t6_project.elf
```

这里的 `text` 是代码段，`data` 是已初始化数据段（初始值在 Flash 里），`bss` 是未初始化数据段（直接在 RAM 里分配）。你可以用 `text + data` 来估算占用的 Flash 空间。

最后是两个自定义目标：`flash` 和 `erase`。它们会调用我们之前写的 `flash.sh` 和 `erase.sh` 脚本，让你可以用 `make flash` 或 `cmake --build build --target flash` 来直接烧录固件，不用手动敲 st-flash 命令。

## 常见编译错误速查

即使你照着上面一步步来，也还是可能遇到各种问题。这里列出几个我踩过的坑和对应的解决方法。

**错误：`startup_stm32f103x8.s: No such file or directory`**

你把启动文件名写错了。Blue Pill 用的是 `startup_stm32f103xb.s`（medium-density），不是 `x8`。去 CMSIS 目录下 `ls` 一下，确认文件名正确。

**错误：`'LSI_VALUE' undeclared here`**

你缺少 `stm32f1xx_hal_conf.h` 文件，或者这个文件里没有定义必要的宏。确保你的 `include` 路径包含了 HAL 驱动的 Inc 目录，并且 `stm32f1xx_hal_conf.h` 存在。通常这个文件在 `STM32F1xx_HAL_Driver/Inc/` 里有模板版本，需要复制到你的项目里并修改。

**错误：`multiple definition of 'HAL_InitTick'`**

你把 `*_template.c` 文件也编进去了。检查你的 `HAL_SRC` 列表，确保用 `list(FILTER ... EXCLUDE REGEX ".*_template\\.c$")` 过滤掉了这些模板文件。

**错误：`undefined reference to '_init'` 或 `undefined reference to '__libc_init_array'`**

这是 newlib 的问题。`_init` 是 C++ 全局对象构造时会被调用的函数，但裸机环境没有提供实现。你需要创建一个 `syscalls.c` 文件提供 `_init` 的空实现。这个问题我们会在下一篇详细讲解如何实现自己的系统调用存根。

**警告：`ignoring option '-fno-rtti' because it is not a valid option for C language`**

你把 C++ 专属选项加到了公共编译选项里，导致编译 C 文件时 GCC 警告。用 generator expression 把这些选项包起来：`"$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"`。

现在你可以试着运行 `./build.sh`，如果一切顺利，你应该能在 `build/` 目录下看到 `.elf` 和 `.bin` 文件，终端里会显示固件的大小信息。如果有报错，对照上面的错误列表一个个排查。

下一篇文章我们会讲解如何实现 syscalls.c 来解决 `_init` 的 undefined reference 问题，以及如何用 C++ 重写启动代码，让全局对象的构造和析构正确执行。到那时，你就可以在 main 函数里直接写 C++ 代码，用 `std::vector`、`std::string` 等标准库容器了。
