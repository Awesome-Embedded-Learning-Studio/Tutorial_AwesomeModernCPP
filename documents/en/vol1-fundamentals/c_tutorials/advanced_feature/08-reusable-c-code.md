---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: From modular design, header interfaces, and opaque pointers to platform abstraction layers, systematically master the engineering methods for organizing C code, and how C++'s namespace/class/PIMPL inherit these ideas
difficulty: intermediate
order: 108
platform: host
prerequisites:
- Advanced Pointers: Incomplete Types and Multilevel Pointers
- Structures and Memory Alignment
- Compilation and Linking Basics
reading_time_minutes: 24
tags:
- host
- cpp-modern
- intermediate
- 工程实践
- 基础
title: Building Reusable C Code
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/08-reusable-c-code.md
  source_hash: a132b383095d133c8f6c52873c8e74b30dba00c45031338b2595a806282ab861
  translated_at: '2026-09-25T13:54:24+00:00'
  engine: anthropic
  token_count: 4600
---
# Building Reusable C Code

Anyone who has written tens of thousands of lines of C has probably lived through this—at the start of a project everything is fine, a few `.c` files cobbled together just run, but as features pile up the code turns into a tangled mess: headers are included every which way, global variables fly everywhere, changing one struct field drags a dozen-plus source files into recompiling, and just when you finally get it working on the PC, moving to an STM32 unleashes a whole new pile of porting problems. Honestly, the root of all this pain is usually not a wrong algorithm or a blown pointer—it is that "code organization" was never taken seriously from day one.

How do other languages handle this problem? Java has `package` and `interface`, Rust has `mod` and `trait`, Python has `__init__.py` and naming conventions—they all provide modular infrastructure at the language level. And C? C has none of it. No namespace, no class, no access control, no module system. What C hands us is the preprocessor's `#include` and `#ifndef`, plus a big pile of discipline we have to enforce on ourselves.

That does not mean clean, modular code is impossible in C—it just means we have to do manually what other languages do for you automatically. Understanding these manual techniques matters a lot, because C++'s `namespace`, `class` access control, the PIMPL idiom, and even C++20 Modules are all engineered upgrades of these hand-rolled C practices. Once the C way clicks, you can truly understand why C++ is designed the way it is.

In this article we will systematically walk through this methodology—from modular design principles and header interface design, to hiding implementations behind opaque pointers, to configuration management and cross-platform porting.

Every code example in this article compiles and runs in a standard C environment. The C++ bridge sections use C++17. We recommend always compiling with `-Wall -Wextra` to catch potential problems.

```text
Platform: Linux / macOS / Windows (MSVC/MinGW)
Compiler: GCC >= 9 or Clang >= 12
Standard: -std=c11 (C parts) / -std=c++17 (C++ comparison parts)
Dependencies: pthread (needed by the thread-safety example; provided by default on Linux)
```

## Step 1 — Figure Out What a "Good Module" Is

Before touching concrete techniques, we need to pin down what "modularization" actually means. Many people think modularizing means splitting code into multiple `.c` files—that is only a split in form, not real modularization. Picture organizing a toolbox: dumping every tool into one big drawer is "splitting" (things are physically separated, but finding anything is still a chore), whereas labeling each drawer and deciding "this drawer holds only wrenches, that one only screwdrivers"—that is modularization. Real modularization satisfies one core principle: **every module is an independent, replaceable compilation unit with a clean interface**.

What does a good module look like? Suppose we are writing a UART driver module. The header exposes only the types and functions callers need to know about; all implementation details are hidden in the `.c` file; every internally used function gets `static`; and dependencies between modules show up clearly through the include relationships among headers. The payoff: when you need to port the UART driver from an STM32F1 to an ESP32, you only swap the corresponding `.c` implementation—not a single line of caller code changes.

A module's file layout usually looks like this:

```text
uart_driver/
  ├── uart_driver.h    // Public interface: type declarations, function declarations, documentation comments
  ├── uart_driver.c    // Private implementation: full struct definition, static functions, internal variables
  └── uart_config.h    // Configuration parameters (optional, for compile-time configuration)
```

This structure looks simple, but the devil is in the details. Let us take it apart piece by piece.

