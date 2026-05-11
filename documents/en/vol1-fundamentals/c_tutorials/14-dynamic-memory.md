---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Gain a deep understanding of C's dynamic memory allocation mechanism,
  master the proper use of malloc/calloc/realloc/free, learn to identify common memory
  errors and their debugging methods, and compare the design philosophies of C++ RAII
  (Resource Acquisition Is Initialization) and smart pointers.
difficulty: intermediate
order: 18
platform: host
prerequisites:
- 结构体与内存对齐
reading_time_minutes: 10
tags:
- host
- cpp-modern
- intermediate
- 进阶
- 内存管理
title: Dynamic Memory Management
translation:
  source: documents/vol1-fundamentals/c_tutorials/14-dynamic-memory.md
  source_hash: dcc5f3ef7edc08f41942ce7aa7e288f4f78dcb1c44ede990bcf240a17f2a6eed
  translated_at: '2026-04-20T03:32:45.406116+00:00'
  engine: anthropic
  token_count: 1483
---
# Dynamic Memory Management

So far, all the programs we have written use variables whose sizes are determined at compile time. But the real world does not work that way—we do not know how many characters a user will type in advance, we do not know how many records will be collected before running, and the size of data packets from a client might differ every time. The common thread in these scenarios is: **before the program runs, you cannot determine how much memory is needed**.

C solves this problem through dynamic memory management—requesting a block of memory of a specified size from the system while the program is running, and returning it when done. This set of APIs seems to consist of only four functions: `malloc`, `calloc`, `realloc`, and `free`, which takes barely ten minutes to learn. But using them correctly is one thing, and keeping your program from crashing is another—memory leaks, dangling pointers, double frees, and out-of-bounds writes can each cause your program to crash for inexplicable reasons.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Draw a program's memory layout diagram, explaining the responsibilities of the text/rodata/data/bss/heap/stack segments
> - [ ] Correctly use `malloc`/`calloc`/`realloc`/`free` and handle errors
> - [ ] Identify and avoid five common memory errors
> - [ ] Use Valgrind and AddressSanitizer to detect memory issues
> - [ ] Understand how RAII and smart pointers solve the pain points of manual C memory management

## Environment Setup

We will conduct all subsequent experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

## Step One—Understand What a Program Looks Like in Memory

When a loader places an executable file into memory and starts running it, the operating system allocates a block of virtual address space for it. This space is divided into several regions with different functions:

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

The **text segment** (.text) stores compiled machine instructions and is typically read-only. The **read-only data segment** (.rodata) stores `const` global variables and string literals. The **initialized data segment** (.data) stores global and `static` variables that have non-zero initial values at definition. The **BSS segment** (.bss) stores global and `static` variables that are uninitialized or initialized to zero—the key difference is that `.bss` does not take up space in the executable file; it only records "N bytes need to be zeroed." The **heap** is where dynamic memory allocation happens; memory requested by `malloc` comes from here. The **stack** is used for function calls, storing local variables and return addresses.

## Step Two—Master malloc/calloc/realloc/free

Stack management is fully automatic—a stack frame is allocated when a function is called and automatically reclaimed when it returns. It is extremely fast (moving a single register), but it has a size limit (8 MB by default on Linux), and the memory is only valid during the current function's execution.

Heap management is handed over to the programmer. It is flexible but must be managed manually—forgetting to free causes a memory leak, and freeing twice causes a crash. In real projects, the following scenarios require the heap: the data volume cannot be determined at compile time, the data's lifetime spans function calls, or the data volume is too large for the stack.

## malloc—Give Me a Block of Memory

```c
void* malloc(size_t size);
```

`malloc` takes the number of bytes to allocate and returns a `void*` pointer. A basic example:

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

Key points: write `sizeof(*numbers)` instead of `sizeof(int)`, so the allocation size automatically adjusts when you change the pointer type. **Always check for NULL immediately after every malloc**—this is an ironclad rule. The contents of memory allocated by `malloc` are **uninitialized**—reading from it yields garbage values.

## calloc—Allocate and Zero

```c
void* calloc(size_t num, size_t size);
```

`calloc` allocates memory and **zeros it out entirely**. Use it when you need zero-initialized structures or arrays, as it is safer. `calloc` can also detect parameter multiplication overflow, providing an extra layer of protection compared to `malloc(num * size)`.

## realloc—Resize (Possibly Relocate)

```c
void* realloc(void* ptr, size_t new_size);
```

`realloc` is used to resize previously allocated memory. It expands in place or finds a new space and relocates.

⚠️ **The classic pitfall**: `realloc` might return `NULL` (out of memory), but the original pointer remains valid. If you write `ptr = realloc(ptr, new_size)` directly, and it returns `NULL`, the original `ptr` is lost—resulting in a memory leak. The correct approach:

```c
int* temp = realloc(numbers, 20 * sizeof(int));
if (temp == NULL) {
    free(numbers);
    return 1;
}
numbers = temp;  // 成功了才更新指针
```

## free—Borrow and Return

```c
void free(void* ptr);
```

