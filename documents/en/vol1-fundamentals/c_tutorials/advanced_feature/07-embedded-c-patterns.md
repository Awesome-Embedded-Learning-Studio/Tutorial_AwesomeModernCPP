---
chapter: 1
cpp_standard:
- 11
- 17
description: Register access patterns, proper use of `volatile`, interrupt-safe programming,
  peripheral abstraction layer design, and bare-metal development patterns
difficulty: intermediate
order: 107
platform: host
prerequisites:
- 结构体、联合体与内存对齐
- 函数指针与回调机制
- 指针进阶：多级指针、指针与 const
reading_time_minutes: 17
tags:
- host
- cpp-modern
- intermediate
- 嵌入式
- 单片机
title: Embedded C Programming Patterns
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md
  source_hash: 44dabb8c455d7a5563bc16965235b0f6af1d490541fd892e391cb6c8b66f2928
  token_count: 3384
  translated_at: '2026-05-26T10:37:36.534206+00:00'
---
# Embedded C Programming Patterns

When writing desktop applications, we rarely worry about whether the compiler will silently optimize away a memory read, or whether two pieces of code will trample the same data at the same time. But once we turn our attention to bare-metal—no operating system, no standard library, not even a standard ``main`` entry point—these problems all surface. Embedded C programming has its own pattern language: registers are mapped using structures, hardware state must be protected with ``volatile``, and data exchange between interrupts and the main loop requires carefully designed synchronization mechanisms.

In this tutorial, we break down these patterns one by one. Understanding these patterns is a necessary prerequisite for learning embedded C++ applications later in this series—``constexpr`` register configuration, zero-overhead abstraction, and type-safe hardware access.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Master three register access patterns (bit manipulation, struct mapping, atomic access)
> - [ ] Correctly use the ``volatile`` qualifier and understand its semantic boundaries
> - [ ] Implement interrupt-safe data exchange patterns
> - [ ] Design a layered peripheral abstraction layer
> - [ ] Understand the startup process and linker script of bare-metal programs

## Environment Setup

The code in this article targets the ARM Cortex-M platform, but all concepts and patterns apply equally to other architectures. We can verify compilation on the host machine using a cross-compiler:

````text
平台：ARM Cortex-M3/M4（STM32F1/F4 等）
编译器：arm-none-eabi-gcc >= 10
主机验证：gcc -Wall -Wextra -std=c11（非硬件相关代码）
依赖：无
````

## Step 1 — Figuring Out How to Interact with Hardware Registers

The most fundamental operation in embedded development is reading and writing hardware registers—those peripheral control ports mapped into the memory address space. Let's look at three access patterns, ranging from primitive to elegant.

### Bit Manipulation: The Most Primitive Yet Most Flexible

Every bit in a peripheral register has an independent meaning. For example, in a GPIO port mode register, the lower two bits might control the mode (input/output/alternate function/analog), and the next two bits control the pull-up and pull-down resistors. First, let's define a set of generic bit manipulation macros; almost every embedded project has a similar utility header file:

````c
// bit_ops.h — 通用位操作工具
#define BIT_SET(reg, n)       ((reg) |=  (1U << (n)))
#define BIT_CLEAR(reg, n)     ((reg) &= ~(1U << (n)))
#define BIT_TOGGLE(reg, n)    ((reg) ^=  (1U << (n)))
#define BIT_READ(reg, n)      (((reg) >> (n)) & 1U)

// 写入字段：将 reg 中 [high:low] 区间写入 val
#define FIELD_WRITE(reg, val, high, low) \
    do { \
        uint32_t mask = ~(((1U << ((high) - (low) + 1)) - 1) << (low)); \
        (reg) = ((reg) & mask) | (((val) & ((1U << ((high) - (low) + 1)) - 1)) << (low)); \
    } while (0)
````

> ⚠️ **Pitfall Warning**
> If the ``reg`` in a macro parameter is an expression with side effects (like ``*ptr++``), it will be evaluated multiple times. In production code, we recommend using ``static inline`` functions instead, but the macro version is so widespread in embedded codebases that you need to be able to read it.