## Step 2 — Design a Clean Header Interface

The header file is the sole contract between a module and the outside world, so it must be clean, stable, and self-contained. "Self-contained" means: after someone `#include`s your header, they never have to manually include anything else for it to compile.

### Header Guards and Include Discipline

Header guards are the basics—`#ifndef`/`#define`/`#endif`, or `#pragma once` (every mainstream compiler supports it), both work. What matters more is include discipline: a header should include only what it directly depends on. If your header uses `size_t`, then `#include <stddef.h>`; if it uses `uint32_t`, then `#include <stdint.h>`. Never rely on the assumption that "the caller has surely included it already"—that is digging a pit for yourself.

Let us write an example of a clean header:

```c
// uart_driver.h — an example of a clean header
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration; the internal structure stays hidden
typedef struct UartDriver UartDriver;

// Error codes
typedef enum {
    kUartOk       = 0,
    kUartErrParam = -1,
    kUartErrBusy  = -2,
    kUartErrIo    = -3
} UartResult;

// Configuration struct — what the caller needs to know
typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
} UartConfig;

// Lifecycle management
UartDriver* uart_create(const UartConfig* config);
void        uart_destroy(UartDriver* drv);

// Data operations
UartResult uart_send(UartDriver* drv, const uint8_t* data, size_t len);
UartResult uart_receive(UartDriver* drv, uint8_t* buf, size_t buf_size,
                        size_t* received);

#ifdef __cplusplus
}
#endif

#endif // UART_DRIVER_H
```

You may have noticed the `#ifdef __cplusplus` pair. This is not C++ code, but adding `extern "C"` is a good habit—it ensures that when this header gets included by C++ code, the linker can find these C-style functions correctly. Plenty of well-known C libraries (SQLite, libcurl, zlib) do exactly this.

### What Must Never Appear in a Header File

A few things should never appear in a public header. Defining a `static` function in a header means every compilation unit that includes it gets its own copy—wasting space and inviting strange linkage problems. The same goes for internal constants defined as macros and implementation-only types: anything starting with an underscore or carrying "internal" or "priv" in its name does not belong in a public header.

```c
// Never do this — internal implementation details in a public header
#ifndef BAD_MODULE_H
#define BAD_MODULE_H

#define INTERNAL_BUFFER_SIZE 256      // should not be exposed
#define MAGIC_NUMBER 0xDEADBEEF       // should not be exposed

// The full struct is exposed — callers can access the fields directly
typedef struct {
    uint8_t buffer[256];              // internal buffer; callers should never see it
    int head;                         // internal state
    int tail;
    int count;
} BadQueue;

void bad_queue_push(BadQueue* q, uint8_t val);
static void internal_helper(void) {   // one copy per compilation unit!
    // ...
}

#endif
```

Expose a struct's full definition in the header, and sooner or later callers will not resist touching the internal fields directly. Once you change the struct layout, every source file that includes the header must recompile—in a large project, that can be minutes of build time. Worse, callers may already depend on your internal implementation, so you cannot change it even if you want to.

## Step 3 — Hide the Implementation Behind Opaque Pointers

In the previous article on advanced pointers we already met incomplete types and the basic usage of opaque pointers; now let us re-examine them in the context of modular design. The opaque pointer is C's most powerful information-hiding tool—you can think of it as the C edition of the `private` keyword in object-oriented languages. Callers know "this thing exists", but not what it looks like inside; they can only manipulate it through the functions you provide.

### A Complete Module Example: The Ring Buffer

Let us write a complete ring buffer module that ties together header design, opaque pointers, and error handling. First the header—the only thing callers ever need to include:

```c
// ring_buffer.h — the ring buffer's public interface
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque type — all the caller ever holds is a pointer
typedef struct RingBuffer RingBuffer;

// Create and destroy
RingBuffer* ringbuf_create(size_t capacity);
void        ringbuf_destroy(RingBuffer* rb);

// Data operations
bool   ringbuf_push(RingBuffer* rb, uint8_t data);
bool   ringbuf_pop(RingBuffer* rb, uint8_t* out);
size_t ringbuf_count(const RingBuffer* rb);
bool   ringbuf_is_empty(const RingBuffer* rb);
bool   ringbuf_is_full(const RingBuffer* rb);

#ifdef __cplusplus
}
#endif

#endif // RING_BUFFER_H
```

