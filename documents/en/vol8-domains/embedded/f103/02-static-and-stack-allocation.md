---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Use static storage and stack allocation
difficulty: intermediate
order: 2
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 5
tags:
- cpp-modern
- intermediate
- stm32f1
title: Static Storage and Stack Allocation Strategies
translation:
  source: documents/vol8-domains/embedded/02-static-and-stack-allocation.md
  source_hash: 0cba8954f9980ced0b2829a9f9be01961440017c445994a7b66a87e2192e41c7
  translated_at: '2026-06-16T04:11:26.635328+00:00'
  engine: anthropic
  token_count: 920
---
# Embedded C++ Tutorial — Static Storage and Stack Allocation Strategies

> I caught a cold recently and took a long break to recover...

In embedded systems, memory resources are scarce and unevenly distributed (Flash, SRAM, specialized high-speed SRAM, etc.). Deciding whether to place data in the **static area** (global, static variables, constants) or on the **stack** (function local variables, temporary objects) directly impacts program reliability, startup time, code maintainability, and real-time performance. This blog post covers concepts, implementation details, common pitfalls, and practical engineering strategies with example code.

------

## What are Static Storage and Stack Allocation? (Quick Definitions)

**Static storage**: Memory allocated at compile/link time, including `.text` (code + rodata), `.data` (initialized global/static variables, copied to RAM at runtime), and `.bss` (uninitialized global/static variables, zeroed at runtime). These variables exist for the entire lifetime of the program or until explicitly changed.

**Stack allocation**: Memory allocated by the stack pointer during function calls, used for local variables, return addresses, and register saving. The stack space is released when the function returns.

------

## Why Be Careful in Embedded Systems?

- **Predictability**: Static storage size is visible at link time; stack growth depends on the execution path, making it hard to statically guarantee that no overflow will occur.
- **Real-time performance**: Dynamic allocation or large stack frames can cause unpredictable latency. Stack usage within interrupt contexts requires special attention.
- **Memory layout**: ROM/Flash and different grades of SRAM (on-chip vs. external) differ significantly in speed and capacity. Static data can be placed in appropriate regions (e.g., putting large read-only tables in Flash).
- **Reentrancy and thread safety**: Global/static variables are not thread-safe by default; in an RTOS environment, extra synchronization is required. Stack data is inherently thread-safe for the current thread (each thread has its own stack).

------

## So, What Uses Static Storage?

- **Read-only constants (`const`)**: In common ARM/GCC environments, these are placed in the `.rodata` section of Flash and do not consume RAM at runtime (unless forced to copy). Using `constexpr` for lookup tables, firmware version strings, etc., is a great way to save RAM.
- **Initialized static variables (`.data`)**: The compiler generates initialization data in Flash, which is copied to RAM at startup, thus consuming RAM.
- **Uninitialized static variables (`.bss`)**: These are zeroed at startup, consume RAM, but do not occupy large chunks of initialization data in Flash.
- **Placement control**: You can use linker scripts and attributes to control data placement into specific sections (such as fast SRAM, uninitialized sections `.noinit`, etc.).
- **Issues to avoid**:
  - Making large arrays or buffers static permanently occupies memory. If not planned correctly, this wastes memory or leads to shortages.
  - Static mutable variables must account for concurrent access (interrupts, threads) using `volatile`, mutexes, atomic operations, etc.

Example: Placing a large lookup table in Flash

```cpp
// Placed in .rodata/Flash by default
constexpr int SineTable[360] = { /* ... */ };
```

If you need to explicitly place it in a specific section (like Flash):

```cpp
__attribute__((section(".my_flash_section"))) const int BigTable[1024] = { /* ... */ };
```

------

## Linker Script Example

In embedded projects, we usually modify the linker script to place sections in appropriate memory regions.

```ld
MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 512K
  RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 128K
  FAST_RAM (rwx)  : ORIGIN = 0x20020000, LENGTH = 32K
}

SECTIONS
{
  .text : { *(.text*) } > FLASH

  /* Place critical data in fast RAM */
  .fastdata : { *(.fastdata*) } > FAST_RAM AT > FLASH

  .data : { *(.data*) } > RAM AT > FLASH
  .bss : { *(.bss*) } > RAM
}
```

This is very common in U-Boot, where `__attribute__((section(".fastdata")))` is used in code to place performance-sensitive data in FAST_RAM.

------

## Risks and Usage of Stack Allocation

- **Large local variables can easily trigger stack overflow**. For example:

```cpp
void riskyFunction() {
    // Danger: 10KB on the stack!
    uint8_t buffer[10240];
    // ...
}
```

- **Recursion**: Most embedded systems should avoid recursion (difficult to estimate maximum depth).
- **Variable Length Arrays (VLA) / `alloca`**: Features that change stack usage at runtime are extremely risky in embedded systems; try to disable or use them with caution.
- **Temporary objects inside functions**: Small objects should be prioritized for the stack; large objects should be static or on the heap (if allowed).

Alternative approach: Make large buffers static or put them in task-specific memory pools.

------

## C++ Specifics (Construction, Destruction, Placement New)

- **Static object construction order**: The construction order of global static objects across different files is not guaranteed (the "Static Initialization Order Fiasco"). During the embedded startup phase, try to explicitly write critical initialization in `main()` or init functions.
- **Placement new**: You can explicitly construct objects on static/stack/specific memory regions (often used in heap-less systems):

```cpp
// Static buffer
alignas(std::string) unsigned char buffer[sizeof(std::string)];

void demo() {
    // Construct object in place
    std::string* str = new (buffer) std::string("Hello World");

    str->append("!");

    // Must manually call destructor
    str->~string();
}
```

This is very useful in scenarios without `malloc`, but you must manage the object lifecycle carefully.

------

## Strategies Without `malloc` (Required by Many Embedded Projects)

- Use **fixed-size object pools or ring buffers** to replace the heap.
- Implement type-safe allocation interfaces via templates or handwritten pools.
- Prioritize static allocation for all long-lived buffers (like network packet buffers) and place them in appropriate sections.

Simple ring buffer (illustrative):

```cpp
template<typename T, size_t N>
class RingBuffer {
    T buffer[N] = {}; // Static storage, stack-like usage
    size_t head = 0;
    size_t tail = 0;

public:
    bool push(const T& item) { /* ... */ }
    bool pop(T& item) { /* ... */ }
};
```

## Conclusion

In embedded C++ development, **static storage provides predictability and controllable long-term memory usage**, while the **stack provides locality and thread isolation**. When choosing, consider: buffer size, access patterns (concurrency/interrupts), performance (speed/access latency), and testability (stack usage is measurable). In practice, prioritize placing large objects, lookup tables, and DMA buffers in static regions or dedicated RAM; place short-lived, temporary objects on the stack; strictly control dynamic allocation, and use object pools or placement-new to manage memory when necessary.

------

## Code Examples
