---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Gain an in-depth understanding of the C language dynamic memory allocation
  mechanism, master the proper use of `malloc`, `calloc`, `realloc`, and `free`, recognize
  common memory errors and debugging methods, and compare the design philosophies
  of C++ RAII and smart pointers.
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
  source_hash: 3836764443c6a59bf37fa71374e3af7a47c1784857804cc5ad250ad3f0d161f8
  translated_at: '2026-06-13T11:42:32.736720+00:00'
  engine: anthropic
  token_count: 1480
---
# Dynamic Memory Management

All the programs we have written so far have had variable sizes determined at compile time. But the real world doesn't work that way—we don't know how many characters a user will input beforehand, we don't know how many records will be collected before running, and data packets sent by clients might be different every time. The common denominator in these scenarios is: **before the program runs, you cannot determine how much memory is needed.**

C's solution to this problem is dynamic memory management—requesting a block of memory of a specified size from the system while the program is running, and returning it when done. This set of APIs looks like just four functions: `malloc`, `calloc`, `realloc`, `free`, which takes ten minutes to learn. But using them correctly is one thing; keeping them from crashing is another—memory leaks, dangling pointers, double frees, out-of-bounds writes—each one can crash your program inexplicably.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Draw a memory layout diagram and explain the responsibilities of the text/rodata/data/bss/heap/stack sections.
> - [ ] Correctly use `malloc`/`calloc`/`realloc`/`free` and handle errors.
> - [ ] Identify and avoid five common memory errors.
> - [ ] Use Valgrind and AddressSanitizer to detect memory issues.
> - [ ] Understand how RAII and smart pointers solve the pain points of manual C management.

## Environment Setup

We will conduct all subsequent experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-g -O0 -Wall -Wextra`

## Step 1 — Figure out what a program looks like in memory

When an executable is loaded into memory by the loader to start running, the operating system allocates a block of virtual address space for it. This space is divided into several functionally distinct areas:

```text
High Addresses
  +------------------+
  |     Stack        |  grows downward
  +------------------+
  |       |          |
  |       v          |
  |                  |
  |       ^          |
  |       |          |
  +------------------+
  |      Heap        |  grows upward
  +------------------+
  |      BSS         |  Uninitialized global/static
  +------------------+
  |      Data        |  Initialized global/static
  +------------------+
  |     Text         |  Machine code
  +------------------+