The header reveals nothing about `RingBuffer`'s internal structure—`typedef struct RingBuffer RingBuffer;` is just a forward declaration plus a typedef. Callers can only obtain a `RingBuffer*` pointer and operate on it through the functions we provide. They have no idea whether the buffer is backed by an array or a linked list, no idea at all—and that is exactly the point.

Next comes the implementation file. Note that the full struct definition appears only here:

```c
// ring_buffer.c — ring buffer implementation
#include "ring_buffer.h"
#include <stdlib.h>

// The full struct definition appears only in the .c file
struct RingBuffer {
    uint8_t* data;      // dynamically allocated buffer
    size_t   capacity;   // total capacity
    size_t   head;       // write position
    size_t   tail;       // read position
    size_t   count;      // current element count
};

RingBuffer* ringbuf_create(size_t capacity) {
    RingBuffer* rb = (RingBuffer*)malloc(sizeof(RingBuffer));
    if (!rb) return NULL;

    rb->data = (uint8_t*)malloc(capacity);
    if (!rb->data) {
        free(rb);
        return NULL;
    }

    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return rb;
}

void ringbuf_destroy(RingBuffer* rb) {
    if (rb) {
        free(rb->data);
        free(rb);
    }
}

bool ringbuf_push(RingBuffer* rb, uint8_t data) {
    if (!rb || rb->count == rb->capacity) return false;

    rb->data[rb->head] = data;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;
    return true;
}

bool ringbuf_pop(RingBuffer* rb, uint8_t* out) {
    if (!rb || rb->count == 0) return false;

    *out = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    return true;
}

size_t ringbuf_count(const RingBuffer* rb) {
    return rb ? rb->count : 0;
}

bool ringbuf_is_empty(const RingBuffer* rb) {
    return rb ? (rb->count == 0) : true;
}

bool ringbuf_is_full(const RingBuffer* rb) {
    return rb ? (rb->count == rb->capacity) : true;
}
```

With it written, let us verify:

```text
$ gcc -Wall -Wextra -std=c11 -c ring_buffer.c -o ring_buffer.o
(no output = compiled successfully, no warnings or errors)
```

Let us write a simple test to confirm the behavior is correct:

```c
// test_ringbuf.c
#include "ring_buffer.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    RingBuffer* rb = ringbuf_create(4);
    assert(rb != NULL);

    assert(ringbuf_is_empty(rb));
    assert(!ringbuf_is_full(rb));

    ringbuf_push(rb, 10);
    ringbuf_push(rb, 20);
    ringbuf_push(rb, 30);
    assert(ringbuf_count(rb) == 3);

    uint8_t val;
    assert(ringbuf_pop(rb, &val) && val == 10);
    assert(ringbuf_pop(rb, &val) && val == 20);
    assert(ringbuf_count(rb) == 1);

    ringbuf_destroy(rb);
    printf("All tests passed!\n");
    return 0;
}
```

```text
$ gcc -Wall -std=c11 test_ringbuf.c ring_buffer.c -o test_ringbuf && ./test_ringbuf
All tests passed!
```

There are several design decisions here worth noting. The first thing every public function does is check whether the `rb` parameter is `NULL`—C has no exception mechanism, so the best we can do is intercept null pointers at the entrance instead of triggering a segfault deep inside the function. `const RingBuffer*` appears in the query functions' parameters—that is a promise to callers: this function will not modify the buffer's state.

The opaque pointer pattern has one classic way to faceplant: the caller gets a `NULL` (say, `ringbuf_create` returned `NULL` because memory ran out), then calls `ringbuf_push(rb, data)` without checking. Although our implementation NULL-checks every function, do not count on every library doing the same. Make a habit of checking return values—especially for functions that allocate memory.

The power of this opaque pointer pattern lies here: if one day we want to switch the ring buffer from dynamic allocation to a static array, add thread safety, or apply a power-of-2 optimization (bitwise operations instead of modulo), we only need to modify `ring_buffer.c`. Not one line of caller code changes—callers do not even need to recompile—as long as the interface signatures in the header stay the same.

