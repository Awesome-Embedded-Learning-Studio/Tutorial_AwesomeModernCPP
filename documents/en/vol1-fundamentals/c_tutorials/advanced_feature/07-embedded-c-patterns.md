---
chapter: 1
cpp_standard:
- 11
- 17
description: Register access patterns, correct use of volatile, interrupt-safe programming, peripheral abstraction layer design, and bare-metal development patterns
difficulty: intermediate
order: 107
platform: host
prerequisites:
- Structures and Memory Alignment
- Function Pointers and the Callback Pattern
- 'Advanced Pointers: Multilevel Pointers, Pointers and const'
reading_time_minutes: 16
tags:
- host
- cpp-modern
- intermediate
- 嵌入式
- 单片机
title: Embedded C Programming Patterns
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md
  source_hash: 660f3902dc7a7de554f854a10d40fe59837ed4a7fe76cbaadcf62e4d76332aa7
  translated_at: '2026-11-23T01:12:36+00:00'
  engine: glm-5.3-fence-fix
  token_count: 13372
---
# Embedded C Programming Patterns

When we write desktop programs, we barely need to care whether the compiler might quietly optimize away a memory read, or whether two pieces of code might stomp on the same data at the same instant. But the moment you turn your gaze to bare metal—no operating system, no standard library, not even a standard `main` entry point—all of these questions come pouring out. Embedded C programming has a pattern language of its own: registers are mapped through structures, hardware state must be protected with `volatile`, and data exchange between interrupts and the main loop needs carefully designed synchronization mechanisms.

In this tutorial we take these patterns apart one by one. Understanding them is a necessary prerequisite for the embedded applications of C++ that come later in this series—`constexpr` register configuration, zero-overhead abstraction, and type-safe hardware access.

The code in this article targets ARM Cortex-M, but every concept and pattern applies equally well to other architectures. On the host, you can verify compilation with a cross-compiler:

```text
平台：ARM Cortex-M3/M4（STM32F1/F4 等）
编译器：arm-none-eabi-gcc >= 10
主机验证：gcc -Wall -Wextra -std=c11（非硬件相关代码）
依赖：无
```

## Step 1 — Figure Out How to Talk to Hardware Registers

The most fundamental operation in embedded development is reading and writing hardware registers—those peripheral control ports mapped into the memory address space. Let's look at three access patterns, from the rawest to the most elegant.

### Bit Manipulation: The Most Primitive and the Most Flexible

Every bit in a peripheral register carries its own meaning. For example, a GPIO port's mode register might use the lowest 2 bits to control the mode (input/output/alternate/analog) and the next 2 bits to control pull-up/pull-down. First, let's define a set of generic bit-manipulation macros—nearly every embedded project has a utility header somewhere that looks like this:

```c
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
```

If the `reg` macro argument is an expression with side effects (say `*ptr++`), it will be evaluated multiple times. In production code, `static inline` functions are the preferred replacement, but the macro versions are so ubiquitous in embedded codebases that you need to be able to read them.

Let's see these macros configure an imaginary GPIO port. Suppose GPIOA's base address is `0x40020000`, offset `0x00` holds the mode register `MODER`, and every 2 bits control one pin:

```c
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
```

Notice the `*(volatile uint32_t*)` cast—`volatile` tells the compiler that the value at this address may be changed by hardware at any moment, so every read and write must genuinely reach memory; it cannot be cached or optimized away.

### Structure Mapping: Giving Registers Names

Raw address offsets and bit operations get the job done, but the readability is dreadful—who can tell at a glance that `*(uint32_t*)(0x40020000 + 0x14)` is GPIOA's output data register? Structure mapping is the more elegant solution:

```c
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
```

Now the configuration code becomes wonderfully clear: `GPIOA->MODER &= ~(3U << 10); GPIOA->MODER |= (1U << 10);`.

Structure mapping carries one implicit premise: the memory layout must match the hardware register layout exactly. Most ARM peripheral registers are arranged 32-bit aligned, which matches `uint32_t`'s natural alignment perfectly. When there is reserved space between registers, you have to add `volatile uint32_t RESERVED0` placeholders to the structure—that is exactly how the CMSIS headers for Cortex-M do it.

### Atomic Access: The BSRR Pattern

The pin configuration above used the three-step "read-modify-write". That is fine when no interrupt interferes, but if an interrupt lands between the "read" and the "write", and that interrupt also modifies the same register—your "write" then clobbers the interrupt's modification. This is the classic read-modify-write race.

Some peripherals offer atomic-operation registers to solve this problem. The STM32 GPIO's BSRR is a typical example—writing 1 to a bit in the low 16 bits sets the corresponding pin, writing 1 to a bit in the high 16 bits clears it, and writing 0 has no effect. You just write to it, and the hardware guarantees atomicity:

```c
// 原子置位 PA5 和 PA6
GPIOA->BSRR = (1U << 5) | (1U << 6);
// 原子清除 PA7
GPIOA->BSRR = (1U << (7 + 16));
```

