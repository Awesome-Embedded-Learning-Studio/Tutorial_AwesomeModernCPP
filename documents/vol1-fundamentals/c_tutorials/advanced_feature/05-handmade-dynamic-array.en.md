---
title: Hand-Rolling a Dynamic Array — Implementing a Container from Scratch
description: Design and implement a type-safe dynamic array library from scratch,
  understand memory resizing strategies, error handling patterns, and API design principles,
  and pave the way to understanding `std::vector`.
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
  source_hash: 6084d869ef9de53c27f62e52bddc1968e6ca3d8af3b9a94ddc37d81961b38ab0
  translated_at: '2026-04-20T03:53:27.562573+00:00'
  engine: anthropic
  token_count: 3971
---
# Building a Dynamic Array from Scratch — Implementing a Container from Zero

When writing C programs, one of the most painful things is that array sizes must be determined at compile time. If you want to store 10 items, you declare `int arr[10]`; later, when requirements change to 100, you have to go back, modify the code, and recompile. Even worse, in many cases you have no idea how many items will arrive at runtime — how many records a user enters, how many packets the network receives, how many samples a sensor collects — these can only be determined at runtime.

`malloc` does solve the uncertain-size problem, but it only allocates without growing — once it fills up and you want to add more, you must manually `realloc`, manage capacity yourself, and handle errors on your own. `malloc` and `realloc` calls scattered throughout your codebase quickly become a maintenance nightmare. In Python you can casually write `lst = []`, and in C++ you have `std::vector` — both automatically grow. But the C standard library has no such thing, so we must build it ourselves.

Today we will build a complete dynamic array library from scratch. Along the way, we will clarify data structure design, memory growth and shrinkage strategies, and error handling patterns. Finally, we will compare our implementation with C++'s `std::vector` to see how the standard library handles these same problems.

> **Learning Objectives**
>
> - [ ] Understand the necessity of the size/capacity/data three-field design in dynamic arrays
> - [ ] Master the 2x growth strategy and its amortized O(1) complexity analysis
> - [ ] Understand shrinkage timing choices to avoid frequent `realloc` calls
> - [ ] Master the enum return code error handling pattern
> - [ ] Be able to independently design a complete set of CRUD APIs
> - [ ] Understand the internal mechanism of `std::vector` and its correspondence to our hand-rolled C version

## Environment Notes

All code examples in this article compile and run in a standard C environment. When compiling, we recommend always including `-Wall -Wextra` — dynamic array implementation involves extensive pointer arithmetic and `malloc`/`realloc` calls, and compiler warnings can help you catch many potential issues.

```text
gcc -Wall -Wextra -std=c11 -o dynarr dynarr.c
```

## Step 1 — Understand What a Dynamic Array Actually Is

From a physical storage perspective, a dynamic array is still a contiguous block of memory, no different from a plain array. The key difference is that a dynamic array separates "used space" from "reserved space," and uses a pointer to access this memory indirectly, so it can swap for a larger block when needed. You can think of it as a warehouse that can automatically "move to a bigger building" — when the shelves are full, you switch to a warehouse with more shelves, move all the old goods over, and to the outside world the address changed but the interface for storing and retrieving goods remains the same.

Let's start with the simplest prototype:

```c
typedef struct {
    void *data;
    size_t size;
} DynArr;
```

`data` points to contiguous memory allocated on the heap, and `size` records the current number of elements. But you will notice a fatal problem: we use `void *`, so we don't know how large each element is. For an `int` array the stride is 4 bytes, for `double` it is 8 bytes, and for a custom struct it could be dozens of bytes. Without element size information, we cannot locate the Nth element at all.

So we need to add `elem_size` and `capacity`:

```c
typedef struct {
    void *data;       // 指向堆上连续内存的指针
    size_t size;      // 当前元素个数
    size_t capacity;  // 总容量（最多能放多少个元素）
    size_t elem_size; // 每个元素的字节大小
} DynArr;
```

The four fields each have their own role: `data` manages "where it exists," `size` manages "how many are used," `capacity` manages "how many slots there are in total," and `elem_size` manages "how large each slot is." With `elem_size`, locating the address of the `i`th element is `(char *)arr->data + i * arr->elem_size` — we must first cast to `char *` because `sizeof(char)` is exactly 1 byte, making the pointer arithmetic a precise byte offset. Doing addition directly on `void *` will cause a compiler error (the C standard does not allow it, although GCC permits it as an extension, but it is not portable).

