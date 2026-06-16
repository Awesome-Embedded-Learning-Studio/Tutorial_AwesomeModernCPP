---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Design and implement a type-safe dynamic array library from scratch.
  We will explore memory resizing strategies, error handling patterns, and API design
  principles, paving the way for a deep understanding of `std::vector`.
difficulty: intermediate
order: 105
platform: host
prerequisites:
- 指针进阶：多级指针、指针与 const
- 动态内存管理：malloc/free/realloc 的正确使用
- 结构体、联合体与内存对齐
- C 语言陷阱与常见错误
reading_time_minutes: 18
tags:
- host
- cpp-modern
- intermediate
- 进阶
- 容器
- 内存管理
title: Implementing a Dynamic Vector from Scratch
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/05-handmade-dynamic-array.md
  source_hash: 8624b8af8483340173b64ce6df2bb5883c2e94cfbd8ef3b3fe473fedf49fc6ef
  translated_at: '2026-06-16T05:55:19.070534+00:00'
  engine: anthropic
  token_count: 3962
---
# Building a Dynamic Array from Scratch — Implementing a Container from Zero

One of the most painful aspects of writing C programs is that array sizes must be determined at compile time. You need to store ten items, so you declare `int arr[10]`. Later, requirements change to store one hundred, so you have to go back, modify the code, and recompile. Even worse, in many cases, you simply don't know how many items will arrive at runtime—how many user records, network packets, or sensor samples will be processed—these are only known at runtime.

`malloc` does solve the problem of uncertain size, but it only handles allocation, not growth. If it fills up and you want to add more, you must manually `realloc`, manage capacity yourself, and handle errors on your own. `malloc`, `realloc`, `free`, and `size` variables scattered throughout the codebase quickly become a maintenance nightmare. In Python, you can simply write `list.append(x)`, and in C++, you have `std::vector`—both handle resizing automatically. However, the C standard library lacks such a utility, so we must build it ourselves.

Today, we will start from zero and hand-roll a complete dynamic array library. Through this process, we will clarify data structure design, memory expansion and contraction strategies, error handling patterns, and finally compare our implementation with C++'s `std::vector` to see how the standard library handles these tasks.

> **Learning Objectives**
>
> - [ ] Understand the necessity of the three-field design: size, capacity, and data.
> - [ ] Master the 2x expansion strategy and its amortized O(1) complexity analysis.
> - [ ] Understand when to shrink capacity to avoid frequent `realloc` calls.
> - [ ] Master the error handling pattern using enum return codes.
> - [ ] Be able to independently design a complete CRUD API.
> - [ ] Understand the internal mechanisms of `std::vector` and its correspondence to the C implementation.

## Environment Setup

