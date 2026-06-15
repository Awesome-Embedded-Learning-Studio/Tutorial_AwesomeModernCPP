---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Detailed introduction to common GCC/Clang compiler options, including
  language standards, optimization levels, warning control, and C++ runtime trimming.
difficulty: beginner
order: 2
platform: host
prerequisites:
- 'Chapter 0: 前言与基础'
reading_time_minutes: 7
related: []
tags:
- cpp-modern
- host
- intermediate
title: Guide to Common Compiler Options
translation:
  engine: anthropic
  source: documents/vol7-engineering/02-compiler-options.md
  source_hash: 3cbac8ed01ae577e224ab152b4ec1d5ceea65745a9a2b5b418cbb10e7a3986ca
  token_count: 1542
  translated_at: '2026-06-15T09:31:23.887546+00:00'
---
# Modern Embedded C++ Tutorial: Common Compiler Flags Guide

In real-world embedded development, every single byte of Flash and RAM is truly saved by the developer. Although C++ carries the bias of being a "heavyweight language," by configuring compiler flags reasonably, we can precisely trim runtime overhead, achieving performance and size that even surpass hand-written C code. (I believe you have already seen this in Chapter 0).

------

## 0 Some Basics

#### Language Standard Control: `-std=c++XX`

This is the most direct way to define the "modernity" of a project.

- **Flag format**: `-std=c++11`, `-std=c++14`, `-std=c++17`, `-std=c++23`.
- **GNU Extension version**: `-std=gnu++XX`. Compared to the standard `-std=c++XX`, it allows using some GCC-specific non-standard extensions (like special inline assembly syntax). In low-level embedded development, we sometimes have to use the `-std=gnu++XX` version.

#### Why choose `-std=c++17` or above in embedded?

- **The power of `constexpr`**: In C++17, a large amount of logic can be moved to compile-time calculation, directly reducing runtime CPU load and Flash footprint.
- **`std::span` (C++20)**: It is the perfect replacement for passing buffers in embedded development, safer and with zero overhead compared to traditional raw pointers.
- **Structured binding**: Makes parsing complex sensor data structures extremely elegant.

------

#### Preprocessor and Macros: `-D` and `-U`

In embedded development, due to hardware differences, we often need "conditional compilation."

- **`-D`**: Define a macro.
  - Example: `-DDEBUG=1` or `-DSTM32F10X`.
  - **Modern practice**: Try to control this via `target_compile_definitions` in CMake rather than filling your code with `#ifdef`.
- **`-U`**: Undefine a defined macro.

> **Warning**: Over-reliance on macros makes code paths difficult to test (Code Coverage cannot cover branches where macros are disabled). In modern C++, it is recommended to prioritize `if constexpr` combined with constant objects.

------

#### Path Search and Library Linking: `-I`, `-isystem`, `-L`, `-l`

This is the place where beginners are most prone to configuration errors in CMake.

