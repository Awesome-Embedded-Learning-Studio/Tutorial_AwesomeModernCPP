---
title: "术语表"
description: "项目中英文技术术语标准翻译对照表"
---

# 术语表

本文档收录项目教程中出现的核心术语，按领域分组，提供中英文对照。目的是确保全文术语翻译一致，避免同一概念在不同文章中出现不同译法。

## C++ 语言特性

| English | 中文 | 备注 |
|---------|------|------|
| RAII (Resource Acquisition Is Initialization) | 资源获取即初始化 | C++ 核心资源管理范式 |
| move semantics | 移动语义 | C++11 核心特性，避免不必要的拷贝 |
| rvalue reference | 右值引用 | `T&&`，移动语义的基础 |
| perfect forwarding | 完美转发 | `std::forward`，保持值类别 |
| copy elision | 拷贝消除 | 编译器优化，省略拷贝/移动操作 |
| return value optimization (RVO) | 返回值优化 | 命名 NRVO，未命名 URVO |
| zero-overhead abstraction | 零开销抽象 | C++ 设计哲学，不为未使用的功能付费 |
| smart pointer | 智能指针 | `unique_ptr`、`shared_ptr`、`weak_ptr` |
| unique pointer | 独占指针 | `std::unique_ptr`，独占所有权 |
| shared pointer | 共享指针 | `std::shared_ptr`，引用计数共享所有权 |
| weak pointer | 弱引用指针 | `std::weak_ptr`，打破循环引用 |
| intrusive pointer | 侵入式指针 | 引用计数嵌入对象内部 |
| constexpr | 常量表达式 | 编译期求值，C++11 引入 |
| consteval | 立即函数 | C++20，强制编译期求值 |
| constinit | 常量初始化 | C++20，避免静态初始化顺序问题 |
| SFINAE (Substitution Failure Is Not An Error) | 替换失败并非错误 | 模板元编程基础机制 |
| CRTP (Curiously Recurring Template Pattern) | 奇异递归模板模式 | 静态多态惯用法 |
| template | 模板 | 泛型编程基础 |
| template specialization | 模板特化 | 为特定类型提供定制实现 |
| template instantiation | 模板实例化 | 编译器根据模板生成具体代码 |
| generic programming | 泛型编程 | 基于模板的编程范式 |
| type safety | 类型安全 | 编译期捕获类型错误 |
| type deduction / inference | 类型推断 | `auto`、`decltype`、模板参数推断 |
| type traits | 类型特征 | `<type_traits>`，编译期类型查询 |
| concepts | 概念 | C++20，对模板参数的命名约束 |
| constraints | 约束 | `requires` 子句，限制模板参数 |
| lambda expression | Lambda 表达式 | 匿名函数对象，C++11 引入 |
| structured binding | 结构化绑定 | C++17，`auto [a, b] = ...` |
| enum class | 限定作用域枚举 | C++11，类型安全的枚举 |
| variant | 变体类型 | `std::variant`，类型安全的联合体 |
| optional | 可选值 | `std::optional`，可能为空的值 |
| expected | 预期值 | C++23，携带错误信息的返回值 |
| any | 任意类型 | `std::any`，类型擦除的容器 |
| scope guard | 作用域守卫 | 析构时执行清理动作 |
| coroutine | 协程 | C++20，`co_await`/`co_yield`/`co_return` |
| module | 模块 | C++20，替代头文件的编译单元 |
| range | 范围 | C++20，组合式算法库 |
| view | 视图 | 范围库中的惰性求值适配器 |
| undefined behavior (UB) | 未定义行为 | 标准未规定的行为，结果不可预测 |
| one definition rule (ODR) | 唯一定义规则 | 每个实体在程序中只能有一个定义 |
| stack unwinding | 栈展开 | 异常处理时逐层析构栈上对象 |
| designated initializer | 指定初始化器 | C++20，`{.x = 1, .y = 2}` |
| user-defined literal | 用户自定义字面量 | `operator""_suffix` |
| spaceship operator | 飞船运算符 | C++20，`<=>` 三路比较 |
| atomic operation | 原子操作 | 不可分割的并发安全操作 |
| memory order | 内存序 | 原子操作的排序约束 |
| lock-free | 无锁 | 不使用互斥锁的并发算法 |
| mutex | 互斥量 | 互斥锁，保护共享数据 |
| semaphore | 信号量 | 计数同步原语 |
| critical section | 临界区 | 同一时刻只允许一个线程执行的代码段 |
| dead lock | 死锁 | 多个线程互相等待对方释放资源 |
| thread | 线程 | `std::thread`，并发执行单元 |
| span | 视图跨度 | `std::span`，对连续序列的非拥有视图 |
| EBO (Empty Base Optimization) | 空基类优化 | 空类作为基类时不占空间 |
| static polymorphism | 静态多态 | 编译期多态，基于 CRTP 或模板 |