All code examples in this article are compiled and run in a standard C environment. It is recommended to always compile with `-Wall -Wextra`—implementing a dynamic array involves extensive pointer arithmetic and `memcpy`/`memmove` calls, so compiler warnings can help you catch many potential issues.

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
依赖：无
```

## Step One — Understanding What a Dynamic Array Actually Is

From a physical storage perspective, a dynamic array is essentially a contiguous block of memory, no different from a standard array. The key difference is that a dynamic array separates "used space" from "reserved space" and accesses this memory indirectly via a pointer. This allows it to swap in a larger block when necessary. You can think of it as a warehouse that automatically "moves to a bigger building"—when the shelves are full, we move to a warehouse with more shelves, carrying all the old goods with us. To the outside world, the address changes, but the interface for storing and retrieving goods remains the same.

Let's start with a very basic prototype:

```c
typedef struct {
    void* data;          // 连续内存块
    size_t size;         // 当前有多少个元素
} DynamicArray;
```

`data` points to a contiguous block of memory allocated on the heap, and `size` records the current number of elements. However, you will notice a critical issue: we are using `void*`, so we do not know the size of each element. For an `int` array, the stride is four bytes; for `double`, it is eight bytes; and for a custom struct, it could be tens of bytes. Without the element size information, we cannot locate the Nth element at all.

Therefore, we need to add `capacity` and `element_size`:

```c
typedef struct _DynamicArray_ {
    void* data;              // 连续内存块（存储实际数据）
    size_t size;             // 当前元素个数
    size_t capacity;         // 当前分配的总容量（元素个数计）
    size_t element_size;     // 单个元素的字节大小
} DynamicArray;
```

These four fields each serve a specific purpose: `data` manages "where it exists," `size` manages "how many are used," `capacity` manages "total slots," and `element_size` manages "size of each slot." With `element_size`, calculating the address of the $i$-th element is `(char*)data + i * element_size`—we must cast to `char*` first because `char` is exactly one byte, ensuring the pointer arithmetic results in a precise byte offset. Performing arithmetic directly on `void*` causes a compiler error (the C standard forbids it; while GCC allows it as an extension, it is not portable).

> ⚠️ **Warning**
> `size` represents "how many valid elements there actually are," while `capacity` represents "how many elements this memory block can hold at most," so `size <= capacity`. If you use `capacity` instead of `size` as the upper bound during iteration, you will read uninitialized garbage data.

The internal data layout of `std::vector` is almost identical to ours, except that the template parameter `T` replaces the `void*` + `element_size` combination, ensuring type safety is guaranteed at compile time. `sizeof(std::vector<int>)` is 24 bytes in most implementations—three 8-byte fields (pointer + size + capacity)—and `element_size` does not need to be stored after template instantiation.

## Step 2 — Establishing an Error Handling System

Before writing functional logic, let's address an engineering problem: what do we do when a function fails? The laziest approach is to call `exit(-1)` immediately upon error—this is common in educational code, but it is a disaster in real-world engineering. You wouldn't want to kill the entire server process just because a single `push_back` failed, right?

We use an enumeration to establish a clear error code system:

```c
typedef enum _DynamicArrayStatus_ {
    kSuccess            = 0,    // 正常执行
    kNullPointer        = -1,   // 传入了 NULL 指针
    kOutOfMemory        = 1,    // 内存分配失败
    kIndexOutOfRange    = -2,   // 下标越界
    kInvalidOperation   = -3    // 非法操作（如对空数组 pop）
} DynamicArrayStatus;
```

Every function returns a `DynamicArrayStatus`, allowing the caller to determine whether the operation succeeded and the reason for any failure. We can use helper macros to output friendly error messages:

```c
#define SHOW_ERROR(err)                                                      \
    do {                                                                     \
        const char* msg = "";                                                \
        switch (err) {                                                       \
            case kNullPointer:      msg = "NULL pointer passed";      break; \
            case kOutOfMemory:      msg = "Memory allocation failed";  break; \
            case kIndexOutOfRange:  msg = "Index out of range";       break; \
            case kInvalidOperation: msg = "Invalid operation";        break; \
            default: break;                                                  \
        }                                                                    \
        fprintf(stderr, "[DynamicArray Error] %s\n", msg);                   \
    } while (0)
