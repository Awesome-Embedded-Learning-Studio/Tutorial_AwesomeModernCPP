---
title: "ARM Architecture and System Fundamentals"
description: "Starting from the von Neumann and Harvard architectures, break down the ARM Cortex-M instruction set, register file, exception vector table, and processor modes, and build a mental model of the underlying hardware"
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
cpp_standard: [11, 14, 17]
prerequisites:
  - "C Fundamentals: Data Types and Memory"
  - "Pointers and Memory Addresses"
  - "Basic Embedded Development Concepts"
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/01-arm-architecture-fundamentals.md
  source_hash: 8181c1a641ab20874cc37f35b10564b8fbc00ef4351c867993b8ff4d6ddc456e
  translated_at: '2026-09-25T13:40:05+00:00'
  engine: anthropic
  token_count: 15000
---
# ARM Architecture and System Fundamentals

Honestly, if you have been writing C/C++ on a PC, odds are you have never cared how the processor actually turns a line of code into electrical signals—the x86 world is too abstract, and the compiler and the operating system shield you from nearly every low-level detail. But once you step into the embedded rabbit hole, especially when you are facing an ARM Cortex-M series MCU, this knowledge stops being a bonus and becomes a precondition for writing correct code. We have seen far too many people jump straight onto STM32 without being able to explain what processor modes or the exception vector table even are, and when a HardFault hits, all they can do is stare blankly at the registers.

Developers in other languages, Python or Java say, basically never need to care about any of this—the virtual machine or interpreter has already abstracted the hardware away for you. C/C++ is different: their design philosophy is "stay close to the metal," and between the machine code the compiler generates and the source code you write there is only a thin layer of abstraction. With ARM being the absolute mainstream of today's embedded world, understanding its architecture means understanding what actually happens on the chip to every line of C you write. And the tie to C++ is even tighter—object layout, cache-friendly design, exception-handling overhead: every one of these topics hooks directly into ARM's hardware characteristics.

In this tutorial we take the ARM processor apart from an architectural point of view and figure out its memory architecture, instruction set, register file, exception mechanism, and processor modes. The goal is not to turn you into an assembly writer; it is to give you a clear mental model of what happens underneath when you write C/C++—so that when you mark a register `volatile`, you know why, and when you debug a HardFault caused by a stack overflow, you can locate it fast.

This installment leans theoretical but stays close to real hardware; every code example can be verified under an ARM toolchain.

```text
Platform: ARM Cortex-M3/M4 (representative chips: STM32F1/F4 series)
Toolchain: GCC ARM Embedded (arm-none-eabi-gcc) >= 10.x
           or STM32CubeIDE / PlatformIO (the same toolchain underneath)
Standards: -std=c11 (C parts) / -std=c++17 (C++ comparison parts)
Hardware: no development board needed to follow along; having an STM32F103 or STM32F407 to cross-check is even better
Reference architecture: ARMv7-M (Cortex-M3/M4), with ARMv7-A (Cortex-A series) comparisons along the way
```

## Step 1 — Figure Out How the Processor Accesses Memory

The first thing to tackle is the processor's memory architecture—how the CPU deals with memory. It looks like a basic question, but it directly determines a lot of everyday phenomena—why code runs faster on some chips than on others, for instance, or why DMA always needs specific address regions configured.

### Von Neumann Architecture — One Bus for Everything

The defining trait of the von Neumann architecture: instructions and data share the same bus and the same memory space. The CPU reaches memory through a single address bus—whether you are reading code or data, it is the same road. Picture a single-lane road: instructions and data queue up and pass through one at a time, no traveling side by side. The upside is simple hardware—only one bus and one memory, and the cost comes down. The core ideas of the early 8051 microcontrollers and of most general-purpose computers trace back to this design.

The problem is just as obvious: with instructions and data squeezed onto the same bus, the CPU cannot fetch an instruction and read or write data at the same time. In practice that means capped performance—you want to execute an addition and write the result back to memory in one breath? Sorry, the bus is busy fetching the next instruction; get in line. This is the so-called "von Neumann bottleneck".

