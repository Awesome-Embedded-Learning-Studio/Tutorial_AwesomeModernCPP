---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 从模块化设计、头文件接口、不透明指针到平台抽象层，系统掌握 C 代码的工程化组织方法，以及 C++ 的 namespace/class/PIMPL
  如何继承这些思想
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
title: 构建可复用的C代码
---
# 构建可复用的C代码

写过几万行 C 代码的朋友大概都有这种体验——项目刚开始的时候一切都好，几个 `.c` 文件拼拼凑凑就能跑起来，但随着功能越堆越多，代码开始变成一团乱麻：头文件到处乱 include，全局变量满天飞，改一个结构体字段要牵动十几个源文件重新编译，好不容易在 PC 上调通了，换到 STM32 上又是一堆移植问题。说实话，这些痛苦的根源往往不是算法写错了或者指针用炸了，而是从一开始就没有认真对待"代码组织"这件事。

其他语言是怎么处理这个问题的呢？Java 有 `package` 和 `interface`，Rust 有 `mod` 和 `trait`，Python 有 `__init__.py` 和命名约定——它们都在语言层面提供了模块化的基础设施。C 语言呢？C 语言什么都没有。没有 namespace，没有 class，没有 access control，没有 module system。C 语言给我们的是预处理器的 `#include` 和 `#ifndef`，外加一大堆需要我们自己遵守的纪律。

但这并不意味着 C 语言写不出干净的模块化代码——只是我们需要用手动的手段来达到别的语言自动帮你做到的事情。理解这些手动技巧非常重要，因为 C++ 的 `namespace`、`class` 访问控制、PIMPL 惯用法乃至 C++20 的 Modules，全都是对 C 语言这些手工实践的工程化升级。搞明白了 C 的做法，你才能真正理解 C++ 为什么这么设计。

这篇文章我们就来系统梳理这套方法论——从模块化设计原则、头文件接口设计、不透明指针隐藏实现，到配置管理和跨平台移植。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 理解模块化设计的核心原则，将功能拆分为独立的编译单元
> - [ ] 编写干净的头文件接口，做到"头文件里只有声明，没有实现"
> - [ ] 使用不透明指针模式隐藏实现细节
> - [ ] 区分编译期配置和运行时配置的使用场景
> - [ ] 编写平台抽象层实现跨平台移植
> - [ ] 管理 API 版本兼容性

## 环境说明

本文所有代码示例均可在标准 C 环境下编译运行。C++ 衔接部分使用 C++17 标准。建议始终带上 `-Wall -Wextra` 编译选项来捕捉潜在问题。

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
依赖：pthread（线程安全示例需要，Linux 默认提供）
```

## 第一步——搞清楚什么是"好的模块"

在讲具体技术之前，我们需要先把"模块化"这个概念说清楚。很多朋友以为模块化就是把代码拆成多个 `.c` 文件——这只是形式上的拆分，不等于真正的模块化。你可以把它想象成整理工具箱：把所有工具一股脑扔进一个大抽屉里叫"拆分"（物理上确实分开了，但找起来还是费劲），而给每个抽屉贴标签、规定"这个抽屉只放扳手、那个抽屉只放螺丝刀"，这才是模块化。真正的模块化要满足一个核心原则：**每个模块都是一个独立的、可替换的、接口清晰的编译单元**。

好的模块长什么样？假设我们正在写一个 UART 驱动模块。头文件只暴露调用者需要知道的类型和函数，实现细节全部藏在 `.c` 文件里，内部使用的函数全部加上 `static`，模块之间的依赖通过头文件的 include 关系清晰体现。这样做的好处是：当你需要把 UART 驱动从 STM32F1 移植到 ESP32 的时候，只需要替换对应的 `.c` 实现，调用者代码一行都不用改。

一个模块的文件组织通常是这样的：

```text
uart_driver/
  ├── uart_driver.h    // 公开接口：类型声明、函数声明、文档注释
  ├── uart_driver.c    // 私有实现：结构体完整定义、静态函数、内部变量
  └── uart_config.h    // 配置参数（可选，编译期配置用）
```

这个结构看起来很简单，但魔鬼在细节里。接下来我们逐个拆解。

## 第二步——设计干净的头文件接口

头文件是一个模块和外部世界的唯一契约，所以它必须干净、稳定、自包含。所谓"自包含"是指：使用者 `#include` 你的头文件之后，不需要再手动 include 任何其他东西就能编译通过。

