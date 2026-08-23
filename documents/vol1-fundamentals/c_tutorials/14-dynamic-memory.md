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


```c
#include <stdio.h>
#include <stdlib.h>

/* 把上面的 intvec_init / intvec_push / intvec_free 粘贴到此处 */

int main(void) {
    IntVec v;
    intvec_init(&v);
    printf("初始容量 = %zu\n", v.capacity);

    for (int i = 0; i < 10; ++i) {
        intvec_push(&v, i * 10);
        printf("push %d -> size=%zu cap=%zu\n", i * 10, v.size, v.capacity);
    }

    printf("数组内容: ");
    for (int i = 0; i < (int)v.size; ++i) printf("%d ", v.data[i]);
    printf("\n");

    intvec_free(&v);
    printf("释放后 data = %p\n", (void*)v.data);
    return 0;
}
```

```bash
gcc -std=c11 -Wall -Wextra intvec_test.c -o intvec_test && ./intvec_test
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
释放后 data = (nil)
```

可以看到 `size` 到 5 时容量从 4 翻倍到 8，再到 8 成 16；`realloc` 扩容后旧元素被无损保留。`intvec_free` 把 `data` 置为 NULL，之后误用会立刻崩溃而不是静默读垃圾。

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

::: details 参考答案

```c
#include <stddef.h>
#include <stdio.h>

/* 单个内存块可容纳的最大字节数。 */
#define MEMORY_POOL_MAX_BLOCK_SIZE 64U
/* 单个内存池可管理的最大内存块数量。 */
#define MEMORY_POOL_MAX_BLOCK_COUNT 16U
/* 程序可同时创建的内存池实例最大数量。 */
#define MEMORY_POOL_MAX_INSTANCES 2U

/* 每个内存块的起始地址都按 max_align_t 对齐。 */
//max_align_t是对齐要求最严格的基础 C 类型，以此为基准可以适应不同的数据类型
typedef union {
    max_align_t alignment;//用于强制整个内存块按最严格的基础类型对齐
    unsigned char bytes[MEMORY_POOL_MAX_BLOCK_SIZE];//以字节数组形式存储的实际内存数据
} PoolBlock;

typedef struct  {
    PoolBlock blocks[MEMORY_POOL_MAX_BLOCK_COUNT];//内存块数组，实际存放数据的内存槽位
    unsigned char in_use[MEMORY_POOL_MAX_BLOCK_COUNT];//每个内存块的占用标记数组，非 0 表示已分配
    size_t block_size;//每个内存块的字节大小（由 pool_create 指定）
    size_t block_count;//实际使用的内存块数量（由 pool_create 指定）
    int active;//内存池实例是否已被创建并投入使用（非 0 表示有效）
}MemoryPool;

/* 使用静态实例代替运行时堆内存分配。 */
static MemoryPool memory_pools[MEMORY_POOL_MAX_INSTANCES];

/* 判断内存池指针是否指向本模块管理的静态内存池实例。 */
static int pool_is_managed(const MemoryPool *pool)
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
static int pool_block_index(const MemoryPool *pool, const void *block,
                            size_t *index)
{
    size_t i;

    if (pool == NULL || block == NULL || index == NULL) {
        return 0;
    }

    for (i = 0U; i < pool->block_count; ++i) {
        if (block == (const void *)pool->blocks[i].bytes) {
            *index = i;
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
MemoryPool *pool_create(size_t block_size, size_t block_count)
{
    MemoryPool *pool;
    size_t i;

    if (block_size == 0U || block_size > MEMORY_POOL_MAX_BLOCK_SIZE ||
        block_count == 0U || block_count > MEMORY_POOL_MAX_BLOCK_COUNT) {
        return NULL;
    }

    for (i = 0U; i < MEMORY_POOL_MAX_INSTANCES; ++i) {
        pool = &memory_pools[i];
        if (!pool->active) {
            size_t block_index;

            pool->block_size = block_size;
            pool->block_count = block_count;
            pool->active = 1;
            for (block_index = 0U; block_index < block_count; ++block_index) {
                pool->in_use[block_index] = 0U;
            }
            return pool;
        }
    }

    return NULL;
}

/*
 * 从内存池中分配一个空闲内存块。
 * 分配成功返回内存块首地址；内存池无效或已满时返回 NULL。
 */
void *pool_alloc(MemoryPool *pool)
{
    size_t i;

    if (!pool_is_managed(pool) || !pool->active) {
        return NULL;
    }

    for (i = 0U; i < pool->block_count; ++i) {
        if (pool->in_use[i] == 0U) {
            pool->in_use[i] = 1U;
            return pool->blocks[i].bytes;
        }
    }

    return NULL;
}

/*
 * 释放由 pool_alloc 从指定内存池分配的内存块。
 * pool 或 block 无效、block 不属于该内存池或已释放时，函数直接返回。
 */
void pool_free(MemoryPool *pool, void *block)
{
    size_t index;

    if (!pool_is_managed(pool) || !pool->active ||
        !pool_block_index(pool, block, &index) || pool->in_use[index] == 0U) {
        return;
    }

    pool->in_use[index] = 0U;
}

/*
 * 销毁内存池并将其静态实例标记为空闲。
 * 内存池无效或已销毁时，函数直接返回。
 */
void pool_destroy(MemoryPool *pool)
{
    size_t i;

    if (!pool_is_managed(pool) || !pool->active) {
        return;
    }

    for (i = 0U; i < pool->block_count; ++i) {
        pool->in_use[i] = 0U;
    }
    pool->block_size = 0U;
    pool->block_count = 0U;
    pool->active = 0;
}

int main(void)
{
    enum { BLOCK_COUNT = 4 };
    MemoryPool *pool;
    int *values[BLOCK_COUNT];
    int *reused_value;
    size_t i;

    pool = pool_create(sizeof(int), BLOCK_COUNT);
    if (pool == NULL) {
        fputs("内存池创建失败\n", stderr);
        return 1;
    }

    for (i = 0U; i < BLOCK_COUNT; ++i) {
        values[i] = (int *)pool_alloc(pool);
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
    reused_value = (int *)pool_alloc(pool);
    if (reused_value == NULL) {
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

`pool_create`创建内存池，`pool_create`的作用是类似动态管理的堆区。创建内存池时需指定内存池类型：`int`,`float`等都可以，由于 `max_align_t alignment`是最严格的对齐要求，所以无论是`pool_create`、`pool_alloc`，还是首地址都满足它们的对齐，那么创建时满足`block_size ≤ MEMORY_POOL_MAX_BLOCK_SIZE`即可。注意在创建时每个内存块的`block_size` 指定每块字节数；`block_count` 指定块数量,`block_count`最大不超过`MEMORY_POOL_MAX_BLOCK_COUNT`，当创建`block_count<MEMORY_POOL_MAX_BLOCK_COUNT`时只有`block_count`个内存块可以使用，剩下的内存块将不可以使用，创建时的最大内存池数量为`MEMORY_POOL_MAX_INSTANCES`,超过后将不再可以进行创建.
`pool_alloc`的作用类似与`malloc`,从内存池中分配一个内存块.`pool_free`是通过`pool_block_index`查找对应的内存块位置并进行内存的回收，`pool_free`的作用类似与`free`,
`pool_destroy`将创建的内存池进行回收方便创建新的其他类型的内存池，否则创建的内存池数量超过`MEMORY_POOL_MAX_INSTANCES`时将不可以再次创建，由于创建的内存池依赖`memory_pools`这个静态数组,本质上是属于静态存储区BSS段,"静态存储期生命周期贯穿整个程序，若不手动 pool_destroy，它们会一直占用 MEMORY_POOL_MAX_INSTANCES 个名额直到程序退出"由 OS 统一回收。如果不进行二次使用可以不进行回收但还是希望养成好习惯.

编译运行这个内存池示例：

```bash
gcc -std=c11 -Wall -Wextra memory_pool.c -o memory_pool && ./memory_pool
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