### Harvard Architecture — Two Buses, Each Minding Its Own Business

The Harvard architecture takes the other road: instructions and data each get their own bus and their own memory space. It is the single lane upgraded to a dual carriageway—instruction fetch and data reads/writes can proceed simultaneously, theoretically doubling throughput. Most DSP chips and many modern microcontrollers adopt a pure Harvard design or a variant of it.

But a pure Harvard design is not a cure-all either. If your program needs self-modifying code (rare in embedded work), or you want one block of memory to serve as both code and data, the hardware gets a lot less flexible—you would have to design extra machinery so the two buses can reach into each other's memory space.

### Modified Harvard Architecture — ARM's Practical Choice

Real-world ARM Cortex-M3/M4 parts rarely go to either extreme; they use what is called a **modified Harvard architecture**. Think of it this way: from the software's point of view the address space is unified (like von Neumann), while from the hardware's point of view instruction fetch and data access can proceed in parallel (like Harvard).

Concretely, the Cortex-M3/M4 has three AHB-Lite buses: the I-Code bus fetches instructions exclusively from the Code region (`0x00000000`–`0x1FFFFFFF`, where Flash is mapped), the D-Code bus handles data access to the Code region (loading constants from Flash, for example), and the System bus serves the SRAM and peripheral regions. I-Code and D-Code can work in parallel, so code in Flash and constant data in Flash can be accessed at the same time, which significantly improves execution efficiency.

If you look at the STM32F407's memory map, you will find that the 512 MB span from `0x00000000` to `0x1FFFFFFF` is marked as the Code region, while SRAM starts at `0x20000000`. ARM officially recommends giving D-Code higher priority than I-Code during bus arbitration—because if a data access gets blocked, the processor cannot move forward, while instruction prefetch can afford to wait a little.

Even though the Cortex-M has multiple buses, they are not truly "fully parallel"—if I-Code and D-Code hit Flash at the same time, everything still has to go through the Flash controller's arbitration. On the STM32F1 the Flash is only 16 bits wide with no cache, so the parallel-bus advantage is heavily discounted; the STM32F4 has a 128-bit-wide Flash interface plus the Adaptive Real-Time cache (ART Accelerator), and the gap becomes very obvious. When you are picking a chip, do not skip this metric.

## Step 2 — Understand How the ARM Instruction Set Is Encoded

With the memory architecture sorted out, let us look at ARM's instruction set. This part directly affects the size and execution efficiency of the code you generate, and it is especially critical on resource-constrained MCUs.

### The ARM Instruction Set (32-bit) — Expressive but Bulky

ARM's original instruction set (A32) is 32-bit fixed-length encoding, four bytes per instruction. The encoding space is plentiful and can express rich operations—conditional execution, inline shifts through the barrel shifter, multi-register transfers (`LDM/STM`), and other advanced features. The strength of 32-bit instructions is expressiveness: a single instruction can do a lot, so the performance ceiling is high. The price is just as obvious—code size. On a small MCU whose Flash is only tens of KB, this overhead is not something you can ignore.

### The Thumb Instruction Set (16-bit) — Small but Limited

To solve the code density problem, ARM introduced the Thumb instruction set (T16) in the ARMv4T architecture, compressing most commonly used instructions into 16-bit encodings. The cost is losing some advanced features—in Thumb state most instructions no longer support conditional execution, and use of the barrel shifter is restricted too. What you get in return is a code size reduction of roughly 30% on average, a lifesaver for applications tight on Flash.

### Thumb-2 — The Default Choice for Cortex-M

The Cortex-M3/M4 use the **Thumb-2 instruction set**, a mixed encoding scheme: 16-bit and 32-bit instructions are interleaved. The compiler automatically picks the most suitable encoding width for each instruction—16 bits for simple operations, 32 bits for complex ones (loading large immediates, division, and the like). This way you get functional completeness close to the pure ARM instruction set while keeping code density close to pure Thumb.

