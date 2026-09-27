---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Understand C's dynamic memory allocation in depth, master the correct
  use of malloc/calloc/realloc/free, learn to recognize common memory errors and
  how to debug them, and compare the design philosophy of C++ RAII and smart pointers.
difficulty: intermediate
order: 18
platform: host
prerequisites:
- Structures and Memory Alignment
reading_time_minutes: 35
tags:
- host
- cpp-modern
- intermediate
- 进阶
- 内存管理
title: Dynamic Memory Management
translation:
  source: documents/vol1-fundamentals/c_tutorials/14-dynamic-memory.md
  source_hash: fd0e08df40faada6ede7a5d5fc46e47842c0da7d811a84ccac70edfd24fb8f06
  translated_at: '2026-09-25T13:25:26+00:00'
  engine: anthropic
  token_count: 6000
---
# Dynamic Memory Management

In every program we have written so far, the size of each variable was settled at compile time. The real world does not work that way—you cannot know in advance how many characters the user will type, how many records will be collected before the program runs, or that the packets a client sends will be the same size every time. These scenarios share one thing: **before the program runs, you cannot know how much memory it will need**.

C's answer to this problem is dynamic memory management: while the program is running, request a block of memory of a given size from the system, and hand it back when you are done. The API looks like just four functions—`malloc`, `calloc`, `realloc`, and `free`—and ten minutes is enough to learn them. But using them correctly is one thing; using them without crashing is another—memory leaks, dangling pointers, double frees, and out-of-bounds writes can each make your program fall over for no apparent reason.

## Step One — See What Your Program Looks Like in Memory

When the loader places an executable into memory and it starts running, the operating system gives it a span of virtual address space, and that span is divided into several regions with different jobs:

```text
High addresses
┌────────────────────────────────┐
│         Kernel space           │  (inaccessible from user mode)
├────────────────────────────────┤
│             Stack              │  ← grows toward lower addresses
│               ↓                │
│                                │
│            (unused)            │
│                                │
│               ↑                │
│              Heap              │  ← grows toward higher addresses
├────────────────────────────────┤
│      BSS segment (.bss)        │  uninitialized globals/statics
├────────────────────────────────┤
│      Data segment (.data)      │  initialized globals/statics
├────────────────────────────────┤
│   Read-only segment (.rodata)  │  const globals, string literals
├────────────────────────────────┤
│      Code segment (.text)      │  machine instructions (read-only, executable)
└────────────────────────────────┘
Low addresses
```

**The code segment** (.text) holds the compiled machine instructions and is usually read-only. **The read-only data segment** (.rodata) holds `const` global variables and string literals. **The initialized data segment** (.data) holds globals and `static` variables defined with non-zero initial values. **The BSS segment** (.bss) holds globals and `static` variables that are uninitialized or initialized to zero—the key difference is that `.bss` takes no space in the executable file; it merely records "needs N bytes zeroed". **The heap** is where dynamic allocation happens—memory requested by `malloc` comes from here. **The stack** serves function calls, storing local variables and return addresses.

## Step Two — Master malloc/calloc/realloc/free

The stack is managed entirely automatically: a stack frame is allocated on each function call and reclaimed on return. It is extremely fast (just moving a register), but size-limited (8 MB by default on Linux), and the memory is only valid while the current function executes.

The heap hands management over to the programmer. It is flexible, but you must manage it yourself—forget to free and you leak; free twice and you crash. In real projects, the heap is what you need when the amount of data cannot be pinned down at compile time, when the data's lifetime spans function calls, or when the data is too large to live on the stack.

## malloc — Give Me a Block of Memory

```c
void* malloc(size_t size);
```

`malloc` takes the number of bytes you want to allocate and returns a `void*` pointer. A basic example:

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

Key points: write `sizeof(*numbers)` instead of `sizeof(int)`, so that if you change the pointer type later, the allocation size follows automatically. **Checking for NULL immediately after every malloc** is an iron rule. The memory `malloc` returns is **uninitialized**—reading it yields garbage values.

## calloc — Allocate and Zero

```c
void* calloc(size_t num, size_t size);
```

