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
reading_time_minutes: 35
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
gcc -std=c17 -Wall -Wextra -Wpedantic -g -o demo demo.c
valgrind --leak-check=full ./demo
```

### AddressSanitizer (ASan)

编译器内置的内存错误检测工具，性能开销比 Valgrind 小得多：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic -fsanitize=address -g -o demo demo.c
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

/// @brief 初始化，初始容量 4；失败返回 -1
int  intvec_init(IntVec* v);
/// @brief 尾部追加；满时扩容翻倍，失败返回 -1
int  intvec_push(IntVec* v, int value);
/// @brief 释放
void intvec_free(IntVec* v);
```

提示：`realloc(NULL, n)` 等价于 `malloc(n)`，所以第一次分配也能走 realloc。注意 `realloc` 失败时返回 NULL 且不会释放旧块——拿一个临时变量接住返回值，确认成功后再覆盖原指针，否则会泄漏。

::: details 参考答案

**intvec_test.c**

```c
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int* data;
    size_t size;
    size_t capacity;
} IntVec;

static int intvec_init(IntVec* vector)
{
    if (vector == NULL) {
        return -1;
    }

    vector->capacity = 4U; // 初始容量
    vector->size = 0U;
    vector->data = malloc(vector->capacity * sizeof(*vector->data));
    if (vector->data == NULL) {
        vector->capacity = 0U;
        return -1;
    }
    return 0;
}

static int intvec_push(IntVec* vector, int value)
{
    if (vector == NULL) {
        return -1;
    }

    if (vector->size == vector->capacity) {
        size_t new_capacity = vector->capacity * 2U;
        int* new_data = realloc(vector->data,
                                new_capacity * sizeof(*vector->data)); //容量不足翻倍扩充

        if (new_data == NULL) {
            return -1;
        }
        vector->data = new_data;
        vector->capacity = new_capacity;
    }

    vector->data[vector->size++] = value;
    return 0;
}

static void intvec_free(IntVec* vector)
{
    if (vector == NULL) {
        return;
    }

    free(vector->data);
    vector->data = NULL;
    vector->size = 0U;
    vector->capacity = 0U;
}

int main(void)
{
    IntVec vector;
    size_t i;

    if (intvec_init(&vector) != 0) {
        fputs("初始化失败\n", stderr);
        return 1;
    }
    printf("初始容量 = %zu\n", vector.capacity);

    for (i = 0U; i < 10U; ++i) {
        if (intvec_push(&vector, (int)i * 10) != 0) {
            fputs("扩容失败\n", stderr);
            intvec_free(&vector);
            return 1;
        }
        printf("push %d -> size=%zu cap=%zu\n", (int)i * 10, vector.size,
               vector.capacity);
    }

    printf("数组内容: ");
    for (i = 0U; i < vector.size; ++i) {
        printf("%d ", vector.data[i]);
    }
    putchar('\n');

    intvec_free(&vector);
    printf("释放后 data 是否为空: %s\n", vector.data == NULL ? "是" : "否");
    return 0;
}
```

关键是 `realloc` 的返回值先存到 `new_data`、判断成功后再赋给 `vector->data`。要是直接写 `vector->data = realloc(vector->data, ...)`，一旦失败原来的指针会被 `NULL` 覆盖，那块内存再也找不回来，就是泄漏。

