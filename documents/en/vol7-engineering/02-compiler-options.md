---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Detailed introduction to common GCC/Clang compiler flags, including language
  standards, optimization levels, warning control, and C++ runtime trimming.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 'Chapter 0: 前言与基础'
reading_time_minutes: 9
related: []
tags:
- cpp-modern
- host
- intermediate
title: Common Compiler Flags Guide
translation:
  source: documents/vol7-engineering/02-compiler-options.md
  source_hash: 5c0b5aaeb0b1d04771265b3e2d9c8ba630b8f25acd248bc2fc0c5c6e8106abb4
  translated_at: '2026-05-26T11:51:21.186862+00:00'
  engine: anthropic
  token_count: 1543
---
# Modern Embedded C++ Tutorial: Common Compiler Flags Guide

In real-world embedded development, every single byte of Flash and RAM is truly saved by the developer. Although C++ carries the stigma of being a "heavyweight language," we can precisely trim runtime overhead through proper compiler flag configuration, achieving performance and size that even surpass hand-written C code. (I believe you have already seen this in Chapter 0.)

------

## 0 Some Basics

#### Language Standard Control: `-std=`

This is the most direct way to define the "modernity" of a project.

- **Flag format**: `-std=c++11`, `-std=c++14`, `-std=c++17`, `-std=c++20`.
- **GNU extension version**: `gnu++17`. Compared to the standard `c++17`, it allows the use of some GCC-specific non-standard extensions (such as special inline assembly syntax). In low-level embedded development, we sometimes have to use the `gnu++` version.

#### Why Choose `-std=c++17` or Above in Embedded?

- **The power of `constexpr`**: In C++17, a massive amount of logic can be moved to compile-time calculation, directly reducing runtime CPU load and Flash footprint.
- **`std::span` (C++20)**: It is the perfect replacement for passing buffers in embedded development, offering better safety and zero overhead compared to traditional `uint8_t* ptr, size_t len`.
- **Structured binding**: Makes parsing complex sensor data structures extremely elegant.

------

#### Preprocessor and Macro Definitions: `-D` and `-U`

In embedded development, due to hardware differences, we frequently need "conditional compilation."

- **`-D<macro>=<value>`**: Defines a macro.
  - For example: `-DSTM32F407xx` or `-DDEBUG_LEVEL=2`.
  - **Modern approach**: Try to control this via `target_compile_definitions(target PRIVATE STM32F407xx)` in CMake, rather than littering your code with `#define`.
- **`-U<macro>`**: Undefines a previously defined macro.

> **Warning**: Over-reliance on macros makes code paths difficult to test (Code Coverage cannot cover branches with disabled macros). In modern C++, we recommend prioritizing `if constexpr` combined with constant objects.

------

#### Path Search and Library Linking: `-I`, `-isystem`, `-L`, `-l`

This is where beginners most easily make mistakes in CMake configuration.