## Step 4 — Learn to Manage Configuration Parameters

Once modularization reaches a certain level, we discover that some parameters need adjusting per use case—buffer sizes, timeouts, thread-safety switches, and so on. There are roughly two ways to manage these parameters: compile-time configuration and runtime configuration.

### Compile-Time Configuration: Zero-Overhead Flexibility

Compile-time configuration works through macro definitions or a configuration header. It suits parameters that are fixed at compile time and never change while the program runs. The benefit is zero runtime overhead—the compiler can inline the constants straight into the code and even apply constant-folding optimizations.

```c
// ring_config.h — compile-time configuration
#ifndef RING_CONFIG_H
#define RING_CONFIG_H

// Default buffer capacity; can be overridden via a compiler option
// Usage: -DRINGBUF_DEFAULT_CAPACITY=512
#ifndef RINGBUF_DEFAULT_CAPACITY
#define RINGBUF_DEFAULT_CAPACITY 256
#endif

// Whether thread safety is enabled (can be turned off for single-threaded embedded scenarios)
#ifndef RINGBUF_THREAD_SAFE
#define RINGBUF_THREAD_SAFE 0
#endif

// Whether statistics are enabled
#ifndef RINGBUF_ENABLE_STATS
#define RINGBUF_ENABLE_STATS 0
#endif

#endif // RING_CONFIG_H
```

Then the implementation file conditionally compiles based on these macros:

```c
// ring_buffer.c fragment — a conditional compilation example
#include "ring_config.h"

#if RINGBUF_THREAD_SAFE
#include <pthread.h>
#endif

struct RingBuffer {
    uint8_t* data;
    size_t   capacity;
    size_t   head;
    size_t   tail;
    size_t   count;
#if RINGBUF_THREAD_SAFE
    pthread_mutex_t lock;
#endif
#if RINGBUF_ENABLE_STATS
    size_t total_pushed;
    size_t total_popped;
#endif
};

bool ringbuf_push(RingBuffer* rb, uint8_t data) {
    if (!rb || rb->count == rb->capacity) return false;

#if RINGBUF_THREAD_SAFE
    pthread_mutex_lock(&rb->lock);
#endif

    rb->data[rb->head] = data;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;

#if RINGBUF_ENABLE_STATS
    rb->total_pushed++;
#endif

#if RINGBUF_THREAD_SAFE
    pthread_mutex_unlock(&rb->lock);
#endif

    return true;
}
```

This style is extremely common in the embedded world. Through conditional compilation, one codebase can serve both resource-constrained microcontrollers (turn off unneeded features to save Flash and RAM) and full-featured Linux environments.

Note one key detail: do not hardcode compile-time configuration macros directly in the `.c` file. Put them in a separate `ring_config.h`, and wrap each macro in `#ifndef ... #endif`. That way users can override the defaults through compiler options (`-DRINGBUF_DEFAULT_CAPACITY=512`) without modifying the source code.

### Runtime Configuration: Dynamic Flexibility

Runtime configuration is delivered through function parameters or configuration structs; it suits parameters that are only determined at program startup, or that may change while running. The `UartConfig` struct in the UART driver earlier is a typical runtime configuration.

When should you use compile-time configuration, and when runtime? A rough rule of thumb: **in embedded environments, parameters that require a re-flash when changed belong to compile-time configuration, while parameters that may differ across devices or scenarios belong to runtime configuration**. For example, if your product ships in several models with different baud rates, the baud rate should be runtime configuration; but if a module's data buffer size is fixed across the whole product line, compile-time configuration is the better fit.

Do not nest conditional compilation too deeply. If you catch yourself writing three or more levels of `#if ... #endif`, readability collapses fast. A better approach: split differently-configured code into different helper functions, then use a single level of conditional compilation to choose which function to call.

## Step 5 — Nail Cross-Platform Portability with a Platform Abstraction Layer

The core technique for getting code to run on multiple platforms is introducing a Platform Abstraction Layer. The principle is simple: **isolate all platform-specific code in one place, and let upper-layer code call only the abstract interface**. Picture a universal charger—whether your phone is USB-C or Lightning, you plug in an adapter and it charges; that adapter is the "platform abstraction layer".