编译运行：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic intvec_test.c -o intvec_test && ./intvec_test
```

运行结果：

```text
初始容量 = 4
push 0 -> size=1 cap=4
push 10 -> size=2 cap=4
push 20 -> size=3 cap=4
push 30 -> size=4 cap=4
push 40 -> size=5 cap=8
push 50 -> size=6 cap=8
push 60 -> size=7 cap=8
push 70 -> size=8 cap=8
push 80 -> size=9 cap=16
push 90 -> size=10 cap=16
数组内容: 0 10 20 30 40 50 60 70 80 90 
释放后 data 是否为空: 是
```

可以看到 `size` 到 5 时容量从 4 翻倍到 8，再到 8 成 16；`realloc` 扩容后旧元素被无损保留。`intvec_free` 会把结构体成员 `data` 置为 `NULL`，避免它继续保留悬垂地址；释放后仍然不能解引用该成员，应该先重新初始化或检查状态。

:::

### 练习 2：内存错误诊断

**难度：进阶** · 用 ASan 或 Valgrind 识别本篇讲过的内存错误

下面这段代码藏着至少四种内存错误。先读代码猜每一处有什么问题，再用 `gcc -std=c17 -Wall -Wextra -Wpedantic -fsanitize=address`（或 Valgrind）跑一遍，把工具报出的错误类型和位置记下来：

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

下面两份代码是互相独立的替代实现，各自包含 `main`；请任选一份编译，不要把两份代码拼到同一个源文件。

::: details 参考答案一：静态容量版（适合嵌入式）

这份实现不调用 `malloc`，而是预先准备固定数量的池实例和内存块。它的优点是运行时不会受堆碎片或堆分配失败影响；代价是容量和实例数量写死。空闲链表仍放在每个空闲块的开头，不过静态字节存储中的指针值只能通过 `memcpy` 写入和读回：`pool_alloc` 因而仍是 O(1)，为检查异属地址和重复释放而扫描的 `pool_free` 是 O(block_count)。它适合资源上限明确的固件。

```c
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 单个内存块可容纳的最大字节数。 */
#define MEMORY_POOL_MAX_BLOCK_SIZE 64U
/* 单个内存池可管理的最大内存块数量。 */
#define MEMORY_POOL_MAX_BLOCK_COUNT 16U
/* 程序可同时创建的内存池实例最大数量。 */
#define MEMORY_POOL_MAX_INSTANCES 2U
/* 内存块按照 max_align_t 的对齐要求切分。 */
#define MEMORY_POOL_ALIGNMENT _Alignof(max_align_t)
/* 为每个内存池预留最坏情况下所需的存储空间。 */
#define MEMORY_POOL_MAX_BLOCK_STRIDE                                      \
    (((MEMORY_POOL_MAX_BLOCK_SIZE + MEMORY_POOL_ALIGNMENT - 1U) /         \
      MEMORY_POOL_ALIGNMENT) * MEMORY_POOL_ALIGNMENT)

/* 使用联合体确保整块池内存按 max_align_t 对齐。 */
typedef union {
    max_align_t alignment;
    unsigned char bytes[MEMORY_POOL_MAX_BLOCK_STRIDE *
                        MEMORY_POOL_MAX_BLOCK_COUNT];
} PoolStorage;

typedef struct MemoryPool MemoryPool;

struct MemoryPool {
    PoolStorage storage;
    unsigned char* free_list;
    size_t block_stride;
    size_t block_count;
    int active;
};

/* 使用静态实例代替运行时堆内存分配。 */
static MemoryPool memory_pools[MEMORY_POOL_MAX_INSTANCES];

/* 判断内存池指针是否指向本模块管理的静态内存池实例。 */
static int pool_is_managed(const MemoryPool* pool)
{
    size_t i;

    for (i = 0U; i < MEMORY_POOL_MAX_INSTANCES; ++i) {
        if (pool == &memory_pools[i]) {
            return 1;
        }
    }
    return 0;
}

/* 查找内存块在指定内存池中的下标；找到时将下标写入 index。 */
static int pool_block_index(const MemoryPool* pool, const void* block,
                            size_t* index)
{
    size_t i;

    if (pool == NULL || block == NULL || index == NULL) {
        return 0;
    }

    for (i = 0U; i < pool->block_count; ++i) {
        const void* current_block =
            (const void*)(pool->storage.bytes + i * pool->block_stride);

        if (block == current_block) {
            *index = i;
            return 1;
        }
    }

    return 0;
}

/* 将 next 的对象表示复制到空闲块开头，不把 bytes 当作指针对象解引用。 */
static void pool_write_next(unsigned char* block, unsigned char* next)
{
    memcpy(block, &next, sizeof(next));
}

/* 从空闲块开头读回曾由 pool_write_next 写入的指针对象表示。 */
static unsigned char* pool_read_next(const unsigned char* block)
{
    unsigned char* next;

    memcpy(&next, block, sizeof(next));
    return next;
}

/* 判断指定内存块是否已经位于空闲链表中。 */
static int pool_block_is_free(const MemoryPool* pool, const unsigned char* block)
{
    const unsigned char* current;

    if (pool == NULL || block == NULL) {
        return 0;
    }

    for (current = pool->free_list; current != NULL;
         current = pool_read_next(current)) {
        if (current == block) {
            return 1;
        }
    }

    return 0;
}

