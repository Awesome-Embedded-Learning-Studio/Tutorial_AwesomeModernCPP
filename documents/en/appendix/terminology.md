---
chapter: 99
description: The project's standard English-Chinese reference table for technical
  terms
order: 0
reading_time_minutes: 8
tags:
- 基础
title: Glossary
translation:
  source: documents/appendix/terminology.md
  source_hash: 0d52626849b1cc68c57191999e7ed6ad5590173793d81a014f33dce82adee305
  translated_at: '2026-09-27T03:01:43+00:00'
  engine: anthropic
  token_count: 4800
---
# Glossary

This glossary collects the core terms that show up across the project's tutorials, grouped by domain, and pairs each English term with its Chinese counterpart. The point is to keep terminology consistent across the whole site, so the same concept never picks up different translations from one article to the next.

## C++ Language Features

| English | Chinese | Notes |
|---------|------|------|
| RAII (Resource Acquisition Is Initialization) | 资源获取即初始化 | The core C++ resource management paradigm |
| move semantics | 移动语义 | Core C++11 feature, avoids unnecessary copies |
| rvalue reference | 右值引用 | `T&&`, the foundation of move semantics |
| perfect forwarding | 完美转发 | `std::forward`, preserves value category |
| copy elision | 拷贝消除 | Compiler optimization that omits copy/move operations |
| return value optimization (RVO) | 返回值优化 | Named NRVO, unnamed URVO |
| zero-overhead abstraction | 零开销抽象 | C++ design philosophy, you don't pay for what you don't use |
| smart pointer | 智能指针 | `unique_ptr`, `shared_ptr`, `weak_ptr` |
| unique pointer | 独占指针 | `std::unique_ptr`, exclusive ownership |
| shared pointer | 共享指针 | `std::shared_ptr`, reference-counted shared ownership |
| weak pointer | 弱引用指针 | `std::weak_ptr`, breaks circular references |
| intrusive pointer | 侵入式指针 | Reference count embedded inside the object |
| constexpr | 常量表达式 | Compile-time evaluation, introduced in C++11 |
| consteval | 立即函数 | C++20, forces compile-time evaluation |
| constinit | 常量初始化 | C++20, avoids the static initialization order problem |
| SFINAE (Substitution Failure Is Not An Error) | 替换失败并非错误 | Foundational mechanism of template metaprogramming |
| CRTP (Curiously Recurring Template Pattern) | 奇异递归模板模式 | Idiom for static polymorphism |
| template | 模板 | Foundation of generic programming |
| template specialization | 模板特化 | Custom implementations for specific types |
| template instantiation | 模板实例化 | Compiler generates concrete code from a template |
| generic programming | 泛型编程 | Programming paradigm built on templates |
| type safety | 类型安全 | Catches type errors at compile time |
| type deduction / inference | 类型推断 | `auto`, `decltype`, template argument deduction |
| type traits | 类型特征 | `<type_traits>`, compile-time type queries |
| concepts | 概念 | C++20, named constraints on template parameters |
| constraints | 约束 | `requires` clauses, restrict template parameters |
| lambda expression | Lambda 表达式 | Anonymous function object, introduced in C++11 |
| structured binding | 结构化绑定 | C++17, `auto [a, b] = ...` |
| enum class | 限定作用域枚举 | C++11, type-safe enumeration |
| variant | 变体类型 | `std::variant`, a type-safe union |
| optional | 可选值 | `std::optional`, a value that may be empty |
| expected | 预期值 | C++23, return value carrying error information |
| any | 任意类型 | `std::any`, a type-erased container |
| scope guard | 作用域守卫 | Runs cleanup actions on destruction |
| coroutine | 协程 | C++20, `co_await`/`co_yield`/`co_return` |
| module | 模块 | C++20, compilation unit replacing header files |
| range | 范围 | C++20, composable algorithms library |
| view | 视图 | Lazy adapters in the ranges library |
| undefined behavior (UB) | 未定义行为 | Behavior the standard leaves unspecified, with unpredictable results |
| one definition rule (ODR) | 唯一定义规则 | Each entity may have exactly one definition per program |
| stack unwinding | 栈展开 | Destroying stack objects layer by layer during exception handling |
| designated initializer | 指定初始化器 | C++20, `{.x = 1, .y = 2}` |
| user-defined literal | 用户自定义字面量 | `operator""_suffix` |
| spaceship operator | 飞船运算符 | C++20, `<=>` three-way comparison |
| atomic operation | 原子操作 | Indivisible, concurrency-safe operation |
| memory order | 内存序 | Ordering constraints on atomic operations |
| lock-free | 无锁 | Concurrent algorithms that avoid mutexes |
| mutex | 互斥量 | Mutual exclusion lock, protects shared data |
| semaphore | 信号量 | Counting synchronization primitive |
| critical section | 临界区 | Code region where only one thread executes at a time |
| deadlock | 死锁 | Multiple threads waiting for each other to release resources |
| thread | 线程 | `std::thread`, unit of concurrent execution |
| span | 视图跨度 | `std::span`, non-owning view of a contiguous sequence |
| EBO (Empty Base Optimization) | 空基类优化 | An empty class takes no space as a base class |
| static polymorphism | 静态多态 | Compile-time polymorphism via CRTP or templates |