Suppose our ring buffer needs a fixed-size static array on embedded platforms (no `malloc` there), while on a PC dynamic allocation works fine. We first define a set of platform interfaces:

```c
// platform.h — the platform abstraction layer
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

// Memory allocation interface
void* platform_alloc(size_t size);
void  platform_free(void* ptr);

// Mutex interface (for thread safety)
typedef struct PlatformMutex PlatformMutex;
PlatformMutex* platform_mutex_create(void);
void           platform_mutex_lock(PlatformMutex* mtx);
void           platform_mutex_unlock(PlatformMutex* mtx);
void           platform_mutex_destroy(PlatformMutex* mtx);

#endif // PLATFORM_H
```

Then provide different implementations for different platforms. First the Linux version:

```c
// platform_linux.c — the Linux implementation
#include "platform.h"
#include <stdlib.h>
#include <pthread.h>

void* platform_alloc(size_t size) {
    return malloc(size);
}

void platform_free(void* ptr) {
    free(ptr);
}

struct PlatformMutex {
    pthread_mutex_t mtx;
};

PlatformMutex* platform_mutex_create(void) {
    PlatformMutex* m = (PlatformMutex*)malloc(sizeof(PlatformMutex));
    if (m) pthread_mutex_init(&m->mtx, NULL);
    return m;
}

void platform_mutex_lock(PlatformMutex* mtx) {
    if (mtx) pthread_mutex_lock(&mtx->mtx);
}

void platform_mutex_unlock(PlatformMutex* mtx) {
    if (mtx) pthread_mutex_unlock(&mtx->mtx);
}

void platform_mutex_destroy(PlatformMutex* mtx) {
    if (mtx) {
        pthread_mutex_destroy(&mtx->mtx);
        free(mtx);
    }
}
```

Now the bare-metal version:

```c
// platform_bare_metal.c — bare-metal implementation (STM32/ESP32, etc.)
#include "platform.h"

// On bare metal, a static memory pool replaces malloc
#define kPlatformHeapSize 4096
static uint8_t s_heap[kPlatformHeapSize];
static size_t  s_heap_offset = 0;

void* platform_alloc(size_t size) {
    // A crude bump allocator, for demonstration only
    if (s_heap_offset + size > kPlatformHeapSize) return NULL;
    void* ptr = &s_heap[s_heap_offset];
    s_heap_offset += size;
    // Note: this allocator does not support free
    return ptr;
}

void platform_free(void* ptr) {
    (void)ptr;  // the bump allocator does not support freeing
}

// On bare metal, disabling interrupts replaces the mutex
struct PlatformMutex {
    int irq_state;
};

PlatformMutex* platform_mutex_create(void) {
    return (PlatformMutex*)platform_alloc(sizeof(PlatformMutex));
}

void platform_mutex_lock(PlatformMutex* mtx) {
    // Real code needs the concrete MCU API
    // mtx->irq_state = __disable_irq();
}

void platform_mutex_unlock(PlatformMutex* mtx) {
    // __restore_irq(mtx->irq_state);
}

void platform_mutex_destroy(PlatformMutex* mtx) {
    // the bump allocator does not support freeing
}
```

With a platform abstraction layer in place, the ring buffer code no longer needs to care what platform it runs on—`platform_alloc` calls `malloc` on Linux and carves memory from a static pool on STM32; `platform_mutex_lock` uses `pthread_mutex` on Linux and disables interrupts on bare metal. When porting to a new platform, you write one new `platform_xxx.c` and leave the core business logic untouched, line for line.

Cross-platform code has one more classic type trap: the sizes of fundamental types can differ across platforms. `int` may be 16 bits on an 8-bit MCU and 32 bits on a 32-bit platform; `long` is 64 bits on 64-bit Linux but 32 bits on Windows. So cross-platform code should uniformly use the fixed-width types defined in `<stdint.h>`: `uint8_t`, `uint16_t`, `uint32_t`, `size_t`, and so on.

## Step 6 — Evolve the API Without Breaking It

Once your module is used by multiple projects, API stability becomes a problem you must take seriously. Rename a function or add a parameter, and every caller has to follow along—and if the callers are outside your control, that is a disaster.

### Embedding Version Numbers

