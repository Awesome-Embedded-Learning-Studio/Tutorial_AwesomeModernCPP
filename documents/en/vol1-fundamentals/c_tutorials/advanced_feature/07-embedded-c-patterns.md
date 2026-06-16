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
  source_hash: e4f778ecdc255a226c66eb3e61c0946ddf67d1d159783a0e884f231c956125cb
  translated_at: '2026-06-16T03:38:47.648537+00:00'
  engine: anthropic
  token_count: 3377
---
# Embedded C Programming Patterns

When writing desktop applications, we rarely worry about whether the compiler will optimize away a memory read operation or if two threads will step on the same data. However, once we look at bare-metal—no operating system, no standard library, and sometimes not even a standard `main` entry—these issues surface immediately. Embedded C programming has its own pattern language: mapping registers with structures, protecting hardware state with `volatile`, and carefully designing synchronization mechanisms for data exchange between interrupts and the main loop.

In this tutorial, we break down these patterns one by one. Understanding these patterns is a necessary prerequisite for learning embedded C++ applications—`constexpr` register configuration, zero-overhead abstractions, and type-safe hardware access.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Master three register access modes (bit manipulation, structure mapping, atomic access)
> - [ ] Correctly use the `volatile` qualifier and understand its semantic boundaries
> - [ ] Implement interrupt-safe data exchange patterns
> - [ ] Design a layered peripheral abstraction layer
> - [ ] Understand the startup process and linker scripts of bare-metal programs

## Environment Setup

The code in this article targets the ARM Cortex-M platform, but all concepts and patterns apply to other architectures as well. On the host machine, we can verify the compilation using a cross-compiler:

```bash
# Verify cross-compiler installation
arm-none-eabi-gcc --version

# Compile a test file (do not link yet)
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -c main.c -o main.o
```

## Step 1 — Figuring Out How to Interact with Hardware Registers

The most fundamental operation in embedded development is reading and writing hardware registers—those peripheral control ports mapped to the memory address space. Let's look at three access patterns, ranging from primitive to elegant.

### Bit Manipulation: The Most Primitive and Flexible

Every bit in a peripheral register has an independent meaning. For example, in a GPIO port mode register, the lower 2 bits might control the mode (input/output/alternate/analog), and the next 2 bits control pull-up/pull-down. First, let's define a set of generic bit manipulation macros; almost every embedded project has a similar utility header file:

```c
// Bit manipulation macros
#define SET_BIT(reg, bit)       ((reg) |= (1U << (bit)))
#define CLEAR_BIT(reg, bit)     ((reg) &= ~(1U << (bit)))
#define READ_BIT(reg, bit)      (((reg) >> (bit)) & 1U)
#define MODIFY_REG(reg, mask, val) ((reg) = ((reg) & ~(mask)) | (val))

// Extract bitfield
#define GET_BITS(reg, mask, pos) (((reg) & (mask)) >> (pos))
```

> ⚠️ **Warning**
> If the macro parameters contain expressions with side effects (like `i++`), they will be evaluated multiple times. In production code, `inline` functions are preferred, but macro versions are so prevalent in embedded codebases that you need to be able to read them.

Let's see how these macros configure a hypothetical GPIO port. Assume the GPIOA base address is `0x40020000`, offset `0x00` is the mode register `MODER`, and every 2 bits control one pin:

```c
#define GPIOA_BASE 0x40020000
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))

// Set Pin 5 to Output mode (bits 10:11 = 01)
MODIFY_REG(GPIOA_MODER, (0x3 << 10), (0x1 << 10));
```

Note that `volatile` cast—it tells the compiler: the value at this address can change at any time (by hardware), so every read and write must actually access memory; do not cache or optimize it away.

### Structure Mapping: Giving Registers Names

Using address offsets and bit manipulation directly works, but readability is poor—who can instantly recognize that `*(uint32_t*)0x40020014` is the GPIOA output data register? Structure mapping is a more elegant solution:

```c
typedef struct {
    volatile uint32_t MODER;    // Mode register
    volatile uint32_t OTYPER;   // Output type register
    volatile uint32_t OSPEEDR;  // Output speed register
    volatile uint32_t PUPDR;    // Pull-up/pull-down register
    volatile uint32_t IDR;      // Input data register
    volatile uint32_t ODR;      // Output data register
    uint32_t RESERVED;          // Padding
    volatile uint32_t BSRR;     // Bit set/reset register
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *) 0x40020000)

// Usage
GPIOA->MODER = (GPIOA->MODER & ~(0x3 << 10)) | (0x1 << 10);
GPIOA->BSRR = (1 << 5); // Set Pin 5
```