One point deserves special attention: **Cortex-M series processors support only the Thumb instruction set**, not the traditional 32-bit ARM instructions. So all the code you write on a Cortex-M, whether compiled from C or hand-written assembly, must be Thumb-encoded. The compiler defaults to Thumb mode, so most of the time you do not need to worry—but the moment you write inline assembly or a startup file by hand, this is something you must keep in mind, or you will be rewarded with a beautifully crisp Undefined Instruction exception.

```c
/// @brief A simple example of a Thumb function
/// On Cortex-M, all functions use Thumb encoding by default
int add_values(int a, int b)
{
    return a + b;
}

/// @brief Inline assembly example — reading the Main Stack Pointer (MSP) in Thumb mode
/// Note: in real projects, prefer the CMSIS macro __get_MSP()
uint32_t read_msp(void)
{
    uint32_t msp_value;
    __asm__ volatile("mov %0, sp" : "=r"(msp_value));
    return msp_value;
}
```

If your itchy fingers remove `-mthumb` from the linker script or the compiler options (or mistakenly add `-marm`), linking on a Cortex-M will fail outright—because the Cortex-M instruction decoder simply does not recognize 32-bit ARM encodings. When you hit an `Undefined Instruction` exception, first check whether the build options still carry `-mthumb`.

## Step 3 — Meet the Processor's Workbench: The Register File

If the instruction set is the processor's "language", then registers are its "workbench"—when the CPU computes, data is first moved into registers, operations happen between registers, and the result is written back to memory at the end. Understanding the division of labor among registers is the foundation for understanding how ARM works.

### General-Purpose Registers R0-R15

The ARMv7-M architecture defines sixteen 32-bit general-purpose registers, numbered R0 through R15. Each has its own role; they are not all free for you to use however you like.

**R0-R3** are the argument-passing and return-value registers. Under the AAPCS (ARM Architecture Procedure Call Standard) convention, the first four arguments of a function call travel through R0-R3, and the return value is placed in R0 as well (for a 64-bit return value, R0 and R1 are used together). You can think of them as the express-delivery lane for function calls—if a C function takes no more than four arguments, the call never touches the stack at all, which is very fast. But if you write a function with five arguments, the fifth has to go on the stack: one extra memory access.

**R4-R11** are callee-saved registers. A function may use R4-R11 freely, but it must restore their original values before returning—which means the caller can safely assume these registers survive the call intact. The compiler typically assigns these registers to local variables, especially high-traffic data whose lifetime spans function calls: loop counters, frequently accessed pointers, and the like. If you see a pile of `PUSH {R4-R7, LR}` instructions at the top of a function while debugging, that is the compiler saving the callee-saved registers it plans to use.