/*
 * 创建内存池。
 * block_size 为每个内存块的字节数，block_count 为内存块数量。
 * 创建成功返回内存池指针；参数超出上限或无空闲实例时返回 NULL。
 */
MemoryPool* pool_create(size_t block_size, size_t block_count)
{
    MemoryPool* pool;
    size_t block_stride;
    size_t i;

    if (block_size == 0U || block_size > MEMORY_POOL_MAX_BLOCK_SIZE ||
        block_count == 0U || block_count > MEMORY_POOL_MAX_BLOCK_COUNT) {
        return NULL;
    }

    block_stride = block_size;
    if (block_stride < sizeof(unsigned char*)) {
        block_stride = sizeof(unsigned char*);
    }
    block_stride = ((block_stride + MEMORY_POOL_ALIGNMENT - 1U) /
                    MEMORY_POOL_ALIGNMENT) * MEMORY_POOL_ALIGNMENT;

    for (i = 0U; i < MEMORY_POOL_MAX_INSTANCES; ++i) {
        pool = &memory_pools[i];
        if (!pool->active) {
            size_t block_index;

            pool->block_stride = block_stride;
            pool->block_count = block_count;
            for (block_index = 0U; block_index < block_count; ++block_index) {
                unsigned char* current_block =
                    pool->storage.bytes + block_index * block_stride;
                unsigned char* next_block = NULL;

                if (block_index + 1U < block_count) {
                    next_block = pool->storage.bytes +
                                 (block_index + 1U) * block_stride;
                }
                pool_write_next(current_block, next_block);
            }
            pool->free_list = pool->storage.bytes;
            pool->active = 1;
            return pool;
        }
    }

    return NULL;
}

/*
 * 从内存池中分配一个空闲内存块。
 * 分配成功返回内存块首地址；内存池无效或已满时返回 NULL。
 */
void* pool_alloc(MemoryPool* pool)
{
    unsigned char* block;

    if (!pool_is_managed(pool) || !pool->active) {
        return NULL;
    }

    block = pool->free_list;
    if (block == NULL) {
        return NULL;
    }

    pool->free_list = pool_read_next(block);
    return (void*)block;
}

/*
 * 释放由 pool_alloc 从指定内存池分配的内存块。
 * pool 或 block 无效、block 不属于该内存池或已释放时，函数直接返回。
 */
void pool_free(MemoryPool* pool, void* block)
{
    size_t index;
    unsigned char* released_block;

    if (!pool_is_managed(pool) || !pool->active ||
        !pool_block_index(pool, block, &index)) {
        return;
    }

    released_block = pool->storage.bytes + index * pool->block_stride;
    if (pool_block_is_free(pool, released_block)) {
        return;
    }

    pool_write_next(released_block, pool->free_list);
    pool->free_list = released_block;
}

/*
 * 销毁内存池并将其静态实例标记为空闲。
 * 内存池无效或已销毁时，函数直接返回。
 */
void pool_destroy(MemoryPool* pool)
{
    if (!pool_is_managed(pool) || !pool->active) {
        return;
    }

    pool->free_list = NULL;
    pool->block_stride = 0U;
    pool->block_count = 0U;
    pool->active = 0;
}