## 嵌入式硬件

| English | 中文 | 备注 |
|---------|------|------|
| MCU (Microcontroller Unit) | 微控制器 | 集成 CPU、内存、外设的单芯片 |
| SoC (System on Chip) | 片上系统 | 高度集成的单片系统 |
| register | 寄存器 | 硬件可编程的控制/数据单元 |
| interrupt | 中断 | 硬件信号打断 CPU 正常执行流 |
| interrupt service routine (ISR) | 中断服务程序 | 中断触发时执行的函数 |
| DMA (Direct Memory Access) | 直接内存访问 | 外设与内存间无需 CPU 参与的数据传输 |
| GPIO (General-Purpose I/O) | 通用输入输出 | 可配置的数字引脚 |
| ADC (Analog-to-Digital Converter) | 模数转换器 | 模拟信号转数字信号 |
| DAC (Digital-to-Analog Converter) | 数模转换器 | 数字信号转模拟信号 |
| PWM (Pulse Width Modulation) | 脉宽调制 | 通过占空比控制输出 |
| PLL (Phase-Locked Loop) | 锁相环 | 倍频时钟生成电路 |
| AHB (Advanced High-performance Bus) | 高级高性能总线 | ARM 内部高速总线 |
| APB (Advanced Peripheral Bus) | 高级外设总线 | ARM 内部外设总线 |
| clock tree | 时钟树 | 从晶振到各模块的时钟分发网络 |
| pull-up resistor | 上拉电阻 | 默认拉高电平 |
| pull-down resistor | 下拉电阻 | 默认拉低电平 |
| push-pull | 推挽输出 | 可主动输出高/低电平 |
| open-drain | 开漏输出 | 只能拉低，需外接上拉电阻 |
| debounce | 消抖 | 去除机械按键的抖动信号 |
| watchdog | 看门狗 | 超时复位 CPU 的安全机制 |
| EXTI (External Interrupt) | 外部中断 | 外部引脚触发的中断 |
| peripheral | 外设 | MCU 内部独立功能模块 |
| PCB (Printed Circuit Board) | 印制电路板 | 电子元件的载体 |
| NVIC (Nested Vectored Interrupt Controller) | 嵌套向量中断控制器 | ARM Cortex-M 中断管理器 |
| HAL (Hardware Abstraction Layer) | 硬件抽象层 | ST 官方外设驱动库 |
| linker script | 链接脚本 | 定义内存布局和段分配 |
| startup code | 启动代码 | C 运行时初始化，在 main 之前执行 |

## RTOS（实时操作系统）

