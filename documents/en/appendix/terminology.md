---
chapter: 99
description: Project Standard Translation Table for English and Chinese Technical
  Terms
order: 0
reading_time_minutes: 8
tags:
- 基础
title: Glossary
translation:
  source: documents/appendix/terminology.md
  source_hash: 0d52626849b1cc68c57191999e7ed6ad5590173793d81a014f33dce82adee305
  translated_at: '2026-06-16T03:26:57.108010+00:00'
  engine: anthropic
  token_count: 1623
---
# Glossary

This document collects core terms appearing in the project tutorials, grouped by domain, providing Chinese-English comparison. The goal is to ensure consistent terminology translation throughout the text, preventing different translations for the same concept across different articles.

## C++ Language Features

| English | Chinese | Notes |
|---------|------|------|
| RAII (Resource Acquisition Is Initialization) | 资源获取即初始化 | Core C++ resource management idiom |
| move semantics | 移动语义 | Core C++11 feature, avoids unnecessary copies |
| rvalue reference | 右值引用 | `T&&`, foundation of move semantics |
| perfect forwarding | 完美转发 | `std::forward`, preserves value category |
| copy elision | 拷贝消除 | Compiler optimization, omits copy/move operations |
| return value optimization (RVO) | 返回值优化 | Named NRVO, unnamed URVO |
| zero-overhead abstraction | 零开销抽象 | C++ design philosophy, you don't pay for what you don't use |
| smart pointer | 智能指针 | `std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr` |
| unique pointer | 独占指针 | `std::unique_ptr`, exclusive ownership |
| shared pointer | 共享指针 | `std::shared_ptr`, reference-counted shared ownership |
| weak pointer | 弱引用指针 | `std::weak_ptr`, breaks circular references |
| intrusive pointer | 侵入式指针 | Reference count embedded inside the object |
| constexpr | 常量表达式 | Compile-time evaluation, introduced in C++11 |
| consteval | 立即函数 | C++20, forces compile-time evaluation |
| constinit | 常量初始化 | C++20, avoids static initialization order issues |
| SFINAE (Substitution Failure Is Not An Error) | 替换失败并非错误 | Core mechanism of template metaprogramming |
| CRTP (Curiously Recurring Template Pattern) | 奇异递归模板模式 | Static polymorphism idiom |
| template | 模板 | Foundation of generic programming |
| template specialization | 模板特化 | Providing custom implementations for specific types |
| template instantiation | 模板实例化 | Compiler generating concrete code from templates |
| generic programming | 泛型编程 | Programming paradigm based on templates |
| type safety | 类型安全 | Catching type errors at compile time |
| type deduction / inference | 类型推断 | `auto`, `decltype`, template argument deduction |
| type traits | 类型特征 | `<type_traits>`, compile-time type queries |
| concepts | 概念 | C++20, named constraints on template parameters |
| constraints | 约束 | `requires` clause, restricts template parameters |
| lambda expression | Lambda 表达式 | Anonymous function objects, introduced in C++11 |
| structured binding | 结构化绑定 | C++17, `auto [x, y] = ...` |
| enum class | 限定作用域枚举 | C++11, type-safe enumerations |
| variant | 变体类型 | `std::variant`, type-safe union |
| optional | 可选值 | `std::optional`, values that can be empty |
| expected | 预期值 | C++23, return values carrying error information |
| any | 任意类型 | `std::any`, type-erased container |
| scope guard | 作用域守卫 | Executes cleanup actions upon destruction |
| coroutine | 协程 | C++20, `co_await`/`co_yield`/`co_return` |
| module | 模块 | C++20, compilation unit replacing headers |
| range | 范围 | C++20, composable algorithm library |
| view | 视图 | Lazy evaluation adapters in ranges library |
| undefined behavior (UB) | 未定义行为 | Behavior not defined by the standard, unpredictable results |
| one definition rule (ODR) | 唯一定义规则 | Each entity must have exactly one definition in the program |
| stack unwinding | 栈展开 | Destroying stack objects layer by layer during exception handling |
| designated initializer | 指定初始化器 | C++20, `Type x{.field = value}` |
| user-defined literal | 用户自定义字面量 | `operator ""` |
| spaceship operator | 飞船运算符 | C++20, `<=>` three-way comparison |
| atomic operation | 原子操作 | Indivisible concurrent-safe operations |
| memory order | 内存序 | Ordering constraints on atomic operations |
| lock-free | 无锁 | Concurrent algorithms without mutexes |
| mutex | 互斥量 | Mutual exclusion lock, protects shared data |
| semaphore | 信号量 | Counting synchronization primitive |
| critical section | 临界区 | Code segment allowing only one thread at a time |
| dead lock | 死锁 | Threads waiting for each other to release resources |
| thread | 线程 | `std::thread`, unit of concurrent execution |
| span | 视图跨度 | `std::span`, non-owning view of contiguous sequences |
| EBO (Empty Base Optimization) | 空基类优化 | Empty classes take no space as base classes |
| static polymorphism | 静态多态 | Compile-time polymorphism, based on CRTP or templates |

## Embedded Hardware

