---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: From modular design, header file interfaces, and opaque pointers to platform
  abstraction layers, systematically master the engineering organization methods of
  C code, and how C++ namespaces, classes, and PIMPL inherit these ideas.
difficulty: intermediate
order: 108
platform: host
prerequisites:
- 指针进阶：不完整类型与多级指针
- 结构体与内存布局
- 编译与链接基础
reading_time_minutes: 32
tags:
- host
- cpp-modern
- intermediate
- 工程实践
- 基础
title: Building Reusable C Code
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/08-reusable-c-code.md
  source_hash: 58d1b4309b15042b8c7c0c93c8439713afff651f916d93ebe5daa0e7b1008e53
  translated_at: '2026-04-20T03:57:07.783070+00:00'
  engine: anthropic
  token_count: 4667
---
# Building Reusable C Code

Anyone who has written tens of thousands of lines of C code has probably experienced this—at the start of a project, everything is fine; a few `.c` files cobbled together are enough to get things running. But as features pile up, the code turns into a tangled mess: header files include each other indiscriminately, global variables are everywhere, changing a single struct field triggers recompilation of a dozen source files, and just when you finally get it working on a PC, porting it to an STM32 brings a whole new set of problems. Honestly, the root cause of this pain usually isn't a flawed algorithm or a blown-up pointer—it's failing to take "code organization" seriously from day one.

How do other languages handle this? Java has `package` and `interface`, Rust has `mod` and `trait`, Python has `__init__.py` and naming conventions—they all provide modularization infrastructure at the language level. What about C? C has nothing. No namespaces, no classes, no access control, no module system. What C gives us is the preprocessor's `#include` and `#ifndef`, plus a lot of discipline we have to enforce ourselves.

But this doesn't mean you can't write clean, modular code in C—it just means we need to use manual techniques to achieve what other languages do automatically. Understanding these manual techniques is crucial, because C++'s `namespace`, `class` access control, the PIMPL idiom, and even C++20 Modules are all engineering upgrades built on top of these C practices. Once you understand the C approach, you'll truly grasp why C++ is designed the way it is.

In this article, we'll systematically walk through this methodology—from modular design principles and header file interface design, to hiding implementations with opaque pointers, configuration management, and cross-platform porting.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the core principles of modular design and split functionality into independent compilation units
> - [ ] Write clean header file interfaces, ensuring "declarations only, no implementations in headers"
> - [ ] Use the opaque pointer pattern to hide implementation details
> - [ ] Distinguish between use cases for compile-time and runtime configuration
> - [ ] Write a platform abstraction layer for cross-platform porting
> - [ ] Manage API version compatibility

## Environment Setup

All code examples in this article can be compiled and run in a standard C environment. The C++ bridging sections use the C++17 standard. We recommend always enabling the `-Wall -Wextra` compiler flag to catch potential issues.

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
依赖：pthread（线程安全示例需要，Linux 默认提供）
```

## Step One—Understand What Makes a "Good Module"

Before diving into specific techniques, we need to clarify what "modularization" actually means. Many people think modularization simply means splitting code into multiple `.c` files—but that's just physical separation, not true modularization. Think of it like organizing a toolbox: dumping all your tools into separate drawers is "splitting" (they're physically separated, but still hard to find), whereas labeling each drawer and enforcing "this drawer is only for wrenches, that one is only for screwdrivers"—that's modularization. True modularization must satisfy one core principle: **every module is an independent, replaceable compilation unit with a clear interface**.

What does a good module look like? Suppose we're writing a UART driver module. The header file only exposes the types and functions the caller needs to know about, all implementation details are hidden in the `.c` file, all internal helper functions are prefixed with `static`, and dependencies between modules are clearly reflected through header include relationships. The benefit of this approach is: when you need to port the UART driver from an STM32F1 to an ESP32, you only need to swap out the corresponding `.c` implementation—the caller's code doesn't need to change at all.

A module's file organization typically looks like this:

```text
uart_driver/
  ├── uart_driver.h    // 公开接口：类型声明、函数声明、文档注释
  ├── uart_driver.c    // 私有实现：结构体完整定义、静态函数、内部变量
  └── uart_config.h    // 配置参数（可选，编译期配置用）
