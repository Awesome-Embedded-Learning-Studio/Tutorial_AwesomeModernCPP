---
title: Implementing a Dynamic Vector from Scratch
description: Design and implement a type-safe dynamic array library from scratch.
  We will explore memory expansion and contraction strategies, error handling patterns,
  and API design principles, paving the way for a deeper understanding of `std::vector`.
chapter: 1
order: 105
tags:
- host
- cpp-modern
- intermediate
- 进阶
- 容器
- 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 30
cpp_standard:
- 11
- 14
- 17
prerequisites:
- 指针进阶：多级指针、指针与 const
- 动态内存管理：malloc/free/realloc 的正确使用
- 结构体、联合体与内存对齐
- C 语言陷阱与常见错误
translation:
  source: documents/vol1-fundamentals/c_tutorials/advanced_feature/05-handmade-dynamic-array.md
  source_hash: 1601bf7a93a6e966bb07cd6fc3f6d1d9cc65ac292dfca121f7e3be43be984600
  translated_at: '2026-06-13T11:44:43.115178+00:00'
  engine: anthropic
  token_count: 3969
---
# Hand-Rolling a Dynamic Array — Implementing a Container from Scratch

When writing C programs, one of the most painful aspects is that array sizes must be determined at compile time. You want to store 10 items, you declare `int arr[10]`. Later, requirements change and you need to store 100, so you go back to modify the code and recompile. Even worse, in many cases, you simply don't know how many items will be queued at runtime—how many records the user inputs, how many packets the network receives, how many samples the sensor collects. These are all runtime quantities.

`malloc` does solve the uncertainty of size, but it only handles allocation, not growth. If it gets full and you want to add more, you have to manually `realloc`, manage capacity yourself, and handle errors on your own. Scattered `malloc` and `realloc` calls throughout the code quickly become a maintenance nightmare. In Python, you can just write `list.append()`, and in C++, you have `std::vector`—they both handle resizing automatically. But the C standard library lacks such a utility, so we must build it ourselves.

Today, starting from scratch, we will hand-roll a complete dynamic array library. In this process, we will clarify data structure design, memory expansion and shrinking strategies, and error handling patterns. Finally, we will compare this with C++'s `std::vector` to see how the standard library handles these things.

> **Learning Objectives**
>
> - [ ] Understand the necessity of the size/capacity/data three-field design for dynamic arrays.
> - [ ] Master the 2x expansion strategy and its amortized O(1) complexity analysis.
> - [ ] Understand the timing of shrinking to avoid frequent `realloc`.
> - [ ] Master the error handling pattern using enum return codes.
> - [ ] Be able to independently design a complete CRUD API.
> - [ ] Understand the internal mechanism of `std::vector` and its correspondence with the hand-rolled C version.

## Environment Setup

All code examples in this article are compiled and run in a standard C environment. It is recommended to always compile with `-Wall -Wextra`—implementing a dynamic array involves extensive pointer arithmetic and `malloc` calls, and compiler warnings can help you catch many potential issues.

```bash
gcc main.c dynamic_array.c -Wall -Wextra -O2 -o dynamic_array_demo
```

## Step 1 — Figure Out What a Dynamic Array Actually Is

From a physical storage perspective, a dynamic array is essentially still a contiguous block of memory, no different from a standard array. The key difference is that a dynamic array separates "used space" from "reserved space" and uses a pointer to access this memory indirectly. This allows it to swap for a larger block when needed. You can imagine it as a warehouse that can automatically "move to a bigger house"—when the shelves are full, you swap to a warehouse with more shelves, move the old goods over, and to the outside world, the address changed but the interface for storing and retrieving goods remains the same.

Let's start with a simplest prototype:

```c
struct DynamicArray {
    void* data;      // Pointer to the heap memory
    size_t size;     // Number of elements currently stored
};
```

`data` points to contiguous memory allocated on the heap, and `size` records the current number of elements. But you will notice a fatal problem: we use `void*`, so we don't know how large each element is. For an `int` array, the stride is 4 bytes; for `double`, it's 8 bytes; a custom struct might be tens of bytes. Without element size information, we cannot locate the Nth element at all.

Therefore, we need to add `elem_size` and `capacity`:

```c
struct DynamicArray {
    void* data;      // Pointer to the heap memory
    size_t size;     // Number of elements currently stored
    size_t capacity; // Total number of elements that can be stored
    size_t elem_size;// Size of a single element in bytes
};
```

