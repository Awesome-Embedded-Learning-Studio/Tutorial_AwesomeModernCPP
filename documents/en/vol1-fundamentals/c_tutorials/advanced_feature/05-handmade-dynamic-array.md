---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Design and implement a type-safe dynamic array library from scratch — memory growth and shrink strategies, error-handling patterns, and API design principles, paving the road toward std::vector
difficulty: intermediate
order: 105
platform: host
prerequisites:
- 'Advanced Pointers: Multilevel Pointers, Pointers and const'
- 'Dynamic Memory Management: Using malloc/free/realloc Correctly'
- Structures, Unions, and Memory Alignment
- C Pitfalls and Common Errors
reading_time_minutes: 18
tags:
- host
- cpp-modern
- intermediate
- 进阶
- 容器
- 内存管理
title: Building a Dynamic Array from Scratch — Implementing a Container
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/05-handmade-dynamic-array.md
  source_hash: 60babcaf5657547423feb32e0bf4140d99f6c315e54f285726a74ab5f70f6d2b
  translated_at: '2026-09-25T13:52:30+00:00'
  engine: anthropic
  token_count: 8600
---
# Building a Dynamic Array from Scratch — Implementing a Container

One of the most painful things about writing C is that array sizes must be pinned down at compile time. You want to store ten items, so you declare `int arr[10]`; then the requirements change and you need a hundred, and you have to go back, edit the code, and recompile. Worse, in many situations you have no idea how much data will arrive at runtime — how many records the user enters, how many packets come in over the network, how many samples the sensors collect. None of that is known until the program is actually running.

`malloc` does solve the uncertain-size problem, but it only allocates — it doesn't grow. Once the block is full and you want to keep adding, you have to `realloc` by hand, manage the capacity yourself, and handle errors yourself. `malloc/realloc/free` calls and `size` variables scattered all over the codebase quickly become a maintenance nightmare. In Python you can casually write `list.append(x)`, and in C++ you have `std::vector` — both grow automatically. The C standard library has no such thing, so we have to build one ourselves.

Today we'll start from zero and hand-build a complete dynamic array library. Along the way we'll get clear on data structure design, growth and shrink strategies, and error-handling patterns — and finally compare against C++'s `std::vector` to see how the standard library handles these things.

Every code example in this article compiles and runs under standard C. Always compile with `-Wall -Wextra` — a dynamic array implementation involves a lot of pointer arithmetic and `memcpy/memmove` calls, and compiler warnings will catch quite a few potential problems for you.

```text
Platform: Linux / macOS / Windows (MSVC/MinGW)
Compiler: GCC >= 9 or Clang >= 12
Standard: -std=c11 (the C parts) / -std=c++17 (the C++ comparison parts)
Dependencies: none
```

## Step 1 — Figure Out What a Dynamic Array Actually Is

From the physical storage point of view, a dynamic array is still just one contiguous block of memory, no different from an ordinary array. The crucial difference is that a dynamic array separates "space in use" from "reserved space", and reaches that memory indirectly through a pointer — which is exactly what lets it swap in a bigger block when needed. Picture a warehouse that automatically "moves to a bigger house": when the shelves fill up, you move into a warehouse with more shelves and carry all the goods over. To the outside world the warehouse address has changed, but the interface for storing and retrieving goods has not.

Let's start with the simplest possible sketch:

```c
typedef struct {
    void* data;          // Contiguous memory block
    size_t size;         // How many elements it currently holds
} DynamicArray;
```

`data` points to contiguous memory allocated on the heap, and `size` records the current element count. But you'll notice a fatal problem: we're using `void*`, so we don't know how big each element is. For an `int` array the stride is 4 bytes, for `double` it's 8, and a custom struct might be dozens of bytes. Without the element size, we simply cannot locate the Nth element.

So we need to add `capacity` and `element_size`:

```c
typedef struct _DynamicArray_ {
    void* data;              // Contiguous memory block (stores the actual data)
    size_t size;             // Current number of elements
    size_t capacity;         // Total allocated capacity (in elements)
    size_t element_size;     // Size of a single element in bytes
} DynamicArray;
```

