---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Introduces the basic concepts of cross-compilation and toolchains, and how to configure multi-target builds with CMake
difficulty: beginner
order: 1
platform: host
reading_time_minutes: 13
related: []
tags:
- cpp-modern
- host
- intermediate
title: A Simple Guide to Cross-Compilation and CMake
translation:
  source: documents/vol7-engineering/01-cross-compilation-and-cmake.md
  source_hash: 84a002ca0b920c03b74ee9b6ce4f54a33b6844bf467e78a65dc6806e7a5f0a58
  translated_at: '2026-11-22T16:43:35+00:00'
  engine: glm-5.3-refresh
  token_count: 10373
---
# A Modern Embedded C++ Tutorial: Cross-Compilation Fundamentals and Multi-Target Builds with CMake

In embedded development we often face an interesting challenge: the development environment and the target runtime environment are usually two completely different hardware platforms. You might write code on a powerful x86_64 workstation, yet the final program has to run on an ARM-based microcontroller or a RISC-V processor. That is exactly why cross compilation exists.

This article takes a deep dive into the fundamentals of cross compilation, then walks through in detail how to use CMake, a modern build system, to manage the build process for multiple target platforms. Whether you are new to embedded development or a seasoned developer looking to optimize an existing build workflow, you will find practical knowledge and techniques here.

## Part 1: Cross-Compilation Fundamentals

#### What Is Cross-Compilation

Cross compilation means **the process of building, on one platform (the host platform), an executable program that runs on another platform (the target platform)**. It stands in contrast to the native compilation we all know—native compilation produces a program that runs on the same platform that compiled it.

A simple example: when you compile a C++ program on your Ubuntu x86_64 laptop and that program will run on a Raspberry Pi's ARM processor, you are cross compiling.

#### Why We Need Cross-Compilation

This question really isn't much of a question. Let me pose one instead—would you dare deploy a full compiler toolchain on your MCU? A microcontroller with only a few MB of Flash and a few dozen KB of RAM obviously cannot run GCC.

Besides, even if the target device could theoretically compile code, building on resource-constrained hardware would be painfully slow. By contrast, compiling on a powerful development machine greatly shortens the development cycle and improves productivity. Desktop development environments also tend to have a far more complete ecosystem of development tools—IDEs, debuggers, profilers, and so on—which can noticeably improve the development experience.

#### The Cross-Compilation Toolchain

A cross compilation toolchain is a set of tools dedicated to cross compiling. It typically includes:

- **Cross compiler**: the core of the toolchain. For example, arm-none-eabi-gcc is used for bare-metal ARM development and aarch64-linux-gnu-gcc for ARM64 Linux systems. The compiler translates source code into machine code for the target platform.

- **Cross assembler**: converts assembly code into machine code for the target platform; it usually comes paired with the compiler.

- **Cross linker**: links the multiple object files (.o files) produced by compilation into the final executable or library, handling symbol resolution and address relocation.

- **Standard libraries**: C/C++ standard libraries built for the target platform, including libc, libstdc++, and others. These libraries must be compiled for the target architecture.

- **Auxiliary tools**: objdump (inspecting object files), objcopy (converting object file formats), size (checking program size), nm (viewing the symbol table), and so on.

##### The Target Triplet

In cross compilation we use the "target triplet" to describe the target platform precisely. This triplet usually consists of three or four parts:

```cpp

<架构>-<厂商>-<操作系统>-<ABI>

```

Let's look at a few real examples:

- `arm-none-eabi`: ARM architecture, no vendor, no operating system (bare metal), EABI (Embedded Application Binary Interface)
- `aarch64-linux-gnu`: ARM64 architecture, Linux operating system, GNU toolchain
- `x86_64-w64-mingw32`: x86_64 architecture, Windows operating system, MinGW toolchain
- `riscv64-unknown-elf`: 64-bit RISC-V architecture, unknown vendor, ELF format

Understanding the target triplet is crucial for picking the right toolchain and configuring the build system. Different triplets mean different instruction sets, calling conventions, binary formats, and runtime environments.

#### Challenges of Cross-Compilation

Powerful as it is, cross compilation brings a few challenges:

**Dependency management**: when your program depends on third-party libraries, you need to make sure those libraries are also built for the target platform. You cannot link a library compiled for x86 into an ARM program.

**System call differences**: different operating systems expose different system call interfaces, and these differences must be handled properly in your code.

**Endianness**: different architectures may use different byte orders (big-endian or little-endian), which calls for special care when handling network protocols or file formats.

**Pointer size**: 32-bit and 64-bit architectures have different pointer sizes, which can lead to hard-to-spot bugs.

**Floating-point arithmetic**: floating-point implementations may differ subtly across platforms, and some embedded platforms do not even have a hardware floating-point unit.

## CMake Build System Fundamentals

Er, right—this part has no hands-on practice, so just read through it for now. We will come back and talk this over properly in a dedicated little chapter later on.

### Why Choose CMake