The four fields each have their role: `data` manages "where it exists", `size` manages "how many are used", `capacity` manages "how many slots are there in total", and `elem_size` manages "how big each slot is". With `elem_size`, locating the address of the `i`-th element is `(char*)data + i * elem_size`—we must cast to `char*` first, because `sizeof(char)` is guaranteed to be 1 byte, ensuring pointer arithmetic results in precise byte offsets. Doing addition directly on `void*` will cause a compiler error (not allowed by the C standard; although GCC allows it as an extension, it is not portable).

> ⚠️ **Pitfall Warning**
> `size` is "how many valid elements there actually are", `capacity` is "how many elements this memory block can hold at most", `size <= capacity`. If you use `capacity` instead of `size` as the upper bound during traversal, you will read uninitialized garbage data.

The internal data layout of `std::vector` is almost identical to ours, except that the template parameter `T` replaces the combination of `elem_size` + `void*`, ensuring type safety is guaranteed at compile time. `std::vector<int>` is 24 bytes in most implementations—three 8-byte fields (pointer + size + capacity)—`elem_size` is not needed after template instantiation.

## Step 2 — Establish an Error Handling System

Before writing functional logic, let's solve an engineering problem: what to do when a function fails? The laziest approach is to `exit(1)` immediately upon error—this is common in teaching code, but in actual engineering, it's a disaster. You can't just kill the entire server process because one `malloc` failed, right?

We use an enum to establish a clear error code system:

```c
typedef enum {
    ARR_OK,                  // Success
    ARR_ERR_MALLOC,          // Memory allocation failed
    ARR_ERR_OUT_OF_BOUNDS,   // Index out of bounds
    ARR_ERR_INVALID_ARG,     // Invalid argument (e.g., NULL pointer)
    ARR_ERR_NOT_FOUND        // Element not found
} ArrayResult;
```

Each function returns `ArrayResult`, allowing the caller to judge whether the operation succeeded and the reason for failure. Combined with helper macros, we can output friendly error messages:

```c
#define CHECK_RESULT(call) \
    do { \
        ArrayResult res = (call); \
        if (res != ARR_OK) { \
            fprintf(stderr, "Error at %s:%d: %s\n", \
                    __FILE__, __LINE__, #call); \
            exit(1); \
        } \
    } while (0)
```

Separating the display of error messages from the generation of error codes is a better practice—the caller might want to log errors to a file rather than print to the terminal, or might want to clean up resources after an error. Enum return codes give the caller full control.

## Step 3 — Implement Creation and Destruction

### Creation — Factory Function

In object-oriented languages, this is called a constructor; in C, we call it a factory function—it "produces" an initialized object and returns it to the caller.

```c
ArrayResult array_create(struct DynamicArray* arr, size_t elem_size, size_t initial_capacity) {
    if (!arr || elem_size == 0) return ARR_ERR_INVALID_ARG;

    // Enforce a minimum capacity to avoid frequent resizing
    if (initial_capacity < 8) initial_capacity = 8;

    arr->data = malloc(initial_capacity * elem_size);
    if (!arr->data) return ARR_ERR_MALLOC;

    arr->size = 0;
    arr->capacity = initial_capacity;
    arr->elem_size = elem_size;
    return ARR_OK;
}
```

After allocating the structure's memory, you must immediately check the `malloc` return value—accessing `arr->data` without checking will cause an immediate segmentation fault. We set a minimum capacity of 8 as a rule of thumb; too small leads to frequent resizing, too large wastes memory.

> ⚠️ **Pitfall Warning**
> Note the existence of `arr->data = malloc(...)`. This is a classic resource leak scenario: the struct allocation succeeded, but the data area allocation failed. If you simply `return ARR_ERR_MALLOC` without `free(arr)`, that struct memory is leaked forever. This situation of "allocating some resources but failing subsequent steps" is one of the most error-prone areas in C memory management.

Usage:

```c
struct DynamicArray my_arr;
if (array_create(&my_arr, sizeof(int), 10) != ARR_OK) {
    // Handle error
}
```

Use `sizeof(int)` instead of hardcoding `4`—the size of `int` might vary on different platforms, while `sizeof` is calculated at compile time with zero runtime overhead.

### Destruction — Release Order Must Not Be Reversed

