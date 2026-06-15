---
chapter: 14
difficulty: beginner
order: 3
platform: stm32f1
reading_time_minutes: 17
tags:
- beginner
- cpp-modern
- stm32f1
title: CMake Configuration — Building an STM32 Build System from Scratch
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/00-env-setup/03-cmake-configuration.md
  source_hash: b413fb1eac6642f586a8bb8afe4c0f937d15f7afdcb263c59d5926ae0cbd7f8c
  token_count: 3108
  translated_at: '2026-05-26T11:58:16.514187+00:00'
description: ''
---
# CMake Configuration — Building an STM32 Build System from Scratch

I'm staring at the CMakeLists.txt on my screen, and my coffee has gone cold. If you've been following along through the previous two articles, you should now have a cross-compilation toolchain and the STM32 firmware library downloaded. But the real problem is just beginning: how do we get all of this to compile and link into a .bin file that we can flash into the chip? The first time I did this, I spent half an afternoon just getting CMake to understand that "this is a bare-metal ARM project, don't try to run test programs." Today, we're going to break this build system down from start to finish.

## The Complete CMakeLists.txt Up Front

No more preamble — here's the full configuration, and we'll break it down section by section. This file lives in the project root, right next to build.sh:

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

Alright, I know this file looks a bit intimidating. The first time I wrote one, I "translated" it line by line from the Makefile generated by STM32CubeIDE. But if we break it apart, you'll find that every section has a good reason to exist.

## Basic Cross-Compilation Settings

The first few lines are the "standard approach" for CMake cross-compilation:

```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)
```

Setting `CMAKE_SYSTEM_NAME` to `Generic` tells CMake: this isn't a Linux/Windows/macOS program; it's a bare-metal environment. If you set it to `Linux`, CMake will try to find Linux headers, and you'll be greeted by a whole row of red squiggly lines.

`CMAKE_SYSTEM_PROCESSOR = ARM` is mainly for scripts that detect the CPU architecture. We don't strictly need it in our scenario, but setting it doesn't hurt.

Next, we specify the toolchain. Note the `${CROSS_COMPILE}` prefix here — once you add `arm-none-eabi-`, CMake automatically infers the full toolchain paths. If you can see `arm-none-eabi-gcc` when running the `where` or `which` command, this will work.

The most critical line is this one:

```cmake
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

This setting saved my life. By default, CMake compiles a small program and tries to run it during project configuration to test whether the toolchain is working properly. But here's the problem: we're compiling an ARM program, which simply cannot run on an x86_64 development machine! Without this line, CMake will throw a `try_compile` failure error. By setting it to `STATIC_LIBRARY`, CMake only compiles the test program without trying to link and run it, and the problem is solved.

The last line, `CMAKE_EXPORT_COMPILE_COMMANDS`, isn't strictly necessary, but I highly recommend enabling it. It generates a `compile_commands.json` file, which clangd and VSCode's C++ extension read to get the correct compiler flags. Without it, your IDE won't be able to find STM32 headers, and every call like `HAL_GPIO_WritePin` will be flagged as an "undefined symbol."

## Source File Collection — That Annoying Template Problem

Next, we gather all the source files we need. First, the startup file:

```cmake
file(GLOB STARTUP_SRC
    ${STM32_CMSIS_ROOT}/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s
)
```

Pay attention to the filename here: `startup_stm32f103xb.s`. If you're using a Blue Pill, the chip model is STM32F103C8T6, and the corresponding startup file has the `xb` suffix (standing for medium-density devices, 64KB–128KB Flash). The first time, I accidentally typed `startup_stm32f103x8.s`, and CMake couldn't find the file, throwing a very obscure error. Remember: C8T6 uses `xb`.

Besides the startup file, we also need `system_stm32f1xx.c`. This file contains the `SystemInit()` function, which is called from the startup file and is used to set up the system clock and Flash configuration. Without this file, the linker will complain about `undefined reference to SystemInit`, and you'll spend an hour hunting for where this function actually lives.

Then there are the HAL library source files. At first, I was naive enough to think I could just `GLOB` all `.c` files:

```cmake
file(GLOB HAL_SRC
    ${STM32_HAL_DRIVER_ROOT}/Src/*.c
)
```

If you write it this way too, you'll see this error halfway through compilation:

```text
multiple definition of 'HAL_InitTick'
hal/src/hal_timebase_tim.c:123: first defined here
hal/src/hal_timebase_tim_template.c:98: also defined here
```

The problem is that the STM32 HAL library contains a bunch of `_template.c` files, such as `stm32f1xx_hal_timebase_tim_template.c`. These template files provide default implementations for certain functions, but they should not be compiled alongside the regular HAL files. The solution is to add a filter:

```cmake
list(FILTER HAL_SRC EXCLUDE REGEX ".*_template\\.c$")
```

This line removes all files matching `*_template.c` from the `HAL_SRC` list. The `\\.c` in that regular expression escapes the dot; otherwise, `.` would match any character and might accidentally delete legitimate files. The first time I wrote this, I forgot to escape it, and even `stm32f1xx_hal.c` got excluded, causing the linker to spit out hundreds of `undefined reference` errors.

Finally, we have the user code source files. Right now we only have an empty `main.cpp`, but you can use `GLOB` or manually add more files.

## Compiler Flags — Watch Out for C++-Specific Options

There's not much to say about the common compiler flags; they're mostly ARM-specific options:

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

`-mthumb` is very important. The Thumb instruction set is ARM's 16-bit reduced instruction set, which generates smaller code. For a Blue Pill with only 64KB of Flash, every byte saved counts. `-ffunction-sections` and `-fdata-sections` place each function and data object into its own section. Combined with the `--gc-sections` option at link time, this allows the removal of all unused code. If you don't add these two options, your final firmware could end up absurdly large.

Next are the language-specific flags, which is where beginners most easily trip up:

```cmake
add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-std=c11>"
    "$<$<COMPILE_LANGUAGE:CXX>:-std=c++17>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
)
```

This `__PRESERVED_15__<COMPILE_LANGUAGE:CXX>:...>` syntax is called a **generator expression**, and it's CMake's way of doing conditional logic — it means "only apply these options when compiling C++ files." You might ask: why not just put these options together with the common flags?

The problem is that `-fno-exceptions` and `-fno-rtti` are C++-specific options. GCC will warn that these options are invalid for the C language when compiling C files. Although it's just a warning and won't cause compilation to fail, seeing a screen full of yellow warnings will trigger anyone's OCD. Even worse, certain toolchains (like some versions of ARM GCC) will error out directly when they encounter these options.

Initially, I took a shortcut and added `-fno-exceptions` directly to the common flags. As a result, every single C file in the HAL library generated a warning during compilation. There were over fifty warnings, completely drowning out the real error messages. I only learned later that generator expressions could be used to separate options by language, and finally got some peace and quiet.

## Linker Flags — Why We Need nosys.specs

There are a few key points to explain in the linker flags section:

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

`-specs=nano.specs` tells the linker not to use the standard library's startup files (like `crt0.o`). We have our own startup file specifically written for the STM32; the standard library one would use the wrong memory layout.

`-specs=nano.specs` links against `newlib-nano`, which is a stripped-down version of the newlib C standard library. It removes features like floating-point formatting support and thread safety that aren't needed in embedded scenarios, significantly reducing code size. If you don't add this option, your final firmware could be several KB larger.

`-specs=nosys.specs` is more interesting. It tells the linker: "don't provide implementations for system calls." On Linux, C standard library functions like `printf` operate on file descriptors through system calls. But in a bare-metal environment, there is no operating system, so we need to implement these system calls ourselves (such as `write()`, `read()`, etc.). `nosys.specs` provides a set of empty system call stubs to prevent the linker from throwing `undefined reference` errors. We'll provide our own implementations in a `syscalls.c` file later (which we'll cover in detail in the next article).

`-Wl,--gc-sections` is link-time garbage collection. Combined with `-ffunction-sections` and `-fdata-sections` at compile time, it removes all unreferenced sections. If you only use GPIO and UART, the code for SPI, I2C, and ADC will all be discarded, making the final firmware much smaller.

The last line, `-T`, specifies the linker script file. This file defines the Flash and RAM memory layout, which we'll analyze in detail shortly.

## Linker Script Explained

The linker script is something many engineers don't fully understand, and I was completely lost the first time I encountered it. Simply put, it tells the linker: which code goes into Flash, which variables go into RAM, how large the heap and stack should be, and which address to start executing from. Below is a simplified linker script for the STM32F103C8T6. Let's break down the key parts.

First is the MEMORY definition:

```c
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 20K
}
```

The `rx` and `rwx` here are permission flags: `r` = readable, `w` = writable, `x` = executable. Flash is read-only (it can't be modified after being flashed), so it only has `rx`; RAM is readable, writable, and executable, so it gets `rwx`. `ORIGIN` is the starting address, and `LENGTH` is the size. The STM32F103C8T6 has 128KB of Flash and 20KB of RAM; you can find this data in the chip's datasheet.

Next is the SECTIONS definition, which is the most critical part:

```c
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

`ENTRY(Reset_Handler)` specifies the program's entry point. `Reset_Handler` is a function in the startup file that gets executed when the chip resets.

The `.isr_vector` section holds the interrupt vector table, which is the first thing the STM32 reads when it boots. Note the use of the `KEEP(...)` directive here. If you don't add `KEEP`, the linker might think the vector table is unreferenced (since no code directly accesses it) and delete it during `--gc-sections`. The result is that the chip can't find the vector table after a reset, and the program goes completely off the rails. The first time I compiled, I forgot to add `KEEP`, and after flashing, the chip showed absolutely no response. I spent a whole evening debugging it.

The `.text` section holds all code and read-only data (like string literals). These all reside in Flash.

The `.data` section holds initialized global and static variables, like `int count = 0;`. There's a very critical piece of syntax here: `> RAM AT > FLASH`. What it means is: these variables ultimately need to be placed in RAM (because they need to be modified at runtime), but their initial values are stored in Flash. Why? Because the contents of Flash persist after power-off, while RAM data is lost. The startup code copies the initial values from Flash to RAM in `Reset_Handler`, a process known as "data section initialization."

If you forget to add `AT > FLASH`, the linker will assume the initial values are already in RAM. But since RAM is empty after power-off, the result is that all variable initial values will be wrong. I've seen people debugging and finding that their global variables always had random values, only to discover that the linker script was written incorrectly.

Finally, there's the heap and stack setup:

```c
_stack_start = ORIGIN(RAM) + LENGTH(RAM);
_stack_end = _stack_start - 0x400;  /* 1KB stack */

_heap_start = _ebss;
_heap_end = _stack_start;
```

The stack grows downward from the end of RAM, and the heap grows upward from the end of the BSS section. Here, 1KB is reserved for the stack. If you have deep function call hierarchies or use large local arrays, you might need to increase this value. If the stack overflows, program behavior becomes completely unpredictable — it might crash, or it might jump to a random address and start executing.

## Post-Processing and Custom Targets

After compilation and linking, we need to convert the ELF file into a raw binary format so we can flash it using st-flash or OpenOCD:

```cmake
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Generating ${PROJECT_NAME}.bin"
)
```

`objcopy` concatenates all sections of the ELF file (including .text, .data, .rodata, etc.) in address order into a pure binary file, stripping all ELF metadata. The resulting .bin file can be flashed directly into Flash.

The `size` command displays the size of each section, helping you determine whether the firmware exceeds the Flash capacity:

```text
text    data     bss     dec     hex filename
 4512     124    1024    5660    161c stm32f103c8t6_project.elf
