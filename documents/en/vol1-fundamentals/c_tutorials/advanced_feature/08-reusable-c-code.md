---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: Master the engineering organization methods of C code, from modular design,
  header file interfaces, and opaque pointers to platform abstraction layers, and
  learn how C++ namespaces, classes, and PIMPL inherit these ideas.
difficulty: intermediate
order: 108
platform: host
prerequisites:
- 指针进阶：不完整类型与多级指针
- 结构体与内存布局
- 编译与链接基础
reading_time_minutes: 24
tags:
- host
- cpp-modern
- intermediate
- 工程实践
- 基础
title: Build Reusable C Code
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/08-reusable-c-code.md
  source_hash: 9606a167228f91715e395ecdde2db074f4218e574eee04916eb556416aa6436f
  translated_at: '2026-06-16T03:40:09.385057+00:00'
  engine: anthropic
  token_count: 4667
---
# Building Reusable C Code

Anyone who has written tens of thousands of lines of C code has likely experienced this—everything starts off fine at the beginning of a project, a few `.c` and `.h` files cobbled together and it works. But as features pile up, the code starts to turn into a mess: header files are indiscriminately included everywhere, global variables are all over the place, changing a field in a struct triggers a recompile of a dozen source files, and just when you finally get it working on your PC, porting it to STM32 brings up a whole new set of problems. Honestly, the root of this pain is often not a wrong algorithm or a blown-up pointer, but rather a failure to take "code organization" seriously from the start.

How do other languages handle this? Java has packages and private access, Rust has modules and crates, Python has modules and naming conventions—they all provide modular infrastructure at the language level. What about C? C has nothing. No namespaces, no classes, no access control, no module system. What C gives us is the preprocessor's `#include` and `#define`, plus a lot of discipline we need to enforce ourselves.

But this doesn't mean we can't write clean, modular code in C—it just means we need to achieve what other languages do for you automatically, using manual techniques. Understanding these manual techniques is crucial, because C++'s `namespace`, `private` access control, the PIMPL idiom, and even C++20 Modules are all engineered upgrades over these manual C practices. Once you understand the C way, you can truly understand why C++ is designed the way it is.

In this article, we will systematically review this methodology—from modular design principles and header file interface design to opaque pointers for hiding implementation, configuration management, and cross-platform porting.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the core principles of modular design and split functionality into independent compilation units.
> - [ ] Write clean header file interfaces, ensuring "headers contain only declarations, no implementation."
> - [ ] Use the opaque pointer pattern to hide implementation details.
> - [ ] Distinguish between compile-time and runtime configuration use cases.
> - [ ] Write a Platform Abstraction Layer (PAL) to achieve cross-platform porting.
> - [ ] Manage API version compatibility.

## Environment Setup

All code examples in this text can be compiled and run in a standard C environment. The C++ section uses the C++17 standard. It is recommended to always enable the `-Wall -Wextra` compiler flags to catch potential issues.

```bash
sudo apt install gcc clang make
```

## Step 1 — Figure Out What a "Good Module" Is

Before diving into specific techniques, we need to clarify the concept of "modularity." Many people think modularity simply means splitting code into multiple `.c` files—this is just physical separation, not true modularity. You can think of it like organizing a toolbox: throwing all tools into one big drawer is "splitting" (physically separate, but still hard to find things), whereas labeling drawers and specifying "this drawer is only for wrenches, that one is only for screwdrivers" is modularity. True modularity must satisfy one core principle: **Each module is an independent, replaceable compilation unit with a clear interface**.

What does a good module look like? Suppose we are writing a UART driver module. The header file exposes only the types and functions the caller needs to know; implementation details are completely hidden in the `.c` file; internal helper functions are all marked `static`; dependencies between modules are clearly reflected through header `include` relationships. The benefit is: when you need to port the UART driver from STM32F1 to ESP32, you only need to replace the corresponding `.c` implementation, and the caller code doesn't need to change a single line.

A module's file organization usually looks like this:

```text
my_module/
├── inc/          # Public headers
│   └── my_module.h
├── src/          # Implementation files
│   └── my_module.c
└── tests/        # Unit tests
    └── test_my_module.c
```

This structure looks simple, but the devil is in the details. Let's break them down one by one.

## Step 2 — Design Clean Header File Interfaces

The header file is the only contract between a module and the outside world, so it must be clean, stable, and self-contained. "Self-contained" means: after a user `#include`s your header file, they should be able to compile without manually including anything else.

### Header Guards and Include Principles

Header guards are basic literacy—using `#ifndef`/`#define`/`#endif` or `#pragma once` (supported by mainstream compilers) works. More important is the principle of includes: header files should only include what they directly depend on. If your header file uses `size_t`, then `#include <stddef.h>`; if it uses `uint32_t`, then `#include <stdint.h>`. Never rely on assumptions like "the caller must have already included this"—that's digging a hole for yourself.

Let's write a clean header file example:

```cpp
// inc/uart_driver.h
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Handle for the UART device (opaque pointer)
typedef struct UartDevice UartDevice;

// Configuration structure
typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity; // 0=none, 1=odd, 2=even
} UartConfig;

// API functions
UartDevice* uart_create(const UartConfig* config);
void uart_destroy(UartDevice* dev);
int uart_send(UartDevice* dev, const uint8_t* data, size_t len);
int uart_recv(UartDevice* dev, uint8_t* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif // UART_DRIVER_H
```

You might notice the `#ifdef __cplusplus` lines. This isn't C++ code, but adding `extern "C"` is a good habit—it ensures that when this header is included by C++ code, the linker can correctly find these C-style functions. Many famous C libraries (SQLite, libcurl, zlib) do this.

### Things That Should Absolutely Never Appear in Header Files

There are certain things that should absolutely never appear in public header files. Placing the definition of a `static` function in a header file means every compilation unit that includes it gets a copy, which wastes space and easily leads to strange linking issues. The same applies to macro definitions for internal constants and types used for implementation—anything starting with an underscore or containing "internal" or "priv" should not be in a public header file.

```c
// === DON'T DO THIS ===
// inc/uart_driver.h
static int helper_function() { ... } // Bad!
#define INTERNAL_BUFFER_SIZE 128     // Bad!
typedef struct { ... } InternalCtx;  // Bad!
```

> ⚠️ **Warning**
> Exposing the full definition of a struct in a header file means callers will eventually be tempted to directly access internal fields. Once you modify the struct layout, all source files including this header must be recompiled—in large projects, this could be several minutes of compilation time. Worse, callers might start depending on your internal implementation, making it impossible for you to change it.

## Step 3 — Hide Implementation with Opaque Pointers

In the previous pointer article, we saw the basic usage of incomplete types and opaque pointers. Now, let's re-examine them in the context of modular design. The opaque pointer is the most powerful information-hiding tool in C—you can think of it as the C version of the `private` keyword in object-oriented languages. The caller only knows "this thing exists," but doesn't know what's inside, and can only manipulate it through functions you provide.

### Complete Module Example: Ring Buffer

Let's write a complete ring buffer module, combining header design, opaque pointers, and error handling. First, the header file—this is the only thing the caller needs to include:

```c
// inc/ring_buffer.h
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque pointer
typedef struct RingBuffer RingBuffer;

// Creation and destruction
RingBuffer* ring_buf_create(size_t capacity);
void        ring_buf_destroy(RingBuffer* rb);

// Core operations
int ring_buf_push(RingBuffer* rb, uint8_t data);
int ring_buf_pop(RingBuffer* rb, uint8_t* data);

// Query functions
size_t ring_buf_size(const RingBuffer* rb);
int    ring_buf_is_empty(const RingBuffer* rb);
int    ring_buf_is_full(const RingBuffer* rb);

#ifdef __cplusplus
}
#endif

#endif // RING_BUFFER_H
```

In the header file, we did not expose the internal structure of `RingBuffer`—`typedef struct RingBuffer RingBuffer` is just a forward declaration plus typedef. The caller only gets a `RingBuffer` pointer and can manipulate it through the functions we provide. They don't know if the buffer is implemented with an array or a linked list—they know nothing—which is exactly right.