Let's see how these macros configure a hypothetical GPIO port. Suppose the GPIOA base address is ``0x40020000``, offset ``0x00`` is the mode register ``MODER``, and every two bits control one pin:

````c
#define GPIOA_BASE   0x40020000U
#define GPIOA_MODER  (*(volatile uint32_t*)(GPIOA_BASE + 0x00))
#define GPIOA_ODR    (*(volatile uint32_t*)(GPIOA_BASE + 0x14))

// 将 PA5 配置为输出模式（bit[11:10] = 01）
void gpioa_pin5_output_enable(void)
{
    uint32_t moder = GPIOA_MODER;
    moder &= ~(3U << 10);   // 清除 bit[11:10]
    moder |=  (1U << 10);   // 设置为 01（输出）
    GPIOA_MODER = moder;
}

void gpioa_pin5_set(void)   { BIT_SET(GPIOA_ODR, 5); }
void gpioa_pin5_clear(void) { BIT_CLEAR(GPIOA_ODR, 5); }
````

Note that ``*(volatile uint32_t*)`` cast—``volatile`` tells the compiler: the value at this address can change at any time by hardware, so every read and write must actually access memory, and must not be cached or optimized away.

### Struct Mapping: Giving Registers Names

Using address offsets and bit manipulation directly gets the job done, but the readability is poor—who can tell at a glance that ``*(uint32_t*)(0x40020000 + 0x14)`` is the GPIOA output data register? Struct mapping is a more elegant solution:

````c
typedef struct {
    volatile uint32_t MODER;    // 偏移 0x00
    volatile uint32_t OTYPER;   // 偏移 0x04
    volatile uint32_t OSPEEDR;  // 偏移 0x08
    volatile uint32_t PUPDR;    // 偏移 0x0C
    volatile uint32_t IDR;      // 偏移 0x10
    volatile uint32_t ODR;      // 偏移 0x14
    volatile uint32_t BSRR;     // 偏移 0x18
    volatile uint32_t LCKR;     // 偏移 0x1C
    volatile uint32_t AFRL;    // 偏移 0x20
    volatile uint32_t AFRH;    // 偏移 0x24
} GpioReg;

#define GPIOA  ((GpioReg*) 0x40020000U)
#define GPIOB  ((GpioReg*) 0x40020400U)
````

Now the configuration code becomes very clear: ``GPIOA->MODER &= ~(3U << 10); GPIOA->MODER |= (1U << 10);``.

Struct mapping has an implicit prerequisite: the memory layout must exactly match the hardware register layout. Most ARM peripheral registers are 32-bit aligned, which perfectly matches the natural alignment of ``uint32_t``. If there are reserved spaces between registers, we need to add ``volatile uint32_t RESERVED0`` placeholders in the struct—this is exactly how the Cortex-M CMSIS header files do it.

### Atomic Access: The BSRR Pattern

Earlier, we configured pins using a "read-modify-write" three-step process. This is fine when there is no interrupt interference, but if an interrupt arrives between the "read" and "write" steps, and the interrupt also modifies the same register—your "write" will overwrite the interrupt's modifications. This is the classic read-modify-write race condition.

Some peripherals provide atomic operation registers to solve this problem. The STM32 GPIO BSRR is a typical example—writing 1 to the lower 16 bits sets the corresponding pin, writing 1 to the upper 16 bits clears it, and writing 0 has no effect. Just write to it directly, and the hardware guarantees atomicity:

````c
// 原子置位 PA5 和 PA6
GPIOA->BSRR = (1U << 5) | (1U << 6);
// 原子清除 PA7
GPIOA->BSRR = (1U << (7 + 16));
````

If the hardware does not provide this kind of atomic operation register, we can only rely on disabling interrupts to protect the critical section.

## Step 2 — Understanding What volatile Actually Does and Doesn't Do