```c
void array_destroy(struct DynamicArray* arr) {
    if (arr) {
        free(arr->data);       // 1. Release the data block
        arr->data = NULL;
        arr->size = 0;
        arr->capacity = 0;
    }
}
```

The release order cannot be reversed—if you `free(arr)` first, accessing `arr->data` becomes a Use After Free. Another issue is that after `free(arr->data)`, the `arr->data` pointer itself doesn't automatically become `NULL`; it still points to that freed memory. C function arguments are passed by value, so we rely on the caller to manually set it to NULL:

```c
array_destroy(&my_arr);
my_arr.data = NULL; // Caller must do this manually
```

C++'s RAII mechanism solidifies this create/destroy pairing at the language level—the destructor is called automatically when the object leaves scope, absolutely guaranteeing no memory leaks. In our C version, every step of resource management relies on human discipline.

## Step 4 — Master Capacity Management

### Expansion — 2x Growth Strategy

When `size == capacity`, the array is full, and inserting requires expansion. The question is: how much to expand? If we add 1 each time, inserting N elements continuously requires N `realloc`s, and the total copy amount is 1 + 2 + ... + N = O(N²), which is completely unacceptable. Doubling expansion—doubling the capacity whenever full—requires only about log₂(N) expansions, with a total copy amount ≈ 2N = O(N), which amortizes to O(1) per insertion. It's like moving house: instead of buying one more box each time, you double the floor area of the house—the move itself is tiring, but averaged over every day, it's negligible.

```c
static ArrayResult array_reserve(struct DynamicArray* arr, size_t new_capacity) {
    if (new_capacity < arr->size) return ARR_ERR_INVALID_ARG; // Cannot discard data

    void* new_data = realloc(arr->data, new_capacity * arr->elem_size);
    if (!new_data) return ARR_ERR_MALLOC;

    arr->data = new_data;
    arr->capacity = new_capacity;
    return ARR_OK;
}
```

`realloc` attempts to expand in-place at the original location; if that's not possible, it finds a larger block on the heap and copies the old data over. In either case, the returned pointer points to valid memory, and the old data remains intact.

> ⚠️ **Pitfall Warning**
> `realloc` might return a different address! You must use the return value to update the pointer. If you write `realloc(arr->data, ...)` and don't receive the return value, you lose the new address after moving, and the old address points to freed memory—a double disaster.

### Shrinking — Avoid Thrashing

If an array grew to 10,000 elements and later shrank to just 10, the memory for 9,990 elements is wasted. However, the timing for shrinking is much more nuanced than expansion—consider an array oscillating between 100 and 50: shrinking to 50 triggers a shrink, immediately followed by an insertion, expanding back to 100—this back-and-forth is the classic "thrashing" problem. Our strategy is to shrink to `size` but keep a minimum capacity of 8, called explicitly by the user:

```c
ArrayResult array_shrink_to_fit(struct DynamicArray* arr) {
    if (arr->size == 0) {
        // If empty, free memory and keep a small buffer
        free(arr->data);
        arr->data = malloc(8 * arr->elem_size); // Keep minimal capacity
        arr->capacity = 8;
        return ARR_OK;
    }
    return array_reserve(arr, arr->size);
}
```

`shrink_to_fit` is usually called only when "it's certain there won't be major growth," such as after data loading is complete. The C++ standard does not mandate that `std::vector`'s expansion factor must be 2x—MSVC uses 1.5x, while libstdc++ and libc++ use 2x. 1.5x has higher memory utilization but slightly more expansions.

## Step 5 — Implement Element Access

We provide two access methods: a fast version without bounds checking (like `operator[]`) and a safe version with bounds checking (like `at()`).

```c
// Fast access (no bounds check)
void* array_at_unsafe(const struct DynamicArray* arr, size_t index) {
    return (char*)arr->data + index * arr->elem_size;
}

// Safe access (with bounds check)
ArrayResult array_at(const struct DynamicArray* arr, size_t index, void* out_buffer) {
    if (index >= arr->size) return ARR_ERR_OUT_OF_BOUNDS;
    memcpy(out_buffer, (char*)arr->data + index * arr->elem_size, arr->elem_size);
    return ARR_OK;
}
```

The safe version returns by copying to the caller's buffer because C lacks the concept of references and the data area is `void*`, so the function cannot directly return a value of the correct type. This is indeed more cumbersome than C++'s `operator[]`, but it is the cost of generic programming in C.

