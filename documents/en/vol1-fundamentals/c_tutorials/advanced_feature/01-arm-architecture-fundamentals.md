---
title: ARM Architecture and Fundamentals
description: Starting from the von Neumann and Harvard architectures, we break down
  the ARM Cortex-M instruction set, register file, exception vector table, and processor
  modes to build a low-level hardware mental model.
chapter: 1
order: 101
tags:
- host
- cpp-modern
- intermediate
- 嵌入式
- 寄存器
- 基础
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
prerequisites:
- C 语言基础：数据类型与内存
- 指针与内存地址
- 基本的嵌入式开发概念
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/01-arm-architecture-fundamentals.md
  source_hash: a4cd205e6a3a57b38b85964c73510a5200040c058e79d583799863a28fefc3b3
  translated_at: '2026-04-20T03:35:34.962002+00:00'
  engine: anthropic
  token_count: 3519
---
# ARM Architecture and Fundamentals

Honestly, if you have only ever written C/C++ on a PC, you have probably never cared about how a processor actually turns a line of code into electrical signals. The x86 ecosystem is so abstracted that the compiler and operating system shield you from nearly all low-level details. But once you step into the embedded world, especially when facing ARM Cortex-M series MCUs, this knowledge is no longer a nice-to-have — it is a prerequisite for writing correct code. We have seen too many people jump straight into STM32 without even being able to explain what processor modes or exception vector tables are, leaving them staring blankly at registers when a HardFault hits.

Developers in other languages like Python or Java basically never need to worry about this — the virtual machine or interpreter abstracts away the hardware completely. But C/C++ is different. Their design philosophy is "close to the metal," with only a thin layer of abstraction between the machine code the compiler generates and your source code. As the dominant architecture in embedded systems today, understanding ARM's architecture means understanding exactly what happens on the chip for every line of C code you write. The connection to C++ is even stronger — object layout, cache-friendly design, and exception handling overhead are all topics directly tied to ARM's hardware characteristics.

In this tutorial, we will tear down an ARM processor from an architectural perspective, clarifying its memory architecture, instruction set, register file, exception mechanism, and processor modes. The goal is not to turn you into an assembly programmer, but to give you a clear mental model of what happens at the hardware level when you write C/C++. When you use ``volatile`` to qualify a register, you will know why. When you debug a HardFault caused by a stack overflow, you will be able to pinpoint the issue quickly.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Distinguish between Von Neumann architecture, Harvard architecture, and modified Harvard architecture
> - [ ] Explain the differences and use cases of ARM, Thumb, and Thumb-2 instruction sets
> - [ ] Describe the roles of R0-R15 registers and the AAPCS calling convention
> - [ ] Describe the Cortex-M exception vector table structure and stacking/unstacking mechanism
> - [ ] Understand the division between Thread/Handler modes and privilege levels

## Environment Setup

This chapter is theory-heavy but closely tied to real hardware. All code examples can be verified under an ARM toolchain.

```text
平台：ARM Cortex-M3/M4（代表芯片：STM32F1/F4 系列）
工具链：GCC ARM Embedded（arm-none-eabi-gcc）>= 10.x
       或 STM32CubeIDE / PlatformIO（底层同一套）
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
硬件：阅读过程不需要开发板，有 STM32F103 或 STM32F407 可对照更佳
参考架构：ARMv7-M（Cortex-M3/M4），穿插 ARMv7-A（Cortex-A 系列）对比
```

## Step One — Understanding How the Processor Accesses Memory

The first thing we need to discuss is the processor's memory architecture — how the CPU interacts with memory. This seems basic, but it directly determines many everyday phenomena — like why code runs faster on some chips than others, or why DMA always requires special address region configuration.

### Von Neumann Architecture — One Bus to Rule Them All

The core characteristic of the Von Neumann architecture is that instructions and data share a single bus and a single memory space. The CPU accesses memory through one set of address buses, regardless of whether you are reading code or data — it all travels the same path. You can think of it as a single-lane road — instructions and data line up and take turns, with no way to pass each other. The advantage is hardware simplicity — you only need one bus and one memory, which keeps costs down. The early 8051 MCU and the core philosophy of most general-purpose computers stem from this.