``volatile`` is perhaps the most misunderstood keyword in embedded C.

### What volatile Does

``volatile`` tells the compiler: every access to this object must actually be executed, cannot be optimized away, and cannot be reordered across other ``volatile`` accesses. Specifically, the compiler will not cache the value of a ``volatile`` variable in a register, will not optimize away seemingly "redundant" reads and writes, and will not reorder the sequence of ``volatile`` operations.

````c
// 没有 volatile——编译器可能优化掉整个循环
int* flag = (int*)0x20000000;
while (*flag == 0) {
    // 编译器可能只读一次 flag，然后死循环
}

// 加上 volatile——每次循环都会重新读取
volatile int* flag = (volatile int*)0x20000000;
while (*flag == 0) {
    // 编译器每次都生成内存读取指令
}
````

### What volatile Doesn't Do (This Is More Important)

``volatile`` is **not** a thread synchronization tool. It **does not guarantee** atomicity, and it **does not prevent** CPU out-of-order execution. ``volatile`` only constrains the compiler, not the CPU—ARM Cortex-M can reorder normal memory accesses, so two ``volatile`` writes appear ordered to the compiler, but the CPU might commit them to the bus in a different order. If strict memory ordering is required, we must use memory barrier instructions like DMB/DSB.

Additionally, ``volatile`` does not guarantee the atomicity of read-modify-write operations:

````c
volatile uint32_t counter;
counter++;  // 不是原子的！读、加、写三步
````

``counter++`` is actually a three-step operation: read, add 1, and write back. If an interrupt occurs between the read and the write, and the interrupt also modifies the counter, an update will be lost.

> ⚠️ **Pitfall Warning**
> Reasonable use cases for ``volatile``: hardware register mapping, simple flags shared between interrupts and the main loop. Scenarios where we should not use ``volatile``: inter-thread synchronization (use a mutex or atomic), large data transfers (use DMA), any situation requiring atomic read-modify-write.

## Step 3 — Mastering Interrupt-Safe Programming

Interrupts are the core mechanism of embedded systems—when a hardware event arrives, it interrupts the current execution flow and jumps to the ISR to handle it. The problem is that the ISR and the main loop share the same memory space. If both access the same data simultaneously, the best-case scenario is data corruption, and the worst-case scenario is the system running away.

### Critical Section Protection

The simplest and most brute-force, yet effective, method is to disable interrupts before accessing shared data, and re-enable them after the operation is complete. Here we use a nesting counter to support nested critical sections:

````c
static volatile uint32_t s_critical_nesting = 0;

void critical_enter(void)
{
    __disable_irq();
    s_critical_nesting++;
}

void critical_exit(void)
{
    if (s_critical_nesting > 0) {
        s_critical_nesting--;
    }
    if (s_critical_nesting == 0) {
        __enable_irq();
    }
}
````

> ⚠️ **Pitfall Warning**
> Disabling interrupts comes at a cost: while interrupts are disabled, all interrupts are masked, and system real-time performance degrades. Critical sections must be as short as possible—get in, do the necessary operations, and get out immediately. Never call blocking functions or perform complex calculations inside a critical section.

### Ring Buffer: The Classic Interrupt-Safe Data Structure

The most common communication pattern between interrupts and the main loop is "producer-consumer"—the interrupt writes data in, and the main loop reads data out. The ring buffer is the standard implementation. The beauty of it is that as long as the "write" and "read" operations each execute in only one context, no locks are needed:

````c
#define RING_BUFFER_SIZE 64

typedef struct {
    volatile uint32_t head;     // 写入位置（ISR 修改）
    volatile uint32_t tail;     // 读取位置（主循环修改）
    uint8_t buffer[RING_BUFFER_SIZE];
} RingBuffer;

void ring_buffer_init(RingBuffer* rb)
{
    rb->head = 0;
    rb->tail = 0;
}