int main(void)
{
    enum { BLOCK_COUNT = 4 };
    MemoryPool* pool;
    void* blocks[BLOCK_COUNT];
    void* reused_block;
    int value;
    size_t i;

    pool = pool_create(sizeof(int), BLOCK_COUNT);
    if (pool == NULL) {
        fputs("内存池创建失败\n", stderr);
        return 1;
    }

    for (i = 0U; i < BLOCK_COUNT; ++i) {
        blocks[i] = pool_alloc(pool);
        if (blocks[i] == NULL) {
            fputs("内存池分配失败\n", stderr);
            pool_destroy(pool);
            return 1;
        }
        value = (int)(i + 1U) * 10;
        memcpy(blocks[i], &value, sizeof(value));
    }

    printf("当前数值：");
    for (i = 0U; i < BLOCK_COUNT; ++i) {
        memcpy(&value, blocks[i], sizeof(value));
        printf("%d%s", value, i + 1U == BLOCK_COUNT ? "\n" : " ");
    }

    if (pool_alloc(pool) == NULL) {
        puts("内存池已满，下一次分配返回 NULL");
    }

    pool_free(pool, blocks[1]);
    reused_block = pool_alloc(pool);
    if (reused_block == NULL || reused_block != blocks[1]) {
        fputs("内存池复用失败\n", stderr);
        pool_destroy(pool);
        return 1;
    }
    value = 99;
    memcpy(reused_block, &value, sizeof(value));
    memcpy(&value, reused_block, sizeof(value));
    printf("复用后的数值：%d\n", value);

    pool_destroy(pool);
    return 0;
}
```

`PoolStorage` 用联合体把整块 `bytes` 存储对齐到 `max_align_t`；又因为每块的起始偏移是该对齐值的整数倍，所以每块也满足同一对齐要求。C17 保证 `max_align_t` 的对齐要求不弱于任何标量类型，但它不承诺支持具有扩展对齐要求的任意类型。对齐只解决地址条件，不能把已经声明为字节数组的静态存储变成任意 `T` 对象：`pool_write_next` 和 `pool_read_next` 用 `memcpy` 记录空闲链表指针，`main` 也只用 `memcpy` 写入和读回 `int` 的对象表示，从不把 `bytes` 强转为指针后解引用。调用者复制的数据不得超过传入的 `block_size`，并且使用完后必须调用 `pool_free`；`pool_destroy` 只把静态实例标回空闲。旧指针本身不会因此悬垂，但它不再代表活动内存池，之前分配的块也必须停止使用。

> ⚠️ **踩坑预警**
> `bytes` 是已经声明为 `unsigned char[]` 的对象。C17 的有效类型规则不允许仅因地址已经对齐，就把它当作一个真实存在的 `int` 对象并通过 `int*` 读写；`max_align_t` 不能改变这块静态对象的声明类型。要读取示例中保存的整数，必须先把字节表示复制回一个真正声明为 `int` 的对象。
>
> `malloc` 不同：它返回的存储没有声明类型。在大小和对齐都满足的前提下，首次通过 `int*` 的非字符写入可以为那片已分配存储设定 `int` 的有效类型；静态 `unsigned char[]` 没有这项特权。

:::

::: details 参考答案二：动态 `malloc` + free list 版

```c
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* 内存块空闲时，块起始位置用于保存指向下一个空闲块的指针。 */
typedef struct PoolBlock {
    struct PoolBlock* next;
} PoolBlock;

typedef struct MemoryPool MemoryPool;

struct MemoryPool {
    unsigned char* storage; // malloc 获得的池底层存储区
    PoolBlock* free_list; // 空闲块链表的头指针
};

/*
 * 创建内存池。
 * block_size 为每个内存块的字节数，block_count 为内存块数量。
 * 创建成功返回内存池指针；参数为 0、大小计算溢出或 malloc 失败时返回 NULL。
 */
MemoryPool* pool_create(size_t block_size, size_t block_count)
{
    MemoryPool* pool;
    size_t alignment;
    size_t block_stride;
    size_t storage_size;
    size_t i;

    if (block_size == 0U || block_count == 0U) {
        return NULL;
    }

    if (block_size < sizeof(PoolBlock)) {
        block_size = sizeof(PoolBlock);
    }
    alignment = _Alignof(max_align_t);
    if (block_size > SIZE_MAX - (alignment - 1U)) {
        return NULL;
    }
    block_stride = ((block_size + alignment - 1U) / alignment) * alignment;
    if (block_count > SIZE_MAX / block_stride) {
        return NULL;
    }
    storage_size = block_stride * block_count;

    pool = malloc(sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }

    pool->storage = malloc(storage_size);
    if (pool->storage == NULL) {
        free(pool);
        return NULL;
    }

    pool->free_list = NULL;
    for (i = 0U; i < block_count; ++i) {
        PoolBlock* block = (PoolBlock*)(void*)(pool->storage +
                                               i * block_stride);

        block->next = pool->free_list;
        pool->free_list = block;
    }
    return pool;
}

/*
 * 从内存池中分配一个空闲内存块。
 * 分配成功返回内存块首地址；pool 为 NULL 或内存池已满时返回 NULL。
 */
