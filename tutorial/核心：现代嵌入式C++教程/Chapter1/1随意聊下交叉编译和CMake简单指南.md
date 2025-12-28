# 现代嵌入式C++教程：交叉编译基础与CMake多目标构建

## 引言

在嵌入式开发领域，我们经常面临一个有趣的挑战：开发环境与目标运行环境往往是完全不同的硬件平台。你可能在一台强大的x86_64工作站上编写代码,但最终程序需要运行在ARM架构的微控制器或者RISC-V处理器上。这就是交叉编译(Cross Compilation)存在的意义所在。

本文将深入探讨交叉编译的基础概念,并详细介绍如何使用CMake这一现代构建系统来管理多目标平台的构建流程。无论你是刚接触嵌入式开发的新手,还是希望优化现有构建流程的资深开发者,本文都将为你提供实用的知识和技巧。

## 第一部分:交叉编译基础

#### 什么是交叉编译

交叉编译是指在**一个平台(主机平台,Host Platform)上编译生成能在另一个平台(目标平台,Target Platform)上运行的可执行程序的过程**。这与我们常见的本地编译(Native Compilation)形成对比——本地编译生成的程序在编译它的同一平台上运行。

举个简单的例子:当你在你的Ubuntu x86_64笔记本上编译一个C++程序,并且这个程序将在树莓派的ARM处理器上运行时,你就在进行交叉编译。

#### 为什么需要交叉编译

这个问题其实不算是问题，我问一个问题——您敢在您的单片机上，部署完整的编译工具链嘛？一个只有几MB Flash和几十KB RAM的微控制器显然无法运行GCC编译器。

而且，即使目标设备理论上能够编译代码,在资源受限的硬件上进行编译也会非常缓慢。相比之下,在性能强大的开发机上编译可以大大缩短开发周期,提高工作效率。桌面开发环境通常拥有更完善的开发工具生态系统,包括IDE、调试器、性能分析工具等,这些工具可以显著提升开发体验。

#### 交叉编译工具链

交叉编译工具链(Cross Compilation Toolchain)是一套专门用于交叉编译的工具集合,通常包括:

- **交叉编译器(Cross Compiler)**:这是工具链的核心,例如arm-none-eabi-gcc用于裸机ARM开发,aarch64-linux-gnu-gcc用于ARM64 Linux系统。编译器负责将源代码翻译成目标平台的机器码。

- **交叉汇编器(Cross Assembler)**:将汇编语言代码转换为目标平台的机器码,通常与编译器配套使用。

- **交叉链接器(Cross Linker)**:将编译生成的多个目标文件(.o文件)链接成最终的可执行文件或库文件,处理符号解析和地址重定位。

- **标准库**:针对目标平台编译的C/C++标准库,包括libc、libstdc++等。这些库必须针对目标架构进行编译。

- **辅助工具**:如objdump(查看目标文件)、objcopy(转换目标文件格式)、size(查看程序大小)、nm(查看符号表)等工具。

  ##### 目标三元组(Target Triplet)

在交叉编译中,我们使用"目标三元组"来精确描述目标平台。这个三元组通常由三部分或四部分组成:

```
<架构>-<厂商>-<操作系统>-<ABI>
```

让我们看几个实际例子:

- `arm-none-eabi`:ARM架构,无厂商,无操作系统(裸机),EABI(嵌入式应用二进制接口)
- `aarch64-linux-gnu`:ARM64架构,Linux操作系统,GNU工具链
- `x86_64-w64-mingw32`:x86_64架构,Windows操作系统,MinGW工具链
- `riscv64-unknown-elf`:RISC-V 64位架构,未知厂商,ELF格式

理解目标三元组对于选择正确的工具链和配置构建系统至关重要。不同的三元组意味着不同的指令集、调用约定、二进制格式和运行时环境。

#### 交叉编译的挑战

交叉编译虽然强大,但也带来了一些挑战:

**依赖关系管理**:当程序依赖第三方库时,你需要确保这些库也是为目标平台编译的。不能将为x86编译的库链接到ARM程序中。

**系统调用差异**:不同操作系统的系统调用接口不同,需要在代码中妥善处理这些差异。

**字节序问题**:不同架构可能使用不同的字节序(大端或小端),在处理网络协议或文件格式时需要特别注意。

**指针大小**:32位和64位架构的指针大小不同,可能导致难以察觉的bug。

**浮点运算**:不同平台的浮点运算实现可能有细微差异,某些嵌入式平台甚至没有硬件浮点单元。

## CMake构建系统基础

额，这个地方没有实战，就大伙看看就行，后面的话，专门有个小章节唠唠这个事情。

### 为什么选择CMake

CMake(Cross-platform Make)是一个跨平台的构建系统生成器。它不直接构建程序,而是生成本地构建系统所需的文件(如Makefile、Ninja构建文件或Visual Studio项目文件)。

对于嵌入式开发,CMake具有以下优势:

**跨平台支持**:同一套CMake配置可以在Linux、Windows、macOS上使用,生成对应平台的构建文件。

**交叉编译支持**:CMake原生支持交叉编译,通过Toolchain文件可以轻松配置目标平台。

**模块化设计**:CMake的模块系统便于管理复杂项目的多个组件和依赖关系。

**现代化特性**:支持目标(Target)导向的构建配置,使依赖关系更清晰,配置更直观。

**广泛的IDE支持**:主流IDE如CLion、Visual Studio Code、Qt Creator等都对CMake有良好支持。

### CMake基本概念

在深入交叉编译配置之前,让我们快速回顾CMake的几个核心概念:

**CMakeLists.txt**:这是CMake的配置文件,描述了项目的结构、源文件、依赖关系和构建规则。

**Target(目标)**:可以是可执行文件、库文件或自定义目标。现代CMake推荐以目标为中心的配置方式。

**Generator(生成器)**:决定CMake生成什么类型的构建系统文件,如Unix Makefiles、Ninja、Visual Studio等。

**Build Tree和Source Tree**:源码树包含源代码和CMakeLists.txt,构建树是生成的构建文件和编译产物的存放位置。推荐使用out-of-source构建,保持源码目录整洁。

**变量和缓存**:CMake使用变量来存储配置信息,某些变量会被缓存以便在后续配置中重用。

## CMake交叉编译配置

### 3.1 Toolchain文件的作用

Toolchain文件是CMake交叉编译的核心。它是一个CMake脚本文件,描述了交叉编译所需的所有信息,包括编译器路径、目标系统信息、编译选项等。

使用Toolchain文件的好处:

- **可重用性**:一次配置,多个项目共用
- **版本控制**:可以将Toolchain文件纳入版本控制,确保团队使用相同配置
- **清晰分离**:将平台相关配置与项目逻辑分离

### 编写Toolchain文件

让我们从一个ARM Cortex-M的Toolchain文件示例开始:

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

让我们详细解读这个文件的各个部分:

**CMAKE_SYSTEM_NAME**:指定目标系统类型。`Generic`表示无操作系统的裸机环境,也可以是`Linux`、`Windows`等。

**CMAKE_SYSTEM_PROCESSOR**:指定目标处理器架构,如`arm`、`aarch64`、`riscv64`等。

**编译器设置**:明确指定要使用的交叉编译器。CMake会使用这些编译器而不是系统默认编译器。

**编译器标志**:

- `-mcpu=cortex-m4`:指定目标CPU型号
- `-mthumb`:使用Thumb指令集(代码密度更高)
- `-mfloat-abi=hard`:使用硬件浮点ABI
- `-mfpu=fpv4-sp-d16`:指定浮点单元类型
- `-fno-exceptions`:禁用C++异常(嵌入式常见)
- `-fno-rtti`:禁用运行时类型信息

**CMAKE_FIND_ROOT_PATH_MODE系列**:控制CMake在查找库、头文件等资源时的搜索行为,避免意外使用主机平台的库。

### 更复杂的Toolchain示例:ARM Linux

对于运行Linux的ARM设备(如树莓派),Toolchain文件会有所不同:

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

这个例子引入了`CMAKE_SYSROOT`概念。Sysroot是一个目录,包含了目标系统的根文件系统副本,包括库文件、头文件等。这对于有完整操作系统的目标平台非常重要。

### 使用Toolchain文件

使用Toolchain文件进行配置:

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

重要提示:**Toolchain文件必须在第一次运行CMake时通过`-DCMAKE_TOOLCHAIN_FILE`指定**,之后会被缓存。如果需要更换Toolchain,必须删除构建目录重新配置。

## 第四部分:CMake多目标构建

### 什么是多目标构建

多目标构建是指同一套源代码能够为不同的目标平台生成可执行程序。在嵌入式开发中,这非常常见:

- 为多个硬件变体构建(STM32F4、STM32F7)
- 同时支持开发板和产品板
- 在主机平台构建测试版本和在目标平台构建发布版本
- 支持多个操作系统(Linux、RTOS、裸机)

### 基于构建目录的多目标方案

最简单的多目标构建方案是为每个平台创建独立的构建目录:

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

构建脚本示例:

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

### 条件编译与平台检测

在CMakeLists.txt中,我们需要根据不同平台进行条件配置:

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

### 使用生成器表达式

CMake的生成器表达式(Generator Expressions)提供了更灵活的条件配置方式:

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

### 平台抽象层(HAL)设计

在多目标项目中,良好的硬件抽象层设计至关重要:

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

### 配置变体管理

对于同一架构的不同硬件变体,可以使用CMake的选项和缓存变量:

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

使用时:

```bash
cmake -B build-f407 -DTARGET_BOARD=STM32F407_DISCOVERY \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-cortex-m4.cmake

cmake -B build-f429 -DTARGET_BOARD=STM32F429_DISCO \
      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-cortex-m4.cmake
```