`calloc` allocates memory and **zeroes all of it**. When you need a zero-initialized struct or array, it is the safer choice. `calloc` also detects multiplication overflow between its arguments, one layer of protection that `malloc(num * size)` does not have.

## realloc — Resizing (and Possibly Relocating)

```c
void* realloc(void* ptr, size_t new_size);
```

`realloc` adjusts the size of previously allocated memory. It either extends the block in place or finds new space and moves.

**The classic trap**: `realloc` can return `NULL` (out of memory) while the original pointer remains valid. If you write `ptr = realloc(ptr, new_size)` and it returns `NULL`, the original `ptr` is lost—a memory leak. The correct pattern:

```c
int* temp = realloc(numbers, 20 * sizeof(int));
if (temp == NULL) {
    free(numbers);
    return 1;
}
numbers = temp;  // update the pointer only after success
```

## free — Return What You Borrowed

```c
void free(void* ptr);
```

`free` comes with more caveats than it appears to have: only free pointers returned by an allocation function; after freeing, the pointer becomes dangling; and **setting the pointer to NULL after free is a good habit**—any later misuse then segfaults immediately, which is ten thousand times easier to debug than a use-after-free.

```c
free(numbers);
numbers = NULL;
```

## Step Three — Know the Five Common Memory Errors

### 1. Memory Leaks

You allocated the memory and forgot to free it. The sneakier variants: reassigning a pointer without freeing the old block first (an "overwrite leak"), or forgetting to free along an error-handling branch.

### 2. Dangling Pointers / Use After Free

A pointer into freed memory keeps being used. This error does not necessarily crash right away—that block may not have been handed to anyone else yet, so the data "looks" valid, but it is completely untrustworthy.

### 3. Double Free

Calling `free` twice on the same block of memory. The heap manager's internal data structures get corrupted; the crash may be immediate, or it may lie dormant and strike much later.

### 4. Buffer Overrun

Writing outside the allocated memory region, trashing the metadata or data of neighboring blocks. Off-by-one errors are the typical cause.

### 5. Uninitialized Reads

The contents of memory from `malloc` are indeterminate. Read it before assigning, and you read garbage.

## Debugging Tools

### Valgrind

The classic memory debugging tool on Linux: it detects leaks, illegal reads and writes, uninitialized reads, and double frees. No recompilation needed—just put `valgrind` in front of your program:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic -g -o demo demo.c
valgrind --leak-check=full ./demo
```

### AddressSanitizer (ASan)

A memory error detector built into the compiler, with a performance overhead far smaller than Valgrind's:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic -fsanitize=address -g -o demo demo.c
./demo
```

We recommend keeping ASan on throughout development and testing.

## Bridging into C++ — How RAII Ends the Manual-Management Nightmare

### The Core Idea of RAII

Bind the resource's lifetime to an object's lifetime. The constructor acquires the resource; the destructor releases it. When the object leaves scope, the destructor is guaranteed to run (even if an exception is in flight), so the resource is guaranteed to be released correctly.

### The Three Musketeers of Smart Pointers

`std::unique_ptr`—exclusive ownership; non-copyable but movable. Releases automatically when it leaves scope. Prefer creating one with `std::make_unique`.

`std::shared_ptr`—shared ownership plus reference counting. The memory is released when the last `shared_ptr` is destroyed. Prefer creating one with `std::make_shared`.

`std::weak_ptr`—does not bump the reference count; it exists to break circular references between `shared_ptr`s.

### Standard Library Containers

`std::vector` replaces the hand-rolled malloc'd dynamic array, and `std::string` replaces the malloc'd string buffer. In modern C++ you almost never need to use `new`/`delete` directly, let alone `malloc`/`free`.

## Exercises

### Exercise 1: A Dynamically Growing Array with realloc

**Difficulty: Basic** · Grow the buffer with realloc, following up on this article's realloc discussion

Implement a simple dynamic array of ints: initial capacity 4; each `push_back` doubles the capacity with `realloc` when full.

```c
#include <stddef.h>

typedef struct {
    int*   data;
    size_t size;
    size_t capacity;
} IntVec;

/// @brief Initialize with an initial capacity of 4; returns -1 on failure
int  intvec_init(IntVec* v);
/// @brief Append at the tail; doubles the capacity when full; returns -1 on failure
int  intvec_push(IntVec* v, int value);
/// @brief Free
void intvec_free(IntVec* v);
```