> ⚠️ **Pitfall Warning**
> `size` is "how many valid elements actually exist," `capacity` is "how many elements this memory block can hold at most," `size <= capacity`. If you use `capacity` instead of `size` as the upper bound when iterating, you will read uninitialized garbage data.

The internal data layout of `std::vector` is almost identical to ours, except that the template parameter `T` replaces the `elem_size` + `void *` combination, and type safety is guaranteed at compile time. `std::vector<int>` is 24 bytes in most implementations — three 8-byte fields (pointer + size + capacity), and `elem_size` does not need to be stored after template instantiation.

## Step 2 — Establish an Error Handling System

Before writing functional code, let's solve an engineering problem: what do we do when a function encounters an error? The laziest approach is to `abort()` on error — this is common in teaching code, but in real engineering it is a disaster. You can't just kill the entire server process because one `malloc` failed, right?

We use an enum to establish a clear error code system:

```c
typedef enum {
    DYNARR_OK = 0,
    DYNARR_ERR_ALLOC,
    DYNARR_ERR_OUT_OF_RANGE,
    DYNARR_ERR_NULL_PTR,
    DYNARR_ERR_EMPTY,
} DynArrResult;
```

Every function returns `DynArrResult`, and the caller can check whether the operation succeeded and why it failed. We can pair this with a helper macro to output friendly error messages:

```c
#define DYNARR_CHECK(expr) do {               \
    DynArrResult _rc = (expr);                \
    if (_rc != DYNARR_OK) {                   \
        fprintf(stderr, "Error %d at %s:%d\n", \
                _rc, __FILE__, __LINE__);     \
        return _rc;                           \
    }                                         \
} while (0)
```

Separating error message display from error code generation is an even better approach — the caller might want to write errors to a log file instead of printing to the terminal, or might want to clean up resources after an error. Enum return codes give the caller complete control.

## Step 3 — Implement Creation and Destruction

### Creation — Factory Function

In object-oriented languages this is called a constructor; in C we call it a factory function — it "produces" an initialized object and returns it to the caller.

```c
DynArrResult dynarr_create(DynArr *arr, size_t elem_size, size_t init_capacity) {
    if (arr == NULL || elem_size == 0) return DYNARR_ERR_NULL_PTR;

    arr->elem_size = elem_size;
    arr->size = 0;
    arr->capacity = init_capacity < 8 ? 8 : init_capacity;

    arr->data = malloc(arr->capacity * arr->elem_size);
    if (arr->data == NULL) {
        return DYNARR_ERR_ALLOC;
    }

    return DYNARR_OK;
}
```

After allocating the struct memory, you must immediately check the `malloc` return value — accessing `arr->data` without checking will cause an immediate segfault. We set a minimum capacity of 8 as a rule of thumb; too small leads to frequent growth, too large wastes memory.

> ⚠️ **Pitfall Warning**
> Note the presence of error checking. This is a very classic resource leak scenario: the struct allocation succeeds, but the data area allocation fails. If you simply `return` without `free`ing, that struct memory is leaked forever. This situation of "allocating some resources but failing in subsequent steps" is the most error-prone part of C memory management.

Usage:

```c
DynArr arr;
DYNARR_CHECK(dynarr_create(&arr, sizeof(int), 4));
```

Use `sizeof(int)` instead of hardcoding `4` — the size of `int` may differ across platforms, and `sizeof` is computed at compile time with no runtime overhead.

### Destruction — Release Order Must Not Be Reversed

```c
void dynarr_destroy(DynArr *arr) {
    if (arr == NULL) return;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}
```

The release order must not be reversed — if you `free(arr)` first, accessing `arr->data` is a use-after-free. Another issue is that after `free(arr->data)`, the `arr->data` pointer itself does not become `NULL`; it still points to that freed memory. C function parameters are passed by value, so we can only rely on the caller to manually set it to NULL:

```c
dynarr_destroy(&arr);
// arr.data is now NULL, safe
```

C++'s RAII mechanism solidifies this create/destroy pairing at the language level — the destructor is automatically called when the object leaves scope, so memory absolutely cannot leak. In our C version, every step of resource management relies on human discipline.