void* pool_alloc(MemoryPool* pool)
{
    PoolBlock* block;

    if (pool == NULL) {
        return NULL;
    }

    block = pool->free_list;
    if (block == NULL) {
        return NULL;
    }

    pool->free_list = block->next;
    return (void*)block;
}

/*
 * 释放由 pool_alloc 从指定内存池分配的内存块。
 * block 必须是该池当前已分配、且尚未释放的块；传入异属地址、块内地址或重复
 * 释放违反接口前置条件，行为未定义。pool 或 block 为 NULL 时函数直接返回。
 */
void pool_free(MemoryPool* pool, void* block)
{
    if (pool == NULL || block == NULL) {
        return;
    }

    ((PoolBlock*)block)->next = pool->free_list;
    pool->free_list = block;
}

/*
 * 销毁内存池。pool 为 NULL 时函数直接返回；调用后该指针失效。
 */
void pool_destroy(MemoryPool* pool)
{
    if (pool == NULL) {
        return;
    }

    free(pool->storage);
    free(pool);
}

int main(void)
{
    enum { BLOCK_COUNT = 4 };
    MemoryPool* pool;
    int* values[BLOCK_COUNT];
    int* reused_value;
    size_t i;

    pool = pool_create(sizeof(int), BLOCK_COUNT);
    if (pool == NULL) {
        fputs("内存池创建失败\n", stderr);
        return 1;
    }

    for (i = 0U; i < BLOCK_COUNT; ++i) {
        values[i] = (int*)pool_alloc(pool);
        if (values[i] == NULL) {
            fputs("内存池分配失败\n", stderr);
            pool_destroy(pool);
            return 1;
        }
        *values[i] = (int)(i + 1U) * 10;
    }

    printf("当前数值：%d %d %d %d\n", *values[0], *values[1], *values[2],
           *values[3]);

    if (pool_alloc(pool) == NULL) {
        puts("内存池已满，下一次分配返回 NULL");
    }

    pool_free(pool, values[1]);
    reused_value = (int*)pool_alloc(pool);
    if (reused_value == NULL || reused_value != values[1]) {
        fputs("内存池复用失败\n", stderr);
        pool_destroy(pool);
        return 1;
    }
    *reused_value = 99;
    printf("复用后的数值：%d\n", *reused_value);

    pool_destroy(pool);
    return 0;
}
```

这里真正管理空闲块的是 `free_list`：创建时把每个块的起始位置串成链表；分配时取下表头，把表头推进到 `next`；释放时再把该块插回表头。`pool_create` 初始化链表需要遍历全部块，时间复杂度为 O(block_count)；而 `pool_alloc` 和满足前置条件的 `pool_free` 都只改动常数个指针，时间复杂度是 O(1)。当块处于空闲状态时，块开头暂存 `next` 指针；一旦分配给调用者，这些字节就完全交还给调用者使用。

`pool_create` 接受的是每块的字节数，不是 `int`、`float` 之类的类型；调用者必须保证实际对象放得进该字节数。为容纳空闲链表指针，实际步长至少为 `sizeof(PoolBlock)`；随后再向上取整到 `_Alignof(max_align_t)` 的倍数。C17 规定 `max_align_t` 的对齐要求不低于任何标量类型，因此示例返回的块满足标量对象的对齐要求；具有扩展对齐要求的类型需要额外设计。函数还在对齐和总存储量计算前检查 `size_t` 溢出；只要参数合法且内存足够，`block_size` 与 `block_count` 不受人为上限限制。

池的元数据和底层存储区都来自 `malloc`：底层区域没有预先声明的对象类型，因此空闲时可作为 `PoolBlock` 保存 `next`，分配后也可由调用者按其需要的对象类型使用。`pool_create` 只应在初始化阶段调用；创建成功后，`pool_alloc` 和 `pool_free` 不再调用通用堆分配器。`pool_free` 不扫描链表来防御错误用法，否则释放就会退化为 O(block_count)；它和 `free` 一样要求调用者不能传入异属地址、块内地址或已经释放的指针，违反此前置条件的行为未定义。`pool_destroy` 释放底层存储区和元数据；调用后 `pool` 是悬垂指针，不能继续传给任何 `pool_*` 函数。

编译运行这个内存池示例：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic memory_pool.c -o memory_pool && ./memory_pool
```

运行结果：