```

Separating error message display from error code generation is a better practice—the caller might want to log errors to a file instead of printing to the terminal, or perform resource cleanup after an error occurs. Returning an enumeration code gives the caller full control.

## Step 3 — Implementing Creation and Destruction

### Creation — Factory Functions

In object-oriented languages, this is called a constructor; in C, we call it a factory function—it "produces" an initialized object and returns it to the caller.

```c
/// @brief 创建一个动态数组
/// @param initial_capacity 初始容量
/// @param element_size 单个元素的字节大小
/// @return 指向新创建的动态数组的指针，失败返回 NULL
DynamicArray* dynamic_array_create(size_t initial_capacity, size_t element_size)
{
    DynamicArray* arr = (DynamicArray*)malloc(sizeof(DynamicArray));
    if (arr == NULL) {
        return NULL;
    }

    size_t actual_capacity = (initial_capacity < 8) ? 8 : initial_capacity;
    arr->data = malloc(actual_capacity * element_size);
    if (arr->data == NULL) {
        free(arr);  // 数据区失败，但结构体已分配，记得释放！
        return NULL;
    }

    arr->size = 0;
    arr->capacity = actual_capacity;
    arr->element_size = element_size;
    return arr;
}
```

We must check the `malloc` return value immediately after allocating the structure memory. If we access `arr->data` without checking, the program will immediately segfault. We set a minimum capacity of eight as a heuristic; a value that is too small causes frequent reallocations, while a value that is too large wastes memory.

> ⚠️ **Warning**
> Pay attention to the presence of `free(arr)`. This is a classic resource leak scenario: the structure allocation succeeds, but the data area allocation fails. If you simply `return NULL` without `free(arr)`, that structure memory is leaked forever. This situation, where "some resources are allocated but subsequent steps fail," is one of the most error-prone aspects of C memory management.

Usage:

```c
DynamicArray* nums = dynamic_array_create(16, sizeof(int));
if (nums == NULL) {
    fprintf(stderr, "Failed to create dynamic array\n");
    return -1;
}
```

Use `sizeof(int)` instead of hardcoding `4`—the size of `int` may vary across different platforms. `sizeof` is calculated at compile time and incurs no runtime overhead.

### Destruction—deallocation order must not be reversed

```c
/// @brief 销毁动态数组，释放所有内存
DynamicArrayStatus dynamic_array_destroy(DynamicArray* arr)
{
    if (arr == NULL) {
        return kNullPointer;
    }
    free(arr->data);   // 先释放数据区
    free(arr);          // 再释放结构体
    return kSuccess;
}
```

The deallocation order cannot be reversed—if we call `free(arr)` first, then `arr->data` becomes an access to freed memory (Use After Free). Another issue is that the `arr` pointer itself does not become `NULL` after `destroy`; it still points to that freed memory block. Since C function arguments are passed by value, we must rely on the caller to manually set it to `NULL`:

```c
dynamic_array_destroy(nums);
nums = NULL;  // 手动置 NULL，防止后续误用
```

The RAII mechanism of `std::vector` cements this create/destroy pairing at the language level—the destructor is automatically called when the object leaves scope, so memory absolutely cannot leak. In our C version, every step of resource management relies on manual discipline.

## Step 4 — Mastering Capacity Management

### Reallocation — The 2x Growth Strategy

When `size == capacity`, the array is full, and inserting a new element requires reallocation. The question is: how much should we grow? If we increase by one each time, inserting N elements consecutively requires N calls to `realloc`, resulting in a total copy volume of 1 + 2 + ... + N = O(N²), which is completely unacceptable. Doubling the capacity—whenever full, we double the space—requires only about log₂(N) reallocations, with a total copy volume of ≈ 2N = O(N). Amortized over each insertion, this is O(1). It's like moving house: instead of buying one more box every time, you double the floor area of the house—the move itself is exhausting, but averaged out over the days, it's hardly noticeable.

```c
/// @brief 将容量扩展到至少 min_capacity
DynamicArrayStatus dynamic_array_reserve(DynamicArray* arr, size_t min_capacity)
{
    if (arr == NULL) return kNullPointer;
    if (min_capacity <= arr->capacity) return kSuccess;

    size_t new_capacity = arr->capacity * 2;
    if (new_capacity < min_capacity) new_capacity = min_capacity;

    void* new_data = realloc(arr->data, new_capacity * arr->element_size);
    if (new_data == NULL) return kOutOfMemory;

    arr->data = new_data;
    arr->capacity = new_capacity;
    return kSuccess;
}
```

`realloc` attempts to expand the memory block in place. If that isn't possible, it finds a larger block on the heap and copies the old data over. In either case, the returned pointer points to valid memory, and the old data remains intact.

> ⚠️ **Warning**
> `realloc` might return a different address! You must update the pointer using `arr->data = new_data`. If you write `realloc(arr->data, ...)` without capturing the return value, you lose the new address after the move, and the memory at the old address is freed—a double disaster.

### Shrinking—Avoiding Thrashing

If an array grows to 10,000 elements but later shrinks to just 10, the memory for 9,990 elements is wasted. However, the timing for shrinking is much trickier than for expansion. Consider an array oscillating between 100 and 50 elements: shrinking at 50, followed immediately by an insertion that expands it back to 100, causes constant churn—a classic "thrashing" problem. Our strategy is to shrink to `size` while maintaining a minimum capacity of 8, triggered by an explicit call from the user:

```c
/// @brief 将容量缩减到接近实际大小
DynamicArrayStatus dynamic_array_shrink_to_fit(DynamicArray* arr)
{
    if (arr == NULL) return kNullPointer;

    size_t new_capacity = (arr->size < 8) ? 8 : arr->size;
    if (new_capacity >= arr->capacity) return kSuccess;

    void* new_data = realloc(arr->data, new_capacity * arr->element_size);
    if (new_data == NULL) return kOutOfMemory;  // 缩容失败不影响现有数据

    arr->data = new_data;
    arr->capacity = new_capacity;
    return kSuccess;
}
```

`shrink_to_fit` is typically called only when we are certain the container will not grow significantly again, such as after data loading has finished. The C++ standard does not mandate that the `std::vector` growth factor must be 2x—MSVC uses 1.5x, while libstdc++ and libc++ use 2x. While 1.5x offers better memory utilization, it results in slightly more frequent reallocations.

## Step 5 — Implementing Element Access

We provide two access methods: a fast version that does not check bounds (similar to `std::vector::operator[]`), and a safe version that performs bounds checking (similar to `std::vector::at()`).

```c
/// @brief 不检查边界的快速访问
void* dynamic_array_at_unchecked(const DynamicArray* arr, size_t index)
{
    return (char*)arr->data + index * arr->element_size;
}