`free` has more caveats than it appears: you can only free pointers returned by allocation functions; after freeing, the pointer becomes a dangling pointer; **setting the pointer to NULL after free is a good practice**—if it is accidentally used later, it will immediately cause a segmentation fault, which is ten thousand times easier to debug than a use-after-free.

```c
free(numbers);
numbers = NULL;
```

## Step Three—Recognize Five Common Memory Errors

### 1. Memory Leak

Allocating memory and forgetting to free it. More insidious scenarios include not freeing old memory before reassigning a pointer ("overwrite leak"), or forgetting to free in error-handling branches.

### 2. Dangling Pointer / Use After Free

A pointer to freed memory continues to be used. This error does not necessarily crash immediately—that block of memory might not have been allocated to someone else yet, so the data "looks" valid, but it is completely unreliable.

### 3. Double Free

Calling `free` twice on the same block of memory. The heap manager's internal data structures get corrupted, which might cause an immediate crash or might not manifest until much later.

### 4. Buffer Overflow

Writing outside the allocated memory region, corrupting the metadata of adjacent memory blocks or other data. An off-by-one error is a typical cause.

### 5. Uninitialized Read

The contents of memory allocated by `malloc` are indeterminate. Reading without assigning a value yields garbage values.

## Debugging Tools

### Valgrind

The most classic memory debugging tool on Linux, capable of detecting leaks, illegal reads and writes, uninitialized reads, and double frees. No recompilation is needed—simply prepend `valgrind` to your program:

```bash
gcc -g -o demo demo.c
valgrind --leak-check=full ./demo
```

### AddressSanitizer (ASan)

A compiler-built-in memory error detection tool with much lower performance overhead than Valgrind:

```bash
gcc -fsanitize=address -g -o demo demo.c
./demo
```

We recommend always enabling ASan during development and testing.

## C++ Connection—How RAII Ends the Nightmare of Manual Management

### The Core Idea of RAII

Bind a resource's lifetime to an object's lifetime. The constructor acquires the resource, and the destructor releases it. When an object goes out of scope, its destructor is guaranteed to be called (even if an exception occurs), ensuring the resource is properly released.

### The Three Smart Pointers

`std::unique_ptr`—exclusive ownership, non-copyable but movable. Automatically releases when it goes out of scope. We recommend creating it with `std::make_unique`.

`std::shared_ptr`—shared ownership with reference counting. Releases memory when the last `shared_ptr` is destroyed. We recommend creating it with `std::make_shared`.

`std::weak_ptr`—does not increase the reference count; used to break circular references between `shared_ptr` instances.

### Standard Library Containers

`std::vector` replaces manually malloc'd dynamic arrays, and `std::string` replaces manually malloc'd string buffers. In modern C++, you almost never need to use `new`/`delete` directly, let alone `malloc`/`free`.

## Summary

We started with memory layout, clarified the respective roles of the stack and the heap, dissected the semantics and pitfalls of the four dynamic memory functions one by one, summarized the five most common memory errors, and finally compared C++'s RAII and smart pointers. Dynamic memory management is one of the most error-prone areas in C, but once you master the right methodology and tools, most errors can be avoided.

## Exercises

### Exercise 1: Fixed-Size Memory Pool Allocator

Implement a simple fixed-size memory pool that carves fixed-size blocks from a large memory chunk, supporting allocation and deallocation.

```c
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct MemoryPool MemoryPool;

/// @brief 创建一个固定大小内存池
/// @param block_size 每个块的大小（字节）
/// @param block_count 块的数量
/// @return 指向内存池的指针，失败返回 NULL
MemoryPool* pool_create(size_t block_size, size_t block_count);

/// @brief 从内存池中分配一个块
void* pool_alloc(MemoryPool* pool);

/// @brief 将块归还给内存池
void pool_free(MemoryPool* pool, void* block);

/// @brief 销毁内存池，释放所有内存
void pool_destroy(MemoryPool* pool);

int main(void) {
    // TODO: 创建一个 64 字节/块、共 64 块的内存池
    // TODO: 分配几个块，写入数据，然后释放
    // TODO: 销毁内存池
    return 0;
}
```

Hint: Use a linked list to manage free blocks—store a pointer to the next free block in the first few bytes of each free block.

### Exercise 2: malloc/free Wrapper with Statistics

Implement a wrapper layer around `malloc` and `free` that tracks all allocation and deallocation operations, printing a statistical report when the program exits.

```c
#include <stddef.h>
#include <stdio.h>

/// @brief 带统计的 malloc
void* tracked_malloc(size_t size, const char* file, int line);

/// @brief 带统计的 free
void tracked_free(void* ptr);

/// @brief 打印内存统计报告
void mem_report(void);

#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)

int main(void) {
    // TODO: 用 TMALLOC 分配几块内存
    // TODO: 故意只释放其中一部分
    // TODO: 调用 mem_report() 查看哪些分配没有被释放
    return 0;
}
```

Hint: Use an array or linked list to record information for each allocation. `atexit(mem_report)` can register an exit hook.