## Embedded Hardware

| English | Chinese | Notes |
|---------|------|------|
| MCU (Microcontroller Unit) | 微控制器 | A single chip integrating CPU, memory, and peripherals |
| SoC (System on Chip) | 片上系统 | Highly integrated single-chip system |
| register | 寄存器 | Hardware-programmable control/data unit |
| interrupt | 中断 | Hardware signal that interrupts the CPU's normal flow |
| interrupt service routine (ISR) | 中断服务程序 | Function executed when an interrupt fires |
| DMA (Direct Memory Access) | 直接内存访问 | Peripheral-memory data transfer without the CPU |
| GPIO (General-Purpose I/O) | 通用输入输出 | Configurable digital pins |
| ADC (Analog-to-Digital Converter) | 模数转换器 | Converts analog signals to digital |
| DAC (Digital-to-Analog Converter) | 数模转换器 | Converts digital signals to analog |
| PWM (Pulse Width Modulation) | 脉宽调制 | Controls output via duty cycle |
| PLL (Phase-Locked Loop) | 锁相环 | Circuit that generates multiplied clock signals |
| AHB (Advanced High-performance Bus) | 高级高性能总线 | ARM's internal high-speed bus |
| APB (Advanced Peripheral Bus) | 高级外设总线 | ARM's internal peripheral bus |
| clock tree | 时钟树 | Clock distribution network from crystal to every module |
| pull-up resistor | 上拉电阻 | Pulls the level high by default |
| pull-down resistor | 下拉电阻 | Pulls the level low by default |
| push-pull | 推挽输出 | Can actively drive high or low |
| open-drain | 开漏输出 | Can only pull low, requires an external pull-up resistor |
| debounce | 消抖 | Filters out bounce from mechanical switches |
| watchdog | 看门狗 | Safety mechanism that resets the CPU on timeout |
| EXTI (External Interrupt) | 外部中断 | Interrupt triggered from an external pin |
| peripheral | 外设 | Independent functional module inside the MCU |
| PCB (Printed Circuit Board) | 印制电路板 | Carrier for electronic components |
| NVIC (Nested Vectored Interrupt Controller) | 嵌套向量中断控制器 | ARM Cortex-M interrupt controller |
| HAL (Hardware Abstraction Layer) | 硬件抽象层 | ST's official peripheral driver library |
| linker script | 链接脚本 | Defines memory layout and section placement |
| startup code | 启动代码 | C runtime initialization, runs before main |

## RTOS (Real-Time Operating System)