Hint: `realloc(NULL, n)` is equivalent to `malloc(n)`, so even the first allocation can go through realloc. Note that when `realloc` fails it returns NULL and does not free the old block—catch the return value in a temporary variable, and only overwrite the original pointer after confirming success, or you will leak.

::: details Reference Solution

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

    vector->capacity = 4U; // initial capacity
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
                                new_capacity * sizeof(*vector->data)); // double the capacity when full

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

The key is storing `realloc`'s return value in `new_data` first and assigning it to `vector->data` only after the success check. If you write `vector->data = realloc(vector->data, ...)` directly, then on failure the original pointer is overwritten with `NULL`, that block is lost forever, and you have a leak.

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic intvec_test.c -o intvec_test && ./intvec_test
```

Output:

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

Watch how the capacity doubles from 4 to 8 when `size` reaches 5, and from 8 to 16 at the next growth; after `realloc` grows the buffer, the old elements survive intact. `intvec_free` sets the struct member `data` to `NULL` so it does not keep holding a dangling address; even so, after freeing you still must not dereference that member—re-initialize it first, or check its state.

:::

### Exercise 2: Diagnosing Memory Errors

**Difficulty: Intermediate** · Use ASan or Valgrind to recognize the memory errors covered in this article

The code below hides at least four memory errors. Read the code first and guess what is wrong at each spot, then run it under `gcc -std=c17 -Wall -Wextra -Wpedantic -fsanitize=address` (or Valgrind) and write down the error type and location the tool reports:

```c
#include <stdlib.h>

int main(void) {
    int* p = malloc(4 * sizeof(int));
    p[4] = 42;            // (1) what is wrong here?

    int* q = malloc(sizeof(int));
    free(q);
    *q = 100;             // (2) and here?

    int* r = malloc(1024);
    /* forgot to free(r) */   // (3) and which category is this one?

    free(p);
    free(p);              // (4) one more
    return 0;
}
```

Requirement: for each spot, name which of the error classes from this article it belongs to (out-of-bounds write, use-after-free, leak, double free, uninitialized read), and describe how the tool reported it.

::: details Reference Solution

(1) `p[4]`: only 4 `int`s were allocated (indices 0–3), so `p[4]` is an out-of-bounds write to a heap buffer. ASan reports `heap-buffer-overflow`.

(2) `*q = 100`: `q` was already freed and is then written—a use after free. ASan reports `heap-use-after-free`.

(3) `r` is never freed: a memory leak. Valgrind's `LEAK SUMMARY` lists it; ASan on most platforms also checks for leaks by default (`detect_leaks=1`) and reports `Detected memory leaks` at exit.

(4) `free(p)` twice: a double free. ASan reports `attempting double-free`.

These four map exactly onto the typical memory errors covered in this article, and the tools' error keywords help you quickly pin down which class you are dealing with.

:::

### Exercise 3: A Fixed-Size Memory Pool Allocator (Challenge, Optional)

**Difficulty: Challenge** · Optional; requires teaching yourself the free-list idiom

Implement a fixed-size memory pool: carve fixed-size blocks out of one large chunk of memory and manage the free blocks with a linked list—the first few bytes of each free block hold a pointer to the next free block. We suggest reading up on how an "in-place linked list / free list" works before writing code.

```c
typedef struct MemoryPool MemoryPool;
MemoryPool* pool_create(size_t block_size, size_t block_count);
void*       pool_alloc(MemoryPool* pool);
void        pool_free(MemoryPool* pool, void* block);
void        pool_destroy(MemoryPool* pool);
```

The two listings below are independent alternative implementations, each with its own `main`; pick one to compile, and do not paste both into the same source file.

::: details Reference Solution 1: Static-Capacity Version (Suited for Embedded)

This implementation never calls `malloc`; instead it prepares a fixed number of pool instances and memory blocks up front. Its advantage is that at runtime it is immune to heap fragmentation or heap allocation failure; the cost is that capacity and instance count are hard-coded. The free list still lives at the start of each free block, but pointer values inside static byte storage can only be written and read back via `memcpy`: `pool_alloc` therefore remains O(1), while `pool_free`—which scans to check for foreign addresses and double frees—is O(block_count). It suits firmware with clearly bounded resources.

```c
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Maximum number of bytes a single memory block can hold. */
#define MEMORY_POOL_MAX_BLOCK_SIZE 64U
/* Maximum number of memory blocks a single pool can manage. */
#define MEMORY_POOL_MAX_BLOCK_COUNT 16U
/* Maximum number of pool instances the program can create at the same time. */
#define MEMORY_POOL_MAX_INSTANCES 2U
/* Blocks are carved out at max_align_t's alignment requirement. */
#define MEMORY_POOL_ALIGNMENT _Alignof(max_align_t)
/* Storage reserved for each pool for the worst case. */
#define MEMORY_POOL_MAX_BLOCK_STRIDE                                      \
    (((MEMORY_POOL_MAX_BLOCK_SIZE + MEMORY_POOL_ALIGNMENT - 1U) /         \
      MEMORY_POOL_ALIGNMENT) * MEMORY_POOL_ALIGNMENT)