```c
int value;
if (array_at(&my_arr, 5, &value) == ARR_OK) {
    printf("Element at index 5: %d\n", value);
}
```

## Step 6 — Implement Add and Remove Operations

### push_back — Append to Tail

```c
ArrayResult array_push_back(struct DynamicArray* arr, const void* value) {
    if (arr->size == arr->capacity) {
        ArrayResult res = array_reserve(arr, arr->capacity * 2);
        if (res != ARR_OK) return res;
    }

    void* target = (char*)arr->data + arr->size * arr->elem_size;
    memcpy(target, value, arr->elem_size);
    arr->size++;
    return ARR_OK;
}
```

The target address of `memcpy` is `data + size * elem_size`—skipping all existing elements to arrive at the first empty slot. Thanks to the 2x growth strategy, the total time for N consecutive `push_back`s is O(N), amortizing to O(1).

Let's verify the expansion effect:

```c
struct DynamicArray arr;
array_create(&arr, sizeof(int), 4); // Requested 4, adjusted to 8

for (int i = 0; i < 20; i++) {
    array_push_back(&arr, &i);
    printf("Size: %zu, Cap: %zu\n", arr.size, arr.capacity);
}

array_destroy(&arr);
```

Output:

```text
Size: 1, Cap: 8
...
Size: 8, Cap: 8
Size: 9, Cap: 16  <-- Expanded
...
Size: 16, Cap: 16
Size: 17, Cap: 32 <-- Expanded
...
```

The initial capacity of 4 was bumped to 8. After inserting 20 elements, it underwent two expansions: 8 -> 16 -> 32.

### pop_back — Remove from Tail

```c
ArrayResult array_pop_back(struct DynamicArray* arr) {
    if (arr->size == 0) return ARR_ERR_INVALID_ARG;
    arr->size--;
    return ARR_OK;
}
```

The "deleted" element remains in memory and will be overwritten by the next `push_back`.

> ⚠️ **Pitfall Warning**
> We do not trigger shrinking after `pop_back`—if we `push_back` right after `pop_back`, the shrink was wasted. Shrinking should be explicitly called by the user via `shrink_to_fit`. `std::vector` follows the same design.

### insert and erase — Middle Insertion and Deletion

`insert` needs to shift elements after the insertion position back by one, while `erase` shifts them forward by one to overwrite the deleted element. Both must use `memmove` rather than `memcpy`—because the source and destination memory regions overlap, and `memcpy`'s behavior is undefined in cases of overlap.

```c
ArrayResult array_insert(struct DynamicArray* arr, size_t index, const void* value) {
    if (index > arr->size) return ARR_ERR_OUT_OF_BOUNDS;

    if (arr->size == arr->capacity) {
        ArrayResult res = array_reserve(arr, arr->capacity * 2);
        if (res != ARR_OK) return res;
    }

    void* target = (char*)arr->data + index * arr->elem_size;
    void* src = (char*)arr->data + (index + 1) * arr->elem_size;
    size_t count = (arr->size - index) * arr->elem_size;

    memmove(src, target, count); // Shift elements back
    memcpy(target, value, arr->elem_size); // Write new element
    arr->size++;
    return ARR_OK;
}

ArrayResult array_erase(struct DynamicArray* arr, size_t index) {
    if (index >= arr->size) return ARR_ERR_OUT_OF_BOUNDS;

    void* target = (char*)arr->data + index * arr->elem_size;
    void* src = (char*)arr->data + (index + 1) * arr->elem_size;
    size_t count = (arr->size - index - 1) * arr->elem_size;

    memmove(target, src, count); // Shift elements forward
    arr->size--;
    return ARR_OK;
}
```

Verify insert and erase:

```c
int val = 99;
array_insert(&arr, 2, &val); // Insert 99 at index 2
array_erase(&arr, 0);        // Remove element at index 0
```