Next is the implementation file. Note that the full definition of the struct appears only here:

```c
// src/ring_buffer.c
#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>

struct RingBuffer {
    uint8_t* buffer;
    size_t   capacity;
    size_t   head;
    size_t   tail;
    size_t   count;
};

RingBuffer* ring_buf_create(size_t capacity) {
    RingBuffer* rb = malloc(sizeof(RingBuffer));
    if (!rb) return NULL;

    rb->buffer = malloc(capacity);
    if (!rb->buffer) {
        free(rb);
        return NULL;
    }

    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return rb;
}

void ring_buf_destroy(RingBuffer* rb) {
    if (rb) {
        free(rb->buffer);
        free(rb);
    }
}

int ring_buf_push(RingBuffer* rb, uint8_t data) {
    if (!rb || ring_buf_is_full(rb)) return -1;
    rb->buffer[rb->tail] = data;
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count++;
    return 0;
}

int ring_buf_pop(RingBuffer* rb, uint8_t* data) {
    if (!rb || ring_buf_is_empty(rb)) return -1;
    *data = rb->buffer[rb->head];
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count--;
    return 0;
}

size_t ring_buf_size(const RingBuffer* rb) {
    return rb ? rb->count : 0;
}

int ring_buf_is_empty(const RingBuffer* rb) {
    return rb ? (rb->count == 0) : 1;
}

int ring_buf_is_full(const RingBuffer* rb) {
    return rb ? (rb->count == rb->capacity) : 0;
}
```

After writing this, let's verify it:

```bash
gcc -c src/ring_buffer.c -I inc -o ring_buffer.o
```

Let's write a simple test to confirm the behavior is correct:

```c
// tests/test_ring_buffer.c
#include "ring_buffer.h"
#include <stdio.h>
#include <assert.h>

int main() {
    RingBuffer* rb = ring_buf_create(5);
    assert(rb != NULL);

    // Test basic push/pop
    for (int i = 0; i < 5; i++) {
        assert(ring_buf_push(rb, i) == 0);
    }
    assert(ring_buf_is_full(rb) == 1);

    // Test overflow
    assert(ring_buf_push(rb, 99) == -1);

    // Test pop
    uint8_t val;
    for (int i = 0; i < 5; i++) {
        assert(ring_buf_pop(rb, &val) == 0);
        assert(val == i);
    }
    assert(ring_buf_is_empty(rb) == 1);

    ring_buf_destroy(rb);
    printf("All tests passed!\n");
    return 0;
}
```

```bash
gcc tests/test_ring_buffer.c ring_buffer.o -o test_runner
./test_runner
# Output: All tests passed!
```

There are a few notable design decisions here. The first thing every public function does is check if the `rb` parameter is `NULL`—because C has no exception mechanism, the best we can do is intercept null pointers at the entry to avoid triggering a segmentation fault deep inside the function. `const` appears in the parameters of query functions, promising the caller: this function will not modify the buffer's state.

> ⚠️ **Warning**
> There is a common failure scenario with the opaque pointer pattern: the caller gets a `NULL` (e.g., `ring_buf_create` returns `NULL` due to memory exhaustion) and then calls `ring_buf_push` without checking. Although our implementation checks for NULL in every function, don't expect all libraries to do this. Cultivate the habit of checking return values—especially for functions involving memory allocation.

The power of this opaque pointer pattern lies in the fact that if we later want to change the ring buffer from dynamic allocation to a static array, or add thread safety, or switch to a power-of-2 optimization (using bitwise operations instead of modulo), we only need to modify `ring_buffer.c`. All caller code remains completely untouched, and doesn't even need recompilation—as long as the interface signatures in the header remain unchanged.

## Step 4 — Learn to Manage Configuration Parameters

Once modularity reaches a certain level, we will find that some parameters need to be adjusted based on specific usage scenarios—buffer size, timeout, thread safety switches, etc. The management of these parameters can be roughly divided into two categories: compile-time configuration and runtime configuration.