- **`-I <dir>` (Include)**: Specifies header file search paths.
- **`-isystem <dir>`**: Specifies "system" header file paths.
  - **The subtlety**: If third-party libraries (like ST's HAL library) generate a lot of meaningless warnings, include them using `-isystem`. The compiler will **automatically suppress all warnings in that directory**, keeping your console clean.
- **`-L <dir>`**: Specifies the search directory for static libraries (`.a`).
- **`-l<name>`**: Links a specified library.
  - Note: If the library name is `libmath.a`, the flag is `-lmath` (dropping the `lib` prefix and the extension).

------

#### Output Management and Debug Information: `-o` and `-g`

- **`-o <file>`**: Specifies the output file name. In cross-compilation, we usually generate an `.elf` file, and then convert it to `.bin` or `.hex` via `objcopy`.
- **`-g` and `-g3`**:
  - `-g` produces standard debug symbols for GDB (GNU Debugger) debugging.
  - **`-g3`**: Even includes debug information for macro definitions. If you need to inspect the value of a certain `#define` during debugging, enable this.
  - **Misconception corrected**: Enabling `-g` **does not** increase the size of the code running on the board. Debug information only exists in the `.elf` file on your computer and will not be flashed into the MCU (Microcontroller Unit)'s Flash.

------

#### Warning Management: `-W` Series (Code Quality)

In safety-sensitive fields like embedded systems, warnings are hidden bugs.

- **`-Wall -Wextra`**: The standard for most developers, enabling the vast majority of valuable warnings.
- **`-Werror`**: **Treats all warnings as errors**.
  - *Recommended practice*: Enforce `-Werror` in CI/CD (Continuous Integration) environments to ensure submitted code has no hidden dangers.
- **`-Wshadow`**: Issues a warning when a local variable name shadows a global variable name, which is extremely useful when switching embedded logic.
- **`-Wdouble-promotion`**: **A must for embedded!** Warns when you inadvertently promote a `float` to a `double`. On an MCU without a hardware double-precision floating-point unit, this causes a massive performance drop.

------

#### Dependency Generation: `-M`, `-MMD`

Have you ever wondered how CMake knows "because you modified a certain header file, these 10 source files need to be recompiled"?

- **`-MMD`**: Generates a dependency file with a `.d` suffix alongside the compilation.
- **Automation**: Modern build systems (CMake/Ninja) handle these flags automatically. Understanding this helps you troubleshoot incremental compilation issues like "why did my code changes not trigger a recompilation."

```cmake

# 编译参数
target_compile_options(${PROJECT_NAME} PRIVATE
    -std=c++17             # 核心：定义语言标准
    -g3                    # 调试：丰富的调试信息
    -Wall -Wextra          # 质量：严格警告
    -Werror                # 质量：零容忍警告
    -Wdouble-promotion     # 性能：防止隐式双精度运算
    -ffunction-sections    # 体积：函数分区
    -fdata-sections        # 体积：数据分区
    -fno-exceptions        # 裁剪：禁用异常
    -fno-rtti              # 裁剪：禁用 RTTI
)

# 链接参数
target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections      # 体积：垃圾回收死代码
    -Wl,-Map=${PROJECT_NAME}.map  # 诊断：生成内存映射文件
)

```

------

## 1. Optimization Levels: Balancing Speed, Size, and Debugging

GCC and Clang provide multiple levels of optimization switches. Understanding their differences is fundamental for embedded developers.

| **Option**     | **Name** | **Core Behavior**                           | **Use Case**                             |
| ------------ | -------- | -------------------------------------- | ---------------------------------------- |
| **`-O0`**    | No optimization   | Maintains a one-to-one correspondence between code and assembly.             | Only for tracking down extremely elusive logic bugs.             |
| **`-Og`**    | Debug optimization | Enables optimizations that do not interfere with debugging observations.             | **The top choice for the development phase**, balancing performance with single-step debugging. |
| **`-O2`**    | Performance optimization | Enables almost all optimizations that trade space for time.   | High-performance computing, RTOS task logic.              |
| **`-Os`**    | Size optimization | Enables options from `-O2` that do not increase code size.    | **The default choice for embedded releases**.               |
| **`-Ofast`** | Fast math optimization | Breaks the IEEE 754 standard (does not guarantee floating-point precision). | Pure mathematical calculations where minor precision differences are acceptable.             |

### 💡 In-Depth Advice: Why Not Use `-O3` in Embedded?

`-O3` performs extensive loop unrolling and function inlining. While speed might improve, on an MCU with severely limited Flash space, it leads to code bloat, and it might even degrade performance due to instruction cache (I-Cache) misses.

------

## 2. Trimming the C++ Runtime: Shedding Heavy "Armor"

Modern C++ carries some features by default that come at a high cost in embedded systems. Through the following two flags, we can "slim down" C++ back to an overhead similar to C.

### 2.1 `-fno-exceptions` (Disable Exceptions)

- **Cost**: C++ exceptions require massive "unwind table" support, which increases Flash usage by about 10% to 20%.
- **Consequence**: You cannot use `try-catch` and `throw`. If the program encounters an error, it will directly call `std::terminate`.
- **Embedded guideline**: In resource-constrained systems (like Cortex-M), **we strongly recommend disabling this**.

### 2.2 `-fno-rtti` (Disable Runtime Type Information)

- **Cost**: To support `dynamic_cast` and `typeid`, the compiler generates extra metadata for every class with virtual functions (information beyond the vtable).
- **Consequence**: You cannot determine an object's true type at runtime.
- **Embedded guideline**: Modern embedded design prefers compile-time polymorphism (templates/CRTP (Curiously Recurring Template Pattern)), making RTTI usually redundant.

------

## 3. Garbage Collecting Unused Code

By default, the compiler compiles an entire source file into one giant binary block. Even if you only use one function from a library, the linker will stuff the entire library's code into Flash.

### 3.1 Compiler Side: Sectioning

- **`-ffunction-sections`**: Packages each function into its own section.
- **`-fdata-sections`**: Packages each global/static variable independently.

### 3.2 Linker Side: Garbage Collection

- **`-Wl,--gc-sections`**: Tells the linker (ld) to scan all sections and thoroughly strip out unreferenced "dead code" from the final `.elf` file.

------

## 4. Best Practice Configuration in CMake

Translating the above theory into code. In your top-level `CMakeLists.txt`, we recommend managing these flags like this:

```cmake

# 创建一个专门的编译选项接口库，方便所有 Target 复用
add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE
    $<$<CONFIG:Release>:-Os>                 # Release 模式优化尺寸
    $<$<CONFIG:Debug>:-Og -g3>               # Debug 模式方便调试
    -fno-exceptions                          # 禁用异常
    -fno-rtti                                # 禁用 RTTI
    -ffunction-sections                      # 函数分区
    -fdata-sections                          # 数据分区
    -Wall -Wextra -Wpedantic                 # 开启严格警告（防患未然）
)

# 链接器选项
target_link_options(project_warnings INTERFACE
    "-Wl,--gc-sections"                      # 链接时删除死代码
    "--specs=nano.specs"                     # 使用精简版 C 库 (Newlib-nano)
)

# 使用时只需要链接这个接口
target_link_libraries(my_firmware PRIVATE project_warnings)

```

------

## 5. The Dangerous `-Ofast` and Floating-Point Traps

In embedded systems, `-Ofast` enables `-ffast-math`. This can lead to:

1. **Precision loss**: To accelerate execution, the compiler might ignore minuscule floating-point errors.
2. **NaN/Inf failure**: It assumes your program will never produce illegal floating-point numbers.
3. **Reordering operations**: This can lead to unstable results in certain algorithms.

**Recommendation**: Unless you are doing pure digital signal processing (DSP) and have complete control over precision, always stick to using `-Os` or `-O2`.

## Run Online

Compare the assembly code generated by the compiler under different optimization levels (-O0 / -Os / -O2) online, and observe the effects of inlining and constant folding:

<OnlineCompilerDemo
  title="常见编译器选项"
  source-path="code/examples/vol7/14_compiler_options.cpp"
  description="对比 -O0 / -Os / -O2 下编译器生成的汇编，观察内联和常量折叠"
  allow-x86-asm
  arm-source-path="code/examples/compiler_explorer/compiler_opts_arm.cpp"
  allow-arm-asm
/>