```text
当前数值：10 20 30 40
内存池已满，下一次分配返回 NULL
复用后的数值：99
```

:::

想一下：内存池相比直接 `malloc`/`free`，在嵌入式或实时系统里有什么好处？为什么它能做到 O(1) 分配释放、且不产生碎片？

### 练习 4：带统计的 malloc/free 包装器（挑战·可选）

**难度：挑战** · 可选，建议学完第 15 章预处理器后再做

包装 `malloc`/`free`，记录每次分配的文件名和行号，程序退出时打印还没释放的清单。需要用到 `__FILE__`/`__LINE__` 宏（第 15 章才讲）和 `atexit` 注册退出钩子。

```c
#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)
```

::: details 参考答案

完整程序分为三个文件；`debug_log.h` 只提供调试日志宏，`tracked.h` 公开跟踪器接口，`tracked.c` 实现分配记录和报告。

**debug_log.h**

```c
#pragma once

#include <stdio.h>

/* 默认开启日志；编译时用 -DNDEBUG 可关闭 */
#ifdef NDEBUG
#define DEBUG_LOG(fmt, ...) ((void)(0))
#else
#define DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif
```

**tracked.h**

```c
#pragma once

#include <stddef.h>

void* tracked_malloc(size_t size, const char* file, unsigned int line);
void  tracked_free(void* address, const char* file, unsigned int line);

/* 自动记录调用处的文件名和行号。 */
#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)
#define TFREE(address) tracked_free((address), __FILE__, __LINE__)
```

**tracked.c**

```c
#include <stdio.h>
#include <stdlib.h>

#include "debug_log.h"
#include "tracked.h"

/* malloc/free 跟踪记录的最大数量。 */
#define TRACKED_ALLOCATION_MAX 128U

/* active 为 1 表示该内存块尚未被 free。 */
typedef struct {
    void* address;
    size_t size;
    const char* file;
    unsigned int line;
    int active;
} AllocationRecord;

static AllocationRecord allocation_records[TRACKED_ALLOCATION_MAX];

/* 内存报告注册标志。 */
static int mem_report_registered;

static void mem_report(void);

/* 注册内存报告退出钩子：在第一次使用跟踪器时调用。
 * 作用：向 atexit 注册 mem_report，使程序退出时自动打印泄漏统计。
 * 通过 mem_report_registered 标志保证只在第一次真正需要时注册一次，
 * 之后就跳过，避免在同一函数里重复注册造成浪费或冲突。 */
static void register_mem_report(void)
{
    if (!mem_report_registered) {
        if (atexit(mem_report) == 0) {
            mem_report_registered = 1;
        } else {
            fputs("无法注册内存报告退出钩子\n", stderr);
        }
    }
}
/* 在记录表中查找指定地址对应的记录。
 * address 为要查找的 malloc 起始地址；
 * active_only 非 0 时只查找仍处于活跃（active == 1）状态的记录，
 * 为 0 时查找任意状态的记录（包含已释放的）。
 */
static AllocationRecord* find_record(void* address, int active_only)
{
    size_t i;

    for (i = 0U; i < TRACKED_ALLOCATION_MAX; ++i) {
        if (allocation_records[i].address == address &&
            (!active_only || allocation_records[i].active)) {
            return &allocation_records[i];
        }
    }
    return NULL;
}

/* 调用 malloc 并登记一条分配记录。 */
void* tracked_malloc(size_t size, const char* file, unsigned int line)
{
    AllocationRecord* record = NULL;
    void* address;
    size_t i;

    register_mem_report();
    address = malloc(size);
    if (address == NULL) {
        return NULL;
    }

    /* 在记录表中查找第一个空闲槽位。 */
    for (i = 0U; i < TRACKED_ALLOCATION_MAX; ++i) {
        if (!allocation_records[i].active) {
            record = &allocation_records[i];
            break;
        }
    }

    /* 记录表已满：无法跟踪这次分配，撤销分配以免造成无法追踪的泄漏。 */
    if (record == NULL) {
        fputs("内存跟踪记录表已满，分配被撤销\n", stderr);
        free(address);
        return NULL;
    }

    /* 填充该槽位的信息，并把 active 置 1 表示内存块处于活跃状态。 */
    record->address = address;
    record->size = size;
    record->file = file;
    record->line = line;
    record->active = 1;
    return address;
}

/* 调用 free 并维护对应的分配记录。 */
void tracked_free(void* address, const char* file, unsigned int line)
{
    AllocationRecord* record;

    if (address == NULL) {
        return;
    }

    record = find_record(address, 1);
    if (record != NULL) {
        record->active = 0;
        free(address);
        return;
    }

    /* 已经释放过的地址保留在表中，用于识别重复释放。 */
    record = find_record(address, 0);
    if (record != NULL) {
        fprintf(stderr, "重复释放地址 %p（调用位置 %s:%u）\n",
                address, file, line);
    } else {
        fprintf(stderr, "忽略未登记地址 %p 的释放（调用位置 %s:%u）\n",
                address, file, line);
    }
}

/* 内存泄漏报告函数：由 atexit 在程序退出时自动调用。
 * 功能：
 *   遍历整张记录表，统计仍处于活跃状态的记录并逐条打印，
 *   最后给出汇总结论（无泄漏 / 泄漏块数量）。
 */
static void mem_report(void)
{
    size_t i;
    size_t leak_count = 0U;

    for (i = 0U; i < TRACKED_ALLOCATION_MAX; ++i) {
        if (allocation_records[i].active) {
            ++leak_count;
            DEBUG_LOG("内存泄漏 #%zu: 地址=%p, 大小=%zu 字节, 分配位置=%s:%u",
                      leak_count, allocation_records[i].address,
                      allocation_records[i].size, allocation_records[i].file,
                      allocation_records[i].line);
        }
    }

    if (leak_count == 0U) {
        fputs("内存报告：没有未释放的内存\n", stderr);
    } else {
        DEBUG_LOG("内存报告：共 %zu 个未释放块", leak_count);
    }
}

/* 程序入口：演示跟踪内存分配、释放与退出报告流程。 */
int main(void)
{
    void* tracked_block;
    int* problem_block;

    tracked_block = TMALLOC(32U);
    problem_block = TMALLOC(4U * sizeof(*problem_block));
    if (tracked_block == NULL || problem_block == NULL) {
        fputs("跟踪内存分配失败\n", stderr);
        TFREE(tracked_block);
        TFREE(problem_block);
        return 1;
    }
    TFREE(tracked_block);
    // TFREE(problem_block);
    return 0;
}
```