## Step 4 — Nail Down Capacity Management

### Growth — 2x Strategy

When `size == capacity` the array is full, and inserting another element requires growth. The question is: how much to grow? If you add 1 each time, inserting N elements consecutively requires N `realloc` calls, and the total copy volume is 1 + 2 + ... + N = O(N²), which is completely unacceptable. Doubling — doubling the capacity each time it fills up — requires only about log₂(N) growths, with a total copy volume of ≈ 2N = O(N), amortized to O(1) per insertion. It is like moving houses — instead of buying one more box each time, you double the house area each time — the move itself is exhausting, but averaged over each day, you barely feel it.

```c
static DynArrResult dynarr_reserve(DynArr *arr, size_t new_capacity) {
    if (arr == NULL) return DYNARR_ERR_NULL_PTR;
    if (new_capacity <= arr->capacity) return DYNARR_OK;

    void *new_data = realloc(arr->data, new_capacity * arr->elem_size);
    if (new_data == NULL) return DYNARR_ERR_ALLOC;

    arr->data = new_data;
    arr->capacity = new_capacity;
    return DYNARR_OK;
}
```

`realloc` tries to expand in place at the original location; if that fails, it finds a larger block on the heap and copies the old data over. In either case, the returned pointer points to valid memory and the old data is intact.

> ⚠️ **Pitfall Warning**
> `realloc` may return a different address! You must use the return value to update the pointer. If you write `realloc(arr->data, ...)` without receiving the return value, you lose the new address after the "move," and the memory pointed to by the old address has already been freed — a double disaster.

### Shrinkage — Avoid Thrashing

If an array once grew to 10,000 elements and later shrank to only 10, the memory for 9,990 elements is wasted. But shrinkage timing is much trickier than growth — consider an array oscillating between 100 and 50: shrinking at 50, then immediately needing to insert again, growing back to 100 — this back-and-forth is the classic "thrashing" problem. Our strategy is to shrink to `size * 2` but keep a minimum capacity of 8, called explicitly by the user:

```c
DynArrResult dynarr_shrink_to_fit(DynArr *arr) {
    if (arr == NULL) return DYNARR_ERR_NULL_PTR;
    if (arr->size == 0) {
        free(arr->data);
        arr->data = NULL;
        arr->capacity = 0;
        return DYNARR_OK;
    }
    size_t new_cap = arr->size < 8 ? 8 : arr->size * 2;
    return dynarr_reserve(arr, new_cap);
}
```

`shrink_to_fit` is typically only called when you are "sure there won't be significant growth," such as after data loading is complete. The C++ standard does not mandate that `std::vector`'s growth factor must be 2x — MSVC uses 1.5x, while libstdc++ and libc++ use 2x. A 1.5x factor yields higher memory utilization but slightly more growths.

## Step 5 — Implement Element Access

We provide two access methods: a fast version without bounds checking (similar to `operator[]`) and a safe version with bounds checking (similar to `at()`).

```c
void *dynarr_at(DynArr *arr, size_t index) {
    if (arr == NULL || index >= arr->size) return NULL;
    return (char *)arr->data + index * arr->elem_size;
}

void *dynarr_unchecked(DynArr *arr, size_t index) {
    return (char *)arr->data + index * arr->elem_size;
}
```

The safe version returns a copy into the caller's buffer, because C has no concept of references and the data area is `void *`, so the function cannot directly return a value of the correct type. This is indeed much more cumbersome than C++'s `at()`, but it is the cost of generic programming in C.

```c
int val;
memcpy(&val, dynarr_at(&arr, 0), sizeof(int));
printf("arr[0] = %d\n", val);
```

## Step 6 — Implement Insertion and Deletion

### push_back — Append to End

```c
DynArrResult dynarr_push_back(DynArr *arr, const void *elem) {
    if (arr == NULL || elem == NULL) return DYNARR_ERR_NULL_PTR;

    if (arr->size == arr->capacity) {
        DynArrResult rc = dynarr_reserve(arr, arr->capacity * 2);
        if (rc != DYNARR_OK) return rc;
    }

    void *dest = (char *)arr->data + arr->size * arr->elem_size;
    memcpy(dest, elem, arr->elem_size);
    arr->size++;
    return DYNARR_OK;
}
```

