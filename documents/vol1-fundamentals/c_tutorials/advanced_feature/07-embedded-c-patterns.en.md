---
title: Embedded C Programming Patterns
description: Register access patterns, proper use of `volatile`, interrupt-safe programming,
  peripheral abstraction layer design, and bare-metal development patterns
chapter: 1
order: 107
tags:
- host
- cpp-modern
- intermediate
- 嵌入式
- 单片机
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 17
prerequisites:
- 结构体、联合体与内存对齐
- 函数指针与回调机制
- 指针进阶：多级指针、指针与 const
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/07-embedded-c-patterns.md
  source_hash: d23fd63dcf1a382c8a1352f74951dc1f58b192dea48a730abb073d711812adf2
  translated_at: '2026-04-20T03:56:03.831432+00:00'
  engine: anthropic
  token_count: 3384
---
# Embedded C Programming Patterns

When writing desktop applications, we rarely worry about the compiler silently optimizing away a memory read, or two pieces of code trampling the same data at the same time. But once we turn our attention to bare-metal—no operating system, no standard library, not even a standard entry point—these problems all surface. Embedded C programming has its own pattern language: registers are mapped using structs, hardware state must be protected with `volatile`, and data exchange between interrupts and the main loop requires carefully designed synchronization mechanisms.

In this tutorial, we break down these patterns one by one. Understanding them is a necessary prerequisite for learning embedded C++ later in this series—`constexpr` register configuration, zero-overhead abstraction, and type-safe hardware access.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Master three register access patterns (bit manipulation, struct mapping, atomic access)
> - [ ] Correctly use the `volatile` qualifier and understand its semantic boundaries
> - [ ] Implement interrupt-safe data exchange patterns
> - [ ] Design a layered peripheral abstraction layer
> - [ ] Understand the startup flow and linker script of a bare-metal program

## Environment Notes

The code in this article targets ARM Cortex-M, but all concepts and patterns apply equally to other architectures. On the host machine, we can verify compilation using a cross-compiler:

```text
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -c -o led.o led.c
```

## Step 1 — Figuring Out How to Interact with Hardware Registers

The most fundamental operation in embedded development is reading and writing hardware registers—those peripheral control ports mapped into the memory address space. Let's look at three access patterns, ranging from raw to elegant.

### Bit Manipulation: The Most Primitive Yet Most Flexible

Every bit in a peripheral register has an independent meaning. For example, in a GPIO port mode register, the lower two bits might control the mode (input/output/alternate/analog), and the next two bits control the pull-up/pull-down. First, let's define a set of generic bit manipulation macros; almost every embedded project has a similar utility header file:

```c
#define BIT(n)              (1U << (n))
#define SET_BIT(reg, n)     ((reg) |= BIT(n))
#define CLR_BIT(reg, n)     ((reg) &= ~BIT(n))
#define READ_BIT(reg, n)    (((reg) >> (n)) & 1U)
#define MODIFY_REG(reg, msk, val) ((reg) = ((reg) & ~(msk)) | (val))
```

> ⚠️ **Watch Out**
> If the `reg` parameter in a macro is an expression with side effects (like `*ptr++`), it will be evaluated multiple times. In production code, we recommend using `inline` functions instead, but the macro version is so widespread in embedded codebases that you need to be able to read it.

Let's see how these macros configure a hypothetical GPIO port. Suppose the GPIOA base address is `0x40020000`, offset `0x00` is the mode register `MODER`, and every two bits control one pin:

```c
#define GPIOA_BASE    0x40020000U
#define GPIOA_MODER   (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))

// Set pin 5 to general-purpose output mode (01)
MODIFY_REG(GPIOA_MODER, 3U << 10, 1U << 10);
```

Note that `volatile` cast—`volatile` tells the compiler: the value at this address can change at any time due to hardware, so every read and write must actually access memory and cannot be cached or optimized away.

### Struct Mapping: Giving Registers Names

Using raw address offsets and bit manipulation gets the job done, but readability is poor—who can tell at a glance that `*(uint32_t *)0x40020014` is the GPIOA output data register? Struct mapping is a more elegant solution:

```c
typedef struct {
    volatile uint32_t MODER;    // Mode register,        offset 0x00
    volatile uint32_t OTYPER;   // Output type register,  offset 0x04
    volatile uint32_t OSPEEDR;  // Output speed register, offset 0x08
    volatile uint32_t PUPDR;    // Pull-up/pull-down reg, offset 0x0C
    volatile uint32_t IDR;      // Input data register,   offset 0x10
    volatile uint32_t ODR;      // Output data register,  offset 0x14
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)0x40020000U)
```

Now the configuration code becomes very clear: `GPIOA->MODER |= (1U << 10);`.