```

This structure looks simple, but the devil is in the details. Let's break it down piece by piece.

## Step Two—Design Clean Header File Interfaces

A header file is the sole contract between a module and the outside world, so it must be clean, stable, and self-contained. "Self-contained" means: after a user `#include`s your header file, they don't need to manually include anything else for the code to compile.

### Header Guards and Include Principles

Header guards are fundamental—you can use `#ifndef`/`#define`/`#endif` or `#pragma once` (supported by all mainstream compilers). What matters more is the include principle: a header file should only include what it directly depends on. If your header uses `size_t`, then `#include <stddef.h>`; if it uses `uint32_t`, then `#include <stdint.h>`. Never rely on the assumption that "the caller must have already included it"—that's just digging a hole for yourself.

Let's write a clean header file example:

```c
// uart_driver.h — 一个干净的头文件示例
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明，不暴露内部结构
typedef struct UartDriver UartDriver;

// 错误码
typedef enum {
    kUartOk       = 0,
    kUartErrParam = -1,
    kUartErrBusy  = -2,
    kUartErrIo    = -3
} UartResult;

// 配置结构体——调用者需要知道的东西
typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
} UartConfig;

// 生命周期管理
UartDriver* uart_create(const UartConfig* config);
void        uart_destroy(UartDriver* drv);

// 数据操作
UartResult uart_send(UartDriver* drv, const uint8_t* data, size_t len);
UartResult uart_receive(UartDriver* drv, uint8_t* buf, size_t buf_size,
                        size_t* received);

#ifdef __cplusplus
}
#endif

#endif // UART_DRIVER_H
```

You might notice these two lines: `#ifdef __cplusplus`. This isn't C++ code, but adding `extern "C"` is a good practice—it ensures that when this header is included by C++ code, the linker can correctly find these C-style functions. Many well-known C libraries (SQLite, libcurl, zlib) do this.

### What Absolutely Should Not Appear in Header Files

There are a few things that should never appear in public header files. Putting the definition of an `static` function in a header file means every compilation unit that includes it gets its own copy, which not only wastes space but also easily leads to strange linking issues. The same goes for internal constants defined as macros and implementation-specific types—anything starting with an underscore or containing "internal" or "priv" in its name should not appear in a public header file.

```c
// 千万别这么干——公开头文件里放内部实现细节
#ifndef BAD_MODULE_H
#define BAD_MODULE_H

#define INTERNAL_BUFFER_SIZE 256      // 不该暴露
#define MAGIC_NUMBER 0xDEADBEEF       // 不该暴露

// 把完整结构体暴露出来了——调用者可以直接访问字段
typedef struct {
    uint8_t buffer[256];              // 内部缓冲区，不该让调用者看到
    int head;                         // 内部状态
    int tail;
    int count;
} BadQueue;

void bad_queue_push(BadQueue* q, uint8_t val);
static void internal_helper(void) {   // 每个编译单元一份副本！
    // ...
}

#endif
```

> ⚠️ **Pitfall Warning**
> Exposing the full struct definition in a header file means callers will eventually be tempted to directly access internal fields. Once you modify the struct layout, all source files that include this header must be recompiled—in a large project, this could mean several minutes of compilation time. Even worse, callers might have already come to depend on your internal implementation, making it impossible to change.

## Step Three—Hide Implementations with Opaque Pointers

In the previous pointer deep dive, we saw the basic usage of incomplete types and opaque pointers. Now let's re-examine them in the context of modular design. The opaque pointer is the most powerful information-hiding tool in C—you can think of it as the C equivalent of the `private` keyword in object-oriented languages. The caller only knows "this thing exists" but has no idea what it looks like inside, and can only manipulate it through the functions you provide.

### Complete Module Example: Ring Buffer

Let's write a complete ring buffer module that ties together header file design, opaque pointers, and error handling. First, the header file—this is the only thing the caller needs to include:

```c
// ring_buffer.h — 环形缓冲区公开接口
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 不透明类型——调用者拿到的只是个指针
typedef struct RingBuffer RingBuffer;

// 创建与销毁
RingBuffer* ringbuf_create(size_t capacity);
void        ringbuf_destroy(RingBuffer* rb);

// 数据操作
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

In the header file, we don't expose the internal structure of `RingBuffer`—`typedef struct RingBuffer RingBuffer;` is just a forward declaration plus a typedef. The caller only gets a `RingBuffer*` pointer and can manipulate it through the functions we provide. They don't know whether the buffer is implemented with an array or a linked list—they know nothing, and that's exactly right.

Next is the implementation file. Note that the full struct definition only appears here:

```c
// ring_buffer.c — 环形缓冲区实现
#include "ring_buffer.h"
#include <stdlib.h>

// 完整的结构体定义只出现在 .c 文件里
struct RingBuffer {
    uint8_t* data;      // 动态分配的缓冲区
    size_t   capacity;   // 总容量
    size_t   head;       // 写入位置
    size_t   tail;       // 读取位置
    size_t   count;      // 当前元素数量
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

After writing it, let's verify it works:

```text
$ gcc -Wall -Wextra -std=c11 -c ring_buffer.c -o ring_buffer.o
(无输出 = 编译成功，无警告无错误)
```

Let's write a simple test to confirm the behavior is correct:

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

There are a few noteworthy design decisions here. The first thing every public function does is check whether the `rb` parameter is `NULL`—because C has no exception mechanism, the best we can do is intercept null pointers at the entry point to avoid triggering a segfault deep inside the function. `const RingBuffer*` appears in the parameters of query functions, which is a promise to the caller: this function will not modify the buffer's state.

> ⚠️ **Pitfall Warning**
> The opaque pointer pattern has a common failure scenario: the caller gets an `NULL` (for example, `ringbuf_create` returns `NULL` due to insufficient memory) and then calls `ringbuf_push(rb, data)` without checking. Although our implementation does NULL checks in every function, don't assume all libraries do this. Make it a habit to check return values—especially for functions involving memory allocation.

The power of this opaque pointer pattern lies in the fact that if we later want to change the ring buffer from dynamic allocation to a static array, add thread safety, or switch to a power-of-2 optimization (using bitwise operations instead of modulo), we only need to modify `ring_buffer.c`. All caller code remains completely untouched, and doesn't even need to be recompiled—as long as the interface signatures in the header file don't change.

## Step Four—Learn to Manage Configuration Parameters

Once modularization reaches a certain point, we'll find that some parameters need to be adjusted based on specific use cases—buffer sizes, timeout durations, thread safety toggles, and so on. The management of these parameters generally falls into two categories: compile-time configuration and runtime configuration.

### Compile-Time Configuration: Zero-Overhead Flexibility

Compile-time configuration is implemented through macro definitions or configuration header files, and is suitable for parameters that are determined at compile time and won't change during runtime. The benefit is zero runtime overhead—the compiler can inline constants directly into the code and even perform constant folding optimizations.

```c
// ring_config.h — 编译期配置
#ifndef RING_CONFIG_H
#define RING_CONFIG_H

// 默认缓冲区容量，可通过编译选项覆盖
// 用法: -DRINGBUF_DEFAULT_CAPACITY=512
#ifndef RINGBUF_DEFAULT_CAPACITY
#define RINGBUF_DEFAULT_CAPACITY 256
#endif

// 是否启用线程安全（嵌入式单线程场景可以关闭）
#ifndef RINGBUF_THREAD_SAFE
#define RINGBUF_THREAD_SAFE 0
#endif

// 是否启用统计功能
#ifndef RINGBUF_ENABLE_STATS
#define RINGBUF_ENABLE_STATS 0
#endif

#endif // RING_CONFIG_H
```

Then, in the implementation file, we use conditional compilation based on these macros:

```c
// ring_buffer.c 片段 — 条件编译示例
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

This pattern is extremely common in the embedded world. Through conditional compilation, the same codebase can adapt to resource-constrained MCUs (disabling unnecessary features to save Flash and RAM) and feature-rich Linux environments.

Note a key detail: don't hardcode compile-time configuration macros directly in the `.c` file. Instead, put them in a separate `ring_config.h`, and wrap each macro with `#ifndef ... #endif`. This allows users to override default values via compiler flags (`-DRINGBUF_DEFAULT_CAPACITY=512`) without modifying the source code.

### Runtime Configuration: Dynamic Flexibility

Runtime configuration is passed through function parameters or configuration structs, and is suitable for parameters that are only determined at program startup or might change during execution. The `UartConfig` struct in the UART driver example earlier is a typical runtime configuration.

When should you use compile-time configuration versus runtime configuration? There's a rough guideline: **in embedded environments, use compile-time configuration for parameters that "require re-flashing to change," and use runtime configuration for parameters that "might differ across devices or scenarios."** For example, if your product has multiple models with different baud rates, the baud rate should be a runtime configuration; but if a module's data buffer size is fixed across the entire product line, compile-time configuration is more appropriate.

> ⚠️ **Pitfall Warning**
> Don't nest conditional compilation too deeply. If you find yourself writing three or more levels of `#if ... #endif`, code readability will plummet. A better approach is to split differently configured code into separate helper functions, using a single level of conditional compilation to choose which function to call.

## Step Five—Handle Cross-Platform with a Platform Abstraction Layer

The core technique for making code run on multiple platforms is introducing a Platform Abstraction Layer. The principle is simple: **isolate all platform-specific code in one place, and have upper-layer code only call abstract interfaces**. Think of it like a universal charger—whether your phone uses USB-C or Lightning, plugging in the adapter lets you charge, and that adapter is the "platform abstraction layer."

Suppose our ring buffer needs to use a fixed-size static array on embedded platforms (no `malloc`), while on a PC, dynamic allocation is fine. We first define a set of platform interfaces:

```c
// platform.h — 平台抽象层
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

// 内存分配接口
void* platform_alloc(size_t size);
void  platform_free(void* ptr);

// 互斥锁接口（用于线程安全）
typedef struct PlatformMutex PlatformMutex;
PlatformMutex* platform_mutex_create(void);
void           platform_mutex_lock(PlatformMutex* mtx);
void           platform_mutex_unlock(PlatformMutex* mtx);
void           platform_mutex_destroy(PlatformMutex* mtx);

#endif // PLATFORM_H
```

Then we provide different implementations for different platforms. First, the Linux version:

```c
// platform_linux.c — Linux 实现
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

Next, the bare-metal version:

```c
// platform_bare_metal.c — 裸机实现（STM32/ESP32 等）
#include "platform.h"

// 裸机环境下用静态内存池代替 malloc
#define kPlatformHeapSize 4096
static uint8_t s_heap[kPlatformHeapSize];
static size_t  s_heap_offset = 0;

void* platform_alloc(size_t size) {
    // 简陋的 bump allocator，仅供演示
    if (s_heap_offset + size > kPlatformHeapSize) return NULL;
    void* ptr = &s_heap[s_heap_offset];
    s_heap_offset += size;
    // 注意：这个 allocator 不支持 free
    return ptr;
}

void platform_free(void* ptr) {
    (void)ptr;  // bump allocator 不支持释放
}

// 裸机环境下用关中断代替互斥锁
struct PlatformMutex {
    int irq_state;
};

PlatformMutex* platform_mutex_create(void) {
    return (PlatformMutex*)platform_alloc(sizeof(PlatformMutex));
}

void platform_mutex_lock(PlatformMutex* mtx) {
    // 实际要用具体的 MCU API
    // mtx->irq_state = __disable_irq();
}

void platform_mutex_unlock(PlatformMutex* mtx) {
    // __restore_irq(mtx->irq_state);
}

void platform_mutex_destroy(PlatformMutex* mtx) {
    // bump allocator 不支持释放
}
```

With the platform abstraction layer in place, the ring buffer code doesn't need to care at all about what platform it's running on—`platform_alloc` calls `malloc` on Linux and allocates from a static memory pool on STM32; `platform_mutex_lock` uses `pthread_mutex` on Linux and disables interrupts on bare metal. When porting to a new platform, you only need to write a new `platform_xxx.c`, and the core business logic doesn't change a single line.

Cross-platform code also has a common type trap: the size of fundamental types may differ across platforms. `int` might be 16-bit on an 8-bit MCU but 32-bit on a 32-bit platform, and `long` is 64-bit on 64-bit Linux but 32-bit on Windows. Therefore, cross-platform code should uniformly use the fixed-width types defined in `<stdint.h>`: `uint8_t`, `uint16_t`, `uint32_t`, `size_t`, and so on.

## Step Six—Evolve Your API Stably

When your module is used by multiple projects, API stability becomes an issue you must take seriously. Changing a function name or adding a parameter means all callers have to follow suit—and if you don't control those callers, that's a disaster.

### Embedding Version Numbers

A simple approach is to define version number macros in the header file and provide a runtime query interface:

```c
// ring_buffer.h 片段
#define RINGBUF_VERSION_MAJOR 1
#define RINGBUF_VERSION_MINOR 2
#define RINGBUF_VERSION_PATCH 0

const char* ringbuf_version(void);
```

```c
// ring_buffer.c 片段
const char* ringbuf_version(void) {
    return "1.2.0";
}
```

### The "Add-Only, Don't-Modify" Strategy

When adding new features, try to do so by adding new functions rather than modifying existing function signatures. For example, if your ring buffer originally only supported `uint8_t` and now needs to support multi-byte data, don't change the parameter type of `ringbuf_push` from `uint8_t` to `void*`—that would break all existing callers. The correct approach is to add a new set of functions:

```c
// 原有 API 保持不变
bool ringbuf_push(RingBuffer* rb, uint8_t data);
bool ringbuf_pop(RingBuffer* rb, uint8_t* out);

// 新增：多字节操作
bool   ringbuf_write(RingBuffer* rb, const void* data, size_t len);
size_t ringbuf_read(RingBuffer* rb, void* buf, size_t buf_size);
```

If an old interface truly needs to be deprecated, you can first mark it with a macro to give users a transition buffer period:

```c
// 标记废弃接口
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

The modularization techniques we've labored over in C all have more powerful native support in C++. Understanding the C approach helps us grasp the design motivations and underlying mechanisms of C++ tools—every "new feature" in C++ wasn't invented out of thin air; they are engineering upgrades built on top of C's manual practices.

| C Manual Practice | C++ Native Support | What It Improves |
|-----------|-------------|-----------|
| File-level `static` functions | `private`/`protected` members | Compiler-enforced access control, no reliance on discipline |
| Naming prefixes (`ringbuf_`, `uart_`) | `namespace` | True namespace isolation, no need to manually write prefixes |
| opaque pointer pattern | PIMPL idiom + `unique_ptr` | Automatic memory management, no manual create/destroy |
| `#include` + `#ifndef` guards | C++20 Modules | Eliminates macro pollution, redundant parsing, and fragile dependency order |
| `typedef` | `using` + `auto` | More intuitive type aliases, automatic type deduction |
| Hand-written `deprecated` macros | `[[deprecated]]` attributes | Standardized deprecation marking |

### Replacing Header Partitioning with Namespaces and Classes

C uses files and naming prefixes for logical partitioning, while C++ uses `namespace` for true namespace isolation and `class`'s access control to replace the manual separation of header and source files:

```cpp
// C++ 里，模块化是语言级别的功能
namespace uart {

class Driver {
public:
    // 公开接口——相当于 .h 里的函数声明
    explicit Driver(const Config& config);
    ~Driver();

    Result send(const uint8_t* data, size_t len);
    Result receive(uint8_t* buf, size_t buf_size, size_t& received);

private:
    // 私有实现——相当于 .c 里的 static 函数和内部变量
    struct Impl;
    Impl* pimpl_;
};

} // namespace uart
```

### The Pimpl Idiom—A Compile-Time Firewall

PIMPL (Pointer to Implementation) is the C++ version of C's opaque pointer, but it has an additional important use case in C++: **reducing header file dependencies and speeding up compilation**. In large C++ projects, modifying a single header file can trigger the recompilation of hundreds of source files. If the definitions of private members are hidden in the `Impl`, and the header only needs a forward declaration `struct Impl;`, then modifying private members only affects the `.cpp` file and won't cause massive recompilation.

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
    struct Impl;  // 前向声明
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