The destination of `memcpy` is `(char *)arr->data + arr->size * arr->elem_size` — skipping all existing elements to reach the first empty slot. Thanks to the 2x growth strategy, the total time for N consecutive `push_back` calls is O(N), amortized O(1).

Let's verify the growth behavior:

```c
DynArr arr;
dynarr_create(&arr, sizeof(int), 4);
printf("init: size=%zu, cap=%zu\n", arr.size, arr.capacity);

for (int i = 0; i < 20; i++) {
    dynarr_push_back(&arr, &i);
}
printf("after 20 pushes: size=%zu, cap=%zu\n", arr.size, arr.capacity);
```

```text
init: size=0, cap=8
after 20 pushes: size=20, cap=32
```

The initial capacity of 4 is raised to the minimum of 8, and after inserting 20 elements it undergoes two growths: 8 -> 16 -> 32.

### pop_back — Remove from End

```c
DynArrResult dynarr_pop_back(DynArr *arr) {
    if (arr == NULL) return DYNARR_ERR_NULL_PTR;
    if (arr->size == 0) return DYNARR_ERR_EMPTY;
    arr->size--;
    return DYNARR_OK;
}
```

The "deleted" element still lies in memory, and will be overwritten on the next `push_back`.

> ⚠️ **Pitfall Warning**
> We do not trigger shrinkage after `pop_back` — if you `push_back` right after `pop_back`, the shrinkage was wasted. Shrinkage should be explicitly triggered by the caller via `shrink_to_fit`. `std::vector` follows the same design.

### insert and erase — Middle Insertion and Deletion

`insert` needs to shift all elements after the insertion point back by one, while `erase` shifts them forward by one to overwrite the deleted element. Both must use `memmove` instead of `memcpy` — because the source and destination memory regions overlap, and `memcpy`'s behavior is undefined in overlapping cases.

```c
DynArrResult dynarr_insert(DynArr *arr, size_t index, const void *elem) {
    if (arr == NULL || elem == NULL) return DYNARR_ERR_NULL_PTR;
    if (index > arr->size) return DYNARR_ERR_OUT_OF_RANGE;

    if (arr->size == arr->capacity) {
        DynArrResult rc = dynarr_reserve(arr, arr->capacity * 2);
        if (rc != DYNARR_OK) return rc;
    }

    void *dest = (char *)arr->data + (index + 1) * arr->elem_size;
    void *src  = (char *)arr->data + index * arr->elem_size;
    size_t count = (arr->size - index) * arr->elem_size;
    memmove(dest, src, count);

    memcpy((char *)arr->data + index * arr->elem_size, elem, arr->elem_size);
    arr->size++;
    return DYNARR_OK;
}

DynArrResult dynarr_erase(DynArr *arr, size_t index) {
    if (arr == NULL) return DYNARR_ERR_NULL_PTR;
    if (index >= arr->size) return DYNARR_ERR_OUT_OF_RANGE;

    void *dest = (char *)arr->data + index * arr->elem_size;
    void *src  = (char *)arr->data + (index + 1) * arr->elem_size;
    size_t count = (arr->size - index - 1) * arr->elem_size;
    memmove(dest, src, count);

    arr->size--;
    return DYNARR_OK;
}
```

Verify insert and erase:

```c
int vals[] = {10, 20, 30, 40, 50};
for (int i = 0; i < 5; i++) dynarr_push_back(&arr, &vals[i]);

int new_val = 25;
dynarr_insert(&arr, 2, &new_val);  // 在下标 2 插入 25

dynarr_erase(&arr, 0);             // 删除下标 0
```

```text
after insert: 10 20 25 30 40 50
after erase:  20 25 30 40 50
```

`std::vector::insert` has had an rvalue reference overload since C++11, which can accept move semantics to avoid deep copies. Our C version can only do a shallow copy via `memcpy` — if elements contain dynamically allocated memory (such as strings pointing to `malloc`-allocated buffers), shallow copies will cause double-free crashes. This is a fundamental limitation of generic programming in C.

## Step 7 — Implement Traversal and Search

### Traversal — Callback Function Pattern

The container's interior is `void *`, so it doesn't know the element type. How to "process each element" must be told to the container by the caller through a callback function — a form of "inversion of control":