/* A union ensures the whole pool memory is aligned to max_align_t. */
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

/* Static instances instead of run-time heap allocation. */
static MemoryPool memory_pools[MEMORY_POOL_MAX_INSTANCES];

/* Check whether the pool pointer refers to a static pool instance managed by this module. */
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

/* Find a block's index within the given pool; on success, write the index to index. */
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

/* Copy next's object representation into the start of a free block, without dereferencing bytes as a pointer object. */
static void pool_write_next(unsigned char* block, unsigned char* next)
{
    memcpy(block, &next, sizeof(next));
}

/* Read back, from the start of a free block, the pointer object representation previously written by pool_write_next. */
static unsigned char* pool_read_next(const unsigned char* block)
{
    unsigned char* next;

    memcpy(&next, block, sizeof(next));
    return next;
}

/* Check whether the given block is already on the free list. */
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
 * Create a memory pool.
 * block_size is the number of bytes per block; block_count is the number of blocks.
 * Returns the pool pointer on success; NULL if a parameter exceeds a limit or no instance is free.
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
 * Allocate one free block from the pool.
 * Returns the block's address on success; NULL if the pool is invalid or full.
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
 * Release a block that pool_alloc handed out from the given pool.
 * Returns immediately if pool or block is invalid, the block is foreign to this pool, or it was already freed.
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
 * Destroy the pool and mark its static instance free.
 * Returns immediately if the pool is invalid or already destroyed.
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

`PoolStorage` uses a union to align the whole `bytes` storage to `max_align_t`; and because each block's starting offset is a multiple of that alignment, every block satisfies the same requirement. C17 guarantees that `max_align_t`'s alignment requirement is no weaker than any scalar type's, but it does not promise support for arbitrary types with extended alignment. Alignment only settles the address condition—it cannot turn static storage that was declared as an array of bytes into an arbitrary `T` object: `pool_write_next` and `pool_read_next` record the free-list pointer with `memcpy`, and `main` likewise uses `memcpy` to write and read back the object representation of an `int`, never casting `bytes` to a pointer and dereferencing it. Callers must not copy more data than the `block_size` they passed in, and must call `pool_free` when they are done; `pool_destroy` merely marks the static instance free again. The old pointer itself does not become dangling because of this, but it no longer represents an active pool, and the blocks previously allocated from it must stop being used.

`bytes` is an object declared as `unsigned char[]`. C17's effective-type rules do not allow treating it as a real, live `int` object and reading or writing through an `int*` merely because the address happens to be aligned; `max_align_t` cannot change this static object's declared type. To read the integer saved in the example, you must first copy the byte representation back into an object genuinely declared as `int`.

`malloc` is different: the storage it returns has no declared type. Provided size and alignment are both satisfied, the first non-character write through an `int*` can give that allocated storage the effective type `int`; static `unsigned char[]` enjoys no such privilege.

:::

::: details Reference Solution 2: Dynamic `malloc` + Free-List Version