**R12 (IP)** is the intra-procedure-call scratch register—a long name for a simple job: the linker uses it as a relay when handling long jumps (where the target address exceeds the jump instruction's encoding range). Day-to-day C code basically never touches it directly.

**R13 (SP)** is the stack pointer, pointing at the top of the current stack. ARM has two stack pointers—the Main Stack Pointer (MSP) and the Process Stack Pointer (PSP)—and the CONTROL register selects which one is currently in use. Bare-metal applications typically use only the MSP; with an RTOS running, interrupt handling uses the MSP while threads use the PSP, isolating the interrupt stack from thread stacks. The design is wonderfully clever: even if some thread's stack overflows, it cannot trash the stack space used for interrupt handling.

**R14 (LR)** is the link register, holding the return address of a function call. When the `BL` (Branch with Link) instruction executes, the return address is stored into LR automatically. The clever part: for leaf functions (functions that call no other functions), the return address never needs to be pushed onto the stack—it is already sitting in LR, saving one memory write. But if your function calls another function, LR gets overwritten, so the compiler pushes LR onto the stack at the start of the function.

**R15 (PC)** is the program counter, pointing at the currently executing instruction. On ARM, reading PC usually yields the current instruction's address plus 4 (a side effect of pipeline prefetch), and writing to PC amounts to performing a jump.

```c
/// @brief Demonstrating how the AAPCS calling convention affects register usage
/// The first 4 arguments are passed in R0-R3; the 5th argument has to go on the stack

int fast_path(int a, int b, int c, int d)
{
    // a -> R0, b -> R1, c -> R2, d -> R3
    // all passed through registers, no stack traffic
    return a + b + c + d;
}

int slow_path(int a, int b, int c, int d, int e)
{
    // a -> R0, b -> R1, c -> R2, d -> R3
    // e -> passed on the stack, one extra memory read
    return a + b + c + d + e;
}
```

Disassemble them with `arm-none-eabi-objdump -d` and compare:

```text
; fast_path: everything completes in registers
fast_path:
    add   r0, r0, r1    ; a + b -> R0
    add   r0, r0, r2    ; + c
    add   r0, r0, r3    ; + d
    bx    lr            ; return

; slow_path: the 5th argument is read from the stack
slow_path:
    add   r0, r0, r1
    add   r0, r0, r2
    add   r0, r0, r3
    ldr   r3, [sp]      ; read the 5th argument from the stack
    add   r0, r0, r3
    bx    lr
```

You can see that `slow_path` has one extra `ldr` instruction—that is the price of pushing the fifth argument onto the stack.

Do not stuff a bunch of unrelated variables into a struct just to "save arguments" and pass a pointer—the struct pointer itself takes up a register slot, and indirect access through the pointer adds a layer of dereference overhead. The sensible design is: keep hot-path function arguments to no more than four basic types of `int`/`float` size, and only consider passing a struct pointer for anything beyond that.

### The Program Status Registers — The Three xPSR Siblings

The ARM processor's state information is kept in the program status registers. On Cortex-M these are split into three sub-registers, collectively known as xPSR.

**APSR (Application PSR)** holds the result flags of arithmetic and logic operations: N (Negative), Z (Zero), C (Carry), V (oVerflow), and Q (the saturation flag). The first four are the condition-code flags we all know—`if (a > b)` in C code compiles into a test of these flag bits.

**EPSR (Execution PSR)** contains the Thumb state bit (T-bit) and the interruptible-continuable instruction bits. On Cortex-M the T-bit is always 1 (since only Thumb is supported), so you basically never need to touch it by hand.

**IPSR (Interrupt PSR)** holds the number of the exception currently being executed. In Thread mode IPSR reads 0; if an interrupt is being serviced, IPSR is that interrupt's number. It is especially useful when debugging HardFaults—read IPSR and you can confirm which exception context you are currently in.

```c
/// @brief Understanding C comparisons through the xPSR condition flags
/// The compiler turns conditional tests into checks of the N/Z/C/V flags
int max_value(int a, int b)
{
    // after compiling: CMP R0, R1, then test the APSR flag bits
    if (a > b) {
        return a;  // GT condition: Z=0 and N=C
    }
    return b;
}
```

## Step 4 — Understand Which Mode the Processor Runs In

An ARM processor runs in different "modes", each with its own privilege level and accessible resources. This section is the foundation for understanding the security model and exception handling.

### Cortex-M's Simplified Model: Thread and Handler

The Cortex-M drastically simplifies traditional ARM's seven processor modes, keeping only two: **Thread mode** (executes ordinary application code) and **Handler mode** (executes interrupt service routines and exception handling code). Each mode is further divided into a privileged and an unprivileged level.

After power-on reset, the processor defaults to Thread mode + privileged level. Unless you deliberately drop privileges (by writing the CONTROL register), the whole program runs privileged—very common in bare-metal development, but it also means your code can "legally" do anything, including writing the wrong register and making a peripheral misbehave. In an RTOS scenario, the RTOS usually demotes user threads to the unprivileged level when creating them, so even if a thread runs wild, it cannot directly manipulate critical hardware registers.

Handler mode is always privileged—interrupt handling code needs full hardware access, and that is a hard requirement. When an exception or interrupt occurs, the processor automatically switches from Thread to Handler mode, and switches back when handling completes.

If you accidentally drop to the unprivileged level while in Thread mode, there is no climbing back up—only Handler mode, entered through an exception or interrupt, can raise privileges by manipulating the CONTROL register. So if you plan to use unprivileged mode, always trigger a system call via the SVC (Supervisor Call) instruction to perform operations that require privilege, rather than poking hardware registers directly from unprivileged mode.

## Step 5 — Follow the Exception Vector Table Through the Whole Interrupt Handling Flow

At this point we have the basics of processor modes and registers; now let us string them together and see what an ARM processor actually does when an exception or interrupt occurs.

### Exceptions Are More Than Interrupts

In ARM's terminology, an "exception" is a broader concept than an "interrupt". Interrupts are just one kind of exception; the family also includes Reset, NMI (non-maskable interrupt), HardFault, Memory Management Fault, Bus Fault, Usage Fault, SVCall, PendSV, SysTick, and so on. They all share the same handling machinery and differ only in priority.

### The Vector Table — the Phone Book of Exception Handling

When an exception occurs, the processor needs to know where the corresponding handler is. ARM's solution is the **vector table**—an array of function pointers stored in memory, one entry per exception type.

On Cortex-M, the vector table starts at address `0x00000000` by default (it can be relocated via the VTOR register). The first entry is not a function pointer but the initial stack pointer (MSP) value—an elegant design: on reset, the processor automatically loads this value into SP, with no extra initialization code needed. From the second entry onward sit the Reset Handler, NMI Handler, HardFault Handler, and so on.

```c
/// @brief A sketch of the Cortex-M vector table structure
typedef void (*ExceptionHandler)(void);

/// @brief Vector table layout (simplified; the real table includes more fault vectors)
typedef struct {
    uint32_t         kInitialStackPointer;  // initial MSP value
    ExceptionHandler reset_handler;         // reset
    ExceptionHandler nmi_handler;           // non-maskable interrupt
    ExceptionHandler hardfault_handler;     // hard fault
    ExceptionHandler memmanage_handler;     // memory management fault
    ExceptionHandler busfault_handler;      // bus fault
    ExceptionHandler usagefault_handler;    // usage fault
    // ... several reserved entries omitted ...
    ExceptionHandler svcall_handler;        // system service call
    ExceptionHandler pendsv_handler;        // pendable system service call
    ExceptionHandler systick_handler;       // system tick timer
    // external interrupt vectors start here ...
} VectorTable;
```

### Exception Stacking — the Context the Processor Saves for You

When an exception occurs, the Cortex-M processor automatically saves the values of eight registers onto the current stack: R0, R1, R2, R3, R12, LR, PC, and xPSR. This operation is called "stacking", and the hardware does it all by itself—you never need to write any context-saving code. When exception handling finishes and the return instruction executes, the processor automatically restores those eight registers from the stack ("unstacking").

This design means your interrupt service routine is just an ordinary C function: no special decorator like `__irq` needed (that was the ARM7TDMI-era way of doing things), and the compiler does not need to generate any special prologue or epilogue code. Compare that with the ARM7TDMI days, when you had to write the register save/restore code yourself, and the Cortex-M scheme feels refreshingly clean.

But there is a spot where it is easy to step on a rake: if your stack space is insufficient (say, the stack allotted to some interrupt is too small), the stacking operation itself triggers another exception—and that exception's handling also needs to stack—the result is a cascading stack overflow that finally triggers a HardFault. That is why sensible stack sizing is crucial in Cortex-M development: as a rule of thumb, reserve at least 512 bytes for the main stack, and if an RTOS is running, at least 256 bytes per thread stack.

### Interrupt Priorities — Who Goes First

The ARM Cortex-M supports configurable interrupt priorities. Every interrupt source has a priority register, and the smaller the value, the higher the priority. The Cortex-M3 supports up to 256 priority levels (8-bit width), but in actual implementations most chips use only the upper 4 bits—which is to say, the priority levels actually available to you may number only 16 (STM32F1/F4 are exactly such a case).

Priority grouping splits the 8-bit priority register into two parts: the high bits form the "preemption priority", the low bits the "sub-priority". An interrupt with a higher preemption priority can interrupt a lower-priority handler that is currently being serviced (nested interrupts), while the sub-priority only decides which of several interrupts with the same preemption priority gets handled first. CMSIS provides `NVIC_SetPriorityGrouping()` and `NVIC_SetPriority()` to configure all this. If you are just getting started, the default grouping of 4 bits preemption + 0 bits sub-priority will do fine; leave the tweaking until you actually need fine-grained control.

## Step 6 — Connect This Knowledge to Writing C Code

At this point we have walked through the core concepts of the ARM architecture. You might ask: I write C/C++, not assembly—how exactly does this knowledge show up in real programming? Let us go through a few direct connections.

### Calling Conventions and Function Design

We mentioned earlier that the AAPCS mandates the first four arguments through R0-R3. The direct impact on C function design is this: if you control the function signature, keep the argument count at four or fewer whenever possible, and avoid passing large structs. A common practice is to trim the arguments of frequently called hot-path functions down to four or fewer, leaving the compiler the maximum room to optimize.

### volatile and Register Access

In embedded programming the `volatile` keyword is practically everywhere—every memory-mapped pointer to a hardware register must carry `volatile`. The reason is that compiler optimization assumes memory values do not change "on their own", but a hardware register's value can be modified at any moment by external events (a DMA transfer completing, a peripheral's state changing). `volatile` tells the compiler "really read this address every single time; do not cache the value".

