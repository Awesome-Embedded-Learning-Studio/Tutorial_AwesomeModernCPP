---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 深入理解 C 语言的动态内存分配机制，掌握 malloc/calloc/realloc/free 的正确使用，认识常见内存错误及调试方法，对比
  C++ RAII 和智能指针的设计哲学
difficulty: intermediate
order: 18
platform: host
prerequisites:
- 结构体与内存对齐
reading_time_minutes: 9
tags:
- host
- cpp-modern
- intermediate
- 进阶
- 内存管理
title: 动态内存管理
---
# 动态内存管理

到目前为止我们写的所有程序，变量的大小都在编译期就确定了。但现实世界不是这么运转的——用户输入多少字符事先不知道、运行之前不知道会采集多少条记录、客户端发来的数据包大小可能每次都不同。这些场景的共同点是：**程序运行之前，你无法确定需要多少内存**。

C 语言解决这个问题的手段就是动态内存管理——在程序运行的时候，向系统申请一块指定大小的内存，用完之后再还回去。这组 API 看起来只有四个函数：`malloc`、`calloc`、`realloc`、`free`，学起来十分钟就够。但用对它们是一回事，用不崩是另一回事——内存泄漏、悬垂指针、双重释放、越界写入，每一个都能让你的程序莫名其妙地崩溃。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 画出程序的内存布局图，说明 text/rodata/data/bss/heap/stack 各段的职责
> - [ ] 正确使用 `malloc`/`calloc`/`realloc`/`free` 并处理错误
> - [ ] 识别并避免五种常见内存错误
> - [ ] 使用 Valgrind 和 AddressSanitizer 检测内存问题
> - [ ] 理解 RAII 和智能指针如何解决 C 手动管理的痛点

## 环境说明

我们接下来的所有实验都在这个环境下进行：

- 平台：Linux x86\_64（WSL2 也可以）
- 编译器：GCC 13+ 或 Clang 17+
- 编译选项：`-Wall -Wextra -std=c17`

## 第一步——搞清楚程序在内存中长什么样

当一个可执行文件被加载器放进内存开始运行的时候，操作系统会为它分配一段虚拟地址空间，这段空间被划分为几个功能不同的区域：

```text
高地址
┌──────────────────┐
│    内核空间       │  （用户态不可访问）
├──────────────────┤
│    栈 (stack)    │  ← 向低地址增长
│        ↓         │
│                  │
│     （空闲）      │
│                  │
│        ↑         │
│    堆 (heap)     │  ← 向高地址增长
├──────────────────┤
│  BSS 段 (.bss)   │  未初始化全局/static
├──────────────────┤
│  数据段 (.data)   │  已初始化全局/static
├──────────────────┤
│  只读段 (.rodata) │  const 全局、字符串字面量
├──────────────────┤
│  代码段 (.text)   │  机器指令（只读、可执行）
└──────────────────┘
低地址
```

**代码段**（.text）存放编译后的机器指令，通常是只读的。**只读数据段**（.rodata）存放 `const` 全局变量和字符串字面量。**已初始化数据段**（.data）存放定义时有非零初始值的全局和 `static` 变量。**BSS 段**（.bss）存放未初始化或初始化为零的全局和 `static` 变量——关键区别是 `.bss` 不占用可执行文件空间，只记录"需要 N 字节清零"。**堆**是动态内存分配发生的地方，`malloc` 申请的内存来自这里。**栈**用于函数调用，存储局部变量和返回地址。

## 第二步——掌握 malloc/calloc/realloc/free

栈的管理完全是自动的——函数调用时分配栈帧，返回时自动回收。速度极快（移动一个寄存器），但有大小限制（Linux 默认 8MB），且内存只在当前函数执行期间有效。

堆的管理权交给程序员。灵活但必须自己管理——忘了释放就泄漏，释放两次就崩溃。实际项目中以下场景需要用堆：数据量编译期无法确定、数据生命周期跨越函数调用、数据量太大不适合放栈。

## malloc——给我一块内存

```c
void* malloc(size_t size);
```