```c
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* While a block is free, its start holds the pointer to the next free block. */
typedef struct PoolBlock {
    struct PoolBlock* next;
} PoolBlock;

typedef struct MemoryPool MemoryPool;

struct MemoryPool {
    unsigned char* storage; // pool backing storage obtained from malloc
    PoolBlock* free_list; // head pointer of the free-block list
};

/*
 * Create a memory pool.
 * block_size is the number of bytes per block; block_count is the number of blocks.
 * Returns the pool pointer on success; NULL if a parameter is 0, a size computation overflows, or malloc fails.
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
 * Allocate one free block from the pool.
 * Returns the block's address on success; NULL if pool is NULL or the pool is full.
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
 * Release a block allocated from the given pool by pool_alloc.
 * block must be a block this pool currently has allocated and not yet freed; a foreign
 * address, an interior address, or a double free violates the interface precondition
 * and is undefined behavior. Returns immediately if pool or block is NULL.
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
 * Destroy the pool. Returns immediately if pool is NULL; the pointer is invalid afterwards.
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

What actually manages the free blocks here is `free_list`: at creation time, the start of every block is threaded onto a list; allocation pops the head and advances the head to `next`; freeing inserts the block back at the head. `pool_create` must walk all the blocks to initialize the list, which is O(block_count), while `pool_alloc` and a precondition-satisfying `pool_free` touch only a constant number of pointers—O(1). While a block is free, its beginning doubles as storage for the `next` pointer; once the block is handed to the caller, those bytes belong entirely to the caller.

`pool_create` takes a number of bytes per block, not a type such as `int` or `float`; the caller must guarantee that the actual object fits in that many bytes. To hold the free-list pointer, the effective stride is at least `sizeof(PoolBlock)`, and it is then rounded up to a multiple of `_Alignof(max_align_t)`. C17 requires `max_align_t`'s alignment to be no weaker than any scalar type's, so the blocks this example returns satisfy the alignment requirements of scalar objects; types with extended alignment requirements call for extra design work. The function also checks for `size_t` overflow before computing the alignment and the total storage; as long as the parameters are legal and memory suffices, `block_size` and `block_count` face no artificial caps.

Both the pool's metadata and its backing storage come from `malloc`: the backing region has no pre-declared object type, so while a block is free it can serve as a `PoolBlock` holding `next`, and once allocated the caller may use it as whatever object type they need. `pool_create` should be called only during initialization; once creation succeeds, `pool_alloc` and `pool_free` never touch the general-purpose heap allocator again. `pool_free` does not scan the list to defend against misuse—otherwise freeing would degrade to O(block_count); like `free`, it requires the caller not to pass foreign addresses, interior addresses, or already-freed pointers, and violating that precondition is undefined behavior. `pool_destroy` frees the backing storage and the metadata; afterwards `pool` is a dangling pointer and must not be passed to any `pool_*` function.

Compile and run this memory-pool example:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic memory_pool.c -o memory_pool && ./memory_pool
```

Output:

```text
当前数值：10 20 30 40
内存池已满，下一次分配返回 NULL
复用后的数值：99
```

:::

Think about it: compared with calling `malloc`/`free` directly, what does a memory pool buy you in an embedded or real-time system? Why can it allocate and free in O(1) without producing fragmentation?

### Exercise 4: A Tracking Wrapper for malloc/free (Challenge, Optional)

**Difficulty: Challenge** · Optional; best saved for after Chapter 15 on the preprocessor

Wrap `malloc`/`free` so that every allocation records its file name and line number, and at program exit print the list of everything still not freed. You will need the `__FILE__`/`__LINE__` macros (not covered until Chapter 15) and `atexit` to register an exit hook.

```c
#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)
```

::: details Reference Solution

The complete program is split across three files: `debug_log.h` provides only the debug-logging macro, `tracked.h` exposes the tracker's interface, and `tracked.c` implements allocation recording and reporting.

**debug_log.h**

```c
#pragma once

#include <stdio.h>

/* Logging on by default; compile with -DNDEBUG to turn it off */
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

/* Automatically record the call site's file name and line number. */
#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)
#define TFREE(address) tracked_free((address), __FILE__, __LINE__)
```

**tracked.c**