    bool connect() { /* 调用 socket API ... */ return true; }
    void disconnect() { if (sockfd >= 0) close(sockfd); }
};

NetworkClient::NetworkClient(const std::string& host, uint16_t port)
    : pimpl_(std::make_unique<Impl>()) {
    pimpl_->host = host;
    pimpl_->port = port;
}

// 析构函数必须在 .cpp 里定义，因为 Impl 在这里才完整
NetworkClient::~NetworkClient() = default;

bool NetworkClient::connect()    { return pimpl_->connect(); }
void NetworkClient::disconnect() { pimpl_->disconnect(); }
```

Note that the destructor must be defined in the `.cpp` file (or `= default`), not in the header file—because `Impl` is an incomplete type in the header file, and `unique_ptr`'s destructor needs to know the full definition of `Impl` to correctly delete it.

### The C++20 Module System

C++20 introduced the Modules system, designed to fundamentally replace the header file's `#include` mechanism. Modules directly address many inherent problems of header files—macro pollution, redundant parsing, and fragile dependency order. That said, as of late 2024, mainstream compiler support for modules is still rapidly evolving, and adopting modules in large projects requires significant migration effort. But it's worth understanding as a trend, and we won't dive deeper here (the upcoming advanced C++ volume will cover this in detail).

## Exercises