The four fields each do one job: `data` answers "where is it stored", `size` answers "how many are in use", `capacity` answers "how many slots exist in total", and `element_size` answers "how big is each slot". With `element_size` in hand, the address of the `i`-th element is `(char*)data + i * element_size` — and the cast to `char*` has to come first, because `char` is exactly 1 byte, which makes the pointer arithmetic an exact byte offset. Adding to or subtracting from a `void*` directly is a compile error (the C standard forbids it; GCC allows it as an extension, but it isn't portable).

`size` means "how many valid elements there actually are"; `capacity` means "how many elements this memory can hold at most"; `size <= capacity`. If you iterate using `capacity` instead of `size` as the upper bound, you'll read uninitialized garbage.

The internal data layout of `std::vector` is almost identical to ours — except that the template parameter `T` replaces the `void*` + `element_size` combination, so type safety is guaranteed at compile time. On most implementations `sizeof(std::vector<int>)` is 24 bytes — three 8-byte fields (pointer + size + capacity); once the template is instantiated, `element_size` no longer needs to be stored.

## Step 2 — Build an Error-Handling System

Before writing the functional functions, let's settle an engineering question first: what happens when a function fails? The laziest answer is to `exit(-1)` the moment anything goes wrong — common in teaching code, but a genuine disaster in real projects. You can't kill an entire server process just because one `push_back` failed, can you?

We'll use an enum to build a clear system of error codes:

```c
typedef enum _DynamicArrayStatus_ {
    kSuccess            = 0,    // Completed normally
    kNullPointer        = -1,   // A NULL pointer was passed
    kOutOfMemory        = 1,    // Memory allocation failed
    kIndexOutOfRange    = -2,   // Index out of range
    kInvalidOperation   = -3    // Invalid operation (e.g. popping an empty array)
} DynamicArrayStatus;
```

Every function returns a `DynamicArrayStatus`, so callers can tell whether an operation succeeded and why it failed. Paired with a helper macro, we can also print friendly error messages:

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

Separating the *display* of error messages from the *generation* of error codes is the better design — a caller may want to write errors to a log file rather than print to the terminal, or may want to clean up resources after a failure. Enum return codes hand complete control to the caller.

## Step 3 — Implement Creation and Destruction

### Creation — the Factory Function

In object-oriented languages this is called a constructor; in C we call it a factory function — it "manufactures" a fully initialized object and hands it back to the caller.

```c
/// @brief Create a dynamic array
/// @param initial_capacity the initial capacity
/// @param element_size the size of a single element in bytes
/// @return pointer to the newly created dynamic array, NULL on failure
DynamicArray* dynamic_array_create(size_t initial_capacity, size_t element_size)
{
    DynamicArray* arr = (DynamicArray*)malloc(sizeof(DynamicArray));
    if (arr == NULL) {
        return NULL;
    }

    size_t actual_capacity = (initial_capacity < 8) ? 8 : initial_capacity;
    arr->data = malloc(actual_capacity * element_size);
    if (arr->data == NULL) {
        free(arr);  // The data region failed but the struct is already allocated — free it!
        return NULL;
    }

    arr->size = 0;
    arr->capacity = actual_capacity;
    arr->element_size = element_size;
    return arr;
}
```

Right after allocating the struct memory, you must check the `malloc` return value — touch `arr->data` without checking and the program segfaults on the spot. The minimum capacity of 8 is a rule of thumb: any smaller and the array regrows constantly, any larger and memory is wasted.

Notice the `free(arr)` there. This is a textbook resource-leak scenario: the struct allocation succeeded, but the data-region allocation failed. If you just `return NULL` without the `free(arr)`, that struct memory is leaked forever. This "part of the resources were allocated, then a later step failed" situation is precisely where C memory management goes wrong most often.

Usage:

```c
DynamicArray* nums = dynamic_array_create(16, sizeof(int));
if (nums == NULL) {
    fprintf(stderr, "Failed to create dynamic array\n");
    return -1;
}
```

Pass `sizeof(int)`, never a hardcoded `4` — the size of `int` varies across platforms, and `sizeof` is computed at compile time with zero runtime cost.

### Destruction — the Free Order Must Not Be Reversed

```c
/// @brief Destroy the dynamic array and free all memory
DynamicArrayStatus dynamic_array_destroy(DynamicArray* arr)
{
    if (arr == NULL) {
        return kNullPointer;
    }
    free(arr->data);   // Free the data region first
    free(arr);          // Then free the struct
    return kSuccess;
}
```

The free order must not be reversed — if you `free(arr)` first, then `arr->data` is an access to freed memory (use after free). There's a second problem: after `destroy`, the `arr` pointer itself doesn't become `NULL`; it still points at that freed memory. C passes function arguments by value, so all we can rely on is the caller's discipline to set it to NULL by hand:

```c
dynamic_array_destroy(nums);
nums = NULL;  // Set to NULL manually to prevent accidental reuse
```

`std::vector` locks this create/destroy pairing into the language itself through RAII — the destructor runs automatically when the object leaves scope, and leaking memory becomes essentially impossible. In our C version, every single resource-management step depends on human discipline.

## Step 4 — Get Capacity Management Right

### Growing — the 2x Strategy

When `size == capacity`, the array is full and the next insertion has to grow it. Grow by how much, though? Adding 1 each time means N consecutive insertions trigger N `realloc` calls, for a total copy volume of 1 + 2 + ... + N = O(N^2) — utterly unacceptable. Doubling — every time the array fills, double the capacity — needs only about log₂(N) regrows, with a total copy volume ≈ 2N = O(N), amortized to O(1) per insertion. It's like moving house: instead of buying one more box every time, you double your floor area on each move — that one move is exhausting, but spread across the days you barely notice it.

```c
/// @brief Grow the capacity to at least min_capacity
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

`realloc` first tries to expand in place; if that doesn't work, it finds a bigger region on the heap and copies the old data over. Either way, the returned pointer refers to valid memory and the old data is intact.

`realloc` may return a different address! You must update the pointer with `arr->data = new_data`. If you write `realloc(arr->data, ...)` and throw away the return value, you lose the new address after the move — and the memory at the old address has already been freed. A double disaster.

### Shrinking — Avoiding Thrashing

If an array once grew to 10,000 elements and later gets trimmed down to 10, the memory for 9,990 elements is just sitting there wasted. But the timing of shrinking is far more delicate than that of growing — picture an array oscillating back and forth between 100 and 50: shrink the moment it drops to 50, and the very next insertion grows it right back to 100 — that back-and-forth is the classic "thrashing" problem. Our strategy shrinks down to `size` but keeps a minimum capacity of 8, and it is invoked explicitly by the caller:

```c
/// @brief Shrink the capacity to roughly the actual size
DynamicArrayStatus dynamic_array_shrink_to_fit(DynamicArray* arr)
{
    if (arr == NULL) return kNullPointer;

    size_t new_capacity = (arr->size < 8) ? 8 : arr->size;
    if (new_capacity >= arr->capacity) return kSuccess;

    void* new_data = realloc(arr->data, new_capacity * arr->element_size);
    if (new_data == NULL) return kOutOfMemory;  // A failed shrink leaves the existing data untouched

    arr->data = new_data;
    arr->capacity = new_capacity;
    return kSuccess;
}
```

`shrink_to_fit` is normally called only when you're confident the array won't grow substantially again — after data loading finishes, for example. The C++ standard doesn't mandate a growth factor for `std::vector`: MSVC uses 1.5x, while libstdc++ and libc++ use 2x. 1.5x makes better use of memory, at the cost of slightly more regrows.

## Step 5 — Implement Element Access

We offer two access styles: a fast, unchecked version (like `std::vector::operator[]`) and a safe, bounds-checked version (like `std::vector::at()`).

```c
/// @brief Fast access without bounds checking
void* dynamic_array_at_unchecked(const DynamicArray* arr, size_t index)
{
    return (char*)arr->data + index * arr->element_size;
}

/// @brief Safe access with bounds checking
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

The safe version works by copying into a caller-supplied buffer, because C has no references and the data region is `void*`, so the function can't directly return a correctly typed value. It really is clumsier than C++'s `vec.at(i)`, but that's the price of generic programming in C.

```c
// Usage example
DynamicArray* nums = dynamic_array_create(8, sizeof(int));
int val = 42;
dynamic_array_push_back(nums, &val);

int* p = (int*)dynamic_array_at_unchecked(nums, 0);
printf("%d\n", *p);  // 42

int out;
dynamic_array_at(nums, 0, &out);
printf("%d\n", out);  // 42
```

## Step 6 — Implement Insertion and Deletion

### push_back — Appending at the End

```c
/// @brief Append an element to the end of the array
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

The `memcpy` destination is `(char*)arr->data + arr->size * arr->element_size` — skip past every existing element to land on the first empty slot. Thanks to the 2x growth strategy, N consecutive `push_back` calls cost O(N) total time, O(1) amortized.

Let's watch the growth in action:

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

The initial capacity of 4 was raised to the guaranteed minimum of 8, and after 20 insertions the array went through two regrows: 8 -> 16 -> 32.

### pop_back — Removing from the End

```c
/// @brief Remove the element at the end of the array
DynamicArrayStatus dynamic_array_pop_back(DynamicArray* arr)
{
    if (arr == NULL) return kNullPointer;
    if (arr->size == 0) return kInvalidOperation;
    arr->size--;
    return kSuccess;
}
```

The "removed" element is still lying there in memory; the next `push_back` will overwrite it.

We deliberately don't trigger a shrink after `pop_back` — if you `pop` and then immediately `push` again, the shrink was wasted work. Shrinking should be an explicit `shrink_to_fit` call from the caller. `std::vector::pop_back` makes the same design choice.

### insert and erase — Inserting and Removing in the Middle

`insert` has to shift every element after the insertion position one slot toward the tail; `erase` shifts the elements one slot toward the head to overwrite the removed one. Both must use `memmove` rather than `memcpy` — the source and destination regions overlap, and `memcpy` on overlapping regions is undefined behavior.

```c
/// @brief Insert an element at the given position
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

/// @brief Remove the element at the given position
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

Verifying insert and erase:

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

Since C++11, `std::vector::push_back` has an rvalue-reference overload that accepts move semantics and avoids deep copies. Our C version can only shallow-copy through `memcpy` — if an element owns dynamically allocated memory (say, a `malloc`'d string), a shallow copy leads to a double-free crash. This is a fundamental limitation of generic programming in C.

## Step 7 — Implement Traversal and Search

### Traversal — the Callback Pattern

The container's interior is `void*` and knows nothing about the element type, so "what to do with each element" has to be supplied by the caller as a callback function — a kind of inversion of control:

```c
/// @brief Iterate over the dynamic array, invoking the callback on each element
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

The callback pattern is used all over the C standard library — `qsort`'s comparator and `bsearch` both follow this exact recipe.

### Search — Linear Search

"Comparing for equality" also has to come from the caller:

```c
/// @brief Find an element in the dynamic array
/// @return the index if found, SIZE_MAX otherwise
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

Time complexity is O(N). If you need faster, sort first and switch to binary search. C++'s `std::find` pairs iterators with lambda expressions and reads far more elegantly than callbacks; and C++20 Ranges turns traversal, filtering, and transformation into chained calls.

## C++ Comparison — The Design Trade-offs of std::vector

At this point we have hand-built a complete dynamic array library. Let's step back and compare it systematically with `std::vector` — understanding these trade-offs matters far more than memorizing APIs.

Our `void*`-based genericity creates three problems: no type checking, a manually passed `element_size`, and forced casts inside callbacks. `std::vector<T>` solves all three with templates — the compiler pins down the type `T` at instantiation, all type checking happens at compile time, and `sizeof(T)` is computed automatically. `std::vector`'s destructor frees the internal array whether the function returns normally or exits through an exception — that is the core idea of RAII: the resource's lifetime is bound to the object's lifetime. C++11 move semantics turn `vec2 = std::move(vec1)` into an O(1) pointer swap, while in C the only option is to `memcpy` the entire block of data.

Two functions that are easy to confuse: `reserve(n)` changes only `capacity`, never `size` — it pre-allocates memory but creates no new elements; `resize(n)` does change `size` — the extra positions are value-initialized and surplus elements are destroyed. Our C version implements only `reserve`; `resize` is left as an exercise. Also, `std::vector<bool>` applies a bit-packing optimization (each `bool` occupies just 1 bit), at the cost of not being able to take a single element's address. And C++17's `std::span<T>` provides a non-owning view over contiguous memory — an extremely important composition tool.

## Exercises

The exercises below give only function signatures and requirement descriptions — the implementations are yours to write.

### Exercise 1: Implement resize

**Difficulty: basic** · reserve plus filling in default values

`reserve` changes only the capacity, not the size; `resize` has to change the size. When the new size is larger than the old, the extra positions should be filled with a default value.

```c
/// @brief Change the number of elements in the dynamic array
/// @param default_value pointer to the default value (used to fill new positions); may be NULL (fill with zeros)
DynamicArrayStatus dynamic_array_resize(
    DynamicArray* arr,
    size_t new_size,
    const void* default_value
);
// Exercise: implement it yourself
```

### Exercise 2: Implement filter

**Difficulty: basic** · a predicate that returns a new array

Given a dynamic array and a filter predicate, return a newly created dynamic array containing only the elements that satisfy the predicate.

```c
/// @brief Filter the elements of a dynamic array by predicate
DynamicArray* dynamic_array_filter(
    const DynamicArray* arr,
    int (*pred)(const void* element)
);
// Exercise: implement it yourself
```

### Exercise 3: Implement map

**Difficulty: intermediate** · a transform function, and the output element size may differ

Given a dynamic array and a transform function, apply the transform to every element and store the results in a new array to return.

```c
/// @brief Apply a transform function to every element of the dynamic array
/// @param out_element_size the element size of the output array (may differ from the input)
DynamicArray* dynamic_array_map(
    const DynamicArray* arr,
    void (*transform)(const void* in, void* out),
    size_t out_element_size
);
// Exercise: implement it yourself
```

### Exercise 4: Implement concatenation

**Difficulty: basic** · merge two arrays of the same type

Concatenate two dynamic arrays of the same element type into a new dynamic array.

```c
/// @brief Concatenate two dynamic arrays into a new dynamic array
DynamicArray* dynamic_array_concat(
    const DynamicArray* arr1,
    const DynamicArray* arr2
);
// Exercise: implement it yourself
```

> **Self-assessment**: if you find the exercises hard, revisit the design ideas in the corresponding sections. Especially resize — it is essentially a combination of reserve + memset/memcpy; think through which positions need filling and with what value, and the code practically writes itself.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