| English | 中文 | 备注 |
|---------|------|------|
| RTOS (Real-Time Operating System) | 实时操作系统 | 保证响应时间的操作系统 |
| scheduler | 调度器 | 决定哪个任务获得 CPU |
| context switch | 上下文切换 | 保存/恢复任务执行状态 |
| priority inversion | 优先级反转 | 低优先级任务阻塞高优先级任务 |
| preemptive scheduling | 抢占式调度 | 高优先级任务可抢占低优先级 |
| cooperative scheduling | 协作式调度 | 任务主动让出 CPU |
| task / thread | 任务 / 线程 | RTOS 中的执行单元 |
| tick | 系统节拍 | RTOS 的基本时间单位 |
| deadline | 截止时间 | 任务必须完成的时间点 |
| queue | 消息队列 | 任务间传递数据的 FIFO |
| priority inheritance | 优先级继承 | 解决优先级反转的协议 |
| inter-process communication (IPC) | 进程间通信 | 任务间数据交换机制 |
| binary semaphore | 二值信号量 | 只有 0/1 两种状态的信号量 |
| counting semaphore | 计数信号量 | 可大于 1 的信号量 |
| event group | 事件组 | 多位标志的事件同步机制 |
| idle task | 空闲任务 | 无其他任务就绪时运行 |
| real-time | 实时 | 确定性的响应时间要求 |

## 工具链

| English | 中文 | 备注 |
|---------|------|------|
| cross-compile | 交叉编译 | 在一个平台上生成另一个平台的代码 |
| toolchain | 工具链 | 编译器 + 汇编器 + 链接器的集合 |
| CMake | CMake | 跨平台构建系统生成器 |
| Makefile | Makefile | make 构建工具的配置文件 |
| flash | 烧录 | 将程序写入目标芯片 |
| debug probe | 调试探针 | 连接主机与目标板的硬件调试器 |
| JTAG | JTAG | 联合测试行动组调试接口 |
| SWD (Serial Wire Debug) | 串行线调试 | ARM 两线调试接口 |
| OpenOCD | OpenOCD | 开源片上调试器 |
| ELF (Executable and Linkable Format) | ELF 格式 | 可执行可链接格式，编译器输出 |
| hex | Intel HEX 格式 | 烧录用的文本格式 |
| objcopy | 对象复制 | 格式转换工具（ELF→HEX/BIN） |
| compiler flag | 编译器选项 | 控制编译行为的命令行参数 |
| optimization level | 优化等级 | `-O0`/`-O1`/`-O2`/`-Os`/`-O3` |
| preprocessor | 预处理器 | 处理 `#include`、`#define` 等 |
| linker | 链接器 | 将目标文件合并为可执行文件 |
| assembler | 汇编器 | 将汇编代码转为目标文件 |
| build system | 构建系统 | 自动化编译流程的工具 |
| dependency | 依赖 | 一个模块需要另一个模块 |
| static library | 静态库 | 编译时链接的 `.a`/`.lib` 文件 |
| shared library | 动态库 | 运行时加载的 `.so`/`.dll` 文件 |

## 调试

| English | 中文 | 备注 |
|---------|------|------|
| breakpoint | 断点 | 暂停程序执行的标记 |
| watchpoint | 观察点 | 监视内存/变量变化的标记 |
| trace | 跟踪 | 记录程序执行流 |
| semihosting | 半主机 | 目标板通过调试器使用主机 I/O |
| ITM (Instrumentation Trace Macrocell) | 指令跟踪宏单元 | ARM Cortex-M 调试输出 |
| ETM (Embedded Trace Macrocell) | 嵌入式跟踪宏单元 | 指令级执行跟踪 |
| logic analyzer | 逻辑分析仪 | 捕获多路数字信号的工具 |
| oscilloscope | 示波器 | 观察电信号波形的仪器 |
| GDB (GNU Debugger) | GDB 调试器 | GNU 开源调试器 |
| core dump | 核心转储 | 程序崩溃时的内存快照 |
| backtrace | 调用栈回溯 | 函数调用链的回溯信息 |
| single-step | 单步执行 | 逐条指令/语句执行 |
| memory leak | 内存泄漏 | 分配的内存未被释放 |
| stack overflow | 栈溢出 | 栈空间用尽 |