```c
/// @brief The canonical memory-mapped register access pattern
/// volatile guarantees that every access really reads/writes the hardware
#define GPIOA_ODR_ADDRESS ((volatile uint32_t*)0x40020014U)

void set_gpio_pin(int pin)
{
    // without volatile, the compiler may consider consecutive writes to the same address redundant and optimize them away
    *GPIOA_ODR_ADDRESS |= (1U << pin);
}
```

### Stack Usage and Memory Layout Awareness

Once you understand ARM's stacking mechanism and dual-stack design, you have solid ground to stand on when planning memory usage. In a bare-metal application, you need to make sure the linker script allocates enough space for the stack; in an RTOS application, you need to give each thread a sensible stack size. Rules of thumb: a simple thread with no floating-point operations starts at 256 bytes; threads doing floating-point work or having deep function call chains need 512-1024 bytes. And if the Cortex-M4's FPU is enabled, exception stacking additionally saves 16 floating-point registers (S0-S15) plus the FPSCR—an extra 68 bytes of overhead that you cannot afford to ignore.

## Bridging to C++

If you came here from the C++ part of this tutorial, the relationship between this low-level knowledge and C++ is actually far bigger than you might imagine. ARM's hardware characteristics directly shaped many C++ design decisions.

### Cache-Friendly Design and Data Locality