- **`-I` (Include)**: Specify header file search paths.
- **`-isystem`**: Specify "system" header file paths.
  - **The subtlety**: If a third-party library (like ST's HAL library) generates a lot of meaningless warnings, use `-isystem` to include them. The compiler will **automatically suppress all warnings in that directory**, keeping your console clean.
- **`-L`**: Specify the search directory for static libraries (`.a` files).
- **`-l`**: Link the specified library.
  - Note: If the library name is `libfoo.a`, the parameter is `-lfoo` (remove the `lib` prefix and extension).

------

#### Output Management and Debug Info: `-o` and `-g`

- **`-o`**: Specify the output filename. In cross-compilation, we usually generate an ELF file, and then convert it to HEX or BIN via `objcopy`.
- **`-g` and `-g3`**:
  - `-g` produces standard debugging symbols for GDB debugging.
  - **`-g3`**: Even includes debug information for macro definitions. If you need to check the value of a specific macro during debugging, turn this on.
  - **Misconception corrected**: Enabling `-g` **does not** increase the code size running on the board. Debugging information only exists in the ELF file on your computer and is not flashed into the MCU's Flash.

------

#### Warning Management: The `-W` Series (Code Quality)

In safety-sensitive fields like embedded systems, warnings are hidden bugs.

- **`-Wall`**: The standard for most developers, enabling most valuable warnings.
- **`-Werror`**: **Treat all warnings as errors**.
  - *Recommended practice*: Force `-Werror` in CI/CD (Continuous Integration) environments to ensure submitted code has no hidden dangers.
- **`-Wshadow`**: Warns when a local variable name shadows a global variable name, which is extremely useful during embedded logic switching.
- **`-Wdouble-promotion`**: **Embedded essential!** Warns when you unintentionally promote a `float` to a `double`. On MCUs without double-precision hardware FPU, this leads to a catastrophic drop in performance.

------

#### Dependency Generation: `-M`, `-MD`

Have you ever wondered how CMake knows "since you modified a header file, these 10 source files need to be recompiled"?

- **`-MD`**: Generates a dependency relationship file with a `.d` suffix during compilation.
- **Automation**: Modern build systems (CMake/Ninja) handle these options automatically. Understanding this helps you troubleshoot incremental compilation issues like "why didn't the compiler react after I changed the code".

```text
# Example of generated dependencies (foo.o: foo.c foo.h)
main.o: main.cpp config.h hal.hpp
```

------

## 1. Optimization Levels: Balancing Speed, Size, and Debugging

GCC and Clang provide multi-level optimization switches. Understanding their differences is a fundamental skill for embedded developers.

| **Option** | **Name** | **Core Behavior** | **Applicable Scenarios** |
| --- | --- | --- | --- |
| **`-O0`** | No optimization | Maintains a one-to-one correspondence between code and assembly. | Only for tracking down extremely difficult logic bugs. |
| **`-Og`** | Debug optimization | Enables optimizations that do not affect debugging observation. | **First choice for development phase**, balancing performance and single-stepping. |
| **`-O2`** | Performance optimization | Enables almost all optimizations that do not trade space for time. | High-performance computing, RTOS task logic. |
| **`-Os`** | Size optimization | Enables options in `-O2` that do not increase code size. | **Default choice for embedded release**. |
| **`-Ofast`** | Fast optimization | Disregards IEEE 754 standard (does not guarantee floating-point precision). | Pure math calculations where minor precision differences are acceptable. |

### 💡 Deep Dive: Why not use `-O3` in embedded?

`-O3` performs extensive loop unrolling and function inlining. While speed might increase, on MCUs with tight Flash space, it leads to code bloat. It might even degrade performance due to instruction cache (I-Cache) misses.

------

## 2. Trimming C++ Runtime: Removing Heavy "Armor"

Modern C++ carries some features by default that are extremely expensive in embedded contexts. Through the following two options, we can "slim down" C++ to have overhead similar to C.

### 2.1 `-fno-exceptions` (Disable Exceptions)

- **Cost**: C++ exceptions require massive "unwind table" support, increasing Flash footprint by about 10%~20%.
- **Consequence**: Cannot use `try` and `catch`. If the program errors, it will directly call `std::terminate`.
- **Embedded guideline**: In resource-constrained systems (like Cortex-M), **strongly recommended to disable**.

### 2.2 `-fno-rtti` (Disable Run-Time Type Information)

- **Cost**: To support `dynamic_cast` and `typeid`, the compiler generates extra metadata (information beyond the vtable) for every class with virtual functions.
- **Consequence**: Cannot determine the real type of an object at runtime.
- **Embedded guideline**: Modern embedded design prefers compile-time polymorphism (templates/CRTP), so RTTI is usually redundant.

------

## 3. Garbage Collecting Unused Code

By default, the compiler compiles the entire source file into one massive binary block. Even if you only use one function from a library, the linker will stuff the entire library's code into Flash.

### 3.1 Compiler Side: Sectioning

- **`-ffunction-sections`**: Packages each function independently into a section.
- **`-fdata-sections`**: Packages each global/static variable independently.

### 3.2 Linker Side: Garbage Collection

- **`-Wl,--gc-sections`**: Tells the linker (ld) to scan all sections and thoroughly remove "dead code" that is not referenced from the final ELF file.

------

## 4. Best Practice Configuration in CMake

Translating the above theory into code. In your top-level `CMakeLists.txt`, we recommend managing these options like this:

```cmake
# 1. Set language standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Use pure standard mode, disable GNU extensions

# 2. Optimization and Debug Symbols
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

# 3. Compiler Flags
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wdouble-promotion
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics> # Disable mutex guard for static locals
)

# 4. Linker Flags (Garbage Collection)
add_link_options(
    -Wl,--gc-sections
    # If using newlib-nano, specify the lib path
    -Wl,--print-memory-usage # Print memory usage report after linking
)
```

------

## 5. Dangerous `-Ofast` and Floating-Point Traps

In embedded systems, `-Ofast` enables `-ffast-math`. This can lead to:

1. **Precision loss**: To speed up execution, the compiler may ignore tiny floating-point errors.
2. **NaN/Inf failure**: It assumes your program will never produce illegal floating-point numbers.
3. **Reordering operations**: This can lead to unstable results in some algorithms.

**Recommendation**: Unless you are doing pure digital signal processing (DSP) and have full control over precision, always stick to `-O2` or `-Os`.

## Run Online

Compare the assembly code generated by the compiler under different optimization levels (`-O0` / `-Os` / `-O2`) online to observe the effects of inlining and constant folding:

<OnlineCompilerDemo
  title="Common Compiler Options"
  source-path="code/examples/vol7/14_compiler_options.cpp"
  description="Compare assembly generated under -O0 / -Os / -O2, observe inlining and constant folding"
  allow-x86-asm
  arm-source-path="code/examples/compiler_explorer/compiler_opts_arm.cpp"
  allow-arm-asm
/>