The problem is also obvious: because instructions and data crowd onto the same bus, the CPU cannot fetch instructions and read/write data simultaneously. In practice, this means limited performance — want to execute an addition while writing the result back to memory? Sorry, the bus is busy fetching the next instruction, so you have to wait. This is the so-called "Von Neumann bottleneck."

### Harvard Architecture — Two Buses, Each Minding Its Own Business

The Harvard architecture takes a different approach: instructions and data each have their own bus and their own memory space. It is like turning the single-lane road into a dual-lane highway — instruction fetching and data read/write can happen simultaneously, theoretically doubling throughput. Most DSP chips and many modern microcontrollers use a pure Harvard architecture or a variant of it.

But a pure Harvard architecture is not a silver bullet either. If your program needs self-modifying code (rare in embedded), or if you want to use a block of memory as both code and data space, the hardware is not flexible enough — you would need to design an extra mechanism to allow the two buses to access each other's memory spaces.

### Modified Harvard Architecture — ARM's Practical Choice

Real-world ARM Cortex-M3/M4 processors rarely go to extremes, instead adopting what is called the **Modified Harvard Architecture**. You can understand it this way: from a software perspective, the address space is unified (like Von Neumann), but from a hardware perspective, instruction fetching and data access can happen in parallel (like Harvard).

Specifically, Cortex-M3/M4 has three AHB-Lite buses: the I-Code bus exclusively fetches instructions from the Code region (`0x00000000`–`0x1FFFFFFF`, where Flash is mapped), the D-Code bus handles data access in the Code region (like loading constants from Flash), and the System bus handles access to the SRAM and peripheral regions. I-Code and D-Code can operate in parallel, so code in Flash and constant data in Flash can be accessed simultaneously, significantly improving execution efficiency.

If you look at the STM32F407 memory map, you will find that the 512MB space from address `0x00000000` to `0x1FFFFFFF` is marked as the Code region, while `0x20000000` onward is the SRAM region. ARM officially recommends that during bus arbitration, D-Code should have higher priority than I-Code — because if a data access is blocked, the processor cannot proceed, whereas instruction prefetching can afford to wait a bit.

> ⚠️ **Watch Out**
> Although Cortex-M has multiple buses, they are not truly "fully parallel" — if I-Code and D-Code access Flash simultaneously, they still go through Flash controller arbitration. On the STM32F1, Flash is only 16 bits wide with no cache, so the parallel bus advantage is significantly diminished; on the STM32F4, with its 128-bit wide Flash interface and Adaptive Real-Time (ART) Accelerator, the difference is very noticeable. Do not forget to check this metric when selecting a chip.

## Step Two — Understanding How ARM Instructions Are Encoded

With the memory architecture cleared up, let us look at the ARM instruction set. This directly impacts the size and execution efficiency of your generated code, which is especially critical on resource-constrained MCUs.

### ARM Instruction Set (32-bit) — Expressive But Bulky

ARM's original instruction set (A32) uses fixed-length 32-bit encoding, with each instruction occupying four bytes. The encoding space is generous enough to express rich operations — conditional execution, inline barrel shifter shifts, multiple register transfers (`LDM/STM`), and other advanced features. The benefit of 32-bit instructions is strong expressiveness; a single instruction can do a lot, raising the performance ceiling. The downside is equally obvious — larger code size, an overhead that cannot be ignored on small MCUs with only a few dozen KB of Flash.

### Thumb Instruction Set (16-bit) — Compact But Limited

To solve the code density problem, ARM introduced the Thumb instruction set (T16) in the ARMv4T architecture, compressing most commonly used instructions into 16-bit encodings. The trade-off is the loss of some advanced features — in Thumb state, most instructions no longer support conditional execution, and barrel shifter usage is restricted. But in exchange, code size typically shrinks by about 30%, a lifesaver for applications where Flash is tight.

### Thumb-2 — The Default Choice for Cortex-M