ARM processors (especially the Cortex-A series) have multiple levels of cache. Understanding the size and behavior of cache lines (usually 32 or 64 bytes) directly affects C++ data structure design. Packing the fields a hot path touches frequently compactly at the front of the struct and leaving cold data at the back, or controlling alignment with `alignas`, can all significantly improve performance—at the C tutorial stage you only need to build the awareness; later C++ chapters will dig in deep.

```cpp
// An unfriendly layout: hot and cold data interleaved
struct BadSensorData {
    uint32_t timestamp;   // hot
    char name[32];        // cold — hogging the cache line
    float value;          // hot
    int calibration_id;   // cold
    float raw_value;      // hot
};

// A friendly layout: hot data concentrated in the first 16 bytes, one cache line and done
struct GoodSensorData {
    uint32_t timestamp;   // hot
    float value;          // hot
    float raw_value;      // hot
    // --- the cache line boundary is roughly here ---
    char name[32];        // cold
    int calibration_id;   // cold
};
```

### Memory Layout and the ABI of C++ Objects

C++ object memory layout on ARM platforms follows the ABI rules of the AAPCS: ordinary member variables are laid out in declaration order, the virtual table pointer (vptr) sits at the start of the object, and multiple inheritance may bring multiple vptrs. These layout details are crucial when serializing, transmitting over a network, or interoperating with C code. If you write an object-oriented driver framework in C++ on a Cortex-M, understanding the vptr's position and size lets you calculate precisely how many bytes a driver object actually occupies.