Low Addresses
```

The **Text Segment** (.text) stores compiled machine instructions and is usually read-only. The **Read-Only Data Segment** (.rodata) stores `const` global variables and string literals. The **Initialized Data Segment** (.data) stores global and `static` variables that have non-zero initial values. The **BSS Segment** (.bss) stores global and `static` variables that are uninitialized or initialized to zero—the key difference is that **BSS** does not take up space in the executable file; it only records "need N bytes zeroed". The **Heap** is where dynamic memory allocation happens; memory applied for via `malloc` comes from here. The **Stack** is used for function calls, storing local variables and return addresses.

## Step 2 — Master malloc/calloc/realloc/free

Stack management is completely automatic—stack frames are allocated when a function is called and automatically reclaimed when it returns. It is extremely fast (moving one register), but has size limitations (8MB by default on Linux), and memory is only valid during the execution of the current function.

Heap management is handed over to the programmer. It is flexible but must be managed manually—if you forget to free it, it leaks; if you free it twice, it crashes. In actual projects, the following scenarios require the heap: data size cannot be determined at compile time, data lifetime spans function calls, or data is too large for the stack.

## malloc — Give me a block of memory

```cpp
void* malloc(size_t size);
```

`malloc` accepts the number of bytes to allocate and returns a `void*` pointer. A basic example:

```cpp
int* arr = (int*)malloc(10 * sizeof(int));
if (!arr) {
    // Handle error
    perror("malloc failed");
    exit(EXIT_FAILURE);
}
```

Key points: Write `sizeof(*arr)` instead of `sizeof(int)`, so the allocation size changes automatically when the pointer type changes. **Checking for NULL immediately after every malloc is an iron rule.** Memory allocated by `malloc` is **uninitialized**—you are reading garbage values.

## calloc — Allocate and zero out

```cpp
void* calloc(size_t nmemb, size_t size);
```

`calloc` allocates memory and **clears it to zero**. Use it when you need zero-initialized structures or arrays—it is safer. `calloc` can also detect parameter multiplication overflow, providing an extra layer of protection compared to `malloc`.

## realloc — Expand capacity (might move house)

```cpp
void* realloc(void* ptr, size_t size);
```

`realloc` is used to adjust the size of allocated memory. It expands in place or finds new space and moves.

⚠️ **The classic pitfall**: `realloc` may return `NULL` (out of memory), but the original pointer is still valid. If you write `ptr = realloc(ptr, new_size);` directly, once it returns `NULL`, the original `ptr` is lost—memory leak. The correct way:

```cpp
void* new_ptr = realloc(ptr, new_size);
if (!new_ptr) {
    // Handle error, ptr is still valid
    perror("realloc failed");
} else {
    ptr = new_ptr;
}
```

## free — Return what you borrow

```cpp
void free(void* ptr);
```

The precautions for `free` are more than they seem: you can only `free` pointers returned by allocation functions; after freeing, the pointer becomes a dangling pointer; **setting to NULL after free is a good habit**—subsequent misuse will cause an immediate segmentation fault, which is ten thousand times easier to debug than use-after-free.

```cpp
free(ptr);
ptr = NULL;  // Good habit
```

## Step 3 — Recognize five common memory errors

### 1. Memory Leak

Allocating and forgetting to free. More insidious scenarios are not releasing old memory before reassigning a pointer ("overwrite leak"), or forgetting to free in error handling branches.

### 2. Dangling Pointer / Use After Free

A pointer pointing to freed memory is continued to be used. This error doesn't necessarily crash immediately—that block of memory might not have been allocated to someone else yet, the data "looks" valid, but it is completely unreliable.

### 3. Double Free

Calling `free` twice on the same block of memory. The heap manager's internal data structures are corrupted, which may cause an immediate crash or strike much later.

### 4. Buffer Overflow

Writing outside the allocated memory area, corrupting metadata of adjacent memory blocks or other data. Off-by-one errors are a typical cause.

### 5. Uninitialized Read

The content of memory allocated by `malloc` is uncertain. Reading without assigning reads garbage values.

## Debugging Tools

### Valgrind

The most classic memory debugging tool on Linux, capable of detecting leaks, illegal reads/writes, uninitialized reads, and double frees. No need to recompile, just add `valgrind` before the program:

```bash
gcc -g program.c -o program
valgrind --leak-check=full ./program
```

### AddressSanitizer (ASan)

A compiler-built memory error detection tool with much lower performance overhead than Valgrind:

```bash
gcc -g -O1 -fsanitize=address -fno-omit-frame-pointer program.c -o program
./program
```

It is recommended to always enable ASan during development and testing phases.

## C++ Transition — How RAII ends the nightmare of manual management

### Core Idea of RAII

Bind the lifecycle of a resource to the lifecycle of an object. The constructor acquires the resource, the destructor releases it. When the object leaves scope, the destructor is guaranteed to be called (even if exceptions occur), and the resource is guaranteed to be released correctly.

### The Three Smart Pointers

`std::unique_ptr` — Exclusive ownership, not copyable but movable. Automatically releases when leaving scope. Recommended to create with `std::make_unique`.

`std::shared_ptr` — Shared ownership + reference counting. Releases memory when the last `shared_ptr` is destroyed. Recommended to create with `std::make_shared`.

`std::weak_ptr` — Does not increase reference count, used to break circular references between `shared_ptr`s.

### Standard Library Containers

`std::vector` replaces dynamic arrays with manual `malloc`, and `std::string` replaces string buffers with manual `malloc`. In modern C++, you almost never need to use `malloc`/`free` directly, let alone `new`/`delete`.

## Summary

We started with memory layout, clarified the roles of stack and heap, dissected the semantics and traps of the four dynamic memory functions one by one, summarized the five most common memory errors, and finally compared C++'s RAII and smart pointers. Dynamic memory management is one of the most error-prone areas in C, but after mastering the correct methodology and tools, most errors can be avoided.

## Exercises

### Exercise 1: Fixed-Size Memory Pool Allocator

Implement a simple fixed-size memory pool that carves fixed-size blocks from a large block of memory, supporting allocation and reclamation.

```cpp
// Implement a fixed-size memory pool
#define BLOCK_SIZE 64
#define POOL_SIZE 1024

void* pool_alloc();
void pool_free(void* ptr);
```

Hint: Use a linked list to manage free blocks—the first few bytes of each free block store a pointer to the next free block.

### Exercise 2: malloc/free Wrapper with Statistics

Implement a wrapper layer for `malloc` and `free` that tracks all allocation and deallocation operations and prints a statistical report when the program exits.

```cpp
// Implement a wrapper for malloc/free
void* tracked_malloc(size_t size, const char* file, int line);
void tracked_free(void* ptr);

// Macro to automatically capture file and line
#define MALLOC(size) tracked_malloc(size, __FILE__, __LINE__)
#define FREE(ptr) tracked_free(ptr)
```

Hint: Use an array or linked list to record information for each allocation. `atexit` can register an exit hook.