Cortex-M3/M4 uses the **Thumb-2 instruction set**, a mixed-encoding scheme that interleaves 16-bit and 32-bit instructions. The compiler automatically selects the most appropriate encoding width for each instruction — simple operations use 16 bits, complex operations (like loading large immediates, division, etc.) use 32 bits. This way, you get functional completeness close to the pure ARM instruction set while maintaining code density close to pure Thumb.

One point is particularly worth noting: **Cortex-M series processors only support the Thumb instruction set** and do not support the traditional 32-bit ARM instruction set. So all code you write on Cortex-M, whether compiled from C or hand-written in assembly, must be Thumb-encoded. The compiler defaults to Thumb mode, so you rarely need to worry about it in most cases — but if you are writing inline assembly or a custom startup file, you must remember this, otherwise you will be rewarded with a beautiful Undefined Instruction exception.

```c
/// @brief 一个简单的 Thumb 函数示例
/// Cortex-M 上所有函数默认使用 Thumb 编码
int add_values(int a, int b)
{
    return a + b;
}

/// @brief 内嵌汇编示例——在 Thumb 模式下读取主栈指针（MSP）
/// 注意：实际项目中推荐用 CMSIS 的 __get_MSP() 宏
uint32_t read_msp(void)
{
    uint32_t msp_value;
    __asm__ volatile("mov %0, sp" : "=r"(msp_value));
    return msp_value;
}
```

> ⚠️ **Watch Out**
> If you accidentally remove `-mthumb` in your linker script or compiler flags (or erroneously add `-marm`), linking on Cortex-M will fail outright — because the Cortex-M instruction decoder simply does not understand 32-bit ARM encoding. When you encounter a `Undefined Instruction` exception, first check whether your compiler flags include `-mthumb`.

## Step Three — Getting to Know the Processor's "Workbench": The Register File

If the instruction set is the processor's "language," then registers are its "workbench" — when the CPU performs calculations, data is first moved into registers, operations happen between registers, and the results are finally written back to memory. Understanding the division of labor among registers is fundamental to understanding how ARM operates.

### General-Purpose Registers R0-R15

The ARMv7-M architecture defines sixteen 32-bit general-purpose registers, numbered R0 through R15. They each have specific roles, and not all registers can be used freely.

**R0-R3** are argument and return value registers. Per the AAPCS (ARM Architecture Procedure Call Standard) convention, the first four arguments of a function call are passed through R0-R3, and the return value is also placed in R0 (for 64-bit return values, R0 and R1 are used together). You can think of them as the "express lane" for function calls — if a C function has no more than four arguments, the call process does not need to touch the stack at all, making it very fast. But if you write a function with five arguments, the fifth one gets pushed to the stack, adding an extra memory access.

**R4-R11** are callee-saved registers. A function can freely use R4-R11, but must restore their original values before returning — meaning the caller can safely assume these registers will not be clobbered after a function call. The compiler typically allocates these registers to local variables, especially high-frequency data like loop counters and frequently accessed pointers whose lifetimes span function calls. If you see a bunch of ``PUSH {R4-R7, LR}`` instructions at the beginning of a function while debugging, that is the compiler saving the callee-saved registers it plans to use.

**R12 (IP)** is the intra-procedure-call scratch register. The name is long but the purpose is simple — the linker uses it as a temporary holding area when handling long jumps (where the target address exceeds the encoding range of the branch instruction). You rarely touch it directly when writing C code.

**R13 (SP)** is the stack pointer, pointing to the top of the current stack. ARM has two stack pointers — the Main Stack Pointer (MSP) and the Process Stack Pointer (PSP) — selected via the CONTROL register. Bare-metal applications typically use only the MSP; if running an RTOS, interrupt handling uses the MSP while threads use the PSP, achieving isolation between the interrupt stack and thread stacks. This design is quite elegant — even if a thread overflows its stack, it will not corrupt the stack space used by interrupt handling.

**R14 (LR)** is the link register, holding the return address of a function call. When a ``BL`` (Branch with Link) instruction is executed, the return address is automatically stored in LR. The beauty of this is that for leaf functions (functions that do not call other functions), there is no need to push the return address onto the stack — it is already saved in LR, saving one memory write. But if your function calls another function, the value in LR gets overwritten, so the compiler pushes LR onto the stack at the start of the function.