// ISR 中调用：只有 ISR 修改 head
uint32_t ring_buffer_write(RingBuffer* rb, uint8_t data)
{
    uint32_t next_head = (rb->head + 1) % RING_BUFFER_SIZE;
    if (next_head == rb->tail) {
        return 0;  // 缓冲区满
    }
    rb->buffer[rb->head] = data;
    rb->head = next_head;
    return 1;
}

// 主循环中调用：只有主循环修改 tail
uint32_t ring_buffer_read(RingBuffer* rb, uint8_t* data)
{
    if (rb->head == rb->tail) {
        return 0;  // 缓冲区空
    }
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    return 1;
}
````

The key constraint is that ``head`` is only modified by the writer, and ``tail`` is only modified by the reader. Because both sides only read the other's pointer and only write their own pointer, no mutex is needed.

### The Golden Rule of Interrupt Handling

For simple "event occurred" notifications, a single ``volatile`` flag is sufficient:

````c
static volatile uint8_t s_timer_flag = 0;

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR &= ~TIM_SR_UIF;
        s_timer_flag = 1;
    }
}

// 主循环
if (s_timer_flag) {
    s_timer_flag = 0;
    handle_timer_event();  // 重活在主循环处理
}
````

The ISR does the absolute minimum—clearing the interrupt flag and setting the application-layer flag. This is the golden rule of interrupt handling: **keep the ISR as short as possible, and leave the heavy lifting to the main loop**.

## Step 4 — Designing a Layered Peripheral Abstraction Layer

If an embedded project directly manipulates register addresses in business logic, the code will become an unmaintainable, unportable plate of spaghetti. The solution is to introduce a peripheral abstraction layer (PAL) to encapsulate hardware details in low-level drivers.

### Three-Layer Architecture

A reasonable layering usually looks like this: the bottom layer contains register definitions and bit manipulation utilities (tied to a specific chip), the middle layer contains peripheral drivers (GPIO, UART, SPI, and other modules), and the top layer contains application logic (which never touches registers). The interface design of the middle layer should be chip-agnostic:

````c
// gpio_driver.h — 硬件无关的接口
typedef enum {
    kGpioModeInput  = 0,
    kGpioModeOutput = 1,
    kGpioModeAltFunc = 2,
    kGpioModeAnalog = 3
} GpioMode;

typedef struct {
    GpioReg* port;   // 指向 GPIO 端口的寄存器结构体
    uint8_t  pin;    // 引脚号 0-15
} GpioPin;

void gpio_init(const GpioPin* gpio, GpioMode mode, GpioPull pull);
void gpio_write(const GpioPin* gpio, bool value);
bool gpio_read(const GpioPin* gpio);
void gpio_toggle(const GpioPin* gpio);
````

````c
// gpio_driver.c — 实现细节
void gpio_init(const GpioPin* gpio, GpioMode mode, GpioPull pull)
{
    uint32_t moder = gpio->port->MODER;
    moder &= ~(3U << (gpio->pin * 2));
    moder |=  ((uint32_t)mode << (gpio->pin * 2));
    gpio->port->MODER = moder;

    uint32_t pupdr = gpio->port->PUPDR;
    pupdr &= ~(3U << (gpio->pin * 2));
    pupdr |=  ((uint32_t)pull << (gpio->pin * 2));
    gpio->port->PUPDR = pupdr;
}

void gpio_write(const GpioPin* gpio, bool value)
{
    if (value) {
        gpio->port->BSRR = (1U << gpio->pin);
    } else {
        gpio->port->BSRR = (1U << (gpio->pin + 16));
    }
}
````

The upper application layer never touches registers:

````c
static const GpioPin kLedPin = { GPIOA, 5 };

gpio_init(&kLedPin, kGpioModeOutput, kGpioPullNone);
gpio_toggle(&kLedPin);
````

When switching chips, we only need to change the bottom-layer register definitions and the middle-layer implementation; the upper application code remains completely untouched. The ``GpioPin`` struct packages "which pin of which port" into a passable object, which is much clearer than passing ``(GPIOA, 5)`` raw parameters everywhere.