```c
//debug_log.h
#pragma once

#include <stdio.h>

/* 默认开启日志；编译时用 -DNDEBUG 可关闭 */
#ifdef NDEBUG
    #define DEBUG_LOG(fmt, ...)  ((void)(0))
#else
    #define DEBUG_LOG(fmt, ...) \
        fprintf(stderr, "[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif
```

```c
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug_log.h"

// malloc/free 跟踪记录的最大数量。
#define TRACKED_ALLOCATION_MAX 128U
  
// active为1 表示该内存块尚未被 free，属于潜在泄漏； 0 表示已被 free 或已被重复释放检测使用。
typedef struct {
    void *address;//实际 malloc 返回的内存块首地址（NULL 表示空）；
    size_t size;//本次分配的字节数，由调用方传入的 size 决定；
    const char *file;//记录该次分配发生所在源文件（由 __FILE__ 宏提供）；
    unsigned int line;//记录该次分配发生所在行号（由 __LINE__ 宏提供）；
    int active;//标记该记录当前是否仍处于“活跃”状态：
} AllocationRecord;

static AllocationRecord allocation_records[TRACKED_ALLOCATION_MAX];

//内存报告注册标志。记录 atexit(mem_report) 是否注册成功：
static int mem_report_registered;

/* mem_report 的声明：定义在文件下方，先在此声明以供 atexit 使用。 */
void mem_report(void);

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
//在记录表中查找指定地址对应的记录。
 //address 要查找的 malloc 起始地址；
// active_only 为真(非0)时只查找仍处于活跃(active==1)状态的记录，
//为假(0)时查找任意状态的记录（包含已释放的）。

static AllocationRecord *find_record(void *address, int active_only)
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

//带跟踪的 malloc：调用真正的 malloc 并登记一条分配记录。
void *tracked_malloc(size_t size, const char *file, unsigned int line)
{
    AllocationRecord *record = NULL;
    void *address;
    size_t i;

    register_mem_report();
    address = malloc(size);
    if (address == NULL) {
        return NULL;
    }

    /* 统一输出本次分配的大小，方便观察挂钩/监控效果。 */
    printf("size = %zu\n", size);

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

//带跟踪的 free：调用真正的 free 并维护对应的分配记录。
void tracked_free(void *address, const char *file, unsigned int line)
{
    AllocationRecord *record;

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
void mem_report(void)
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

/* 宏封装：把普通 malloc/free 替换成带文件名与行号的跟踪版本。
 * TMALLOC(size)  实际调用 tracked_malloc，并自动填入 __FILE__、__LINE__；
 * TFREE(address) 实际调用 tracked_free，并自动填入 __FILE__、__LINE__。
 * 这样调用者无需手动编写文件名与行号参数，易于日常使用。 */
#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)
#define TFREE(address) tracked_free((address), __FILE__, __LINE__)

/* 程序入口：演示跟踪内存分配、释放与退出报告流程。 */
int main(void)
{
    void *tracked_block;
    int *problem_block;

    tracked_block = TMALLOC(32U);
    problem_block = TMALLOC(sizeof(int)*4);
    if (tracked_block == NULL) {
        fputs("跟踪内存分配失败\n", stderr);
        return 1;
    }
     TFREE(tracked_block);
     TFREE(problem_block);
    return 0;
}
```

atexit 是 C/C++ 标准库 <stdlib.h> 中用来注册一个在程序正常退出时自动执行的回调函数。
`tracked_malloc`使用`malloc`和`__FILE__, __LINE__`机制动态分配内存并将分配时的位置记录，方便日后忘回收时进行快速查找
通过`allocation_records`记录表将状态进行记录
`tracked_free`使用`free`和`__FILE__, __LINE__`机制释放内存和查找是否重复释放和释放错误地址，通过`find_record`进行对释放地址的判断
`register_mem_report`在首次`tracked_malloc`是进行`atexit`注册`mem_repor`确定程序运行结束后内存的状态（判断是否全部释放和有几个未释放和当初的分配位置

运行结果：

```bash
gcc -std=gnu11 -Wall -Wextra tracked.c -o tracked && ./tracked
```

```text
size = 32
size = 16
内存报告：没有未释放的内存
```

:::

提示：用一个数组或链表记录每次分配的地址、大小、位置；`free` 时按地址匹配并标记已释放；`atexit(mem_report)` 注册退出时打印剩余未释放项。