**R15 (PC)** is the program counter, pointing to the currently executing instruction. On ARM, reading PC typically yields the current instruction address plus four (due to pipeline prefetching), and writing to PC is equivalent to performing a jump.

```c
/// @brief 演示 AAPCS 调用约定对寄存器使用的影响
/// 前 4 个参数通过 R0-R3 传递，第 5 个参数需要压栈

int fast_path(int a, int b, int c, int d)
{
    // a -> R0, b -> R1, c -> R2, d -> R3
    // 全部通过寄存器传递，无栈操作
    return a + b + c + d;
}

int slow_path(int a, int b, int c, int d, int e)
{
    // a -> R0, b -> R1, c -> R2, d -> R3
    // e -> 栈传递，多一次内存读操作
    return a + b + c + d + e;
}
```

Let us use ``arm-none-eabi-objdump -d`` to disassemble and see the difference:

```text
; fast_path: 全部在寄存器中完成
fast_path:
    add   r0, r0, r1    ; a + b -> R0
    add   r0, r0, r2    ; + c
    add   r0, r0, r3    ; + d
    bx    lr            ; 返回

; slow_path: 第 5 个参数从栈上读取
slow_path:
    add   r0, r0, r1
    add   r0, r0, r2
    add   r0, r0, r3
    ldr   r3, [sp]      ; 从栈上读第 5 个参数
    add   r0, r0, r3
    bx    lr
```

You can see that ``slow_path`` has an extra ``ldr`` instruction — that is the cost of pushing the fifth argument to the stack.

> ⚠️ **Watch Out**
> Do not try to "save arguments" by stuffing a bunch of unrelated variables into a struct and passing a pointer — the struct pointer itself takes up a register slot, and indirect access through a pointer adds a layer of dereferencing overhead. A reasonable design is to keep hot-path functions to no more than four arguments of basic types ``int``/``float`` in size, and only consider passing a struct pointer for anything beyond that.

### Program Status Registers — The xPSR Trio

The ARM processor's status information is stored in program status registers. On Cortex-M, this is split into three sub-registers, collectively called xPSR.

**APSR (Application PSR)** holds the result flags of arithmetic and logic operations: N (Negative), Z (Zero), C (Carry), V (oVerflow), and Q (saturation flag). The first four are the familiar condition code flags; ``if (a > b)`` in C code compiles down to checks against these flag bits.

**EPSR (Execution PSR)** contains the Thumb state bit (T-bit) and the interruptible-continuable instruction bit. The T-bit on Cortex-M is always 1 (because only Thumb is supported), so you basically never need to manipulate it manually.

**IPSR (Interrupt PSR)** holds the exception number of the currently executing exception. In Thread mode, IPSR is 0; if an interrupt is being handled, IPSR is the number of that interrupt. This is particularly useful when debugging HardFaults — reading IPSR lets you confirm which exception context you are in.

```c
/// @brief 通过 xPSR 的条件标志理解 C 代码的比较操作
/// 编译器会将条件判断转换为对 N/Z/C/V 标志的检测
int max_value(int a, int b)
{
    // 编译后：CMP R0, R1，然后检测 APSR 的标志位
    if (a > b) {
        return a;  // GT 条件：Z=0 且 N=C
    }
    return b;
}
```

## Step Four — Understanding the Processor's "Modes"

ARM processors run in different "modes," each with different privilege levels and accessible resources. This section is the foundation for understanding the security model and exception handling.

### Cortex-M's Simplified Model: Thread and Handler

Cortex-M drastically simplifies the traditional ARM's seven processor modes, keeping only two: **Thread mode** (for executing normal application code) and **Handler mode** (for executing interrupt service routines and exception handling code). Each mode is further divided into privileged and unprivileged levels.

After power-on reset, the processor defaults to Thread mode + privileged level. If you do not actively drop privileges (by writing to the CONTROL register), your entire program runs in privileged state — this is common in bare-metal development, but it also means your code can "legally" do anything, including writing to the wrong register and causing peripheral misbehavior. In RTOS scenarios, the RTOS typically drops privileges to unprivileged level when creating user threads, so even if a thread goes astray, it cannot directly manipulate critical hardware registers.