### Compile-Time Configuration: Zero-Cost Flexibility

Compile-time configuration is implemented through macro definitions or configuration header files, suitable for parameters that are determined at compile-time and do not change during runtime. The benefit is zero runtime overhead—the compiler can inline constants directly into the code and even perform constant folding optimizations.

```c
// inc/my_module_config.h
#ifndef MY_MODULE_CONFIG_H
#define MY_MODULE_CONFIG_H

// Buffer size (default 256, can be overridden by compiler flags)
#ifndef MODULE_BUFFER_SIZE
#define MODULE_BUFFER_SIZE 256
#endif

// Enable debug output
#ifndef MODULE_DEBUG
#define MODULE_DEBUG 0
#endif

// Thread safety switch
#ifndef MODULE_THREAD_SAFE
#define MODULE_THREAD_SAFE 0
#endif

#endif // MY_MODULE_CONFIG_H
```

Then, in the implementation file, use conditional compilation based on these macros:

```c
// src/my_module.c
#include "my_module_config.h"

#if MODULE_THREAD_SAFE
    #include <pthread.h>
    static pthread_mutex_t mutex;
#endif

void module_function() {
#if MODULE_THREAD_SAFE
    pthread_mutex_lock(&mutex);
#endif

    // Core logic...

#if MODULE_THREAD_SAFE
    pthread_mutex_unlock(&mutex);
#endif
}
```

This style is very common in the embedded field. Through conditional compilation, the same codebase can adapt to resource-constrained microcontrollers (turning off unneeded features to save Flash and RAM) and feature-rich Linux environments.

Note a key detail: don't hardcode compile-time configuration macros directly in the `.c` file. Instead, put them in a separate `*_config.h`, and wrap every macro with `#ifndef`. This allows users to override default values via compiler options (`-D`) without modifying the source code.

### Runtime Configuration: Dynamic Flexibility

Runtime configuration is passed through function parameters or configuration structures, suitable for parameters determined at program startup or that may change during execution. The `UartConfig` structure in the UART driver earlier is a typical runtime configuration.

When to use compile-time vs. runtime configuration? There's a rough rule: **In embedded environments, parameters that "require re-flashing if changed" use compile-time configuration, while parameters that "might differ between devices or scenarios" use runtime configuration.** For example, if your product has multiple models with different baud rates, the baud rate should be runtime configuration; but if a module's data buffer size is fixed across the entire product line, compile-time configuration is more appropriate.

> ⚠️ **Warning**
> Don't nest conditional compilation too deeply. If you find yourself writing more than three levels of `#if`, code readability drops drastically. A better approach is to split code for different configurations into separate helper functions, using one layer of conditional compilation to choose which function to call.

## Step 5 — Handle Cross-Platform with a Platform Abstraction Layer

To get code running on multiple platforms, the core technique is introducing a Platform Abstraction Layer (PAL). The principle is simple: **isolate all platform-specific code in one place, and upper-level code only calls abstract interfaces**. You can think of it like a universal charger—whether your phone uses USB-C or Lightning, plug in the adapter and it charges; the adapter is that "platform abstraction layer."

Suppose our ring buffer needs to use fixed-size static arrays on embedded platforms (no `malloc`), while on the PC, dynamic allocation is fine. First, we define a set of platform interfaces:

```c
// inc/platform.h
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

// Memory allocation interface
void* platform_malloc(size_t size);
void  platform_free(void* ptr);

// Critical section interface (for thread safety)
void  platform_enter_critical(void);
void  platform_exit_critical(void);

#endif // PLATFORM_H
```

Then provide different implementations for different platforms. First, the Linux version:

```c
// src/platform_linux.c
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>

void* platform_malloc(size_t size) {
    return malloc(size);
}

void platform_free(void* ptr) {
    free(ptr);
}

void platform_enter_critical(void) {
    // Linux user-space: use pthread mutex if needed
}

void platform_exit_critical(void) {
    // Linux user-space: release pthread mutex
}
```

Now the bare-metal version:

```c
// src/platform_baremetal.c
#include "platform.h"
#include "stm32f4xx.h"

// Static memory pool for bare-metal
static uint8_t memory_pool[4096];
static size_t pool_offset = 0;

void* platform_malloc(size_t size) {
    // Simple bump allocator from static pool
    if (pool_offset + size > sizeof(memory_pool)) return NULL;
    void* ptr = &memory_pool[pool_offset];
    pool_offset += size;
    return ptr;
}

void platform_free(void* ptr) {
    // Static pool cannot be easily freed in this simple example
}

void platform_enter_critical(void) {
    __disable_irq(); // ARM Cortex-M specific
}

void platform_exit_critical(void) {
    __enable_irq();
}
```

With the platform abstraction layer, the ring buffer code doesn't need to care what platform it's running on—`platform_malloc` calls `malloc` on Linux and allocates from a static memory pool on STM32; `platform_enter_critical` uses `pthread_mutex` on Linux and disables interrupts on bare-metal. When porting to a new platform, you only need to write a new `platform_*.c`, and the core business logic remains untouched.

Cross-platform code has another common type trap: the size of basic types might differ on different platforms. `int` might be 16-bit on an 8-bit MCU, but 32-bit on a 32-bit platform; `long` is 64-bit on 64-bit Linux but 32-bit on Windows. So cross-platform code should consistently use fixed-width types defined in `<stdint.h>`: `int32_t`, `uint32_t`, `size_t`, etc.

## Step 6 — Evolve the API Stably

When your module is used by multiple projects, API stability becomes a must-treat issue. Changing a function name or adding a parameter means all callers have to change—if you can't control the callers, that's a disaster.

### Embed Version Numbers

A simple approach is to define version number macros in the header file and provide a query interface at runtime:

```c
// inc/my_module.h
#define MY_MODULE_VERSION_MAJOR 1
#define MY_MODULE_VERSION_MINOR 2
#define MY_MODULE_VERSION_PATCH 0

// Returns version in format: 0x010200 (1.2.0)
uint32_t my_module_get_version(void);
```

```c
// src/my_module.c
uint32_t my_module_get_version(void) {
    return (MY_MODULE_VERSION_MAJOR << 16) |
           (MY_MODULE_VERSION_MINOR << 8)  |
           (MY_MODULE_VERSION_PATCH);
}
```

### "Add-Only" Strategy

When adding new features, try to implement them by adding new functions rather than modifying existing function signatures. For example, if your ring buffer originally only supported single-byte `push`, and now needs to support multi-byte data, don't change the parameter type of `ring_buf_push` from `uint8_t` to `void*`—this breaks all existing callers. The correct approach is to add a new group of functions:

```c
// New API for multi-byte operations
int ring_buf_write(RingBuffer* rb, const uint8_t* data, size_t len);
int ring_buf_read(RingBuffer* rb, uint8_t* buffer, size_t len);
```

If an old interface truly needs to be deprecated, you can first mark it with a macro to give users a migration buffer period:

```c
// inc/my_module.h
#if MY_MODULE_VERSION >= 0x020000
    #warning "old_func is deprecated, please use new_func instead"
#endif

// Mark old function as deprecated
__attribute__((deprecated)) void old_func(void);
```

## C++ Transition

The modular techniques we struggled with in C have much more powerful native support in C++. Understanding the C approach helps us understand the design motivation and underlying mechanisms of C++ tools—every "new feature" in C++ wasn't invented out of thin air; they are engineered upgrades over manual C practices.

| C Manual Practice | C++ Native Support | What It Improves |
|-------------------|--------------------|------------------|
| File-level `static` functions | `private`/`protected` members | Compiler-enforced access control, no reliance on self-discipline |
| Naming prefixes (`module_`, `internal_`) | `namespace` | True namespace isolation, no manual prefix writing |
| Opaque pointer pattern | PIMPL idiom + `unique_ptr` | Automatic memory management, no manual create/destroy |
| `#include` + header guards | C++20 Modules | Eliminates macro pollution, redundant parsing, fragile dependency order |
| `typedef` + macros | `using` + `auto` | More intuitive type aliases, automatic type deduction |
| Hand-written `__attribute__((deprecated))` macro | `[[deprecated]]` attribute | Standardized deprecation marking |