### 头文件保护和 include 原则

头文件保护是基本功——用 `#ifndef`/`#define`/`#endif` 或者 `#pragma once`（主流编译器都支持）都行。更重要的是 include 的原则：头文件里应该只 include 它直接依赖的东西。如果你的头文件里用到了 `size_t`，那就 `#include <stddef.h>`；如果用到了 `uint32_t`，那就 `#include <stdint.h>`。千万不要依赖"调用者肯定已经 include 过了"这种假设——那是在给自己挖坑。

我们来写一个干净的头文件示例：

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

你可能会注意到 `#ifdef __cplusplus` 这两行。这不是 C++ 代码，但加上 `extern "C"` 是一个好习惯——它确保这个头文件被 C++ 代码 include 的时候，链接器能正确找到这些 C 风格的函数。很多知名的 C 库（SQLite、libcurl、zlib）都这么做。

### 头文件里绝对不该出现的东西

有几样东西绝对不应该出现在公开头文件里。`static` 函数的定义放在头文件里意味着每个 include 它的编译单元都会得到一份副本，不仅浪费空间还容易导致奇怪的链接问题。宏定义的内部常量和实现用的类型也是同理——任何以"下划线开头"或者名字里带 "internal"、"priv" 的东西，都不应该出现在公开头文件里。

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

> ⚠️ **踩坑预警**
> 把结构体完整定义暴露在头文件里，调用者迟早会忍不住直接访问内部字段。一旦你修改了结构体布局，所有 include 这个头文件的源文件都要重新编译——在大型项目中，这可能是几分钟的编译时间。更糟的是，调用者可能已经在依赖你的内部实现了，你想改都改不了。

## 第三步——用不透明指针把实现藏起来

上篇指针进阶里我们已经见过不完整类型和 opaque pointer 的基本用法，现在我们把它放进模块化设计的语境里重新审视。不透明指针是 C 语言中最强大的信息隐藏工具——你可以把它理解为面向对象语言里 `private` 关键字的 C 语言版。调用者只知道"有这么个东西"，但不知道它里面长什么样，只能通过你提供的函数来操作它。

### 完整的模块示例：环形缓冲区

我们来写一个完整的环形缓冲区模块，把头文件设计、不透明指针、错误处理全部串起来。先看头文件——这是调用者唯一需要 include 的东西：

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

头文件里我们没有暴露 `RingBuffer` 的内部结构——`typedef struct RingBuffer RingBuffer;` 只是前向声明加 typedef。调用者只能拿到一个 `RingBuffer*` 指针，然后通过我们提供的函数来操作它。他们不知道缓冲区是用数组实现的还是用链表实现的，什么都不知道——这就对了。

接下来是实现文件。注意结构体的完整定义只出现在这里：

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

写完之后我们验证一下：

```text
$ gcc -Wall -Wextra -std=c11 -c ring_buffer.c -o ring_buffer.o
(无输出 = 编译成功，无警告无错误)
```

我们来写一个简单的测试来确认行为正确：

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

这里有几个值得注意的设计决策。所有公开函数的第一件事都是检查 `rb` 参数是否为 `NULL`——因为 C 语言没有异常机制，我们能做的就是在入口处拦截空指针，避免在函数深处触发段错误。`const RingBuffer*` 出现在查询函数的参数里，这是在向调用者承诺：这个函数不会修改缓冲区的状态。

> ⚠️ **踩坑预警**
> opaque pointer 模式有一个常见的翻车场景：调用者拿到 `NULL`（比如 `ringbuf_create` 因为内存不足返回了 `NULL`），然后不检查就直接调用 `ringbuf_push(rb, data)`。虽然我们的实现里每个函数都做了 NULL 检查，但不要指望所有库都这么做。养成检查返回值的习惯——特别是涉及内存分配的函数。

这种 opaque pointer 模式的威力在于：如果将来我们想把环形缓冲区从动态分配改成静态数组，或者加上线程安全，或者换成 power-of-2 优化（用位运算代替取模），我们只需要修改 `ring_buffer.c`，所有调用者代码完全不用动，甚至不用重新编译——只要头文件的接口签名不变。

## 第四步——学会管理配置参数

模块化做到一定程度之后，我们会发现有些参数需要根据具体的使用场景来调整——缓冲区大小、超时时间、线程安全开关等等。这些参数的管理方式大致可以分为两类：编译期配置和运行时配置。