Struct mapping has an implicit prerequisite: the memory layout must match the hardware register layout exactly. Most ARM peripheral registers are 32-bit aligned, which perfectly matches the natural alignment of `uint32_t`. If there are reserved spaces between registers, we must add `uint32_t` padding placeholders in the struct—this is exactly how Cortex-M CMSIS header files do it.

### Atomic Access: The BSRR Pattern

Earlier, configuring a pin used a "read-modify-write" three-step process. This is fine when there is no interrupt interference, but if an interrupt arrives between the "read" and "write" steps and also modifies the same register—your "write" will overwrite the interrupt's changes. This is the classic read-modify-write race condition.

Some peripherals provide atomic operation registers to solve this problem. The STM32 GPIO BSRR is a typical example—writing 1 to the lower 16 bits sets the corresponding pin, writing 1 to the upper 16 bits clears it, and writing 0 has no effect. Just write to it directly, and the hardware guarantees atomicity:

```c
// Atomically set pin 5, clear pin 3
GPIOA->BSRR = (1U << 5) | (1U << (16 + 3));
```

If the hardware lacks such atomic operation registers, we can only rely on disabling interrupts to protect the critical section.

## Step 2 — Understanding What volatile Actually Does and Doesn't Do

`volatile` is probably the most misunderstood keyword in embedded C.

### What volatile Does

`volatile` tells the compiler: every access to this object must actually be executed, cannot be optimized away, and cannot be reordered across other `volatile` accesses. Specifically: the compiler will not cache the value of a `volatile` variable in a register, will not optimize away seemingly "redundant" reads and writes, and will not reorder the sequence of `volatile` operations.

```c
volatile uint32_t *flag = (volatile uint32_t *)0x40021000U;

// The compiler MUST emit both writes
*flag = 0x01;
*flag = 0x02;  // Not optimized away!

// The compiler MUST read from memory each time
uint32_t a = *flag;
uint32_t b = *flag;  // Two separate memory reads
```

### What volatile Doesn't Do (This Is More Important)

`volatile` is **not** a thread synchronization tool. It **does not guarantee** atomicity, and it **does not prevent** CPU out-of-order execution. `volatile` only constrains the compiler, not the CPU—ARM Cortex-M can reorder normal memory accesses, so two `volatile` writes appear ordered to the compiler, but the CPU might commit them to the bus in a different order. If strict memory ordering is required, we must use memory barrier instructions like DMB/DSB.

Additionally, `volatile` does not guarantee the atomicity of read-modify-write operations:

```c
volatile uint32_t counter = 0;

void ISR_Handler(void) {
    counter++;  // NOT atomic!
}
```

`counter++` is actually a three-step operation: read, add 1, write back. If an interrupt occurs between the read and write, and the interrupt also modifies `counter`, an update will be lost.

> ⚠️ **Watch Out**
> Reasonable use cases for `volatile`: hardware register mapping, simple flags shared between interrupts and the main loop. Scenarios where we should not use `volatile`: inter-thread synchronization (use mutex or atomic), large data transfers (use DMA), any situation requiring atomic read-modify-write.

## Step 3 — Mastering Interrupt-Safe Programming

Interrupts are the core mechanism of embedded systems—when a hardware event arrives, it breaks the current execution flow and jumps to the ISR to handle it. The problem is that the ISR and the main loop share the same memory space. If both access the same data simultaneously, the best case is data corruption, and the worst case is the system flying off the rails.

### Critical Section Protection

The simplest and most brute-force but effective method: disable interrupts before accessing shared data, and re-enable them after the operation is complete. Here we use a nesting counter to support nested critical sections:

```c
static uint32_t critical_nesting = 0;

static inline void enter_critical(void) {
    __disable_irq();
    critical_nesting++;
}

static inline void exit_critical(void) {
    if (critical_nesting > 0) {
        critical_nesting--;
        if (critical_nesting == 0) {
            __enable_irq();
        }
    }
}
```

> ⚠️ **Watch Out**
> Disabling interrupts has a cost: while interrupts are disabled, all interrupts are masked, and system real-time performance degrades. Critical sections must be as short as possible—get in, do the necessary operations, and get out immediately. Never call blocking functions or perform complex calculations inside a critical section.

### Ring Buffer: The Classic Interrupt-Safe Data Structure

The most common communication pattern between interrupts and the main loop is "producer-consumer"—the interrupt writes data in, and the main loop reads data out. The ring buffer is the standard implementation, and its beauty lies in the fact that as long as "writing" and "reading" each execute in only one context, no locks are needed:

```c
#define RING_BUF_SIZE 128

typedef struct {
    uint8_t  buffer[RING_BUF_SIZE];
    volatile uint16_t head;  // Write position (modified only by producer)
    volatile uint16_t tail;  // Read position  (modified only by consumer)
} RingBuffer;

bool ring_put(RingBuffer *rb, uint8_t data) {
    uint16_t next = (rb->head + 1) % RING_BUF_SIZE;
    if (next == rb->tail) return false;  // Full
    rb->buffer[rb->head] = data;
    rb->head = next;
    return true;
}

bool ring_get(RingBuffer *rb, uint8_t *data) {
    if (rb->head == rb->tail) return false;  // Empty
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUF_SIZE;
    return true;
}
```

The key constraint is: `head` is modified only by the writer, and `tail` is modified only by the reader. Because both sides only read the other's pointer and only write their own pointer, no mutex is needed.

### The Golden Rule of Interrupt Handling

For simple "event occurred" notifications, a single `volatile` flag bit is sufficient:

```c
volatile bool uart_rx_ready = false;

void USART1_IRQHandler(void) {
    if (USART1->ISR & USART_ISR_RXNE) {
        uart_rx_ready = true;
        USART1->ICR = USART_ICR_RXNECF;  // Clear interrupt flag
    }
}

int main(void) {
    // ...
    while (1) {
        if (uart_rx_ready) {
            uart_rx_ready = false;
            process_rx_data();
        }
    }
}
```

The ISR does the absolute minimum—clear the interrupt flag and set the application-layer flag. This is the golden rule of interrupt handling: **keep the ISR as short as possible, and leave the heavy lifting to the main loop**.

## Step 4 — Designing a Layered Peripheral Abstraction Layer

If an embedded project directly manipulates register addresses in business logic, the code becomes an unmaintainable, unportable plate of spaghetti. The solution is to introduce a peripheral abstraction layer (PAL), encapsulating hardware details in low-level drivers.

### Three-Layer Architecture

A reasonable layering usually looks like this: the bottom layer is register definitions and bit manipulation utilities (tied to a specific chip), the middle layer is peripheral drivers (GPIO, UART, SPI, and other modules), and the top layer is application logic (completely register-free). The middle layer's interface design should be chip-agnostic:

```c
// --- Middle layer: chip-agnostic interface ---
typedef struct {
    uint8_t port;
    uint8_t pin;
} GpioPin;

void gpio_set_output(const GpioPin *pin);
void gpio_set_high(const GpioPin *pin);
void gpio_set_low(const GpioPin *pin);
```

```c
// --- Bottom layer: chip-specific implementation ---
#define PORT_A 0
#define PORT_B 1

static GPIO_TypeDef *get_port_base(uint8_t port) {
    switch (port) {
        case PORT_A: return GPIOA;
        case PORT_B: return GPIOB;
        default:     return NULL;
    }
}

void gpio_set_high(const GpioPin *pin) {
    GPIO_TypeDef *base = get_port_base(pin->port);
    if (base) base->BSRR = (1U << pin->pin);
}
```

The upper application layer never touches registers:

```c
static const GpioPin led = {PORT_A, 5};

int main(void) {
    gpio_set_output(&led);
    while (1) {
        gpio_set_high(&led);
        delay_ms(500);
        gpio_set_low(&led);
        delay_ms(500);
    }
}
```

When switching chips, we only need to change the bottom-layer register definitions and the middle-layer implementation; the upper application code remains completely untouched. The `GpioPin` struct packages "which pin on which port" into a passable object, which is much clearer than passing `port, pin` bare parameters everywhere.

## Step 5 — Understanding the Bare-Metal Program Startup Flow

Without an operating system, even `main` is not the first thing executed. Understanding the complete flow of a bare-metal program from power-on to entering `main` is fundamental knowledge.

### Startup Code

The flow after ARM Cortex-M powers on: the CPU reads the initial stack pointer (the first 32-bit word) and the reset vector (the second 32-bit word, i.e., the Reset_Handler address) from the vector table, then jumps to Reset_Handler. Reset_Handler does three things: copy the `.data` section from Flash to SRAM, zero out the `.bss` section, and call `main`.

```c
typedef void (*handler_t)(void);

extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

void Reset_Handler(void) {
    // 1. Copy .data from Flash to SRAM
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // 2. Zero out .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // 3. Call main
    main();

    // 4. If main returns, loop forever
    while (1);
}

__attribute__((section(".isr_vector")))
const handler_t vector_table[] = {
    (handler_t)&_estack,   // Initial stack pointer
    Reset_Handler,         // Reset handler
    // ... other interrupt handlers
};
```

> ⚠️ **Watch Out**
> Symbols like `_sidata`, `_sdata` are not real variables—they are address labels defined in the linker script. After declaring them with `extern` in C code, taking their address yields the start and end positions of the corresponding sections. The vector table uses `__attribute__((section(".isr_vector")))` to be forcibly placed at the beginning of Flash, and `__attribute__((weak))` allows users to override default interrupt handlers.