If the hardware has no such atomic-operation register, the only recourse is protecting the critical section by disabling interrupts.

## Step 2 — Understand What volatile Does and Doesn't Do

`volatile` is probably the most deeply misunderstood keyword in embedded C.

### What volatile Does

`volatile` tells the compiler: every access to this object must actually happen—it must not be optimized away, and it must not be reordered to the other side of another `volatile` access. Concretely: the compiler will not cache a `volatile` variable's value in a register, will not optimize away reads and writes that look "redundant", and will not reorder the sequence of `volatile` operations.

```c
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
```

### What volatile Does Not Do (This Matters More)

`volatile` is **not** a thread-synchronization tool. It does **not** guarantee atomicity, and it does **not** stop the CPU's out-of-order execution. `volatile` constrains only the compiler, not the CPU—ARM Cortex-M may reorder ordinary memory accesses, so two `volatile` writes look ordered from the compiler's point of view, yet the CPU may commit them to the bus in a different order. If you need strict memory ordering, you must use memory-barrier instructions such as DMB/DSB.

Moreover, `volatile` does not make read-modify-write operations atomic:

```c
volatile uint32_t counter;
counter++;  // 不是原子的！读、加、写三步
```

`counter++` is actually a three-step operation: read, add 1, write back. If an interrupt fires between the read and the write, and the interrupt also modifies counter, one update is lost.

Legitimate uses of `volatile`: hardware register mapping, and simple flags shared between an interrupt and the main loop. Situations where `volatile` should not be used: synchronization between threads (use a mutex or atomics), bulk data transfer (use DMA), and anything that requires an atomic read-modify-write.

## Step 3 — Master Interrupt-Safe Programming

Interrupts are the core mechanism of embedded systems—a hardware event arrives, breaks the current execution flow, and jumps into an ISR to be handled. The problem is that the ISR and the main loop share the same memory space; if both access the same data at once, the mild outcome is corrupted data and the severe one is a system that runs off into the weeds.

### Critical Section Protection

The crudest-but-effective method: disable interrupts before touching shared data, re-enable them when done. Here a nesting counter is used to support nested critical sections:

```c
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
```

Disabling interrupts has a price: while they are disabled, every interrupt is masked and the system's real-time behavior degrades. Critical sections must be kept as short as possible—get in, do the necessary operation, get out immediately. Never call blocking functions or do heavy computation inside a critical section.

### The Ring Buffer: The Classic Interrupt-Safe Data Structure

The most common communication pattern between an interrupt and the main loop is "producer-consumer"—the interrupt writes data in, the main loop reads it out. The ring buffer is the standard implementation, and its elegance is that as long as "write" and "read" each run in exactly one context, no lock is needed:

```c
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
```

The key constraint: `head` is modified only by the writer, `tail` only by the reader. Because each side only reads the other's pointer and only writes its own, no mutex is required.

### The Golden Rule of Interrupt Handling

For a simple "event happened" notification, a single `volatile` flag is enough:

```c
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
```

The ISR does the bare minimum—clear the interrupt flag, set the application-level flag. This is the golden rule of interrupt handling: **keep the ISR as short as possible and leave the heavy lifting to the main loop**.

## Step 4 — Design a Layered Peripheral Abstraction Layer

If an embedded project manipulates register addresses directly from its business logic, the code becomes an unportable plate of spaghetti. The way out is to introduce a peripheral abstraction layer (PAL) that encapsulates the hardware details inside low-level drivers.

### The Three-Layer Architecture

A sensible layering usually looks like this: the bottom layer holds register definitions and bit-manipulation utilities (bound to the specific chip), the middle layer holds peripheral drivers (GPIO, UART, SPI, and friends), and the top layer holds application logic (which never touches registers). The middle layer's interface must be designed to be independent of the specific chip:

```c
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
```

```c
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
```

The application layer above never touches registers:

```c
static const GpioPin kLedPin = { GPIOA, 5 };

gpio_init(&kLedPin, kGpioModeOutput, kGpioPullNone);
gpio_toggle(&kLedPin);
```

When you switch chips, only the bottom-layer register definitions and the middle-layer implementation change; the application code above does not move at all. The `GpioPin` structure packages "which pin of which port" into a passable object—far clearer than passing bare `(GPIOA, 5)` arguments everywhere.

## Step 5 — Understand the Boot Flow of a Bare-Metal Program

Free of an operating system, even `main` is not the first thing executed. Understanding the complete flow of a bare-metal program from power-on to entering `main` is a fundamental skill.

### Startup Code

The ARM Cortex-M flow after power-on: the CPU reads the initial stack pointer (the first 32-bit word) and the reset vector (the second 32-bit word, i.e. the Reset_Handler address) from the vector table, then jumps to Reset_Handler. Reset_Handler does three things: copy the `.data` section from Flash to SRAM, zero the `.bss` section, and call `main`.

```c
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
```