| English | Chinese | Notes |
|---------|------|------|
| RTOS (Real-Time Operating System) | 实时操作系统 | OS that guarantees response times |
| scheduler | 调度器 | Decides which task gets the CPU |
| context switch | 上下文切换 | Saves/restores task execution state |
| priority inversion | 优先级反转 | A low-priority task blocks a high-priority one |
| preemptive scheduling | 抢占式调度 | High-priority tasks can preempt low-priority ones |
| cooperative scheduling | 协作式调度 | Tasks voluntarily yield the CPU |
| task / thread | 任务 / 线程 | Unit of execution in an RTOS |
| tick | 系统节拍 | Basic time unit of an RTOS |
| deadline | 截止时间 | Point in time by which a task must finish |
| queue | 消息队列 | FIFO for passing data between tasks |
| priority inheritance | 优先级继承 | Protocol that resolves priority inversion |
| inter-process communication (IPC) | 进程间通信 | Mechanism for exchanging data between tasks |
| binary semaphore | 二值信号量 | Semaphore with only two states, 0/1 |
| counting semaphore | 计数信号量 | Semaphore whose count can exceed 1 |
| event group | 事件组 | Multi-bit flag event synchronization mechanism |
| idle task | 空闲任务 | Runs when no other task is ready |
| real-time | 实时 | Requirement for deterministic response times |

## Toolchain

| English | Chinese | Notes |
|---------|------|------|
| cross-compile | 交叉编译 | Build code for one platform on another |
| toolchain | 工具链 | Compiler + assembler + linker bundle |
| CMake | CMake | Cross-platform build system generator |
| Makefile | Makefile | Configuration file for the make build tool |
| flash | 烧录 | Writes the program into the target chip |
| debug probe | 调试探针 | Hardware debugger linking host and target board |
| JTAG | JTAG | Joint Test Action Group debug interface |
| SWD (Serial Wire Debug) | 串行线调试 | ARM's two-wire debug interface |
| OpenOCD | OpenOCD | Open-source on-chip debugger |
| ELF (Executable and Linkable Format) | ELF 格式 | Executable and Linkable Format, compiler output |
| hex | Intel HEX 格式 | Text format used for flashing |
| objcopy | 对象复制 | Format conversion tool (ELF→HEX/BIN) |
| compiler flag | 编译器选项 | Command-line parameter that controls compilation |
| optimization level | 优化等级 | `-O0`/`-O1`/`-O2`/`-Os`/`-O3` |
| preprocessor | 预处理器 | Handles `#include`, `#define`, and friends |
| linker | 链接器 | Merges object files into an executable |
| assembler | 汇编器 | Turns assembly code into object files |
| build system | 构建系统 | Tool that automates the build process |
| dependency | 依赖 | One module needing another |
| static library | 静态库 | `.a`/`.lib` files linked at compile time |
| shared library | 动态库 | `.so`/`.dll` files loaded at runtime |

## Debugging

| English | Chinese | Notes |
|---------|------|------|
| breakpoint | 断点 | Marker that pauses program execution |
| watchpoint | 观察点 | Marker that watches memory/variable changes |
| trace | 跟踪 | Records program execution flow |
| semihosting | 半主机 | Target board uses host I/O through the debugger |
| ITM (Instrumentation Trace Macrocell) | 指令跟踪宏单元 | ARM Cortex-M debug output |
| ETM (Embedded Trace Macrocell) | 嵌入式跟踪宏单元 | Instruction-level execution tracing |
| logic analyzer | 逻辑分析仪 | Tool that captures multi-channel digital signals |
| oscilloscope | 示波器 | Instrument for viewing electrical signal waveforms |
| GDB (GNU Debugger) | GDB 调试器 | GNU's open-source debugger |
| core dump | 核心转储 | Memory snapshot taken when a program crashes |
| backtrace | 调用栈回溯 | Traceback of the function call chain |
| single-step | 单步执行 | Executes one instruction/statement at a time |
| memory leak | 内存泄漏 | Allocated memory that is never freed |
| stack overflow | 栈溢出 | Stack space exhausted |
