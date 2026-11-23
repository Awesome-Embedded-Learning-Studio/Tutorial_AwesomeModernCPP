---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: A detailed guide to common GCC/Clang compiler options, including language
  standards, optimization levels, warning control, and C++ runtime trimming
difficulty: beginner
order: 2
platform: host
reading_time_minutes: 8
related: []
tags:
- cpp-modern
- host
- intermediate
title: A Guide to Common Compiler Options
translation:
  source: documents/vol7-engineering/02-compiler-options.md
  source_hash: f36f7a3ab2282ba2e5d141bc131ff7f850af39a98a8d60a83a52b4e70eb31f9c
  translated_at: '2026-11-22T16:43:36+00:00'
  engine: glm-5.3-refresh
  token_count: 6134
---
# Modern Embedded C++ Tutorial: A Guide to Common Compiler Parameters

In real-world embedded development, every single byte of Flash and RAM is genuinely something the developer has to save. C++ may carry the reputation of being a "heavyweight language", but with the compiler options configured sensibly, we can trim the runtime overhead so precisely that performance and code size can even beat hand-written C. (I trust you all saw this for yourselves back in Chapter 0.)

------

## 0 Some Basics

#### Language Standard Control: `-std=`

This is the most direct way to define how "modern" your project is.

- **Flag format**: `-std=c++11`, `-std=c++14`, `-std=c++17`, `-std=c++20`.
- **The GNU extended variant**: `gnu++17`. Compared with the standard `c++17`, it allows a number of GCC-specific non-standard extensions (such as special inline assembly syntax). In low-level embedded development, you sometimes have no choice but to use the `gnu++` variant.

#### Why Choose `-std=c++17` or Above for Embedded Work

- **The power of `constexpr`**: in C++17, a large amount of logic can be moved to compile-time evaluation, directly cutting the runtime CPU load and Flash footprint.
- **`std::span` (C++20)**: the perfect replacement for passing buffers around in embedded development — safer than the traditional `uint8_t* ptr, size_t len` pair, with zero extra overhead.
- **Structured bindings**: they make parsing complex sensor data structures remarkably elegant.

------

#### Preprocessor and Macro Definitions: `-D` and `-U`

In embedded work, hardware differences mean we constantly need "conditional compilation".

- **`-D<macro>=<value>`**: defines a macro.
  - For example: `-DSTM32F407xx` or `-DDEBUG_LEVEL=2`.
  - **The modern approach**: control these through CMake with `target_compile_definitions(target PRIVATE STM32F407xx)` wherever possible, instead of filling your code with `#define`s.
- **`-U<macro>`**: undefines an already-defined macro.

> **Warning**: leaning on macros too heavily makes code paths hard to test (code coverage cannot reach the branches where the macro is switched off). In modern C++, prefer `if constexpr` combined with constant objects.

------

#### Path Search and Library Linking: `-I`, `-isystem`, `-L`, `-l`

This is where beginners most easily get their CMake configuration wrong.