/// @brief 带边界检查的安全访问
DynamicArrayStatus dynamic_array_at(
    const DynamicArray* arr, size_t index, void* out
)
{
    if (arr == NULL || out == NULL) return kNullPointer;
    if (index >= arr->size) return kIndexOutOfRange;
    memcpy(out, (char*)arr->data + index * arr->element_size, arr->element_size);
    return kSuccess;
}
```

The safe version returns a copy to the caller's buffer. Since C lacks references and the data area is a `void*`, the function cannot directly return a value of the correct type. This is indeed more cumbersome than C++'s `vec.at(i)`, but it is the cost of generic programming in C.

```c
// 使用示例
DynamicArray* nums = dynamic_array_create(8, sizeof(int));
int val = 42;
dynamic_array_push_back(nums, &val);

int* p = (int*)dynamic_array_at_unchecked(nums, 0);
printf("%d\n", *p);  // 42

int out;
dynamic_array_at(nums, 0, &out);
printf("%d\n", out);  // 42
```

## Step 6 — Implementing Add and Remove Operations

### push_back — Appending to the End

```c
/// @brief 在数组尾部追加一个元素
DynamicArrayStatus dynamic_array_push_back(DynamicArray* arr, const void* element)
{
    if (arr == NULL || element == NULL) return kNullPointer;

    if (arr->size >= arr->capacity) {
        DynamicArrayStatus s = dynamic_array_reserve(arr, arr->capacity * 2);
        if (s != kSuccess) return s;
    }

    memcpy(
        (char*)arr->data + arr->size * arr->element_size,
        element,
        arr->element_size
    );
    arr->size++;
    return kSuccess;
}
```

The destination address for `memcpy` is `(char*)arr->data + arr->size * arr->element_size`—skipping all existing elements to reach the first empty slot. Due to the 2x growth strategy, the total time for N consecutive `push_back` operations is O(N), which is amortized O(1).

Let's verify the resizing behavior:

```c
DynamicArray* nums = dynamic_array_create(4, sizeof(int));
printf("Initial: size=%zu, capacity=%zu\n", nums->size, nums->capacity);