Symbols like `_estack` and `_sdata` are not real variables—they are address labels defined in the linker script. Once declared `extern` in C code, taking their address yields the start or end of the corresponding section. The vector table is forced to the beginning of Flash with `__attribute__((section(".isr_vector")))`, and `__attribute__((weak))` allows users to override the default interrupt handlers.

### The Linker Script

The linker script tells the linker the program's memory layout—where Flash starts and ends, where SRAM starts and ends, and which section goes where. The key concept is `> RAM AT > FLASH`—the `.data` section's run address is in RAM, but its load address is in Flash. After power-on, the startup code copies it into RAM. The `.bss` section has only start and end addresses, and the startup code simply zeroes it.

```c
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
```

## Bridging to C++

Embedded C++ carries a few important constraints: exceptions require stack-unwinding runtime support, so most bare-metal projects disable them with `-fno-exceptions` and report errors through return values instead; RTTI (`dynamic_cast`/`typeid`) adds code size and is usually disabled with `-fno-rtti`; and bare metal has no OS heap manager, so `new`/`delete` are unavailable by default—full static allocation is recommended (`std::array` instead of `std::vector`, fixed-size containers and memory pools instead of dynamic allocation).

C++'s improvements for embedded code concentrate on three fronts:

| C Pattern | C++ Improvement |
|-----------|-----------------|
| Manually ensuring init/cleanup pairing | RAII constructors/destructors manage it automatically |
| Macros for bit manipulation | `constexpr` computes configuration values at compile time |
| Register config tables consulted at runtime | Templates bake port/pin constants in at compile time, generating code as efficient as handwritten |
| Function pointers + `void*` context | `std::function` or template callbacks |

`constexpr` is especially valuable in the embedded domain—compute register configuration values at compile time, and at runtime simply write precomputed constants. This removes the runtime computation overhead and eliminates the chance of runtime errors in one stroke. When this series later digs into the embedded applications of C++, we will expand in detail on how `constexpr` + templates deliver a zero-overhead hardware abstraction layer.

## Common Pitfalls Quick Reference

| Pitfall | Description | Fix |
|---------|-------------|-----|
| Using `volatile` as thread synchronization | `volatile` guarantees neither atomicity nor memory ordering | Use atomic operations or interrupt masking |
| Structure mapping without padding | Compiler padding doesn't match the hardware layout | Check the manual and add `RESERVED` fields |
| Doing too much in the ISR | Interrupt latency grows and the system responds slower | The ISR only sets flags; heavy work goes to the main loop |
| Read-modify-write race | An interrupt modifies the same register in the read-write gap | Use an atomic-operation register (BSRR) or disable interrupts |
| `main` returns | On bare metal, no OS takes over after `main` returns | Put an infinite loop after `main()` in the startup code |

## Exercises

### Exercise 1: A Generic Ring Buffer

**Difficulty: Intermediate** · Turn the uint8_t version into a generic void* version

Rework the article's `uint8_t` ring buffer into a generic version (implemented with `void*` + element size):

```c
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
```

Hint: use `memcpy` internally for generic byte copying, change `head`/`tail` to absolute counts (a `uint32_t` fears no overflow), and compute the actual index as `count % capacity`.

### Exercise 2: Designing a UART Abstraction Layer Interface

**Difficulty: Intermediate** · Design the interface only; no interrupt timing implementation

Following the peripheral abstraction layer approach in this article, design a chip-independent UART abstraction layer interface. You are not required to implement interrupt-driven byte-by-byte transmission; you only need to define the interface clearly: which fields the driver structure needs (transmit/receive buffers, state), the signatures of the `init`/`write`/`read` trio, and what `write` should return when the buffer is full.

```c
typedef struct { /* 你设计 */ } UartDriver;

void  uart_init(UartDriver* uart, uint32_t baud);
size_t uart_write(UartDriver* uart, const uint8_t* data, size_t len);
size_t uart_read(UartDriver* uart, uint8_t* data, size_t len);
```

Think about it: why does hiding the buffers and state inside the structure and exposing only function interfaces keep the upper-layer code from being welded to a specific chip?

### Exercise 3: Reading a Linker Script

**Difficulty: Basic** · Read and explain an existing script; writing one from scratch not required

Find an existing Cortex-M linker script (the startup-flow section of this article provided an example) and explain it section by section: which regions does the `MEMORY` command define? Why does the vector table sit at the start of Flash? What does `AT > FLASH` on the `.data` section mean? Why does the `.bss` section use `NOLOAD`?

Then think once more: to change Flash in this script to 256K and SRAM to 64K, which lines would you need to touch?

## References

- [ARM Cortex-M Programming Guide](https://developer.arm.com/documentation)
- [The volatile keyword - cppreference](https://en.cppreference.com/w/c/language/volatile)
- [GCC Linker Script reference](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [CMSIS - Cortex Microcontroller Software Interface Standard](https://arm-software.github.io/CMSIS_5/General/html/index.html)