Now the configuration code becomes very clear: `GPIOA->MODER = ...`.

Structure mapping has an implicit premise: the memory layout must match the hardware register layout exactly. Most ARM peripheral registers are aligned 32-bit, matching the natural alignment of `uint32_t`. If there are reserved spaces between registers, we must add `uint32_t RESERVED` placeholders in the struct—this is exactly how Cortex-M CMSIS headers work.

### Atomic Access: The BSRR Pattern

The previous pin configuration used a "read-modify-write" sequence. This is fine when there are no interrupts, but if an interrupt occurs between the "read" and the "write", and the ISR modifies the same register, your "write" will overwrite the interrupt's modification. This is the classic read-modify-write race condition.

Some peripherals provide atomic operation registers to solve this. The STM32 GPIO `BSRR` is a classic example—writing 1 to the lower 16 bits sets the corresponding pin, writing 1 to the upper 16 bits clears it, and writing 0 does nothing. Just write directly; the hardware guarantees atomicity:

```c
// Set Pin 5 (atomic operation)
GPIOA->BSRR = (1 << 5);

// Reset Pin 5 (atomic operation)
GPIOA->BSRR = (1 << (16 + 5));
```

If the hardware lacks such atomic registers, we must rely on disabling interrupts to protect critical sections.

## Step 2 — Understanding What `volatile` Does and Doesn't Do

`volatile` is likely the most misunderstood keyword in embedded C.

### What `volatile` Does

`volatile` tells the compiler: every access to this object must actually be executed; it cannot be optimized away, nor can it be reordered across other `volatile` accesses. Specifically: the compiler will not cache the value of a `volatile` variable in a register, will not optimize seemingly "redundant" reads/writes, and will not reorder the sequence of `volatile` operations.

```c
volatile uint32_t *flag = (volatile uint32_t *)0x40000000;

// The compiler will perform exactly 10 memory reads
for (int i = 0; i < 10; i++) {
    if (*flag) break;
}
```

### What `volatile` Doesn't Do (More Important)

`volatile` is **not** a thread synchronization tool. It **does not guarantee** atomicity, and it **does not prevent** CPU out-of-order execution. `volatile` constrains the compiler, not the CPU—ARM Cortex-M can reorder normal memory accesses, so two `volatile` writes might appear ordered to the compiler, but the CPU might commit them to the bus in a different order. If strict memory ordering is required, memory barrier instructions like DMB/DSB must be used.

Additionally, `volatile` does not guarantee the atomicity of read-modify-write operations:

```c
volatile int counter = 0;

// Interrupt-safe? NO.
void increment_counter(void) {
    counter++; // Actually three steps: read, add 1, write back
}
```

`counter++` is actually a "read, add 1, write back" sequence. If an interrupt modifies `counter` between the read and the write, an update is lost.

> ⚠️ **Warning**
> **Reasonable use cases for `volatile`**: Hardware register mapping, simple flags shared between interrupts and the main loop. **Scenarios where `volatile` should NOT be used**: Thread synchronization (use mutex or atomic), bulk data transfer (use DMA), any situation requiring atomic read-modify-write.

## Step 3 — Mastering Interrupt-Safe Programming

Interrupts are the core mechanism of embedded systems—hardware events break the current execution flow and jump to the ISR. The problem is that the ISR and the main loop share the same memory space; if both access the same data simultaneously, it can lead to data corruption or system crashes.

### Critical Section Protection

The simplest but effective method: disable interrupts before accessing shared data, and re-enable them after. Here, a nested counter is used to support nested critical sections:

```c
static uint32_t irq_lock_state = 0;

void enter_critical_section(void) {
    // Disable interrupts and save previous state
    irq_lock_state = __get_PRIMASK();
    __disable_irq();
}

void exit_critical_section(void) {
    // Restore previous state
    if (!(irq_lock_state & 1)) {
        __enable_irq();
    }
}

// Usage
void update_shared_data(int value) {
    enter_critical_section();
    shared_buffer[index++] = value;
    exit_critical_section();
}
```

> ⚠️ **Warning**
> Disabling interrupts has a cost: all interrupts are masked during the critical section, degrading system real-time performance. Critical sections must be kept as short as possible—get in, do the necessary work, and get out immediately. Never call blocking functions or perform complex calculations inside a critical section.

### Ring Buffer: The Classic Interrupt-Safe Data Structure

The most common communication pattern between interrupts and the main loop is "producer-consumer"—the interrupt writes data, and the main loop reads it. The ring buffer is the standard implementation. The beauty is that as long as "writing" and "reading" are each executed in only one context, no locks are needed:

```c
#define BUFFER_SIZE 16

typedef struct {
    volatile uint16_t head; // Modified only by writer
    volatile uint16_t tail; // Modified only by reader
    uint8_t data[BUFFER_SIZE];
} ring_buffer_t;

bool ring_push(ring_buffer_t *buf, uint8_t byte) {
    uint16_t next_head = (buf->head + 1) % BUFFER_SIZE;
    if (next_head == buf->tail) return false; // Full
    buf->data[buf->head] = byte;
    buf->head = next_head;
    return true;
}

bool ring_pop(ring_buffer_t *buf, uint8_t *byte) {
    if (buf->tail == buf->head) return false; // Empty
    *byte = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % BUFFER_SIZE;
    return true;
}
```

The key constraint is: `head` is modified only by the writer, and `tail` is modified only by the reader. Since both sides only read the other's pointer and write their own, no mutex is required.

### Golden Rule of Interrupt Handling

For simple "event occurred" notifications, a simple `volatile bool` flag is sufficient:

```c
volatile bool data_ready = false;

void UART_IRQHandler(void) {
    if (UART->ISR & UART_FLAG_RXNE) {
        rx_byte = UART->RDR;
        data_ready = true; // Set flag for main loop
        UART->ICR = UART_FLAG_RXNE; // Clear interrupt
    }
}

int main(void) {
    while (1) {
        if (data_ready) {
            process_byte(rx_byte);
            data_ready = false;
        }
        // Other tasks...
    }
}
```

The ISR does the bare minimum—clear the interrupt flag, set the application-level flag. This is the golden rule of interrupt handling: **Keep the ISR as short as possible, leave the heavy lifting to the main loop.**

## Step 4 — Designing a Layered Peripheral Abstraction Layer

If an embedded project manipulates register addresses directly in business logic, the code becomes an unmaintainable, unportable mess. The solution is to introduce a Peripheral Abstraction Layer (PAL) to encapsulate hardware details in low-level drivers.

### Three-Layer Architecture

A reasonable layering usually looks like this: the bottom layer is register definitions and bit manipulation utilities (chip-specific), the middle layer is peripheral drivers (GPIO, UART, SPI, etc.), and the top layer is application logic (completely register-agnostic). The middle layer interface should be chip-independent:

```c
// hal_gpio.h (Hardware Abstraction Layer)
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} gpio_pin_t;

void gpio_init(gpio_pin_t *pin, uint32_t mode, uint32_t pull);
void gpio_write(gpio_pin_t *pin, int value);
int gpio_read(gpio_pin_t *pin);
```

```c
// hal_gpio.c (Implementation)
void gpio_write(gpio_pin_t *pin, int value) {
    if (value) {
        pin->port->BSRR = (1 << pin->pin); // Set
    } else {
        pin->port->BSRR = (1 << (16 + pin->pin)); // Reset
    }
}
```

The upper application touches no registers at all:

```c
// main.c
gpio_pin_t led = {GPIOA, 5};
gpio_init(&led, GPIO_MODE_OUTPUT, GPIO_NOPULL);

while (1) {
    gpio_write(&led, 1);
    delay_ms(500);
    gpio_write(&led, 0);
    delay_ms(500);
}
```

When switching chips, only the bottom layer register definitions and middle layer implementation need to change; the upper application code remains untouched. The `gpio_pin_t` structure bundles "which port and which pin" into a passable object, which is much clearer than passing raw `GPIOA, 5` parameters everywhere.

## Step 5 — Understanding the Bare-Metal Startup Process

Without an operating system, even `main` isn't the first thing executed. Understanding the complete flow from power-on to `main` is a fundamental skill.

### Startup Code

The ARM Cortex-M flow after power-on: The CPU reads the initial stack pointer (first 32-bit word) and reset vector (second 32-bit word, i.e., `Reset_Handler` address) from the vector table, then jumps to `Reset_Handler`. `Reset_Handler` does three things: copy the `.data` section from Flash to SRAM, zero out the `.bss` section, and call `SystemInit`.

```c
// startup.c
void Reset_Handler(void) {
    // Copy .data section (Flash -> RAM)
    uint32_t *src = &_sidata; // Load address
    uint32_t *dst = &_sdata;  // Run address
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero .bss section
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // System initialization (clock config, etc.)
    SystemInit();

    // Jump to main
    main();

    // If main returns, trap here
    while (1) {}
}
```