for (int i = 0; i < 20; i++) {
    dynamic_array_push_back(nums, &i);
}
printf("After 20 pushes: size=%zu, capacity=%zu\n", nums->size, nums->capacity);
dynamic_array_destroy(nums);
nums = NULL;
```

```text
Initial: size=0, capacity=8
After 20 pushes: size=20, capacity=32
```

The initial capacity of four is guaranteed to be eight. After inserting 20 elements, it undergoes two reallocations: 8 -> 16 -> 32.

### pop_back——Tail Deletion

```c
/// @brief 删除数组尾部的元素
DynamicArrayStatus dynamic_array_pop_back(DynamicArray* arr)
{
    if (arr == NULL) return kNullPointer;
    if (arr->size == 0) return kInvalidOperation;
    arr->size--;
    return kSuccess;
}
```

The "deleted" elements are still physically present in memory and will be overwritten during the next `push_back`.

> ⚠️ **Warning**
> We do not trigger capacity reduction after `pop_back`—if we shrink immediately after a `pop` only to `push` again right away, the effort is wasted. Shrinking should be explicitly triggered by the caller via `shrink_to_fit`. `std::vector::pop_back` follows the same design.

### insert and erase — Insertion and Deletion in the Middle

`insert` needs to shift all elements after the insertion position back by one spot, while `erase` shifts elements forward by one spot to overwrite the deleted element. Both operations must use `memmove` instead of `memcpy`—because the source and destination memory regions overlap, and `memcpy` has undefined behavior when dealing with overlapping memory.

```c
/// @brief 在指定位置插入一个元素
DynamicArrayStatus dynamic_array_insert(
    DynamicArray* arr, size_t index, const void* element
)
{
    if (arr == NULL || element == NULL) return kNullPointer;
    if (index > arr->size) return kIndexOutOfRange;

    if (arr->size >= arr->capacity) {
        DynamicArrayStatus s = dynamic_array_reserve(arr, arr->capacity * 2);
        if (s != kSuccess) return s;
    }

    memmove(
        (char*)arr->data + (index + 1) * arr->element_size,
        (char*)arr->data + index * arr->element_size,
        (arr->size - index) * arr->element_size
    );
    memcpy(
        (char*)arr->data + index * arr->element_size,
        element,
        arr->element_size
    );
    arr->size++;
    return kSuccess;
}

/// @brief 删除指定位置的元素
DynamicArrayStatus dynamic_array_erase(DynamicArray* arr, size_t index)
{
    if (arr == NULL) return kNullPointer;
    if (index >= arr->size) return kIndexOutOfRange;

    memmove(
        (char*)arr->data + index * arr->element_size,
        (char*)arr->data + (index + 1) * arr->element_size,
        (arr->size - index - 1) * arr->element_size
    );
    arr->size--;
    return kSuccess;
}
```

Verify `insert` and `erase`:

```c
DynamicArray* nums = dynamic_array_create(8, sizeof(int));
for (int i = 0; i < 5; i++) dynamic_array_push_back(nums, &i);  // [0,1,2,3,4]
int val = 99;
dynamic_array_insert(nums, 2, &val);    // [0,1,99,2,3,4]
dynamic_array_erase(nums, 0);           // [1,99,2,3,4]

for (size_t i = 0; i < nums->size; i++) {
    printf("%d ", *(int*)dynamic_array_at_unchecked(nums, i));
}
printf("\n");
dynamic_array_destroy(nums);
nums = NULL;
```

```text
1 99 2 3 4
```

`std::vector::push_back` has an overload for rvalue references since C++11, allowing us to use move semantics to avoid deep copies. Our C version, however, can only perform shallow copies via `memcpy`. If an element contains dynamically allocated memory (such as a string allocated by `malloc`), a shallow copy will lead to a double free crash. This is a fundamental limitation of generic programming in C.

## Step 7 — Implementing Traversal and Search

### Traversal — Callback Function Pattern

Since the container uses `void*` internally, it is unaware of the element type. Therefore, the caller must inform the container "how to process each element" via a callback function—a form of "Inversion of Control":

```c
/// @brief 遍历动态数组，对每个元素调用回调函数
DynamicArrayStatus dynamic_array_foreach(
    const DynamicArray* arr,
    void (*callback)(void* element)
)
{
    if (arr == NULL || callback == NULL) return kNullPointer;
    for (size_t i = 0; i < arr->size; i++) {
        callback((char*)arr->data + i * arr->element_size);
    }
    return kSuccess;
}
```

```c
void print_int(void* element) {
    printf("%d ", *(int*)element);
}