### The Cost of Exception Handling

On embedded ARM platforms, the runtime overhead of the C++ exception handling mechanism (try/catch/throw) needs serious consideration. Exception handling tables and unwind information noticeably increase binary size, and the stack unwinding process when an exception is thrown involves a lot of memory operations. On a Cortex-M where both Flash and RAM are tight, many teams choose to add `-fno-exceptions` at compile time to disable C++ exceptions entirely, handling errors via return codes instead. This is not "not C++ enough"—it is a sensible trade-off against your resources.

### constexpr and Compile-Time Computation

Many operations that require table lookups at runtime (CRC computation, bit-manipulation mask generation) can, if completed at compile time through `constexpr` functions, save both Flash and runtime. On entry-level chips like the Cortex-M0/M0+, which do not even have a hardware divider, the value of compile-time computation is especially pronounced.

## Exercises

Here are a few exercises for you to wrestle with on your own. This installment is a theoretical survey, so the exercises are mainly conceptual analysis; the hands-on ones that need a board or QEMU are marked challenge/optional.

### Exercise 1: The Role of the IPSR Register

**Difficulty: Basic** · conceptual analysis, no code needed

The IPSR is part of the Cortex-M program status registers (xPSR). Please answer: at what point in time is the IPSR updated by hardware? What information does it record? Why does reading the IPSR inside an interrupt service routine help you determine "which exception is currently being handled"?

### Exercise 2: Debugging a HardFault

**Difficulty: Intermediate** · describe the debugging approach in words, no board required

When a program triggers a HardFault by accessing an illegal address, the hardware pushes the current registers onto the stack. Please describe the entire flow from the HardFault firing to locating "the address of the faulting instruction": which values on the stack do you need to read? Where does the pushed PC point?

Hint: the exception handling section of this installment covered this—the stack frame pointer the HardFault Handler receives points to `{R0, R1, R2, R3, R12, LR, PC, xPSR}`.

### Exercise 3: Analyzing AAPCS Parameter Passing

**Difficulty: Intermediate** · requires the arm-none-eabi-gcc toolchain

Write two functions: one taking 4 int parameters, the other taking 6. Disassemble them with `arm-none-eabi-objdump -d` and compare the call sequences; observe that the first 4 arguments travel through R0–R3 and figure out where arguments 5 and 6 go.

```c
int exercise_aapcs_4(int a, int b, int c, int d) {
    return a + b + c + d;
}

int exercise_aapcs_6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}
```

### Exercise 4: Vector Table Relocation (Challenge, Optional)

**Difficulty: Challenge** · optional; requires prior knowledge of startup files, linker scripts, and bootloaders

Read a Cortex-M startup file (such as startup_stm32f407xx.s) and draw out the complete vector table layout; then modify the linker script to relocate the vector table to RAM, achieving runtime dynamic modification of interrupt vectors. This is the foundation of bootloader development.

## References

- [ARM Cortex-M4 Technical Reference Manual - Bus Interfaces](https://developer.arm.com/documentation/ddi0439/b/Functional-Description/Interfaces/Bus-interfaces)
- [AAPCS32 Specification (ARM ABI)](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [Joseph Yiu: The Definitive Guide to ARM Cortex-M3](https://www.keil.com/dd/docs/datashts/arm/cortex_m3/r1p1/ddi0337e_cortex_m3_r1p1_trm.pdf)
- [ARM Cortex-M - Wikipedia](https://en.wikipedia.org/wiki/ARM_Cortex-M)
- [cppreference: the volatile keyword](https://en.cppreference.com/w/c/language/volatile)