Handler mode is always privileged — interrupt handling code needs full hardware access, which is a hard requirement. When an exception or interrupt occurs, the processor automatically switches from Thread mode to Handler mode, and switches back automatically when handling is complete.

> ⚠️ **Watch Out**
> If you accidentally drop to unprivileged level in Thread mode, you cannot elevate back — only Handler mode, triggered by an exception/interrupt, can manipulate the CONTROL register to raise privileges. So if you plan to use unprivileged mode, make sure to use an SVC (Supervisor Call) instruction to trigger a system call for operations that require privileges, rather than directly manipulating hardware registers in unprivileged mode.

## Step Five — Walking Through the Full Interrupt Handling Process via the Exception Vector Table

By now we have the foundational knowledge of processor modes and registers. Let us connect the dots — see exactly what the ARM processor does when an exception or interrupt occurs.

### Exceptions Are More Than Just Interrupts

In ARM terminology, "exception" is a broader concept than "interrupt." Interrupts are just one type of exception; others include: Reset, NMI (Non-Maskable Interrupt), HardFault, Memory Management Fault, Bus Fault, Usage Fault, SVCall, PendSV, SysTick, and more. They all share the same handling mechanism, just with different priorities.

### Vector Table — The "Phone Book" of Exception Handling

When an exception occurs, the processor needs to know where the corresponding handler function is located. ARM's solution is the **Vector Table** — an array of function pointers stored in memory, with each exception type corresponding to one entry.

On Cortex-M, the vector table starts at address ``0x00000000`` by default (it can be relocated via the VTOR register). The first entry is not a function pointer, but the initial value of the Main Stack Pointer (MSP) — this is a clever design where the processor automatically loads this value into SP on reset, requiring no additional initialization code. Starting from the second entry, the Reset Handler, NMI Handler, HardFault Handler, and so on are placed in sequence.

```c
/// @brief Cortex-M 向量表结构示意
typedef void (*ExceptionHandler)(void);

/// @brief 向量表布局（简化版，实际还包括更多 Fault 向量）
typedef struct {
    uint32_t         kInitialStackPointer;  // 初始 MSP 值
    ExceptionHandler reset_handler;         // 复位
    ExceptionHandler nmi_handler;           // 不可屏蔽中断
    ExceptionHandler hardfault_handler;     // 硬件错误
    ExceptionHandler memmanage_handler;     // 内存管理错误
    ExceptionHandler busfault_handler;      // 总线错误
    ExceptionHandler usagefault_handler;    // 用法错误
    // ... 省略若干保留项 ...
    ExceptionHandler svcall_handler;        // 系统服务调用
    ExceptionHandler pendsv_handler;        // 可挂起的系统调用
    ExceptionHandler systick_handler;       // 系统滴答定时器
    // 外部中断向量从此开始 ...
} VectorTable;
```

### Exception Stacking — The "Scene" Automatically Saved by the Processor

When an exception occurs, the Cortex-M processor automatically saves the values of eight registers on the current stack: R0, R1, R2, R3, R12, LR, PC, and xPSR. This operation is called "stacking," and it is done entirely by hardware — you do not need to write any code to save the context. When exception handling is complete and the return instruction is executed, the processor automatically restores these eight registers from the stack ("unstacking").

This design means your interrupt service routine is just a plain C function — you do not need to add special qualifiers like ``__irq`` (that was the approach in the ARM7TDMI era), and the compiler does not need to generate special prologue and epilogue code. Compared to the ARM7TDMI era where you had to write your own register save/restore code, the Cortex-M approach is refreshingly clean.

But there is an easy pitfall here: if your stack space is insufficient (for example, if the stack allocated for a particular interrupt is too small), the stacking operation will trigger another exception — and handling that exception also requires stacking — resulting in a cascading stack overflow that ultimately triggers a HardFault. Therefore, reasonable stack size planning is crucial in Cortex-M development. A general recommendation is to reserve at least 512 bytes for the main stack, and if running an RTOS, each thread stack should also be at least 256 bytes.