`atexit` 是 C/C++ 标准库 `<stdlib.h>` 中用来注册一个在程序正常退出时自动执行的回调函数。

`tracked_malloc` 借助 `malloc` 和 `__FILE__`/`__LINE__` 动态分配内存，并把分配位置记入 `allocation_records` 记录表，方便日后忘记回收时快速定位泄漏来源。

`tracked_free` 借助 `free` 和 `__FILE__`/`__LINE__` 释放内存，并通过 `find_record` 判断是否为重复释放或释放了未登记的错误地址。

`register_mem_report` 在首次调用 `tracked_malloc` 时通过 `atexit` 注册 `mem_report`，程序运行结束后据此判断内存状态（是否全部释放、还有几个未释放、当初在什么位置分配）。

编译运行：

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic tracked.c -o tracked && ./tracked
```

注意：这里的 `DEBUG_LOG` 用 `##__VA_ARGS__` 删除没有额外格式化参数时多出的逗号，这是 GCC 扩展，可以在 GCC 的 C17 模式下使用，但不应被当作严格 ISO C17 的可移植写法。本文件中所有 `DEBUG_LOG` 调用都带有格式化参数，因此 `-Wpedantic` 下也能零警告编译；一旦出现不带额外参数的调用，`-Wpedantic` 就会给出警告。

运行结果：

```text
[tracked.c:135] 内存泄漏 #1: 地址=0x60ff136cd2d0, 大小=16 字节, 分配位置=tracked.c:163
[tracked.c:145] 内存报告：共 1 个未释放块
```

注意：`[tracked.c:135]`、`分配位置=tracked.c:163` 里的行号是示意值。`__FILE__`/`__LINE__` 输出的是宏展开处的实际源码行，随文件排版变化，读者看到的数字会与上面不同——以你编译后的输出为准。

:::

提示：用一个数组或链表记录每次分配的地址、大小、位置；`free` 时按地址匹配并标记已释放；`atexit(mem_report)` 注册退出时打印剩余未释放项。
