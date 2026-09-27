---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: A deep dive into how the linker works, how to write linker scripts, and
  how startup code is implemented.
difficulty: beginner
order: 3
platform: host
reading_time_minutes: 12
related: []
tags:
- cpp-modern
- host
- intermediate
title: Linker and Linker Scripts
translation:
  source: documents/vol7-engineering/03-linker-and-linker-scripts.md
  source_hash: e1086aba6bb4e8c5a904cc09c1d4cccc4ce2fe4b61f0130933ff95ec9584494d
  translated_at: '2026-09-27T02:31:53+00:00'
  engine: anthropic
  token_count: 3100
---
# Linker and Linker Scripts: From Principles to Practice

If you have read my blog series on C/C++ compilation principles, you should already have a rough idea of what the linker does. A quick recap: the compiler turns source code into object files, and the linker is the last stage of the build pipeline—it combines those object files into the final executable program.

> Further reading:
>
> - [In-Depth Understanding of C/C++ Compilation and Linking — CSDN Blog](https://blog.csdn.net/charlie114514191/article/details/152921903)
> - [Understanding C/C++ Compilation and Linking: An Introduction — an article by 老老老陈醋 on Zhihu](https://zhuanlan.zhihu.com/p/1972593756701189002)

In embedded development, the linker's importance is often underestimated. In reality, linker configuration and optimization strategies directly affect the program's code size and runtime performance—and can even decide whether the program starts at all. In this article we will dig into how the linker works, with a focus on writing linker scripts and implementing startup code, so you can build embedded programs that are smaller, faster, and more reliable.

------

## 1. How the Linker Works

Before diving into linker scripts, let's pin down what the linker actually does. Understanding these basics will make writing and debugging linker scripts much easier.

### 1.1 The Linker's Four Core Tasks

The linker's job looks mysterious, but it boils down to four core tasks:

**(1) Symbol Resolution**

When you call in one file a function defined in another, the compiler only knows the function's name—not its actual address. The linker's job is to find the real definition and wire the two together:

```cpp
// file1.cpp
void printMessage() {
    // Function implementation
}

// file2.cpp
extern void printMessage();  // This is only a declaration
int main() {
    printMessage();  // The linker is responsible for finding the actual function address
}

```

**(2) Address Assignment**

The linker assigns the final memory addresses for all the code and data in the program. This sounds trivial, but on embedded systems it is critical—different kinds of memory (FLASH, RAM) live at different physical addresses and have different access characteristics.

**(3) Section Merging**

Every object file the compiler generates contains multiple sections—`.text` (code), `.data` (initialized data), `.bss` (uninitialized data), and so on. The linker merges all sections of the same kind from every file together, forming the unified layout of the final executable.

**(4) Library Linking**

Programs usually rely on the standard library or third-party libraries. The linker is responsible for extracting the needed code from those libraries and integrating it into the final executable.

------

## 2. Why Do Embedded Systems Need a Custom Linker Script

Now that you understand the linker's basic work, you might ask: don't the compiler and linker do all this automatically? Why do we still have to write linker scripts by hand? Here is why—embedded systems are diverse, and sometimes ship in mass production; you have to mind these details to tune costs.

### 2.1 Memory Constraints of Embedded Systems

On an embedded system, memory is a scarce and scattered resource—fundamentally different from a general-purpose computer:

- **The boot vector must sit at a specific address**: after reset, the processor reads the interrupt vector table from a fixed address
- **Program code must live in FLASH**: FLASH is non-volatile storage, so the code survives power loss
- **Read-only constants should reside in FLASH**: make full use of the FLASH space and save precious RAM
- **Runtime variables need to be in RAM**: RAM is readable and writable, but its data is lost on power-off
- **C++ global objects must be constructed properly**: calling their constructors requires dedicated startup code
- **Stack and heap must be configured correctly too**: make sure the program has enough stack space and heap space

The default policies of compilers and linkers are designed for general-purpose systems and simply cannot satisfy these hardware constraints. That is why we need a **linker script**—the configuration file in which we tell the linker "how memory should be organized on this special piece of hardware".

### 2.2 Core Concepts of a Linker Script

Before writing a linker script, let's understand a few of the most important concepts:

**MEMORY region definitions** define the name, start address, and length of each physical memory region. For example:

```c
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

```

**SECTIONS output-section definitions** tell the linker how to organize the input sections (from the object files) into output sections, and which MEMORY region each goes to:

```c
SECTIONS {
  .text : { *(.text*) } > FLASH
  .data : { *(.data*) } > RAM
}

```

**Symbol exports** A linker script can define symbols, and the startup code will use them, for example:

- `_sdata` / `_edata`: the start and end addresses of the `.data` section
- `_sbss` / `_ebss`: the start and end addresses of the `.bss` section
- `_estack`: the top-of-stack address

**Common control commands**

- `KEEP()`: prevents certain sections from being optimized away (such as the interrupt vector table)
- `PROVIDE()`: supplies a default value for a symbol
- `ASSERT()`: performs constraint checks at link time

### 2.3 What Each Section Is For

Understanding what the different sections do is essential for writing a linker script correctly:

- **`.text`** — the executable code section, usually placed in FLASH
- **`.rodata`** — the read-only constants section (string literals, for example), also placed in FLASH
- **`.data`** — initialized global/static variables. This section is special: at link time its contents sit in FLASH (because the initial values must be preserved), but at runtime it must be copied into RAM (because the variables need to be writable)
- **`.bss`** — uninitialized global/static variables; it exists only in RAM and must be zeroed at startup. Because there are no initial values to store, `.bss` takes up no FLASH space

------

## 3. Hands-On: Writing a Complete Linker Script

Theory done—let's write a real, working linker script. This example targets an ARM Cortex-M microcontroller, but the principles apply to all embedded platforms.

### 3.1 A Minimal Working Linker Script

```c
/* minimal-arm.ld - A minimal linker script for ARM Cortex-M */

/* Specify the program entry point */
ENTRY(Reset_Handler)

/* Define the physical memory layout */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Compute the top-of-stack address (the end of RAM) */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Define the output section layout */
SECTIONS
{
  /* The interrupt vector table must sit at the start of FLASH */
  .isr_vector :
  {
    KEEP(*(.isr_vector))  /* Prevent it from being optimized away */
  } > FLASH

  /* Program code and read-only data */
  .text :
  {
    *(.text*)              /* All code */
    *(.rodata*)            /* Read-only constants */
    *(.gcc_except_table)   /* Exception handling table */
    *(.eh_frame)           /* Stack unwinding information */

    /* Preserve init and destructor function pointers */
    KEEP(*(.init))
    KEEP(*(.fini))
    KEEP(*(.init_array*))
    KEEP(*(.fini_array*))
  } > FLASH

  /* Initialized data section (must be copied from FLASH to RAM) */
  .data : AT(ADDR(.text) + SIZEOF(.text))
  {
    _sdata = .;           /* Mark the start address in RAM */
    *(.data*)
    _edata = .;           /* Mark the end address in RAM */
  } > RAM

  /* Record where the data section sits in FLASH (for copying) */
  _sidata = LOADADDR(.data);

  /* Uninitialized data section (must be zeroed) */
  .bss :
  {
    _sbss = .;            /* Mark the start address */
    *(.bss*)
    *(COMMON)
    _ebss = .;            /* Mark the end address */
  } > RAM

  /* Export the start of the heap */
  _end = .;
  PROVIDE(end = _end);
}


```

### 3.2 Walking Through the Script

The key points of this script:

1. **The interrupt vector table** (`.isr_vector`) must be placed at the very beginning of FLASH, because the processor reads it from a fixed address after reset
2. **The code section** (`.text`) comes right after it, holding all the executable code and read-only constants
3. **The dual addresses of the `.data` section**:
   - `AT(ADDR(.text) + SIZEOF(.text))` specifies the load address (LMA), i.e. where the data sits in FLASH
   - `> RAM` specifies the run-time address (VMA), i.e. where the data should be in RAM while the program runs
   - The startup code needs to copy the data from the LMA to the VMA
4. **Symbol exports**: symbols such as `_sdata`, `_edata`, `_sbss`, and `_ebss` are used by the startup code

------

## 4. Startup Code: Bringing the Linker Script to Life

With a linker script in place, the program's memory layout is settled. But that is not enough—we still need startup code to do the critical initialization work so the program can run correctly.

### 4.1 The Complete Startup Flow

After the processor resets, it jumps to `Reset_Handler`. This is the first piece of code the whole program runs, and its duties are:

1. **Disable interrupts** (optional, depending on the platform)
2. **Copy the `.data` section**: copy the initialized data from FLASH to RAM
3. **Zero the `.bss` section**: clear the uninitialized data region
4. **Call the C++ global constructors** (if using C++)
5. The stack pointer needs no manual setup — on Cortex-M the initial SP is loaded by hardware from the first vector-table entry at reset (which is why the sample code below has no such step)
6. **Jump to the `main()` function**

### 4.2 A Startup Code Example

```c
/* startup.c - ARM Cortex-M startup code */

#include <stdint.h>

/* Symbols exported by the linker script (external symbols) */
extern uint32_t _sidata;   /* Start address of .data in FLASH */
extern uint32_t _sdata;    /* Start address of .data in RAM */
extern uint32_t _edata;    /* End address of .data in RAM */
extern uint32_t _sbss;     /* Start address of .bss */
extern uint32_t _ebss;     /* End address of .bss */

/* C++ constructor array (filled in by the linker script) */
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

/* Declaration of the main function */
extern int main(void);

/**
 * Reset handler - the program's true entry point
 */
void Reset_Handler(void) {
  uint32_t *src, *dst;

  /* 1. Copy the .data section from FLASH to RAM */
  src = &_sidata;
  dst = &_sdata;
  while (dst < &_edata) {
    *dst++ = *src++;
  }

  /* 2. Zero the .bss section */
  dst = &_sbss;
  while (dst < &_ebss) {
    *dst++ = 0;
  }

  /* 3. Call the constructors of the C++ global objects */
  for (void (**p)() = __init_array_start; p < __init_array_end; ++p) {
    (*p)();
  }

  /* 4. Jump to the main function */
  main();

  /* If main returns, loop forever */
  while (1);
}

```

### 4.3 Why Each Step Is Needed

**Why copy `.data`?** Initialized global variables need to keep their initial values, and those values are stored in FLASH (non-volatile). But the program needs to modify these variables at runtime, and FLASH is usually read-only—so the data must be copied into RAM.

**Why zero `.bss`?** According to the C/C++ standards, uninitialized global variables should be initialized to 0. But to save FLASH space, the compiler does not store zero values for these variables in the image; instead, the program zeroes the region at startup.

**Why call the constructors?** C++ global objects need to be constructed before `main()`. The compiler places the addresses of these constructors in the `.init_array` array, and the startup code is responsible for calling them one by one.

------

## 5. Special Considerations for C++ Development

If you use C++ for embedded development, there are some extra issues to watch. C++'s advanced features (such as global objects, exceptions, RTTI) bring additional complexity to linking and startup.

### 5.1 Global Object Construction Order

C++ has a famous "static initialization order fiasco":

- **Within the same translation unit**: objects are initialized in the order they appear in the code
- **Across different translation units**: the initialization order is undefined!

This can lead to one object's constructor using another object that has not been constructed yet. The solutions:

1. **Avoid dependencies between global objects** (most recommended)
2. Use the **Meyers singleton** pattern (a function-local static variable)
3. Use **`__attribute__((init_priority(N)))`** (a GCC extension—use with care)

```cpp
// Use a Meyers singleton to avoid initialization-order problems
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;  // Constructed only on the first call
        return instance;
    }
private:
    Logger() = default;
};

```

### 5.2 C++ Support in the Linker Script

Make sure the linker script handles the C++-related sections correctly:

```c
.text : {
    /* ... */
    KEEP(*(.init_array*))    /* Constructor pointer array */
    KEEP(*(.fini_array*))    /* Destructor pointer array */
    *(.eh_frame)             /* Exception handling information */
    *(.gcc_except_table)     /* Exception handling table */
}

```

If these sections get discarded by mistake, the constructors will never be called, or exception handling will fail.

### 5.3 Optimization Advice

The golden rule of embedded C++ development: **skip every advanced feature you can do without**.

- **Disable exceptions**: use the `-fno-exceptions` compiler option (exception handling significantly increases code size)
- **Disable RTTI**: use the `-fno-rtti` compiler option (run-time type information is rarely needed)
- **Avoid dynamic memory allocation**: embedded systems usually lack a full heap implementation
- **Put constants into FLASH**: use `const` and `constexpr` so the data lands in the `.rodata` section

------

## 6. Link Optimization Tips and Best Practices

With the basics mastered, let's see how to further optimize the linking process—shrinking code size and speeding up startup.

### 6.1 Function-Level Link Optimization

Use the compiler's section-splitting options together with the linker's garbage collection:

```bash

# At compile time: put each function and each data object into its own section
arm-none-eabi-gcc -ffunction-sections -fdata-sections ...

# At link time: remove the unused sections
arm-none-eabi-gcc -Wl,--gc-sections ...

```

With this, if the program never calls a certain function, the linker automatically removes it from the final image.

### 6.2 Optimizing Memory Usage

**Tip 1: Put constants into FLASH**

```cpp
const char msg[] = "Hello";              // In .rodata by default (good)
static const int table[] = {1,2,3};      // Also in .rodata (good)

```

**Tip 2: Avoid non-zero initialization of large arrays**

```cpp
// Bad: takes up 10KB of FLASH space (in the .data section)
uint8_t buffer[10240] = {1, 2, 3, ...};

// Good: takes up no FLASH space (in the .bss section); initialize it in main() at startup
uint8_t buffer[10240];

```

**Tip 3: Use `ASSERT` for constraint checking**

```c
SECTIONS {
    .text : { /* ... */ } > FLASH
    ASSERT(SIZEOF(.text) < 0x7E000, "代码段超出 FLASH 空间")
}

```

### 6.3 Optimizing Startup Performance

**Measure constructor overhead** Constructing C++ global objects can be very time-consuming. You can:

1. Measure the startup time with the DWT performance counters
2. Inspect the `.map` file to see which functions take up a lot of space
3. Avoid complex operations in constructors (file I/O, dynamic allocation, peripheral initialization)

**Deferred initialization** Postpone non-urgent initialization until `main()` or first use:

```cpp
// Bad: initialized at startup
Display display;

// Good: initialized when needed
Display* display = nullptr;
void initDisplay() {
    if (!display) {
        display = new Display();
    }
}

```