### Interrupt Priority — Who Goes First

ARM Cortex-M supports configurable interrupt priorities. Each interrupt source has a priority register, where a smaller value means a higher priority. Cortex-M3 supports up to 256 priority levels (8-bit width), but in most actual implementations, only the upper 4 bits are used — meaning you may only have 16 usable priority levels (this is the case for STM32F1/F4).

Priority grouping splits the 8-bit priority register into two parts: the upper bits are the "preemption priority," and the lower bits are the "subpriority." A higher preemption priority interrupt can preempt a lower preemption priority interrupt that is currently being handled (nested interrupts), while subpriority only determines which of two interrupts with the same preemption priority gets handled first. CMSIS provides ``NVIC_SetPriorityGrouping()`` and ``NVIC_SetPriority()`` to configure these. If you are just starting out, using the default 4-bit preemption + 0-bit subpriority grouping is fine; you can dive deeper when you need fine-grained control.

## Step Six — Connecting This Knowledge to Writing C Code

At this point, we have covered the core concepts of ARM architecture. You might ask: I write C/C++ code without using assembly, so how does this knowledge actually manifest in practical programming? Let us walk through a few direct connections.

### Calling Conventions and Function Design

As mentioned earlier, AAPCS specifies that the first four arguments are passed through R0-R3. The direct impact on C function design is: if you can control the function signature, try to keep arguments to no more than four, and avoid passing large structs. A common practice is to trim the parameters of frequently called hot-path functions to four or fewer, giving the compiler maximum room for optimization.

### volatile and Register Access

The ``volatile`` keyword is virtually omnipresent in embedded programming — every hardware register mapping pointer needs ``volatile``. The reason is that compiler optimizations assume memory values will not "change on their own," but hardware register values can be modified at any time by external events (DMA transfer completion, peripheral state changes). ``volatile`` tells the compiler, "always actually read this address, do not cache the value."

```c
/// @brief 典型的寄存器映射访问模式
/// volatile 保证每次访问都真正读写硬件
#define GPIOA_ODR_ADDRESS ((volatile uint32_t*)0x40020014U)

void set_gpio_pin(int pin)
{
    // 没有 volatile，编译器可能认为连续写同一个地址是冗余操作并优化掉
    *GPIOA_ODR_ADDRESS |= (1U << pin);
}
```

### Stack Usage and Memory Layout Awareness

Once you understand ARM's stacking mechanism and dual-stack design, you have a solid basis for planning memory usage. In bare-metal applications, you need to ensure the linker script allocates enough space for the stack; in RTOS applications, you need to allocate a reasonable stack size for each thread. Rule of thumb: simple threads without floating-point operations start at 256 bytes, while threads with floating-point operations or deep call chains need 512-1024 bytes. If you enable the Cortex-M4 FPU, exception stacking will additionally save 16 floating-point registers (S0-S15) plus FPSCR — an extra 68 bytes of overhead that cannot be ignored.

## C++ Connections

If you are coming from the C++ part of this tutorial, the relationship between these low-level details and C++ is actually much bigger than you might think. ARM's hardware characteristics directly influence many C++ design decisions.

### Cache-Friendly Design and Data Locality

ARM processors (especially the Cortex-A series) have multi-level caches. Understanding the size and behavior of cache lines (typically 32 or 64 bytes) directly impacts C++ data structure design. Packing frequently accessed hot-path fields compactly at the beginning of a struct and putting cold data at the end, or using ``alignas`` to control alignment, can significantly improve performance — at the C tutorial stage, you just need to build awareness of this, and subsequent C++ chapters will dive deeper.

```cpp
// 不太友好的布局：热数据和冷数据交替排列
struct BadSensorData {
    uint32_t timestamp;   // 热
    char name[32];        // 冷——挤占了缓存行
    float value;          // 热
    int calibration_id;   // 冷
    float raw_value;      // 热
};

// 友好的布局：热数据集中在前 16 字节，一个缓存行搞定
struct GoodSensorData {
    uint32_t timestamp;   // 热
    float value;          // 热
    float raw_value;      // 热
    // --- 缓存行边界大概在这里 ---
    char name[32];        // 冷
    int calibration_id;   // 冷
};
```