## Step 5 — Understanding the Startup Process of Bare-Metal Programs

Without an operating system, even ``main`` is not the first thing to be executed. Understanding the complete process of a bare-metal program from power-on to entering ``main`` is fundamental knowledge.

### Startup Code

The process after ARM Cortex-M powers on: the CPU reads the initial stack pointer (the first 32-bit word) and the reset vector (the second 32-bit word, which is the Reset_Handler address) from the vector table, and then jumps to Reset_Handler. Reset_Handler does three things: copy the ``.data`` section from Flash to SRAM, zero out the ``.bss`` section, and call ``main``.

````c
// startup.c — 最小启动代码（ARM Cortex-M）
extern uint32_t _estack;    // 栈顶地址（链接脚本定义）
extern uint32_t _sidata;    // .data 在 Flash 中的起始
extern uint32_t _sdata;     // .data 在 SRAM 中的起始
extern uint32_t _edata;     // .data 在 SRAM 中的结束
extern uint32_t _sbss;      // .bss 起始
extern uint32_t _ebss;      // .bss 结束

int main(void);

void default_handler(void) { while (1) {} }

__attribute__((section(".isr_vector")))
void (*const g_vector_table[])(void) = {
    (void (*)(void))(&_estack),    // 初始栈指针
    Reset_Handler,                  // Reset
    NMI_Handler,                    // NMI
    HardFault_Handler,              // Hard Fault
    default_handler,                // MemManage
    default_handler,                // BusFault
    default_handler,                // UsageFault
    0, 0, 0, 0,                    // 保留
    default_handler,                // SVCall
    default_handler,                // Debug Monitor
    0,                              // 保留
    default_handler,                // PendSV
    default_handler,                // SysTick
};

void Reset_Handler(void)
{
    // 1. 把 .data 段从 Flash 复制到 SRAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) { *dst++ = *src++; }

    // 2. 把 .bss 段清零
    dst = &_sbss;
    while (dst < &_ebss) { *dst++ = 0; }

    // 3. 进入 main
    main();
    while (1) {}  // 裸机 main 不应该返回
}

__attribute__((weak)) void NMI_Handler(void) { default_handler(); }
__attribute__((weak)) void HardFault_Handler(void) { default_handler(); }
````

> ⚠️ **Pitfall Warning**
> ``_estack``, ``_sdata``, and similar symbols are not real variables—they are address labels defined in the linker script. After declaring them with ``extern`` in C code, taking their address yields the start and end positions of the corresponding sections. The vector table uses ``__attribute__((section(".isr_vector")))`` to force placement at the beginning of Flash, and ``__attribute__((weak))`` allows users to override default interrupt handler functions.

### Linker Script

The linker script tells the linker about the program's memory layout—where Flash starts and ends, where SRAM starts and ends, and where each section is placed. The key concept is ``> RAM AT > FLASH``—the run address of the ``.data`` section is in RAM, but its load address is in Flash. After power-on, the startup code copies it to RAM. The ``.bss`` section only has start and end addresses, and the startup code zeros it out directly.