`malloc` 接受想要分配的字节数，返回 `void*` 指针。一个基本的例子：

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int* numbers = malloc(10 * sizeof(*numbers));

    if (numbers == NULL) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    for (int i = 0; i < 10; i++) {
        numbers[i] = i * i;
    }

    free(numbers);
    return 0;
}
```

关键点：写成 `sizeof(*numbers)` 而不是 `sizeof(int)`，这样改指针类型时分配大小自动跟着变。**每次 malloc 后立刻检查 NULL** 是铁律。`malloc` 分配的内存内容是**未初始化的**——读到的是垃圾值。

## calloc——分配并清零

```c
void* calloc(size_t num, size_t size);
```

`calloc` 分配内存并**全部清零**。当你需要零初始化的结构体或数组时用它更安全。`calloc` 还能检测参数乘法溢出，比 `malloc(num * size)` 多一层保护。

## realloc——扩容（可能搬家）

```c
void* realloc(void* ptr, size_t new_size);
```

`realloc` 用于调整已分配内存的大小。它在原地扩展或找新空间搬家。

⚠️ **最经典的坑**：`realloc` 可能返回 `NULL`（内存不足），但原来的指针仍然有效。如果你直接写 `ptr = realloc(ptr, new_size)`，一旦返回 `NULL`，原来的 `ptr` 就丢失了——内存泄漏。正确做法：

```c
int* temp = realloc(numbers, 20 * sizeof(int));
if (temp == NULL) {
    free(numbers);
    return 1;
}
numbers = temp;  // 成功了才更新指针
```

## free——有借有还

```c
void free(void* ptr);
```

`free` 的注意事项比它看起来要多：只能 free 由分配函数返回的指针；free 之后指针变成悬垂指针；**free 后置 NULL 是好习惯**——后续误用会立刻段错误，比 use-after-free 好调试一万倍。

```c
free(numbers);
numbers = NULL;
```

## 第三步——认识五种常见内存错误

### 1. 内存泄漏

分配了忘记释放。更隐蔽的场景是重新赋值指针前没释放旧内存（"覆盖泄漏"），或错误处理分支里忘记释放。

### 2. 悬垂指针 / Use After Free

指向已释放内存的指针被继续使用。这种错误不一定立刻崩溃——那块内存可能还没被分配给别人，数据"看起来"有效，但完全不可靠。

### 3. 双重释放

对同一块内存调用两次 `free`。堆管理器的内部数据结构被破坏，可能引发立即崩溃，也可能延迟到很久以后才发作。

### 4. 缓冲区越界

向分配的内存区域之外写入，破坏相邻内存块的元数据或其他数据。off-by-one 错误是典型原因。

### 5. 未初始化读取

`malloc` 分配的内存内容不确定。未赋值就读取，读到的是垃圾值。

## 调试工具

### Valgrind

Linux 上最经典的内存调试工具，能检测泄漏、非法读写、未初始化读取、双重释放。不需要重新编译，直接在程序前面加 `valgrind`：

```bash
gcc -g -o demo demo.c
valgrind --leak-check=full ./demo
```

### AddressSanitizer (ASan)

编译器内置的内存错误检测工具，性能开销比 Valgrind 小得多：

```bash
gcc -fsanitize=address -g -o demo demo.c
./demo
```

推荐在开发和测试阶段始终开启 ASan。

## C++ 衔接——RAII 如何终结手动管理的噩梦

### RAII 的核心思想

把资源的生命周期绑定到对象的生命周期上。构造函数获取资源，析构函数释放资源。对象离开作用域时析构函数一定会被调用（即使发生异常），资源一定会被正确释放。

### 智能指针三剑客

`std::unique_ptr`——独占所有权，不可复制但可移动。离开作用域自动释放。推荐用 `std::make_unique` 创建。

`std::shared_ptr`——共享所有权+引用计数。最后一个 `shared_ptr` 被销毁时释放内存。推荐用 `std::make_shared` 创建。

`std::weak_ptr`——不增加引用计数，用于打破 `shared_ptr` 之间的循环引用。

### 标准库容器

`std::vector` 替代手动 malloc 的动态数组，`std::string` 替代手动 malloc 的字符串缓冲区。在现代 C++ 中，你几乎不需要直接使用 `new`/`delete`，更不用说 `malloc`/`free` 了。

## 小结

我们从内存布局讲起，理清了栈和堆各自的角色，逐一拆解了四个动态内存函数的语义和陷阱，总结了五种最常见的内存错误，最后对比了 C++ 的 RAII 和智能指针。动态内存管理是 C 语言中最容易出错的领域之一，但掌握了正确的方法论和工具之后，大部分错误都是可以避免的。

## 练习

### 练习 1：用 realloc 实现动态增长数组

**难度：基础** · 用 realloc 扩容，承接本篇 realloc 讲解

实现一个简单的动态整数数组：初始容量 4，每次 `push_back` 在满时用 `realloc` 把容量翻倍。

```c
#include <stddef.h>

typedef struct {
    int*   data;
    size_t size;
    size_t capacity;
} IntVec;