- **`-I <dir>` (include)**: specifies header file search paths.
- **`-isystem <dir>`**: specifies paths for "system" header files.
  - **The elegant part**: when a third-party library (ST's HAL library, say) produces mountains of meaningless warnings, include it with `-isystem` and the compiler will **automatically suppress all warnings from that directory**, keeping your console clean.
- **`-L <dir>`**: specifies the search directory for static libraries (`.a`).
- **`-l<name>`**: links the given library.
  - Note: if the library file is named `libmath.a`, the flag is `-lmath` (drop the `lib` prefix and the extension).

------

#### Output Management and Debug Information: `-o` and `-g`

- **`-o <file>`**: specifies the output file name. In cross-compilation we usually generate an `.elf` file first, then convert it to `.bin` or `.hex` with `objcopy`.
- **`-g` and `-g3`**:
  - `-g` produces standard debug symbols for GDB debugging.
  - **`-g3`**: even includes debug information for macro definitions. Turn it on if you need to inspect the value of some `#define` while debugging.
  - **Misconception corrected**: enabling `-g` does **not** increase the size of the code running on the board. Debug information lives only in the `.elf` file on your computer; none of it is flashed into the MCU's Flash.

------

#### Warning Governance: The `-W` Series (Code Quality)

In a safety-sensitive field like embedded, warnings are bugs in hiding.

- **`-Wall -Wextra`**: the standard kit for the vast majority of developers, turning on most of the warnings that matter.
- **`-Werror`**: **treats all warnings as errors**.
  - *Recommended practice*: force `-Werror` on in CI/CD (continuous integration) environments, so that no committed code slips through with hidden problems.
- **`-Wshadow`**: warns when a local variable name shadows a global one — extremely useful when toggling embedded logic.
- **`-Wdouble-promotion`**: **a must-have for embedded!** It warns when you inadvertently promote a `float` to a `double`. On MCUs without a double-precision hardware floating-point unit, this causes performance to plummet.

------

#### Dependency Generation: `-M`, `-MMD`

Have you ever wondered how CMake knows that "because you changed one header, these 10 source files need to be recompiled"?

- **`-MMD`**: while compiling, also generates a dependency file with a `.d` extension.
- **Automation**: modern build systems (CMake/Ninja) handle these flags for you automatically. Understanding them helps you troubleshoot incremental-build puzzles like "why did nothing recompile after I changed the code".

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

GCC and Clang provide several tiers of optimization switches. Understanding their differences is a fundamental skill for embedded developers.

| **Option**     | **Name** | **Core Behavior**                        | **Use Cases**                            |
| ------------ | -------- | -------------------------------------- | ---------------------------------------- |
| **`-O0`**    | No optimization | Keeps a one-to-one mapping between code and assembly. | Only for tracking down extremely elusive logic bugs. |
| **`-Og`**    | Debug optimization | Enables optimizations that do not interfere with debugging observation. | **The first choice during development**, balancing performance with single-stepping. |
| **`-O2`**    | Performance optimization | Enables almost every optimization that does not trade space for time. | High-performance computing, RTOS task logic. |
| **`-Os`**    | Size optimization | Enables the `-O2` options that do not increase code size. | **The default choice for embedded releases**. |
| **`-Ofast`** | Maximum-speed optimization | Breaks the IEEE 754 standard (no floating-point precision guarantees). | Pure mathematical computation where slight precision deviations are acceptable. |

### 💡 Deep Advice: Why You Should Avoid `-O3` in Embedded Work

`-O3` performs massive amounts of loop unrolling and function inlining. The speed may well improve, but on an MCU where Flash space is already stretched thin, it bloats the code — and may even reduce performance through instruction cache (I-Cache) misses.

------

## 2. Trimming the C++ Runtime: Taking Off the Heavy "Armor"

Modern C++ carries a few features by default that come at a very high cost in embedded contexts. With the following two options, we can slim C++ back down to C-like overhead.

### 2.1 `-fno-exceptions` (Disabling Exceptions)

- **Cost**: C++ exceptions require heavy-duty unwind table support, which adds roughly 10% to 20% to the Flash footprint.
- **Consequence**: you cannot use `try-catch` or `throw`. If the program fails, it goes straight to `std::terminate`.
- **Embedded guideline**: on resource-constrained systems (such as Cortex-M), **disabling exceptions is strongly recommended**.

### 2.2 `-fno-rtti` (Disabling Runtime Type Information)

- **Cost**: to support `dynamic_cast` and `typeid`, the compiler generates extra metadata for every class with virtual functions (information beyond the vtable).
- **Consequence**: you can no longer determine an object's real type at runtime.
- **Embedded guideline**: modern embedded design leans toward compile-time polymorphism (templates/CRTP), so RTTI is usually redundant.

------

## 3. Garbage-Collecting Unused Code

By default, the compiler compiles each source file into one giant binary blob. Even if you use only a single function from a library, the linker stuffs the entire library's code into Flash.

### 3.1 Compiler Side: Sectioning

- **`-ffunction-sections`**: packs each function into its own section.
- **`-fdata-sections`**: packs each global/static variable into its own section.

### 3.2 Linker Side: Garbage Collection

- **`-Wl,--gc-sections`**: tells the linker (`ld`) to scan all sections and completely strip the unreferenced "dead code" out of the final `.elf` file.

------

## 4. Best-Practice Configuration in CMake

Turning the theory above into code. In your top-level `CMakeLists.txt`, it is a good idea to manage these options like this:

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

In embedded work, `-Ofast` enables `-ffast-math`. This can lead to:

1. **Precision loss**: to gain speed, the compiler may ignore tiny floating-point errors.
2. **NaN/Inf handling breaking down**: it assumes your program will never produce invalid floating-point numbers.
3. **Reordered operations**: this can make some algorithms produce unstable results.

**Recommendation**: unless you are doing pure digital signal processing (DSP) with complete control over precision, always stick with `-Os` or `-O2`.

## Run It Online

Compare online the assembly code the compiler generates at different optimization levels (-O0 / -Os / -O2), and observe the effects of inlining and constant folding:

<OnlineCompilerDemo
  title="Common Compiler Options"
  source-path="code/examples/vol7/14_compiler_options.cpp"
  description="Compare the assembly generated under -O0 / -Os / -O2 and observe inlining and constant folding"
  allow-x86-asm
  arm-source-path="code/examples/compiler_explorer/compiler_opts_arm.cpp"
  allow-arm-asm
/>