One simple approach is to define version macros in the header and provide a runtime query interface:

```c
// ring_buffer.h fragment
#define RINGBUF_VERSION_MAJOR 1
#define RINGBUF_VERSION_MINOR 2
#define RINGBUF_VERSION_PATCH 0

const char* ringbuf_version(void);
```

```c
// ring_buffer.c fragment
const char* ringbuf_version(void) {
    return "1.2.0";
}
```

### The "Add, Don't Modify" Strategy

When adding new features, implement them by adding new functions rather than modifying the signatures of existing ones. Say your ring buffer originally supported only `uint8_t` and now needs multi-byte data: do not change `ringbuf_push`'s parameter type from `uint8_t` to `void*`—that breaks every existing caller. The right move is to add a new family of functions:

```c
// The existing API stays untouched
bool ringbuf_push(RingBuffer* rb, uint8_t data);
bool ringbuf_pop(RingBuffer* rb, uint8_t* out);

// New: multi-byte operations
bool   ringbuf_write(RingBuffer* rb, const void* data, size_t len);
size_t ringbuf_read(RingBuffer* rb, void* buf, size_t buf_size);
```

If an old interface genuinely must be retired, first mark it with a macro to give users a migration grace period:

```c
// Marking an interface as deprecated
#ifdef __GNUC__
#define RINGBUF_DEPRECATED \
    __attribute__((deprecated("use ringbuf_write instead")))
#elif defined(_MSC_VER)
#define RINGBUF_DEPRECATED \
    __declspec(deprecated("use ringbuf_write instead"))
#else
#define RINGBUF_DEPRECATED
#endif

RINGBUF_DEPRECATED bool ringbuf_push_batch(RingBuffer* rb,
                                            const uint8_t* data,
                                            size_t len);
```

## Bridging to C++

All the modularization tricks we wrestled with in C have stronger native support in C++. Understanding the C approach helps us understand the design motives and underlying mechanics of the C++ toolset—none of C++'s "new features" was invented out of thin air; each is an engineered upgrade of a manual C practice.

| Manual C Practice | Native C++ Support | What It Improves |
|-------------------|--------------------|------------------|
| File-level `static` functions | `private`/`protected` members | Compiler-enforced access control, not self-discipline |
| Name prefixes (`ringbuf_`, `uart_`) | `namespace` | Genuine namespace isolation, no hand-written prefixes |
| The opaque pointer pattern | The PIMPL idiom + `unique_ptr` | Automatic memory management, no manual create/destroy |
| `#include` + `#ifndef` guards | C++20 Modules | Eliminates macro pollution, repeated parsing, and fragile dependency ordering |
| `typedef` | `using` + `auto` | More intuitive type aliases, automatic type deduction |
| Hand-written `deprecated` macros | The `[[deprecated]]` attribute | A standardized deprecation marker |

### namespace and class in Place of Header Partitioning

C uses files and name prefixes for logical partitioning; C++ uses `namespace` to provide genuine namespace isolation, and `class` access control replaces manually separating header and source files:

```cpp
// In C++, modularization is a language-level feature
namespace uart {

class Driver {
public:
    // Public interface — equivalent to the function declarations in a .h
    explicit Driver(const Config& config);
    ~Driver();

    Result send(const uint8_t* data, size_t len);
    Result receive(uint8_t* buf, size_t buf_size, size_t& received);

private:
    // Private implementation — equivalent to the static functions and internal variables in a .c
    struct Impl;
    Impl* pimpl_;
};

} // namespace uart
```

### The Pimpl Idiom — A Compile-Time Firewall

PIMPL (Pointer to Implementation) is the C++ edition of C's opaque pointer, but in C++ it has an extra important use: **reducing header dependencies to speed up compilation**. In a large C++ project, modifying one header can trigger the recompilation of hundreds of source files. If the private members' definitions are all hidden inside `Impl`, the header needs only a forward declaration `struct Impl;`, so modifying private members affects only the `.cpp` file and causes no mass recompilation.