### Exercise 1: String Hash Table with Opaque Pointers

Implement a simple string-to-integer mapping table using opaque pointers to hide the internal implementation. Requirements:

```c
// hashmap.h — 你需要编写的公开接口
#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>

typedef struct HashMap HashMap;

HashMap* hashmap_create(size_t bucket_count);
void     hashmap_destroy(HashMap* map);

/// 插入键值对，如果 key 已存在则覆盖旧值
/// @return 0 表示成功，非零表示失败
int hashmap_insert(HashMap* map, const char* key, int value);

/// 查找 key 对应的值，通过 out 返回
/// @return 0 表示找到，非零表示不存在
int hashmap_lookup(const HashMap* map, const char* key, int* out);

/// 删除指定 key
/// @return 0 表示成功删除，非零表示 key 不存在
int hashmap_remove(HashMap* map, const char* key);

#endif // HASHMAP_H
```

Hint: Internally, you can implement the hash table using a simple array of linked lists (chaining). For the hash function, you can use the classic `djb2` algorithm. Remember that all internal types and helper functions must be hidden in the `.c` file.

### Exercise 2: Platform Abstraction Layer Practice

Write a platform abstraction layer for the hash table from Exercise 1 above, replacing the standard library's `malloc`/`free`. Requirements:

```c
// pal.h — 平台抽象层接口
#ifndef PAL_H
#define PAL_H

#include <stddef.h>

void* pal_alloc(size_t size);
void  pal_free(void* ptr);

#endif // PAL_H
```

Please implement two versions: one using the standard library's `malloc`/`free` (suitable for PC), and another using a static memory pool (suitable for embedded bare-metal environments). The hash table's `.c` file should allocate memory by including `pal.h`, rather than calling `malloc` directly.

## References

- [Opaque Pointer Pattern - Wikipedia](https://en.wikipedia.org/wiki/Opaque_pointer)
- [Linux Kernel Coding Style - Chapter 5: Typedefs](https://www.kernel.org/doc/html/latest/process/coding-style.html#typedefs)
- [PIMPL Idiom - cppreference](https://en.cppreference.com/w/cpp/language/pimpl)
- [C++20 Modules - cppreference](https://en.cppreference.com/w/cpp/language/modules)