````c
/* link.ld — Cortex-M3 最小链接脚本 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

_stack_size = 1024;

SECTIONS
{
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text : {
        *(.text*) *(.rodata*)
        _etext = .;
    } > FLASH

    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss : {
        _sbss = .;
        *(.bss*) *(COMMON)
        _ebss = .;
    } > RAM
}
````

## Bridging to C++

Embedded C++ has several important constraints: exceptions require stack unwinding runtime support, which most bare-metal projects disable with ``-fno-exceptions``, using return values to indicate errors instead; RTTI (``dynamic_cast``/``typeid``) increases code size and is usually disabled with ``-fno-rtti``; bare-metal has no OS heap manager, so ``new``/``delete`` are not available by default, and we recommend fully static allocation (``std::array`` instead of ``std::vector``, and fixed-size containers and memory pools instead of dynamic allocation).

C++ improvements to embedded code mainly focus on three areas:

| C Pattern | C++ Improvement |
|--------|----------|
| Manually ensuring init/cleanup pairing | RAII constructors/destructors for automatic management |
| Macros for bit manipulation | ``constexpr`` for compile-time calculation of configuration values |
| Runtime lookup of register configuration tables | Templates固化 port/pin constants at compile time, generating code as efficient as hand-written |
| Function pointers + ``void*`` context | ``std::function`` or template callbacks |

``constexpr`` is particularly valuable in the embedded domain—it calculates register configuration values at compile time, and at runtime, we simply write pre-calculated constants. This eliminates runtime computation overhead and avoids the possibility of runtime errors. Later in this series, when we dive deep into embedded C++ applications, we will detail how ``constexpr`` + templates can achieve a zero-overhead hardware abstraction layer.

## Common Pitfalls Quick Reference

| Pitfall | Description | Solution |
|------|------|----------|
| Using ``volatile`` for thread synchronization | ``volatile`` does not guarantee atomicity or memory ordering | Use atomic operations or disable interrupts for protection |
| Forgetting to add padding in struct mapping | Compiler padding does not match the hardware layout | Check the manual and add ``RESERVED`` fields |
| Doing too much in the ISR | Increased interrupt latency, slower system response | ISR only sets flags, heavy lifting is handled in the main loop |
| Read-modify-write race conditions | An interrupt modifies the same register between the read and write | Use atomic operation registers (BSRR) or disable interrupts |
| Returning from ``main`` | In bare-metal, there is no OS to take over after ``main`` returns | Add an infinite loop after ``main()`` in the startup code |

## Exercises

### Exercise 1: Generic Ring Buffer

Refactor the ``uint8_t`` ring buffer from this article into a generic version (implemented using ``void*`` + element size):

````c
typedef struct {
    // 你需要设计内部字段
} RingBuffer;

/// @brief 初始化环形缓冲区
void ring_buffer_init(RingBuffer* rb, void* storage,
                       size_t item_size, size_t capacity);
/// @brief 写入一个元素
uint32_t ring_buffer_write(RingBuffer* rb, const void* item);
/// @brief 读取一个元素
uint32_t ring_buffer_read(RingBuffer* rb, void* item);
/// @brief 查询当前元素数量
uint32_t ring_buffer_count(const RingBuffer* rb);
````

Hint: Use ``memcpy`` internally for generic byte copying, change ``head``/``tail`` to absolute counts (``uint32_t`` won't overflow), and calculate the actual index via ``count % capacity``.

### Exercise 2: Portable UART Abstraction Layer

Design a chip-agnostic abstraction layer interface for the UART peripheral. The driver internally needs two ring buffers (transmit and receive). ``uart_write`` should first write to the buffer and then trigger the transmit interrupt, with the actual byte-by-byte transmission completed in the ISR.

````c
typedef struct { /* 你设计 */ } UartDriver;

void uart_init(UartDriver* uart, uint32_t baud,
               uint8_t* tx_buffer, uint8_t* rx_buffer, size_t buffer_size);
size_t uart_write(UartDriver* uart, const uint8_t* data, size_t len);
size_t uart_read(UartDriver* uart, uint8_t* data, size_t len);
void uart_irq_handler(UartDriver* uart);  // 在 ISR 中调用
````

### Exercise 3: Linker Script and Startup Code

Write a minimal linker script and startup code for an ARM Cortex-M4 (256K Flash, 64K SRAM). Requirements: define the correct MEMORY regions, place the vector table at the beginning of Flash, handle the ``.data`` section address separation, zero out ``.bss``, and add a safe infinite loop after ``main``.

## References

- [ARM Cortex-M Programming Guide](https://developer.arm.com/documentation)
- [volatile keyword - cppreference](https://en.cppreference.com/w/c/language/volatile)
- [GCC Linker Script Reference](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [CMSIS - Cortex Microcontroller Software Interface Standard](https://arm-software.github.io/CMSIS_5/General/html/index.html)