### Linker Script

The linker script tells the linker about the program's memory layout—where Flash starts and ends, where SRAM starts and ends, and where each section goes. The key concept is `AT>`—the run address of the `.data` section is in RAM, but its load address is in Flash. After power-on, the startup code copies it to RAM. The `.bss` section only has start and end addresses, and the startup code zeros it out directly.

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    SRAM  (rw) : ORIGIN = 0x20000000, LENGTH = 64K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    _sidata = LOADADDR(.data);
    .data :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > SRAM AT> FLASH

    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > SRAM
}
```

## Transitioning to C++

Embedded C++ has several important constraints: exceptions require stack unwinding runtime support, which most bare-metal projects disable with `-fno-exceptions`, using return values to indicate errors instead; RTTI (`dynamic_cast`/`typeid`) increases code size and is usually disabled with `-fno-rtti`; bare-metal has no OS heap manager, so `new`/`delete` are unavailable by default, and we recommend fully static allocation (fixed-size containers and memory pools instead of dynamic allocation, `std::array` instead of `std::vector`).

C++ improvements to embedded code mainly focus on three areas:

| C Pattern | C++ Improvement |
|-----------|-----------------|
| Manually ensuring init/cleanup pairing | RAII constructors/destructors manage automatically |
| Macros for bit manipulation | `constexpr` computes configuration values at compile time |
| Runtime register configuration table lookups | Templates hardcode port/pin constants at compile time, generating code as efficient as hand-written |
| Function pointers + `void*` context | Lambda expressions or template callbacks |

`constexpr` is particularly valuable in the embedded domain—computing register configuration values at compile time and writing precomputed constants directly at runtime eliminates runtime computation overhead and avoids the possibility of runtime errors. Later in this series, when we dive deep into embedded C++ applications, we will detail how `constexpr` + templates achieve a zero-overhead hardware abstraction layer.

## Common Pitfalls Quick Reference

| Pitfall | Description | Solution |
|---------|-------------|----------|
| Using `volatile` for thread synchronization | `volatile` does not guarantee atomicity or memory ordering | Use atomic operations or disable interrupts for protection |
| Forgetting padding in struct mapping | Compiler padding does not match hardware layout | Check the manual and add `uint32_t` reserved fields |
| Doing too much in the ISR | Interrupt latency increases, system response slows | ISR only sets flags, heavy work handled in the main loop |
| Read-modify-write race condition | Interrupt modifies the same register between read and write | Use atomic operation registers (BSRR) or disable interrupts |
| `main` returning | Bare-metal `main` has no OS to take over after it returns | Add an infinite loop after `main` in the startup code |

## Exercises

### Exercise 1: Generic Ring Buffer

Refactor the `uint8_t` ring buffer from this article into a generic version (implemented using `void*` + element size):

```c
typedef struct {
    uint8_t  *buffer;
    uint16_t capacity;
    uint16_t elem_size;
    volatile uint32_t head;
    volatile uint32_t tail;
} GenericRingBuf;

bool gring_put(GenericRingBuf *rb, const void *elem);
bool gring_get(GenericRingBuf *rb, void *elem);
```

Hint: use `memcpy` internally for generic byte copying, change `head`/`tail` to absolute counts (`uint32_t` won't overflow), and calculate the actual index via the modulo operator.

### Exercise 2: Portable UART Abstraction Layer

Design a chip-agnostic abstraction layer interface for the UART peripheral. The driver internally needs two ring buffers (transmit and receive). The send function first writes to the buffer and then triggers the transmit interrupt; the actual byte-by-byte transmission is completed in the ISR.

```c
typedef struct {
    // Chip-specific register base, ring buffers, etc.
} UartDriver;

void uart_init(UartDriver *drv, uint32_t baudrate);
void uart_send(UartDriver *drv, uint8_t byte);
bool uart_recv(UartDriver *drv, uint8_t *byte);
void USART1_IRQHandler(void);  // Calls driver's internal ISR logic
```

### Exercise 3: Linker Script and Startup Code

Write a minimal linker script and startup code for an ARM Cortex-M4 (256K Flash, 64K SRAM). Requirements: define the correct MEMORY regions, place the vector table at the beginning of Flash, handle the `.data` section address separation, zero out `.bss`, and add a safe infinite loop after `main`.

## References

- [ARM Cortex-M Programming Guide](https://developer.arm.com/documentation)
- [volatile keyword - cppreference](https://en.cppreference.com/w/c/language/volatile)
- [GCC Linker Script Reference](https://sourceware.org/binutils/docs/ld/Scripts.html)
- [CMSIS - Cortex Microcontroller Software Interface Standard](https://arm-software.github.io/CMSIS_5/General/html/index.html)