DynamicArray* nums = dynamic_array_create(8, sizeof(int));
for (int i = 10; i <= 50; i += 10) dynamic_array_push_back(nums, &i);
dynamic_array_foreach(nums, print_int);
printf("\n");
```

```text
10 20 30 40 50
```

The callback function pattern is widely used in the C standard library—this is the approach taken by the comparison function in `qsort` and `bsearch`.

### Searching — Linear Search

"Equality comparison" must also be provided by the caller:

```c
/// @brief 在动态数组中查找元素
/// @return 找到返回下标，否则返回 SIZE_MAX
size_t dynamic_array_find(
    const DynamicArray* arr,
    const void* target,
    int (*compare)(const void*, const void*)
)
{
    if (arr == NULL || target == NULL || compare == NULL) return SIZE_MAX;
    for (size_t i = 0; i < arr->size; i++) {
        void* current = (char*)arr->data + i * arr->element_size;
        if (compare(current, target) != 0) return i;
    }
    return SIZE_MAX;
}
```

The time complexity is O(N). If we need faster performance, we can sort first and then use binary search. C++'s `std::find` uses iterators combined with lambda expressions, which is much more elegant to write than callback functions; C++20 Ranges turn traversal, filtering, and transformation into chained calls.

## C++ Comparison: Design Trade-offs of std::vector

At this point, we have implemented a complete dynamic array library from scratch. Let's systematically compare this with `std::vector`. Understanding these design trade-offs is far more important than memorizing the API.

We used `void*` to achieve generic programming, which introduced three problems: lack of type safety, the need to manually pass `element_size`, and the requirement for forced type casting in callback functions. `std::vector<T>` uses templates to perfectly solve all three—the compiler determines type `T` during instantiation, all type checks are completed at compile time, and `sizeof(T)` is calculated automatically. The `std::vector` destructor automatically releases the internal array, whether the function returns normally or exits due to an exception. This embodies the core idea of RAII—binding the resource lifecycle to the object lifecycle. C++11 move semantics make `vec2 = std::move(vec1)` an O(1) pointer swap, whereas in C, we can only `memcpy` the entire block of data.

There are two functions that are easily confused: `reserve(n)` only changes `capacity` without changing `size`, pre-allocating memory but not creating new elements; `resize(n)` changes `size`, filling extra positions with value-initialized values and destructing excess elements. Our C version only implemented `reserve`; `resize` is left as an exercise. Additionally, `std::vector<bool>` is optimized for bit compression (each `bool` takes up only 1 bit), but at the cost of not being able to take the address of individual elements. C++17's `std::span<T>` provides a non-owning view of contiguous memory and is a very important composition tool.

## Exercises

The following exercises provide only the function signature and requirement descriptions. The implementation is left blank.

### Exercise 1: Implement resize

`reserve` only changes capacity, not size, whereas `resize` needs to change size. When the new size is greater than the old size, the extra positions should be filled with default values.

```c
/// @brief 改变动态数组的元素个数
/// @param default_value 指向默认值的指针（用于填充新增位置），可以为 NULL（填零）
DynamicArrayStatus dynamic_array_resize(
    DynamicArray* arr,
    size_t new_size,
    const void* default_value
);
// 练习： 自行实现
```

### Exercise 2: Implement filter

Given a dynamic array and a filter predicate, return a newly created dynamic array containing only the elements that satisfy the condition.

```c
/// @brief 根据谓词过滤动态数组的元素
DynamicArray* dynamic_array_filter(
    const DynamicArray* arr,
    int (*pred)(const void* element)
);
// 练习： 自行实现
```

### Exercise 3: Implement map transformation

Given a dynamic array and a transformation function, we apply the transformation function to each element and store the results in a new array to return.

```c
/// @brief 对动态数组的每个元素应用变换函数
/// @param out_element_size 输出数组的元素大小（可能与输入不同）
DynamicArray* dynamic_array_map(
    const DynamicArray* arr,
    void (*transform)(const void* in, void* out),
    size_t out_element_size
);
// 练习： 自行实现
```

### Exercise 4: Implementing Concatenation

Concatenate two dynamic arrays of the same type into a new dynamic array.

```c
/// @brief 将两个动态数组拼接成一个新的动态数组
DynamicArray* dynamic_array_concat(
    const DynamicArray* arr1,
    const DynamicArray* arr2
);
// 练习： 自行实现
```

> **Self-Assessment**: If you find the implementation exercises difficult, please review the design rationale from the corresponding sections. Specifically for `resize`—it is essentially a combination of `reserve` + `memset`/`memcpy`. Once you clarify which positions need filling and what values to fill them with, the code will follow naturally.

## Reference Resources

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