/// @brief 初始化，初始容量 4
void intvec_init(IntVec* v);
/// @brief 尾部追加；满时扩容翻倍，失败返回 -1
int  intvec_push(IntVec* v, int value);
/// @brief 释放
void intvec_free(IntVec* v);
```

提示：`realloc(NULL, n)` 等价于 `malloc(n)`，所以第一次分配也能走 realloc。注意 `realloc` 失败时返回 NULL 且不会释放旧块——拿一个临时变量接住返回值，确认成功后再覆盖原指针，否则会泄漏。

::: details 参考答案

```c
#include <stdlib.h>

void intvec_init(IntVec* v) {
    v->capacity = 4;
    v->size = 0;
    v->data = malloc(v->capacity * sizeof(int));
}

int intvec_push(IntVec* v, int value) {
    if (v->size >= v->capacity) {
        size_t new_cap = v->capacity * 2;
        int* new_data = realloc(v->data, new_cap * sizeof(int));
        if (new_data == NULL) {
            return -1;          // 扩容失败，旧 data 仍然有效，调用者决定怎么办
        }
        v->data = new_data;
        v->capacity = new_cap;
    }
    v->data[v->size++] = value;
    return 0;
}

void intvec_free(IntVec* v) {
    free(v->data);
    v->data = NULL;
    v->size = v->capacity = 0;
}
```

关键是 `realloc` 的返回值先存到 `new_data`、判断成功后再赋给 `v->data`。要是直接写 `v->data = realloc(v->data, ...)`，一旦失败原来的 `v->data` 就被 NULL 覆盖，那块内存再也找不回来，就是泄漏。

:::

### 练习 2：内存错误诊断

**难度：进阶** · 用 ASan 或 Valgrind 识别本篇讲过的内存错误

下面这段代码藏着至少四种内存错误。先读代码猜每一处有什么问题，再用 `gcc -fsanitize=address`（或 Valgrind）跑一遍，把工具报出的错误类型和位置记下来：

```c
#include <stdlib.h>

int main(void) {
    int* p = malloc(4 * sizeof(int));
    p[4] = 42;            // (1) 这里有什么问题？

    int* q = malloc(sizeof(int));
    free(q);
    *q = 100;             // (2) 这里呢？

    int* r = malloc(1024);
    /* 忘了 free(r) */   // (3) 这又是哪一类？

    free(p);
    free(p);              // (4) 再来一个
    return 0;
}
```

要求：对每一处，写出它属于本篇讲的哪一类错误（越界写、释放后使用、泄漏、双重释放、未初始化读取），并说明工具是怎么报的。

::: details 参考答案

(1) `p[4]`：只分配了 4 个 `int`（下标 0–3），`p[4]` 是堆缓冲区越界写。ASan 报 `heap-buffer-overflow`。

(2) `*q = 100`：`q` 已经 free 又去写，是释放后使用。ASan 报 `heap-use-after-free`。

(3) `r` 没 free：内存泄漏。Valgrind 的 `LEAK SUMMARY` 会列出来；ASan 在多数平台上默认也做泄漏检查（`detect_leaks=1`），退出时报 `Detected memory leaks`。

(4) `free(p)` 两次：双重释放。ASan 报 `attempting double-free`。

这四类正好对应本篇讲的那几种典型内存错误，工具的报错关键词能帮你快速定位是哪一类。

:::

### 练习 3：固定大小内存池分配器（挑战·可选）

**难度：挑战** · 可选，需要自学空闲链表（free list）惯用法

实现一个固定大小内存池：从一块大内存里切出固定大小的块，用链表管理空闲块——每个空闲块的前几个字节存指向下一个空闲块的指针。建议先查资料弄懂"in-place 链表 / free list"是怎么回事再来写。

```c
typedef struct MemoryPool MemoryPool;
MemoryPool* pool_create(size_t block_size, size_t block_count);
void*       pool_alloc(MemoryPool* pool);
void        pool_free(MemoryPool* pool, void* block);
void        pool_destroy(MemoryPool* pool);
```

想一下：内存池相比直接 `malloc`/`free`，在嵌入式或实时系统里有什么好处？为什么它能做到 O(1) 分配释放、且不产生碎片？

### 练习 4：带统计的 malloc/free 包装器（挑战·可选）

**难度：挑战** · 可选，建议学完第 15 章预处理器后再做

包装 `malloc`/`free`，记录每次分配的文件名和行号，程序退出时打印还没释放的清单。需要用到 `__FILE__`/`__LINE__` 宏（第 15 章才讲）和 `atexit` 注册退出钩子。

```c
#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)
```

提示：用一个数组或链表记录每次分配的地址、大小、位置；`free` 时按地址匹配并标记已释放；`atexit(mem_report)` 注册退出时打印剩余未释放项。