CMake (Cross-platform Make) is a cross-platform build system generator. It does not build programs directly; instead, it generates the files a native build system needs (such as Makefiles, Ninja build files, or Visual Studio project files).

For embedded development, CMake has the following advantages:

**Cross-platform support**: the same set of CMake configuration can be used on Linux, Windows, and macOS, generating build files for each platform.

**Cross-compilation support**: CMake supports cross compilation natively, and toolchain files make it easy to configure the target platform.

**Modular design**: CMake's module system makes it easy to manage the many components and dependencies of a complex project.

**Modern features**: it supports target-oriented build configuration, which makes dependencies clearer and configuration more intuitive.

**Broad IDE support**: mainstream IDEs such as CLion, Visual Studio Code, and Qt Creator all support CMake well.

### Basic CMake Concepts

Before diving into cross-compilation configuration, let's quickly review a few core CMake concepts:

**CMakeLists.txt**: this is CMake's configuration file; it describes the project's structure, source files, dependencies, and build rules.

**Target**: this can be an executable, a library, or a custom target. Modern CMake recommends a target-centric configuration style.

**Generator**: this determines what kind of build system files CMake generates, such as Unix Makefiles, Ninja, or Visual Studio.

**Build tree and source tree**: the source tree contains the source code and CMakeLists.txt, while the build tree is where the generated build files and compiled artifacts live. Out-of-source builds are recommended to keep the source directory clean.

**Variables and cache**: CMake uses variables to store configuration information, and some variables are cached so that later configuration runs can reuse them.

## Configuring CMake for Cross-Compilation

### 3.1 The Role of Toolchain Files

Toolchain files are the heart of cross compiling with CMake. A toolchain file is a CMake script that describes everything cross compilation requires: compiler paths, target system information, compiler options, and so on.

The benefits of using a toolchain file:

- **Reusability**: configure once, share across multiple projects
- **Version control**: toolchain files can be placed under version control, ensuring the whole team uses the same configuration
- **Clean separation**: platform-specific configuration is kept apart from project logic

### Writing a Toolchain File

Let's start with an example toolchain file for an ARM Cortex-M:

```cmake

# arm-none-eabi-toolchain.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 指定交叉编译器
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# 指定工具链程序
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(CMAKE_SIZE arm-none-eabi-size)

# 设置编译器标志
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-exceptions -fno-rtti")

# 设置链接器标志
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=nosys.specs -Wl,--gc-sections")

# 搜索路径配置
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

```

Let's walk through the parts of this file in detail:

**CMAKE_SYSTEM_NAME**: specifies the target system type. `Generic` means a bare-metal environment with no operating system; it can also be `Linux`, `Windows`, and so on.

**CMAKE_SYSTEM_PROCESSOR**: specifies the target processor architecture, such as `arm`, `aarch64`, or `riscv64`.

**Compiler settings**: explicitly specifies the cross compilers to use. CMake will use these compilers instead of the system defaults.

**Compiler flags**:

- `-mcpu=cortex-m4`: specifies the target CPU model
- `-mthumb`: use the Thumb instruction set (higher code density)
- `-mfloat-abi=hard`: use the hardware floating-point ABI
- `-mfpu=fpv4-sp-d16`: specifies the floating-point unit type
- `-fno-exceptions`: disable C++ exceptions (common in embedded work)
- `-fno-rtti`: disable run-time type information

**The CMAKE_FIND_ROOT_PATH_MODE family**: controls how CMake searches for libraries, headers, and other resources, preventing accidental use of host-platform libraries.

### A More Complex Toolchain Example: ARM Linux

For ARM devices running Linux (such as a Raspberry Pi), the toolchain file looks somewhat different:

```cmake

# aarch64-linux-gnu-toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 工具链安装路径
set(TOOLCHAIN_PREFIX /usr/aarch64-linux-gnu)

# 编译器
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Sysroot设置(包含目标系统的库和头文件)
set(CMAKE_SYSROOT ${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PREFIX})

# 编译器标志
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

# 搜索配置
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# pkg-config配置
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})

```

This example introduces the `CMAKE_SYSROOT` concept. A sysroot is a directory containing a copy of the target system's root filesystem, including libraries and header files. This is very important for target platforms that run a full operating system.

### Using a Toolchain File

Configure with a toolchain file like this:

```bash

# 创建构建目录
mkdir build-arm && cd build-arm

# 使用toolchain文件配置CMake
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-none-eabi-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      ..

# 构建
cmake --build .

```

Important: **the toolchain file must be specified via `-DCMAKE_TOOLCHAIN_FILE` the first time CMake runs**, after which it is cached. If you need to switch toolchains, you must delete the build directory and reconfigure.

## Part 4: Multi-Target Builds with CMake

### What Is a Multi-Target Build

A multi-target build means the same source code can produce executables for different target platforms. In embedded development this is very common:

- Building for multiple hardware variants (STM32F4, STM32F7)
- Supporting both dev boards and production boards at the same time
- Building test versions on the host platform and release versions for the target platform
- Supporting multiple operating systems (Linux, RTOS, bare metal)

### A Build-Directory-Based Multi-Target Approach

The simplest multi-target scheme is to create a separate build directory for each platform:

```bash

# 项目结构
project/
├── src/
├── include/
├── toolchains/
│   ├── arm-cortex-m4.cmake
│   ├── arm-cortex-m7.cmake
│   └── x86_64-linux.cmake
├── CMakeLists.txt
└── builds/
    ├── cortex-m4/
    ├── cortex-m7/
    └── host/

```

An example build script:

```bash
#!/bin/bash

# 构建Cortex-M4版本
cmake -S . -B builds/cortex-m4 \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-cortex-m4.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build builds/cortex-m4

# 构建Cortex-M7版本
cmake -S . -B builds/cortex-m7 \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-cortex-m7.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build builds/cortex-m7

# 构建主机测试版本
cmake -S . -B builds/host \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build builds/host

```

### Conditional Compilation and Platform Detection

In CMakeLists.txt, we need conditional configuration based on the platform:

```cmake
cmake_minimum_required(VERSION 3.20)
project(EmbeddedApp CXX C ASM)

# 检测目标平台
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    message(STATUS "Building for ARM architecture")

    # ARM特定配置
    add_compile_definitions(TARGET_ARM)

    if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
        message(STATUS "Bare-metal ARM target")
        add_compile_definitions(BARE_METAL)
    endif()

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    message(STATUS "Building for x86_64 architecture")
    add_compile_definitions(TARGET_X86_64)

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    message(STATUS "Building for RISC-V 64-bit")
    add_compile_definitions(TARGET_RISCV64)
endif()

# 添加源文件
set(COMMON_SOURCES
    src/main.cpp
    src/application.cpp
)

# 平台特定源文件
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    list(APPEND COMMON_SOURCES
        src/startup_arm.s
        src/hal_bare_metal.cpp
    )
else()
    list(APPEND COMMON_SOURCES
        src/hal_linux.cpp
    )
endif()

# 创建可执行目标
add_executable(app ${COMMON_SOURCES})

# 平台特定链接配置
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_link_options(app PRIVATE
        -T${CMAKE_SOURCE_DIR}/linker/STM32F407VG.ld
        -Wl,-Map=${CMAKE_BINARY_DIR}/app.map
    )
endif()

```

### Using Generator Expressions

CMake's generator expressions offer a more flexible way to do conditional configuration:

```cmake

# 根据配置类型设置不同的编译选项
target_compile_options(app PRIVATE
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# 根据编译器类型设置选项
target_compile_options(app PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Weverything>
)

# 根据平台设置链接库
target_link_libraries(app PRIVATE
    $<$<PLATFORM_ID:Linux>:pthread>
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

```

### Designing a Hardware Abstraction Layer (HAL)

In a multi-target project, a well-designed hardware abstraction layer matters a great deal:

```cmake

# 创建HAL接口库
add_library(hal_interface INTERFACE)
target_include_directories(hal_interface INTERFACE
    include/hal
)

# 为不同平台创建HAL实现
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    add_library(hal_impl STATIC
        src/hal/gpio_stm32.cpp
        src/hal/uart_stm32.cpp
        src/hal/timer_stm32.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(hal_impl STATIC
        src/hal/gpio_linux.cpp
        src/hal/uart_linux.cpp
        src/hal/timer_linux.cpp
    )
endif()

target_link_libraries(hal_impl PUBLIC hal_interface)

# 应用程序链接HAL
target_link_libraries(app PRIVATE hal_impl)

```

### Managing Configuration Variants

For different hardware variants of the same architecture, you can use CMake options and cache variables:

```cmake

# 定义硬件变体选项
set(TARGET_BOARD "STM32F407_DISCOVERY" CACHE STRING "Target board")
set_property(CACHE TARGET_BOARD PROPERTY STRINGS
    "STM32F407_DISCOVERY"
    "STM32F429_DISCO"
    "CUSTOM_BOARD_V1"
    "CUSTOM_BOARD_V2"
)

# 根据板子配置
if(TARGET_BOARD STREQUAL "STM32F407_DISCOVERY")
    set(MCU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16")
    set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/linker/STM32F407VG.ld")
    add_compile_definitions(STM32F407xx)

elseif(TARGET_BOARD STREQUAL "STM32F429_DISCO")
    set(MCU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16")
    set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/linker/STM32F429ZI.ld")
    add_compile_definitions(STM32F429xx)

endif()

# 应用配置
add_compile_options(${MCU_FLAGS})
target_link_options(app PRIVATE -T${LINKER_SCRIPT})

```

In use:

```bash
cmake -B build-f407 -DTARGET_BOARD=STM32F407_DISCOVERY \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-cortex-m4.cmake

cmake -B build-f429 -DTARGET_BOARD=STM32F429_DISCO \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-cortex-m4.cmake

```