```cpp
// network_client.h
#include <string>
#include <memory>

class NetworkClient {
public:
    explicit NetworkClient(const std::string& host, uint16_t port);
    ~NetworkClient();

    bool connect();
    void disconnect();
    bool send(const std::string& message);

private:
    struct Impl;  // forward declaration
    std::unique_ptr<Impl> pimpl_;
};

// network_client.cpp
#include "network_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

struct NetworkClient::Impl {
    int sockfd = -1;
    std::string host;
    uint16_t port;

    bool connect() { /* calls the socket API ... */ return true; }
    void disconnect() { if (sockfd >= 0) close(sockfd); }
};

NetworkClient::NetworkClient(const std::string& host, uint16_t port)
    : pimpl_(std::make_unique<Impl>()) {
    pimpl_->host = host;
    pimpl_->port = port;
}

// The destructor must be defined in the .cpp, because this is where Impl becomes complete
NetworkClient::~NetworkClient() = default;

bool NetworkClient::connect()    { return pimpl_->connect(); }
void NetworkClient::disconnect() { pimpl_->disconnect(); }
```

Note that the destructor must be defined in the `.cpp` file (or `= default`), not in the header—because inside the header `Impl` is an incomplete type, and `unique_ptr`'s destructor needs `Impl`'s complete definition to delete it correctly.

### C++20 Modules

C++20 introduced the Modules system, aiming to fundamentally replace the header-file `#include` mechanism. Modules directly solve many of the header system's built-in problems—macro pollution, repeated parsing, fragile dependency ordering. That said, honestly, as of late 2024 mainstream compiler support is still evolving rapidly, and adopting modules in large projects takes a fair amount of migration work. Still, it is worth knowing as a trend; we will not expand on it here (a later advanced C++ volume covers it in detail).

## Exercises

### Exercise 1: An Opaque-Pointer Stack Module

**Difficulty: Advanced** · Continues this article's ring_buffer module with a different data structure

Following the opaque-pointer style of this article's ring_buffer, implement an opaque-pointer stack module. The header exposes only `typedef struct Stack Stack;` and the interface functions; the internal structure hides in the .c file:

```c
// stack.h
typedef struct Stack Stack;
Stack* stack_create(size_t capacity);
void   stack_destroy(Stack* s);
int    stack_push(Stack* s, int value);   // returns -1 when full
int    stack_pop(Stack* s);                // what to do when empty is up to you
size_t stack_size(const Stack* s);
```

Think it over: why does the header only write `typedef struct Stack Stack;` instead of expanding the struct definition? Once a caller holds the pointer, can they access members directly with `s->top`?

### Exercise 2: Platform Abstraction Layer in Practice

**Difficulty: Advanced** · Two backends for the same interface

Design a platform abstraction layer for this article's reusable module (or the stack from Exercise 1 above), replacing the direct dependence on `malloc`/`free`:

```c
// pal.h
void* pal_alloc(size_t size);
void  pal_free(void* ptr);
```

Implement two versions: one using the standard library's `malloc`/`free` (suited to PCs), and one using a static memory pool (suited to bare-metal embedded). The module's .c file allocates memory by including `pal.h`.

### Exercise 3: An Opaque-Pointer String Hash Table (Challenge, Optional)

**Difficulty: Challenge** · Optional; requires self-studying chained hash tables; beginners may skip

Implement a string-to-integer hash table behind an opaque pointer (internally: separate chaining over an array of linked lists, `djb2` hash function). We suggest finishing the linked list in Advanced Topics 06 first, then coming back to implement the chaining structure for collision handling.

```c
typedef struct HashMap HashMap;
HashMap* hashmap_create(size_t bucket_count);
void     hashmap_destroy(HashMap* map);
int      hashmap_insert(HashMap* map, const char* key, int value);
int      hashmap_lookup(const HashMap* map, const char* key, int* out);
int      hashmap_remove(HashMap* map, const char* key);
```

## References

- [Opaque pointer pattern - Wikipedia](https://en.wikipedia.org/wiki/Opaque_pointer)
- [Linux Kernel Coding Style - Chapter 5: Typedefs](https://www.kernel.org/doc/html/latest/process/coding-style.html#typedefs)
- [PIMPL idiom - cppreference](https://en.cppreference.com/w/cpp/language/pimpl)
- [C++20 modules - cppreference](https://en.cppreference.com/w/cpp/language/modules)