`std::vector::insert` has an rvalue reference overload in C++11, allowing move semantics to avoid deep copies. Our C version can only do shallow copies via `memcpy`—if an element contains dynamically allocated memory (like a string pointing to `malloc`'d memory), a shallow copy leads to double free crashes. This is a fundamental limitation of generic programming in C.

## Step 7 — Implement Traversal and Search

### Traversal — Callback Function Pattern

The container internals are `void*`, so it doesn't know the element type. Thus, "how to process each element" needs to be told to the container by the caller via a callback function—a form of "Inversion of Control":

```c
typedef void (*ElementCallback)(void* element, void* user_data);

void array_foreach(struct DynamicArray* arr, ElementCallback func, void* user_data) {
    for (size_t i = 0; i < arr->size; i++) {
        func((char*)arr->data + i * arr->elem_size, user_data);
    }
}
```

Usage:

```c
void print_int(void* elem, void* user_data) {
    (void)user_data; // Unused
    printf("%d ", *(int*)elem);
}

array_foreach(&arr, print_int, NULL);
```

The callback function pattern is widely used in the C standard library—the comparison function in `qsort`, and `pthread_create` all follow this routine.

### Search — Linear Search

"Comparing for equality" also needs to be provided by the caller:

```c
typedef bool (*EqualPredicate)(const void* elem, void* user_data);

ArrayResult array_find(const struct DynamicArray* arr, EqualPredicate pred, void* user_data, size_t* out_index) {
    for (size_t i = 0; i < arr->size; i++) {
        if (pred((char*)arr->data + i * arr->elem_size, user_data)) {
            *out_index = i;
            return ARR_OK;
        }
    }
    return ARR_ERR_NOT_FOUND;
}
```

Time complexity is O(N). If you need it faster, you can sort first and then use binary search. C++'s `std::find_if` uses iterators combined with lambda expressions, which is much more elegant to write than callback functions; C++20 Ranges turns traversal, filtering, and transformation into chained calls.

## C++ Comparison: Design Trade-offs in std::vector

At this point, we have hand-rolled a complete dynamic array library. Looking back systematically at `std::vector`, understanding these design trade-offs is far more important than memorizing APIs.

We used `void*` to implement generics, which brought three problems: no type checking, manual passing of `elem_size`, and mandatory type casting in callback functions. `std::vector` uses templates to perfectly solve these three—the compiler determines type `T` upon instantiation, all type checks are completed at compile time, and `sizeof(T)` is calculated automatically. `std::vector`'s destructor automatically releases the internal array, whether the function returns normally or exits via an exception. This is the core idea of RAII—binding resource lifecycle to object lifecycle. C++11's move semantics make `std::vector` return an O(1) pointer swap, whereas in C, you can only `memcpy` the entire block of data.

There are two easily confused functions: `reserve` only changes `capacity` not `size`, pre-allocating memory without creating new elements; `resize` changes `size`, filling extra positions with value-initialized values and destructing excess elements. Our C version only implemented `reserve`; `resize` is left as an exercise. Also, `std::vector<bool>` applies bit compression optimization (each `bool` takes only 1 bit), but at the cost of not being able to take the address of individual elements. C++17's `std::span` provides a non-owning view of contiguous memory and is a very important composition tool.

## Exercises

The following exercises provide only function signatures and requirement descriptions. The implementation is left blank.

### Exercise 1: Implement resize

`reserve` only changes capacity, not size, while `resize` needs to change size. When the new size is larger than the old size, the extra positions should be filled with a default value.

```c
ArrayResult array_resize(struct DynamicArray* arr, size_t new_size, const void* default_value);
```

### Exercise 2: Implement filter

Given a dynamic array and a filter predicate, return a newly created dynamic array containing only elements that satisfy the condition.

```c
ArrayResult array_filter(const struct DynamicArray* src, struct DynamicArray* dest, bool (*predicate)(const void* elem));
```

### Exercise 3: Implement map transformation

Given a dynamic array and a transformation function, apply the transformation function to each element and store the results in a new array to return.

```c
ArrayResult array_map(const struct DynamicArray* src, struct DynamicArray* dest, void (*transform)(void* out_elem, const void* in_elem));
```

### Exercise 4: Implement concatenation

Concatenate two dynamic arrays of the same type into a new dynamic array.

```c
ArrayResult array_concat(const struct DynamicArray* a, const struct DynamicArray* b, struct DynamicArray* result);
```

> **Self-Assessment of Difficulty**: If you find the exercises difficult, please review the design ideas in the corresponding sections. Especially `resize`—it is essentially a combination of `reserve` + `memset`/`memcpy`. Once you figure out which positions need filling and what values to fill, the code will come naturally.

## Reference Resources

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