```

Here, `text` is the code section, `data` is the initialized data section (initial values live in Flash), and `bss` is the uninitialized data section (allocated directly in RAM). You can use `text + data` to estimate the Flash space consumed.

Finally, there are two custom targets: `flash` and `erase`. These call the `flash.sh` and `erase.sh` scripts we wrote earlier, allowing you to flash the firmware directly with `make flash` or `cmake --build build --target flash` without having to manually type st-flash commands.

## Common Compilation Errors Quick Reference

Even if you follow the steps above exactly, you might still run into various issues. Here are a few pitfalls I've stepped into, along with their solutions.

**Error: `startup_stm32f103x8.s: No such file or directory`**

You got the startup file name wrong. The Blue Pill uses `startup_stm32f103xb.s` (medium-density), not `x8`. Go to the CMSIS directory and `ls` to confirm the correct filename.

**Error: `'LSI_VALUE' undeclared here`**

You're missing the `stm32f1xx_hal_conf.h` file, or the necessary macros aren't defined in it. Make sure your `include` path includes the HAL driver's Inc directory, and that `stm32f1xx_hal_conf.h` exists. Usually, there's a template version of this file in `STM32F1xx_HAL_Driver/Inc/` that you need to copy into your project and modify.

**Error: `multiple definition of 'HAL_InitTick'`**

You compiled the `*_template.c` files as well. Check your `HAL_SRC` list and make sure you used `list(FILTER ... EXCLUDE REGEX ".*_template\\.c$")` to filter out these template files.

**Error: `undefined reference to '_init'` or `undefined reference to '__libc_init_array'`**

This is a newlib issue. `_init` is a function called during the construction of C++ global objects, but the bare-metal environment doesn't provide an implementation. You need to create a `syscalls.c` file that provides an empty implementation of `_init`. We'll cover how to implement your own system call stubs in detail in the next article.

**Warning: `ignoring option '-fno-rtti' because it is not a valid option for C language`**

You added C++-specific options to the common compiler flags, causing GCC to warn when compiling C files. Wrap these options with a generator expression: `"__PRESERVED_16__<COMPILE_LANGUAGE:CXX>:-fno-rtti>"`.

Now you can try running `./build.sh`. If all goes well, you should see the `.elf` and `.bin` files in the `build/` directory, and the terminal will display the firmware size information. If you get errors, troubleshoot them one by one using the error list above.

In the next article, we'll cover how to implement syscalls.c to resolve the `_init` undefined reference issues, and how to rewrite the startup code in C++ so that global object construction and destruction execute correctly. Once we do that, you'll be able to write C++ code directly in your main function, using standard library containers like `std::vector` and `std::string`.