### 编译期配置：零开销的灵活性

编译期配置通过宏定义或配置头文件来实现，适用于那些在编译时就确定的、不会在运行过程中改变的参数。好处是零运行时开销——编译器可以直接把常量内联到代码里，甚至做常量折叠优化。

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

然后在实现文件里根据这些宏做条件编译：

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

这种写法在嵌入式领域非常常见。通过条件编译，同一套代码可以适配资源受限的单片机（关掉不需要的功能省 Flash 和 RAM）和功能丰富的 Linux 环境。

注意一个关键细节：编译期配置的宏定义不要直接硬编码在 `.c` 文件里，而是放到单独的 `ring_config.h` 中，并且每个宏都用 `#ifndef ... #endif` 包裹。这样用户可以通过编译选项（`-DRINGBUF_DEFAULT_CAPACITY=512`）来覆盖默认值，而不需要修改源代码。

### 运行时配置：动态灵活性

运行时配置通过函数参数或配置结构体来传递，适用于那些在程序启动时才确定或者运行过程中可能变化的参数。前面 UART 驱动里的 `UartConfig` 结构体就是典型的运行时配置。

什么时候用编译期配置，什么时候用运行时配置？有一个大致的原则：**嵌入式环境中，"改了就要重新烧录"的参数用编译期配置，"不同设备或不同场景可能不同"的参数用运行时配置**。比如你的产品有多个型号，波特率各不相同，那波特率就应该是运行时配置；但如果某个模块的数据缓冲区大小在整个产品线里都是固定的，那编译期配置更合适。

> ⚠️ **踩坑预警**
> 不要把条件编译嵌套得太深。如果你发现自己写了三层以上的 `#if ... #endif`，代码的可读性会急剧下降。更好的做法是把不同配置的代码拆分到不同的辅助函数里，用一层条件编译来选择调用哪个函数。

## 第五步——用平台抽象层搞定跨平台

让代码在多个平台上跑起来，最核心的技术是引入平台抽象层（Platform Abstraction Layer）。原理很简单：**把所有平台相关的代码隔离到一处，上层代码只调用抽象接口**。你可以把它想象成万能充电器——不管你的手机是 USB-C 还是 Lightning，插上转接头就能充，转接头就是那个"平台抽象层"。

假设我们的环形缓冲区在嵌入式平台上需要用固定大小的静态数组（没有 `malloc`），在 PC 上用动态分配就行。我们先定义一组平台接口：

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

然后针对不同平台提供不同的实现。先看 Linux 版本：

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

再看裸机版本：

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

有了平台抽象层之后，环形缓冲区的代码就完全不需要关心自己跑在什么平台上了——`platform_alloc` 在 Linux 上调用 `malloc`，在 STM32 上从静态内存池分配；`platform_mutex_lock` 在 Linux 上用 `pthread_mutex`，在裸机上用关中断。移植到新平台的时候，只需要写一个新的 `platform_xxx.c`，核心业务逻辑一行不改。

跨平台代码还有一个常见的类型陷阱：基本类型的大小在不同平台上可能不同。`int` 在 8 位单片机上可能是 16 位的，在 32 位平台上是 32 位的，`long` 在 64 位 Linux 上是 64 位的但在 Windows 上是 32 位的。所以跨平台代码应该统一使用 `<stdint.h>` 里定义的定宽类型：`uint8_t`、`uint16_t`、`uint32_t`、`size_t` 等等。

## 第六步——让 API 稳定地演进

当你的模块被多个项目使用的时候，API 的稳定性就成了一个必须认真对待的问题。改个函数名或者加个参数，所有调用者都要跟着改——如果调用者不是你能控制的，那就是一场灾难。

### 版本号嵌入

一个简单的做法是在头文件里定义版本号宏，并在运行时提供查询接口：

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

### "只增不改"策略

添加新功能的时候，尽量通过添加新函数来实现，而不是修改现有函数的签名。比如你的环形缓冲区原来只支持 `uint8_t`，现在要支持多字节数据，不要把 `ringbuf_push` 的参数类型从 `uint8_t` 改成 `void*`——这会破坏所有现有调用者。正确的做法是新增一组函数：

```c
// 原有 API 保持不变
bool ringbuf_push(RingBuffer* rb, uint8_t data);
bool ringbuf_pop(RingBuffer* rb, uint8_t* out);

// 新增：多字节操作
bool   ringbuf_write(RingBuffer* rb, const void* data, size_t len);
size_t ringbuf_read(RingBuffer* rb, void* buf, size_t buf_size);
```