```c
#include <stdio.h>
#include <stdlib.h>

#include "debug_log.h"
#include "tracked.h"

/* Maximum number of malloc/free tracking records. */
#define TRACKED_ALLOCATION_MAX 128U

/* active == 1 means this block has not been freed yet. */
typedef struct {
    void* address;
    size_t size;
    const char* file;
    unsigned int line;
    int active;
} AllocationRecord;

static AllocationRecord allocation_records[TRACKED_ALLOCATION_MAX];

/* Flag: memory report registered. */
static int mem_report_registered;

static void mem_report(void);

/* Register the memory-report exit hook: called on the first use of the tracker.
 * Purpose: register mem_report with atexit so leak statistics print automatically at exit.
 * The mem_report_registered flag ensures we register exactly once, on the first real
 * need, and skip afterwards—avoiding wasted or conflicting re-registrations. */
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
/* Find the record for the given address in the record table.
 * address is the malloc start address to look up;
 * when active_only is nonzero, only records still active (active == 1) match;
 * when it is 0, records in any state match (including freed ones).
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

/* Call malloc and log one allocation record. */
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

    /* Find the first free slot in the record table. */
    for (i = 0U; i < TRACKED_ALLOCATION_MAX; ++i) {
        if (!allocation_records[i].active) {
            record = &allocation_records[i];
            break;
        }
    }

    /* Record table full: this allocation cannot be tracked, undo it to avoid an untrackable leak. */
    if (record == NULL) {
        fputs("内存跟踪记录表已满，分配被撤销\n", stderr);
        free(address);
        return NULL;
    }

    /* Fill in the slot's fields and set active to 1 to mark the block live. */
    record->address = address;
    record->size = size;
    record->file = file;
    record->line = line;
    record->active = 1;
    return address;
}

/* Call free and maintain the matching allocation record. */
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

    /* Addresses already freed stay in the table, to identify double frees. */
    record = find_record(address, 0);
    if (record != NULL) {
        fprintf(stderr, "重复释放地址 %p（调用位置 %s:%u）\n",
                address, file, line);
    } else {
        fprintf(stderr, "忽略未登记地址 %p 的释放（调用位置 %s:%u）\n",
                address, file, line);
    }
}

/* Memory-leak report function: invoked automatically by atexit at program exit.
 * What it does:
 *   walk the entire record table, count the still-active records and print each one,
 *   then give a summary verdict (no leaks / number of leaked blocks).
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

/* Program entry: demonstrates tracked allocation, freeing, and the exit report. */
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

`atexit` is the C/C++ standard-library facility in `<stdlib.h>` for registering a callback that runs automatically when the program exits normally.

`tracked_malloc` allocates memory through `malloc` together with `__FILE__`/`__LINE__`, and logs the allocation site into the `allocation_records` table, so that if something is never reclaimed later, the leak's origin is quick to pin down.

`tracked_free` frees memory through `free` together with `__FILE__`/`__LINE__`, and uses `find_record` to tell whether this is a double free or a free of an unregistered bogus address.

`register_mem_report` registers `mem_report` via `atexit` the first time `tracked_malloc` runs, so that after the program finishes you can see the memory picture: whether everything was freed, how many blocks remain unreleased, and where each was originally allocated.

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic tracked.c -o tracked && ./tracked
```

Note: `DEBUG_LOG` here uses `##__VA_ARGS__` to swallow the extra comma when there are no additional format arguments—that is a GCC extension, usable in GCC's C17 mode, but not something to treat as a portable strict ISO C17 construct. Every `DEBUG_LOG` call in this file passes format arguments, so it compiles with zero warnings even under `-Wpedantic`; the moment a call without extra arguments appears, `-Wpedantic` will warn.

Output:

```text
[tracked.c:135] 内存泄漏 #1: 地址=0x60ff136cd2d0, 大小=16 字节, 分配位置=tracked.c:163
[tracked.c:145] 内存报告：共 1 个未释放块
```

Note: the line numbers in `[tracked.c:135]` and `分配位置=tracked.c:163` are illustrative values. `__FILE__`/`__LINE__` output the actual source line where the macro expands, which shifts as the file is reorganized, so the numbers you see will differ from the ones above—trust the output of your own build.

:::

Hint: use an array or linked list to record each allocation's address, size, and location; on `free`, match by address and mark the record as freed; register `atexit(mem_report)` so the remaining unreleased entries are printed at exit.