### C++ Object Memory Layout and ABI

C++ object memory layout on the ARM platform follows the AAPCS ABI specification: ordinary member variables are arranged in declaration order, the virtual function table pointer (vptr) is placed at the beginning of the object, and there may be multiple vptrs with multiple inheritance. These layout details are crucial when serializing, transmitting over networks, or interacting with C code. If you write an object-oriented driver framework in C++ on Cortex-M, understanding the position and size of the vptr helps you precisely calculate how many bytes a driver object actually occupies.

### Exception Handling Overhead

On embedded ARM platforms, the runtime overhead of C++ exception handling (try/catch/throw) needs serious consideration. Exception handling tables and unwinding information significantly increase binary size, and the stack unwinding process during exception throwing involves substantial memory operations. On Cortex-M where both Flash and RAM are tight, many teams choose to add ``-fno-exceptions`` at compile time to completely disable C++ exceptions, using error codes instead. This is not "not C++ enough" — it is a reasonable trade-off given the resources.

### constexpr and Compile-Time Computation

Many operations that would require lookup tables at runtime (CRC calculation, bit manipulation mask generation) can be done at compile time through ``constexpr`` functions, saving both Flash and execution time. On low-end chips like Cortex-M0/M0+ that do not even have a hardware divider, the value of compile-time computation is especially prominent.

## Exercises

The following exercises are left for you to work through — hands-on research, writing code, and verifying on hardware is the real learning path.

```c
/// @brief 练习 1：读取 IPSR 寄存器
/// 使用 GCC 内嵌汇编读取 Cortex-M 的 IPSR 寄存器值
/// 解释在正常运行和进入中断服务函数时读到的值有什么不同
/// 提示：IPSR 是 xPSR 的一部分，可以用 MRS 指令读取
uint32_t exercise_read_ipsr(void)
{
    // TODO: 用内嵌汇编读取 IPSR
    return 0;
}
```

```c
/// @brief 练习 2：触发并调试 HardFault
/// 对一个无效地址执行写操作，故意触发 HardFault
/// 然后在 HardFault Handler 中读取入栈的寄存器值
/// 定位导致异常的指令地址
/// 提示：HardFault Handler 的参数可以拿到栈帧指针
void exercise_trigger_hardfault(void)
{
    // TODO: 写一个无效地址来触发 HardFault
}
```

```c
/// @brief 练习 3：分析 AAPCS 的参数传递
/// 写两个函数：一个接受 4 个 int 参数，另一个接受 6 个
/// 用 arm-none-eabi-objdump -d 反汇编对比调用序列
/// 找出编译器如何分配 R4-R11 给局部变量
int exercise_aapcs_4(int a, int b, int c, int d)
{
    // TODO: 添加局部变量和函数调用，使反汇编更有看头
    return 0;
}

int exercise_aapcs_6(int a, int b, int c, int d, int e, int f)
{
    // TODO: 同上，对比反汇编结果
    return 0;
}
```

```c
/// @brief 练习 4（进阶）：向量表重定位
/// 阅读一个 Cortex-M 启动文件（如 startup_stm32f407xx.s）
/// 画出完整的向量表布局
/// 然后修改链接脚本把向量表重定位到 RAM 中
/// 实现运行时动态修改中断向量（Bootloader 开发的基础技能）
```

## References

- [ARM Cortex-M4 Technical Reference Manual - Bus Interfaces](https://developer.arm.com/documentation/ddi0439/b/Functional-Description/Interfaces/Bus-interfaces)
- [AAPCS32 Specification (ARM ABI)](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [Joseph Yiu: The Definitive Guide to ARM Cortex-M3](https://www.keil.com/dd/docs/datashts/arm/cortex_m3/r1p1/ddi0337e_cortex_m3_r1p1_trm.pdf)
- [ARM Cortex-M - Wikipedia](https://en.wikipedia.org/wiki/ARM_Cortex-M)
- [cppreference: volatile keyword](https://en.cppreference.com/w/c/language/volatile)