### `namespace` and `class` Replace Header File Partitioning

C uses files and naming prefixes for logical partitioning; C++ uses `namespace` for true namespace isolation and `class` access control to replace manually separating header and source files:

```cpp
// Modern C++ Module
namespace uart {

class Driver {
public:
    struct Config { uint32_t baudrate; };

    Driver(const Config& cfg);
    ~Driver();

    void send(std::span<const uint8_t> data);
    // private implementation is hidden by default
private:
    class Impl; // Forward declaration
    Impl* pImpl; // PIMPL pointer
};

} // namespace uart
```

### Pimpl Idiom — Compile-Time Firewall

PIMPL (Pointer to Implementation) is the C++ version of the C opaque pointer, but it has an additional important use in C++: **reducing header dependencies and speeding up compilation**. In large C++ projects, modifying a header file can trigger hundreds of source files to recompile. If private member definitions are hidden in the `.cpp` file, the header only needs a forward declaration `class Impl`. Then, modifying private members only affects the `.cpp` file and doesn't cause massive recompilation.

```cpp
// inc/driver.h
class Driver {
private:
    class Impl; // Forward declaration
    Impl* pImpl; // Opaque pointer
public:
    Driver();
    ~Driver(); // Must be defined in .cpp
    void operate();
};

// src/driver.cpp
class Driver::Impl {
public:
    int heavy_data[1024];
};

Driver::Driver() : pImpl(new Impl()) {}
Driver::~Driver() { delete pImpl; } // Defined here because Impl is complete here
```

Note the destructor must be defined in the `.cpp` file (or defaulted), not in the header—because `Impl` is an incomplete type in the header, and the destructor of `unique_ptr` (or `delete`) needs the complete definition of `Impl` to correctly delete it.

### C++20 Module System

C++20 introduced the Modules system, aiming to fundamentally replace the header file's `#include` mechanism. Modules directly solve many inherent problems of header files—macro pollution, redundant parsing, fragile dependency order. However, honestly, as of the end of 2024, mainstream compiler support for modules is still evolving rapidly, and adopting modules in large projects requires significant migration work. But it's worth understanding as a trend; we won't expand here (the subsequent C++ Advanced volume will cover this specifically).

## Exercises

### Exercise 1: Opaque Pointer String Hash Map

Implement a simple string-to-integer map using opaque pointers to hide the internal implementation. Requirements:

```c
// inc/strmap.h
StrMap* strmap_create(void);
void    strmap_destroy(StrMap* map);

int  strmap_put(StrMap* map, const char* key, int value);
int  strmap_get(StrMap* map, const char* key, int* out_value);
void strmap_remove(StrMap* map, const char* key);
```

**Hint:** Internally, you can use a simple array of linked lists (separate chaining) to implement the hash map. The hash function can use the classic `djb2` algorithm. Remember that all internal types and helper functions must be hidden in the `.c` file.

### Exercise 2: Platform Abstraction Layer Practice

Write a platform abstraction layer for the hash map in Exercise 1 to replace the standard library's `malloc`/`free`. Requirements:

```c
// inc/platform.h
void* plat_malloc(size_t size);
void  plat_free(void* ptr);
```

Please implement two versions: one using the standard library `malloc`/`free` (suitable for PC), and another using a static memory pool (suitable for embedded bare-metal environments). The hash map's `.c` file should allocate memory by including `platform.h` and calling `plat_malloc`, rather than directly calling `malloc`.

## Reference Resources

- [Opaque Pointer - Wikipedia](https://en.wikipedia.org/wiki/Opaque_pointer)
- [Linux Kernel Coding Style - Chapter 5: Typedefs](https://www.kernel.org/doc/html/latest/process/coding-style.html#typedefs)
- [PIMPL - cppreference](https://en.cppreference.com/w/cpp/language/pimpl)
- [C++20 Modules - cppreference](https://en.cppreference.com/w/cpp/language/modules)