> ⚠️ **Warning**
> Symbols like `_sidata`, `_sdata` are not real variables—they are address labels defined in the linker script. Declaring them with `extern` in C code allows taking their address to get the start/end positions of sections. The vector table is forced to the beginning of Flash using `__attribute__((section(".isr_vector")))`, and `__weak` allows users to override default interrupt handlers.

### Linker Script

The linker script tells the linker the memory layout of the program—where Flash starts and ends, where SRAM starts and ends, and where each section goes. The key concept is **LMA (Load Memory Address) vs VMA (Virtual Memory Address)**—the `.data` section's run address (VMA) is in RAM, but its load address (LMA) is in Flash. After power-on, the startup code copies it to RAM. The `.bss` section only has start and end addresses; the startup code zeroes it directly.

```ld
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

SECTIONS {
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
```

## C++ Transition

Embedded C++ has several important constraints: exceptions require stack unwinding runtime support, which most bare-metal projects disable with `-fno-exceptions`, preferring return values for errors; RTTI (`dynamic_cast`/`typeid`) increases code size and is usually disabled with `-fno-rtti`; bare-metal lacks an OS heap manager, so `new`/`delete` are unavailable by default—static allocation (fixed-size containers and memory pools replacing dynamic allocation) is recommended.

C++ improvements for embedded code focus on three areas:

| C Pattern | C++ Improvement |
|--------|----------|
| Manually ensuring init/cleanup pairing | RAII automatic management via constructors/destructors |
| Macros for bit manipulation | `constexpr` compile-time calculation of configuration values |
| Runtime register config table lookup | Templates固化 port/pin constants at compile-time, generating code as efficient as hand-written |
| Function pointers + `void*` context | Lambdas or template callbacks |

`constexpr` is particularly valuable in the embedded field—calculating register configuration values at compile-time allows writing pre-calculated constants directly at runtime, eliminating both runtime calculation overhead and the possibility of runtime errors. Later in this series, when diving deep into C++ embedded applications, we will detail how `constexpr` + templates implement zero-overhead hardware abstraction layers.

## Common Pitfalls Cheat Sheet

| Pitfall | Description | Solution |
|------|------|----------|
| Using `volatile` for thread sync | `volatile` doesn't guarantee atomicity or memory order | Use atomic operations or disable interrupts |
| Forgetting padding in struct mapping | Compiler padding doesn't match hardware layout | Check the manual and add `reserved` fields |
| Doing too much in ISR | Interrupt latency increases, system response slows | ISR only sets flags; heavy work in main loop |
| Read-modify-write race | Interrupt modifies the same register between read and write | Use atomic registers (BSRR) or disable interrupts |
| Returning from `main` | Bare-metal `main` has no OS to catch the return | Add an infinite loop after `main` in startup code |

## Exercises

### Exercise 1: Generic Ring Buffer

Refactor the `uint8_t` ring buffer from the text into a generic version (using `void*` + element size):

```c
typedef struct {
    volatile uint16_t head;
    volatile uint16_t tail;
    uint8_t *buffer;       // External buffer pointer
    uint16_t capacity;     // Max elements
    uint16_t elem_size;    // Size of each element
} generic_ring_t;

bool ring_push(generic_ring_t *ring, const void *data);
bool ring_pop(generic_ring_t *ring, void *data);
```

**Hint**: Use `memcpy` internally for generic byte copying. Change `head`/`tail` to absolute counts (don't worry about overflow), and calculate the actual index via `count % capacity`.

### Exercise 2: Portable UART Abstraction Layer

Design a chip-independent abstraction layer interface for a UART peripheral. The driver needs two ring buffers (TX and RX). The application writes to the buffer first and then triggers the transmit interrupt; actual byte-by-byte transmission is completed in the ISR.

```c
// uart.h
void uart_init(uint32_t baudrate);
void uart_send_byte(uint8_t data);
bool uart_receive_byte(uint8_t *data);
void UART_IRQHandler(void);
```

### Exercise 3: Linker Script and Startup Code

Write a minimal linker script and startup code for an ARM Cortex-M4 (256K Flash, 64K SRAM). Requirements: define correct MEMORY regions, place the vector table at the start of Flash, handle `.data` section address separation, zero out `.bss`, and add a safe infinite loop after `main`.

## Reference Resources

- [ARM Cortex-M Programming Guide](https://developer.arm.com/documentation)
- [volatile keyword - cppreference](https://en.cppreference.com/w/c/language/volatile)
- [GCC Linker Script Reference](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [CMSIS - Cortex Microcontroller Software Interface Standard](https://arm-software.github.io/CMSIS_5/General/html/index.html)