如果某个旧接口确实需要废弃，可以先用宏标记它，给使用者一个迁移的缓冲期：

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

## C++ 衔接

我们在 C 里折腾的这些模块化技巧，在 C++ 中都有更强大的原生支持。理解 C 的做法有助于我们理解 C++ 工具的设计动机和底层机制——C++ 的每一个"新特性"都不是凭空发明的，它们是对 C 语言手工实践的工程化升级。

| C 手工实践 | C++ 原生支持 | 改进了什么 |
|-----------|-------------|-----------|
| 文件级 `static` 函数 | `private`/`protected` 成员 | 编译器强制访问控制，不靠自觉 |
| 命名前缀（`ringbuf_`、`uart_`） | `namespace` | 真正的命名空间隔离，不用手写前缀 |
| opaque pointer 模式 | PIMPL 惯用法 + `unique_ptr` | 自动内存管理，不用手动 create/destroy |
| `#include` + `#ifndef` 保护 | C++20 Modules | 消除宏污染、重复解析、依赖顺序脆弱 |
| `typedef` | `using` + `auto` | 更直观的类型别名，自动类型推导 |
| 手写 `deprecated` 宏 | `[[deprecated]]` 属性 | 标准化的废弃标记 |

### namespace 与 class 替代头文件分区

C 语言用文件和命名前缀来做逻辑分区，C++ 用 `namespace` 提供真正的命名空间隔离，用 `class` 的访问控制替代手动分离头文件和源文件：

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

### Pimpl 惯用法——编译期防火墙

PIMPL（Pointer to Implementation）就是 C 语言 opaque pointer 的 C++ 版本，但它在 C++ 里有一个额外的重要用途：**减少头文件依赖，加速编译**。在大型 C++ 项目中，修改一个头文件可能触发几百个源文件重新编译。如果私有成员的定义都藏在 `Impl` 里，头文件只需要一个前向声明 `struct Impl;`，那么修改私有成员只影响 `.cpp` 文件，不会导致大量重新编译。

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

注意析构函数必须在 `.cpp` 文件里定义（或者 `= default`），不能在头文件里——因为头文件里 `Impl` 是不完整类型，`unique_ptr` 的析构需要知道 `Impl` 的完整定义才能正确 delete。

### C++20 模块系统

C++20 引入了 Modules 系统，旨在从根本上替代头文件的 `#include` 机制。模块直接解决了头文件的许多固有问题——宏污染、重复解析、依赖顺序脆弱。不过说实话，截至 2024 年底，主流编译器对模块的支持还在快速演进中，大型项目采用模块还需要不少迁移工作。但作为趋势了解是值得的，这里先不展开（后续 C++ 进阶卷会专门讲解）。

## 练习

### 练习 1：不透明指针的字符串哈希表

实现一个简单的字符串-整数映射表，使用不透明指针隐藏内部实现。要求：

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

提示：内部可以用一个简单的链表数组（拉链法）来实现哈希表。哈希函数可以用经典的 `djb2` 算法。记住所有内部类型和辅助函数都要藏在 `.c` 文件里。

### 练习 2：平台抽象层实践

为上面练习 1 的哈希表写一个平台抽象层，替换掉标准库的 `malloc`/`free`。要求：

```c
// pal.h — 平台抽象层接口
#ifndef PAL_H
#define PAL_H

#include <stddef.h>

void* pal_alloc(size_t size);
void  pal_free(void* ptr);

#endif // PAL_H
```

请分别实现两个版本：一个使用标准库 `malloc`/`free`（适合 PC），另一个使用静态内存池（适合嵌入式裸机环境）。哈希表的 `.c` 文件应该通过包含 `pal.h` 来分配内存，而不是直接调用 `malloc`。

## 参考资源

- [Opaque Pointer 模式 - Wikipedia](https://en.wikipedia.org/wiki/Opaque_pointer)
- [Linux Kernel Coding Style - Chapter 5: Typedefs](https://www.kernel.org/doc/html/latest/process/coding-style.html#typedefs)
- [PIMPL 惯用法 - cppreference](https://en.cppreference.com/w/cpp/language/pimpl)
- [C++20 模块 - cppreference](https://en.cppreference.com/w/cpp/language/modules)