| English | Chinese | Notes |
|---------|------|------|
| MCU (Microcontroller Unit) | 微控制器 | Single chip integrating CPU, memory, peripherals |
| SoC (System on Chip) | 片上系统 | Highly integrated single-chip system |
| register | 寄存器 | Hardware programmable control/data units |
| interrupt | 中断 | Hardware signal breaking CPU normal execution flow |
| interrupt service routine (ISR) | 中断服务程序 | Function executed when an interrupt triggers |
| DMA (Direct Memory Access) | 直接内存访问 | Data transfer between peripherals and memory without CPU |
| GPIO (General-Purpose I/O) | 通用输入输出 | Configurable digital pins |
| ADC (Analog-to-Digital Converter) | 模数转换器 | Analog signal to digital signal |
| DAC (Digital-to-Analog Converter) | 数模转换器 | Digital signal to analog signal |
| PWM (Pulse Width Modulation) | 脉宽调制 | Controlling output via duty cycle |
| PLL (Phase-Locked Loop) | 锁相环 | Clock multiplication circuit |
| AHB (Advanced High-performance Bus) | 高级高性能总线 | ARM internal high-speed bus |
| APB (Advanced Peripheral Bus) | 高级外设总线 | ARM internal peripheral bus |
| clock tree | 时钟树 | Clock distribution network from crystal to modules |
| pull-up resistor | 上拉电阻 | Defaults to high level |
| pull-down resistor | 下拉电阻 | Defaults to low level |
| push-pull | 推挽输出 | Can actively drive high/low levels |
| open-drain | 开漏输出 | Can only pull low, requires external pull-up |
| debounce | 消抖 | Removing jitter from mechanical switches |
| watchdog | 看门狗 | Safety mechanism to reset CPU on timeout |
| EXTI (External Interrupt) | 外部中断 | Interrupt triggered by external pins |
| peripheral | 外设 | Independent functional modules inside MCU |
| PCB (Printed Circuit Board) | 印制电路板 | Carrier for electronic components |
| NVIC (Nested Vectored Interrupt Controller) | 嵌套向量中断控制器 | ARM Cortex-M interrupt controller |
| HAL (Hardware Abstraction Layer) | 硬件抽象层 | ST official peripheral driver library |
| linker script | 链接脚本 | Defines memory layout and section placement |
| startup code | 启动代码 | C runtime initialization, executes before main |

## RTOS (Real-Time Operating System)

| English | Chinese | Notes |
|---------|------|------|
| RTOS (Real-Time Operating System) | 实时操作系统 | OS guaranteeing response times |
| scheduler | 调度器 | Decides which task gets the CPU |
| context switch | 上下文切换 | Saving/restoring task execution state |
| priority inversion | 优先级反转 | Low-priority task blocking high-priority task |
| preemptive scheduling | 抢占式调度 | High-priority tasks can preempt low-priority ones |
| cooperative scheduling | 协作式调度 | Tasks voluntarily yield the CPU |
| task / thread | 任务 / 线程 | Unit of execution in RTOS |
| tick | 系统节拍 | Basic time unit of RTOS |
| deadline | 截止时间 | Time point by which a task must complete |
| queue | 消息队列 | FIFO for passing data between tasks |
| priority inheritance | 优先级继承 | Protocol to solve priority inversion |
| inter-process communication (IPC) | 进程间通信 | Data exchange mechanism between tasks |
| binary semaphore | 二值信号量 | Semaphore with only 0/1 states |
| counting semaphore | 计数信号量 | Semaphore that can be greater than 1 |
| event group | 事件组 | Multi-bit event synchronization mechanism |
| idle task | 空闲任务 | Runs when no other tasks are ready |
| real-time | 实时 | Deterministic response time requirements |

## Toolchain

| English | Chinese | Notes |
|---------|------|------|
| cross-compile | 交叉编译 | Generating code for one platform on another |
| toolchain | 工具链 | Collection of compiler + assembler + linker |
| CMake | CMake | Cross-platform build system generator |
| Makefile | Makefile | Configuration file for make build tool |
| flash | 烧录 | Writing program to target chip |
| debug probe | 调试探针 | Hardware debugger connecting host and target board |
| JTAG | JTAG | Joint Test Action Group debug interface |
| SWD (Serial Wire Debug) | 串行线调试 | ARM two-wire debug interface |
| OpenOCD | OpenOCD | Open On-Chip Debugger |
| ELF (Executable and Linkable Format) | ELF 格式 | Executable and Linkable Format, compiler output |
| hex | Intel HEX 格式 | Text format for flashing |
| objcopy | 对象复制 | Format conversion tool (ELF→HEX/BIN) |
| compiler flag | 编译器选项 | Command-line parameters controlling compilation |
| optimization level | 优化等级 | `-O0`/`-O1`/`-O2`/`-O3`/`-Os` |
| preprocessor | 预处理器 | Handles `#include`, `#define`, etc. |
| linker | 链接器 | Merges object files into executable |
| assembler | 汇编器 | Converts assembly code to object files |
| build system | 构建系统 | Tool automating the compilation process |
| dependency | 依赖 | One module requiring another |
| static library | 静态库 | `.a`/`.lib` files linked at compile time |
| shared library | 动态库 | `.so`/`.dll` files loaded at runtime |

## Debugging

| English | Chinese | Notes |
|---------|------|------|
| breakpoint | 断点 | Marker to pause program execution |
| watchpoint | 观察点 | Marker monitoring memory/variable changes |
| trace | 跟踪 | Recording program execution flow |
| semihosting | 半主机 | Target board using host I/O via debugger |
| ITM (Instrumentation Trace Macrocell) | 指令跟踪宏单元 | ARM Cortex-M debug output |
| ETM (Embedded Trace Macrocell) | 嵌入式跟踪宏单元 | Instruction-level execution tracing |
| logic analyzer | 逻辑分析仪 | Tool capturing multi-channel digital signals |
| oscilloscope | 示波器 | Instrument for observing electrical signal waveforms |
| GDB (GNU Debugger) | GDB 调试器 | GNU open-source debugger |
| core dump | 核心转储 | Memory snapshot when program crashes |
| backtrace | 调用栈回溯 | History of function call chain |
| single-step | 单步执行 | Executing instruction by instruction / statement by statement |
| memory leak | 内存泄漏 | Allocated memory not being freed |
| stack overflow | 栈溢出 | Stack space exhausted |