```c
typedef void (*DynArrCallback)(void *elem, void *ctx);

DynArrResult dynarr_foreach(DynArr *arr, DynArrCallback fn, void *ctx) {
    if (arr == NULL || fn == NULL) return DYNARR_ERR_NULL_PTR;

    for (size_t i = 0; i < arr->size; i++) {
        void *elem = (char *)arr->data + i * arr->elem_size;
        fn(elem, ctx);
    }
    return DYNARR_OK;
}
```

```c
void print_int(void *elem, void *ctx) {
    (void)ctx;
    printf("%d ", *(int *)elem);
}

dynarr_foreach(&arr, print_int, NULL);
```

```text
20 25 30 40 50
```

The callback function pattern is used extensively in the C standard library — the comparison function in `qsort`, and `pthread_create` all follow this pattern.

### Search — Linear Search

"Comparing for equality" also needs to be provided by the caller:

```c
typedef bool (*DynArrEqualFn)(const void *elem, const void *target);

ssize_t dynarr_find(DynArr *arr, const void *target, DynArrEqualFn eq) {
    if (arr == NULL || target == NULL || eq == NULL) return -1;

    for (size_t i = 0; i < arr->size; i++) {
        void *elem = (char *)arr->data + i * arr->elem_size;
        if (eq(elem, target)) return (ssize_t)i;
    }
    return -1;
}
```

Time complexity is O(N). For faster lookups, you can sort first and then use binary search. C++'s `std::find` uses iterators paired with lambda expressions, which is far more elegant than callback functions; C++20's Ranges turn traversal, filtering, and transformation into chained calls.

## C++ Comparison: Design Trade-offs in std::vector

At this point we have hand-rolled a complete dynamic array library. Looking back and systematically comparing with `std::vector`, understanding these design trade-offs is far more important than memorizing APIs.

Using `void *` for generic programming brought us three problems: no type checking, needing to manually pass `elem_size`, and requiring forced type casts inside callback functions. `std::vector` solves all three perfectly with templates — the compiler determines the type `T` at instantiation time, all type checks are completed at compile time, and `elem_size` is automatically calculated. `std::vector`'s destructor automatically frees the internal array, whether the function returns normally or exits due to an exception — this is the core idea of RAII: binding resource lifetime to object lifetime. C++11's move semantics turn `std::vector` copying into an O(1) pointer swap, while in C we can only `memcpy` the entire data block.

There are two easily confused functions: `reserve` only changes `capacity` without changing `size`, pre-allocating memory without creating new elements; `resize` changes `size`, with extra positions value-initialized and excess elements destroyed. Our C version only implements `reserve`, leaving `resize` as an exercise. Additionally, `std::vector<bool>` uses bit-packing optimization (each `bool` takes only 1 bit), but at the cost of not being able to take the address of individual elements. C++17's `std::span` provides a non-owning view over contiguous memory and is a very important composition tool.

## Exercises

The following exercises only provide function signatures and requirement descriptions, with the implementation left blank.

### Exercise 1: Implement resize

`reserve` only changes capacity without changing size, while `resize` needs to change size. When the new size is greater than the old size, the extra positions should be filled with default values.

```c
DynArrResult dynarr_resize(DynArr *arr, size_t new_size, const void *default_val);
```

### Exercise 2: Implement filter

Given a dynamic array and a filter predicate, return a newly created dynamic array containing only the elements that satisfy the condition.

```c
DynArrResult dynarr_filter(DynArr *src, DynArr *dst,
                           bool (*pred)(const void *elem, void *ctx),
                           void *ctx);
```

### Exercise 3: Implement map transformation

Given a dynamic array and a transformation function, apply the transformation function to each element and store the results in a new array to return.

```c
DynArrResult dynarr_map(DynArr *src, DynArr *dst,
                        void (*transform)(void *dst_elem,
                                          const void *src_elem,
                                          void *ctx),
                        void *ctx);
```

### Exercise 4: Implement concatenation

Concatenate two dynamic arrays of the same type into a new dynamic array.

```c
DynArrResult dynarr_concat(DynArr *a, DynArr *b, DynArr *out);
```

> **Self-Assessment**: If you find the exercises difficult, please review the design思路 of the corresponding sections. Especially for `resize` — it is essentially a combination of `reserve` + `memset`/`memcpy`. Once you figure out which positions need filling and what values to fill them with, the code will naturally follow.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
