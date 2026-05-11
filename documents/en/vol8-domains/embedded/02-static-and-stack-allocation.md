# Embedded C++ Tutorial — Static Storage and Stack Allocation Strategies

> I caught a cold recently and took a long break to rest...

In embedded systems, memory resources are scarce and unevenly distributed (Flash, SRAM, special high-speed SRAM, etc.). Choosing whether to place data in the **static region** (global, static variables, constants) or on the **stack** (function local variables, temporary objects) directly impacts program reliability, startup time, code maintainability, and real-time performance. This blog post provides production-ready strategies and example code, covering concepts, implementation, common pitfalls, and practical recommendations.

------

## What Are Static Storage and Stack Allocation (Quick Definitions)

**Static storage**: Memory allocated at compile time or link time, including `.text` (code + rodata), `.data` (initialized global/static variables, copied to RAM at runtime), and `.bss` (uninitialized global/static variables, zeroed at runtime). These variables exist for the entire lifetime of the program or until explicitly modified.

**Stack allocation**: Memory allocated by the stack pointer during a function call, used for local variables, return addresses, register saving, etc. The stack space is released when the function returns.

------

## Why Be Careful in Embedded Systems?

- **Predictability**: The size of static storage is visible at link time; stack growth depends on the runtime execution path, making it difficult to statically guarantee against overflow.
- **Real-time performance**: Dynamic allocation or large stack frames can introduce unpredictable latency. Stack usage in interrupt contexts requires special attention.
- **Memory layout**: ROM/Flash and different tiers of SRAM (on-chip/external) vary significantly in speed and capacity. Static data can be placed in appropriate regions (e.g., putting large read-only tables in Flash).
- **Reentrancy and thread safety**: Global/static variables are not thread-safe by default; they require additional synchronization in an RTOS environment. Stack data is inherently thread-safe for the current thread (each thread has its own stack).

------

## So, What Qualifies as Static Storage?

- **Read-only constants (const)**: In common ARM/GCC scenarios, these are placed in Flash's `.rodata` and do not consume RAM at runtime (unless forcibly copied). Using `const` for lookup tables, firmware version strings, etc., is a great way to save RAM.
- **Initialized static variables (.data)**: The compiler generates initialization data in Flash, which is copied to RAM at startup, thus consuming RAM.
- **Uninitialized static variables (.bss)**: These are zeroed at startup, consuming RAM but not leaving large blocks of initialization data in Flash.
- **Placement control**: We can use linker scripts and `__attribute__((section("...")))` to control data placement into specific sections (such as fast SRAM, uninitialized sections like `.noinit`, etc.).
- **Pitfalls to avoid**:
  - Making large arrays or buffers static permanently consumes memory. Without proper planning, this can lead to wasted or exhausted memory.
  - Static mutable variables require consideration of concurrent access (interrupts, threads) using `volatile`, mutexes, atomic operations, etc.

Example: Placing a large lookup table in Flash

```c++
// foo.cpp
static const uint16_t sine_table[256] = {
    // ... 256 entries ...
};

```

If we need to explicitly place it in a specific section like `.rodata` / Flash:

```c++
const uint16_t lookup[] __attribute__((section(".rodata.lookup"))) = { ... };

```

------

## Linker Script Example

In embedded engineering, we typically modify the linker script to place sections in appropriate memory regions.

```c
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  FASTRAM(rwx) : ORIGIN = 0x20020000, LENGTH = 32K
}

SECTIONS
{
  .text : { *(.text*) *(.rodata*) } > FLASH

  .data : AT(ADDR(.text) + SIZEOF(.text)) {
    __data_start = .;
    *(.data*)
    __data_end = .;
  } > RAM

  .bss : {
    __bss_start = .;
    *(.bss*)
    __bss_end = .;
  } > RAM

  /* 自定义段放在 FASTRAM */
  .fastdata : {
    *(.fastdata*)
  } > FASTRAM
}

```

This practice is very common in U-Boot, where `__attribute__((section(".fastdata")))` is used in the code to place performance-sensitive data into FASTRAM.

------

## Risks and Usage of Stack Allocation

- **Large local variables easily trigger stack overflow**. For example:

```c++
void foo() {
    uint8_t big_buf[64*1024]; // 很可能超出单个线程/中断栈
    // ...
}

```

- **Recursion**: Most embedded systems should avoid recursion (it is difficult to estimate the maximum depth).
- **Variable Length Arrays (VLA) / alloca**: Features that change stack usage at runtime are extremely risky in embedded systems. We should disable or use them with extreme caution.
- **Temporary objects within functions**: Small objects should preferably be placed on the stack, while large objects should be placed in static storage or the heap (if allowed).

Alternative approach: Make large buffers static or place them in task-specific memory pools.

------

## C++ Specifics (Construction, Destruction, Placement New)

- **Static object construction order**: The construction order of global static objects across different translation units is not guaranteed (the "static initialization order fiasco"). During the embedded startup phase, we should explicitly write critical initializations in `main()` or init functions.
- **placement new**: We can explicitly construct objects on static/stack/specific memory regions (often used in heap-less systems):

```c++
alignas(MyType) static uint8_t buffer[sizeof(MyType)];
MyType* p = new (buffer) MyType(args...);  // placement new
p->~MyType(); // 手动析构

```

This is very useful in malloc-free scenarios, but we must manage the object lifecycle properly.

------

## Strategies Without malloc (Required by Many Embedded Projects)

- Use **fixed-size object pools or ring buffers** to replace the heap.
- Implement type-safe allocation interfaces through templates or hand-written pools.
- For all long-lived buffers (such as network packet buffers), prioritize static allocation and place them in appropriate sections.

A simple ring buffer (illustrative):

```c++
template<size_t N>
class RingBuffer {
  uint8_t buf[N];
  size_t head = 0, tail = 0;
public:
  bool push(uint8_t v) { size_t n = (head+1)%N; if (n==tail) return false; buf[head]=v; head=n; return true; }
  bool pop(uint8_t &out) { if (head==tail) return false; out = buf[tail]; tail=(tail+1)%N; return true; }
};

```

## Conclusion

In embedded C++ development, **static storage provides predictability and controllable long-term memory usage**, while **the stack provides locality and thread isolation**. When making a choice, we should consider: buffer size, access patterns (concurrent/interrupt), performance (speed/access latency), and testability (stack usage can be measured). In practice, we prioritize placing large objects, lookup tables, and DMA buffers in static regions or dedicated RAM; we place small, short-lived temporary objects on the stack; and we strictly control dynamic allocation, using object pools or placement new to manage memory when necessary.

------

## Code Examples
