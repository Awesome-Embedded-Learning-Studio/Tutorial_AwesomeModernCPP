# Building a Dynamic Array from Scratch — Implementing a Container from Zero

One of the most painful things about writing C programs is that array sizes must be determined at compile time. If you want to store 10 items, you declare `int arr[10]`; later, when requirements change to 100, you have to go back, modify the code, and recompile. What's worse, in many cases you have no idea how many items will arrive at runtime — how many records a user enters, how many packets the network receives, how many samples a sensor collects — these can only be determined at runtime.

`malloc` does solve the uncertain size problem, but it only handles allocation, not growth — once it's full and you want to keep adding, you have to manually `realloc`, manage capacity yourself, and handle errors on your own. `malloc` and `realloc` calls scattered throughout your codebase quickly turn into a maintenance nightmare. In Python you can casually write `lst.append(x)`, and in C++ you have `std::vector::push_back` — they both grow automatically. But the C standard library has no such thing, so we must build it ourselves.

Today we'll build a complete dynamic array library from scratch. Along the way, we'll clarify data structure design, memory growth and shrinkage strategies, and error handling patterns. Finally, we'll compare our work against C++'s `std::vector` to see how the standard library handles these same problems.

> **Learning Objectives**
>
> - [ ] Understand the necessity of the size/capacity/data three-field design in dynamic arrays
> - [ ] Master the 2x growth strategy and its amortized O(1) complexity analysis
> - [ ] Understand shrinkage timing choices to avoid frequent `realloc` calls
> - [ ] Master the enum return code error handling pattern
> - [ ] Be able to independently design a complete CRUD API
> - [ ] Understand the internal mechanisms of `std::vector` and their correspondence to our hand-rolled C version

## Environment Notes

All code examples in this article are compiled and run in a standard C environment. When compiling, we recommend always including `-Wall -Wextra` — dynamic array implementation involves extensive pointer arithmetic and `memcpy`/`memmove` calls, and compiler warnings can help you catch many potential issues.

```text
gcc -std=c11 -Wall -Wextra -g dynamic_array.c -o dynamic_array
```

## Step One — Understanding What a Dynamic Array Actually Is

From a physical storage perspective, a dynamic array is still a contiguous block of memory, no different from a plain array. The key difference is that a dynamic array separates "used space" from "reserved space" and uses a pointer for indirect access, so it can swap in a larger block when needed. You can think of it as a warehouse that can automatically "move to a bigger building" — when the shelves are full, you swap to a warehouse with more shelves, move all the old goods over, and to the outside world the address changed but the interface for storing and retrieving goods remains the same.

Let's start with the simplest prototype:

```c
typedef struct {
    void *data;
    size_t size;
} DynArray;
```

`data` points to contiguous memory allocated on the heap, and `size` records the current element count. But you'll notice a fatal problem: we're using `void *`, so we don't know how large each element is. For an `int` array the stride is 4 bytes, for `double` it's 8 bytes, and for a custom struct it could be dozens of bytes. Without element size information, we can't locate the Nth element at all.

So we need to add `elem_size` and `capacity`:

```c
typedef struct {
    void *data;      // 指向堆上连续内存的指针
    size_t size;     // 当前元素个数
    size_t capacity; // 总共能容纳的元素个数
    size_t elem_size;// 每个元素的字节大小
} DynArray;
```

The four fields each have their own role: `data` manages "where it exists", `size` manages "how many are used", `capacity` manages "how many slots there are in total", and `elem_size` manages "how large each slot is". With `elem_size`, locating the address of the `i`th element is simply `(char *)arr->data + i * arr->elem_size` — we must first cast to `char *` because `sizeof(char)` is exactly 1 byte, making the pointer arithmetic a precise byte offset. Doing addition directly on `void *` will cause a compiler error (the C standard doesn't allow it, although GCC permits it as an extension, but it's not portable).

> ⚠️ **Pitfall Warning**
> `size` is "how many valid elements actually exist", `capacity` is "how many elements this memory block can hold at most", they are not the same thing. If you use `capacity` instead of `size` as the upper bound when iterating, you'll read uninitialized garbage data.

The internal data layout of `std::vector` is almost identical to ours, except that the template parameter `T` replaces the `void *` + `elem_size` combination, and type safety is guaranteed at compile time. `sizeof(std::vector<int>)` is 24 bytes on most implementations — three 8-byte fields (pointer + size + capacity), and `elem_size` doesn't need to be stored after template instantiation.

## Step Two — Establishing an Error Handling System

Before writing functional code, let's solve an engineering problem: what do we do when a function fails? The laziest approach is to `abort()` on error — this is common in teaching code, but it's an absolute disaster in real engineering. You can't just kill the entire server process because a single `malloc` failed, right?

We use an enum to establish a clear error code system:

```c
typedef enum {
    DYN_OK = 0,
    DYN_ERR_ALLOC,
    DYN_ERR_OUT_OF_RANGE,
    DYN_ERR_NULL_PTR,
    DYN_ERR_INVALID_SIZE,
} DynResult;
```

Every function returns `DynResult`, and the caller can check whether the operation succeeded and why it failed. We can pair this with a helper macro to output friendly error messages:

```c
#define DYN_CHECK(expr) do {                         \
    DynResult _err = (expr);                         \
    if (_err != DYN_OK) {                            \
        fprintf(stderr, "Error %d at %s:%d\n",       \
                _err, __FILE__, __LINE__);           \
        return _err;                                 \
    }                                                \
} while (0)
```

Separating error message display from error code generation is an even better approach — callers might want to write errors to a log file instead of printing to the terminal, or they might want to clean up resources after an error. Enum return codes give the caller complete control.

## Step Three — Implementing Creation and Destruction

### Creation — Factory Function

In object-oriented languages this is called a constructor; in C we call it a factory function — it "produces" an initialized object and returns it to the caller.

```c
DynResult dyn_create(DynArray *arr, size_t elem_size, size_t init_capacity) {
    if (arr == NULL || elem_size == 0) return DYN_ERR_NULL_PTR;
    if (init_capacity < 8) init_capacity = 8; // 最小容量保底

    arr->data = malloc(init_capacity * elem_size);
    if (arr->data == NULL) return DYN_ERR_ALLOC;

    arr->size = 0;
    arr->capacity = init_capacity;
    arr->elem_size = elem_size;
    return DYN_OK;
}
```

After allocating the struct's memory, you must immediately check the `malloc` return value — accessing `arr->data` without checking will cause an immediate segfault. We set a minimum capacity of 8 as a rule of thumb; too small leads to frequent growth, too large wastes memory.

> ⚠️ **Pitfall Warning**
> Note the presence of the error check. This is a very classic resource leak scenario: the struct allocation succeeded, but the data area allocation failed. If you simply `return` without `free`ing, that struct memory is leaked forever. This situation of "partially allocating resources but failing in subsequent steps" is one of the most error-prone aspects of C memory management.

Usage:

```c
DynArray arr;
DYN_CHECK(dyn_create(&arr, sizeof(int), 4));
```

Use `sizeof(int)` instead of hardcoding `4` — the size of `int` may differ across platforms, and `sizeof` is calculated at compile time with no runtime overhead.

### Destruction — Release Order Must Not Be Reversed

```c
DynResult dyn_destroy(DynArray *arr) {
    if (arr == NULL) return DYN_ERR_NULL_PTR;
    free(arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
    return DYN_OK;
}
```

The release order must not be reversed — if you `free(arr)` first, accessing `arr->data` is a use-after-free. Another issue is that after `free`, the `arr->data` pointer itself doesn't become `NULL`; it still points to that freed memory. C function parameters are passed by value, so we can only rely on the caller to manually set it to NULL:

```c
dyn_destroy(&arr);
// arr.data 现在是 NULL，安全
```

C++'s RAII mechanism solidifies this create/destroy pairing at the language level — the destructor is automatically called when the object leaves scope, so memory absolutely cannot leak. In our C version, every step of resource management relies on human discipline.

## Step Four — Nailing Down Capacity Management

### Growth — The 2x Strategy

When `size == capacity` the array is full, and inserting another element requires growth. The question is: how much to grow? If you add 1 each time, inserting N elements consecutively requires N `realloc` calls, and the total copy volume is 1 + 2 + ... + N = O(N²), which is completely unacceptable. Doubling — doubling the capacity whenever it's full — requires only about log₂(N) growth operations, with a total copy volume of ≈ 2N = O(N), amortized to O(1) per insertion. It's like moving houses — instead of buying one more box each time, you double the house area each time — the move itself is exhausting, but averaged over each day, you barely notice.

```c
static DynResult dyn_grow(DynArray *arr) {
    size_t new_cap = arr->capacity * 2;
    void *new_data = realloc(arr->data, new_cap * arr->elem_size);
    if (new_data == NULL) return DYN_ERR_ALLOC;
    arr->data = new_data;
    arr->capacity = new_cap;
    return DYN_OK;
}
```

`realloc` tries to expand in place at the original location; if that's not possible, it finds a larger block on the heap and copies the old data over. In either case, the returned pointer points to valid memory and the old data is intact.

> ⚠️ **Pitfall Warning**
> `realloc` may return a different address! You must use the return value to update the pointer. If you write `realloc(arr->data, new_size)` without receiving the return value, you lose the new address after the "move", and the memory pointed to by the old address has already been freed — a double disaster.

### Shrinkage — Avoiding Thrashing

If an array once grew to 10,000 elements and later shrank to only 10, the memory for 9,990 elements is wasted. But shrinkage timing is much trickier than growth — consider an array oscillating between 100 and 50: shrinking at 50, then immediately needing to insert again, growing back to 100 — this back-and-forth is the classic "thrashing" problem. Our strategy is to shrink to half capacity but keep a minimum capacity of 8, called explicitly by the user:

```c
DynResult dyn_shrink_to_fit(DynArray *arr) {
    if (arr == NULL) return DYN_ERR_NULL_PTR;
    if (arr->size == 0) {
        free(arr->data);
        arr->data = NULL;
        arr->capacity = 0;
        return DYN_OK;
    }
    size_t new_cap = arr->size < 8 ? 8 : arr->size;
    void *new_data = realloc(arr->data, new_cap * arr->elem_size);
    if (new_data == NULL) return DYN_ERR_ALLOC;
    arr->data = new_data;
    arr->capacity = new_cap;
    return DYN_OK;
}
```

`shrink_to_fit` is typically only called when you're "certain there won't be significant further growth", such as after data loading is complete. The C++ standard doesn't mandate that `std::vector`'s growth factor must be 2x — MSVC uses 1.5x, while libstdc++ and libc++ use 2x. 1.5x has higher memory utilization, but slightly more growth operations.

## Step Five — Implementing Element Access

We provide two access methods: a fast version without bounds checking (similar to `operator[]`) and a safe version with bounds checking (similar to `at()`).

```c
void *dyn_at_unsafe(DynArray *arr, size_t index) {
    return (char *)arr->data + index * arr->elem_size;
}

DynResult dyn_at(DynArray *arr, size_t index, void *out) {
    if (arr == NULL || out == NULL) return DYN_ERR_NULL_PTR;
    if (index >= arr->size) return DYN_ERR_OUT_OF_RANGE;
    memcpy(out, (char *)arr->data + index * arr->elem_size, arr->elem_size);
    return DYN_OK;
}
```

The safe version returns by copying to a caller-provided buffer, because C has no concept of references and the data area is `void *`, so the function can't directly return a correctly typed value. This is indeed much more cumbersome than C++'s `T& at(size_t)`, but that's the cost of generic programming in C.

```c
int val;
DYN_CHECK(dyn_at(&arr, 3, &val));
printf("arr[3] = %d\n", val);
```

## Step Six — Implementing Insertion and Deletion

### push_back — Append to the End

```c
DynResult dyn_push_back(DynArray *arr, const void *elem) {
    if (arr == NULL || elem == NULL) return DYN_ERR_NULL_PTR;
    if (arr->size == arr->capacity) {
        DynResult err = dyn_grow(arr);
        if (err != DYN_OK) return err;
    }
    void *dest = (char *)arr->data + arr->size * arr->elem_size;
    memcpy(dest, elem, arr->elem_size);
    arr->size++;
    return DYN_OK;
}
```

The destination of `memcpy` is `data + size * elem_size` — skipping all existing elements to reach the first empty slot. Thanks to the 2x growth strategy, the total time for N consecutive `push_back` calls is O(N), amortized O(1).

Let's verify the growth behavior:

```c
DynArray arr;
DYN_CHECK(dyn_create(&arr, sizeof(int), 4));

for (int i = 0; i < 20; i++) {
    DYN_CHECK(dyn_push_back(&arr, &i));
}

printf("size: %zu, capacity: %zu\n", arr.size, arr.capacity);
```

```text
size: 20, capacity: 32
```

The initial capacity of 4 was raised to the minimum of 8, and after inserting 20 elements it underwent two growth operations: 8 -> 16 -> 32.

### pop_back — Remove from the End

```c
DynResult dyn_pop_back(DynArray *arr) {
    if (arr == NULL) return DYN_ERR_NULL_PTR;
    if (arr->size == 0) return DYN_ERR_OUT_OF_RANGE;
    arr->size--;
    return DYN_OK;
}
```

The "deleted" element is still lying in memory; it will be overwritten on the next `push_back`.

> ⚠️ **Pitfall Warning**
> We don't trigger shrinkage after `pop_back` — if you `pop_back` and immediately `push_back` again, the shrinkage was done for nothing. Shrinkage should be explicitly invoked by the caller via `shrink_to_fit`. `std::vector::pop_back` follows the same design.

### insert and erase — Middle Insertion and Deletion

`insert` needs to shift all elements after the insertion point back by one position, while `erase` shifts them forward by one to overwrite the deleted element. Both must use `memmove` instead of `memcpy` — because the source and destination memory regions overlap, and `memcpy`'s behavior in overlapping situations is undefined.

```c
DynResult dyn_insert(DynArray *arr, size_t index, const void *elem) {
    if (arr == NULL || elem == NULL) return DYN_ERR_NULL_PTR;
    if (index > arr->size) return DYN_ERR_OUT_OF_RANGE;
    if (arr->size == arr->capacity) {
        DynResult err = dyn_grow(arr);
        if (err != DYN_OK) return err;
    }
    void *dest = (char *)arr->data + (index + 1) * arr->elem_size;
    void *src  = (char *)arr->data + index * arr->elem_size;
    size_t count = (arr->size - index) * arr->elem_size;
    memmove(dest, src, count);
    memcpy((char *)arr->data + index * arr->elem_size, elem, arr->elem_size);
    arr->size++;
    return DYN_OK;
}

DynResult dyn_erase(DynArray *arr, size_t index) {
    if (arr == NULL) return DYN_ERR_NULL_PTR;
    if (index >= arr->size) return DYN_ERR_OUT_OF_RANGE;
    void *dest = (char *)arr->data + index * arr->elem_size;
    void *src  = (char *)arr->data + (index + 1) * arr->elem_size;
    size_t count = (arr->size - index - 1) * arr->elem_size;
    memmove(dest, src, count);
    arr->size--;
    return DYN_OK;
}
```

Verifying insert and erase:

```c
int val = 99;
DYN_CHECK(dyn_insert(&arr, 2, &val));  // 在下标 2 处插入 99
DYN_CHECK(dyn_erase(&arr, 0));         // 删除下标 0 处的元素
```

```text
插入后: [0, 1, 99, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]
删除后: [1, 99, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]
```

`std::vector::insert` has had an rvalue reference overload since C++11, accepting move semantics to avoid deep copies. Our C version can only do a shallow copy via `memcpy` — if elements contain dynamically allocated memory (such as strings pointing to `malloc`-allocated buffers), shallow copies will lead to double free crashes. This is a fundamental limitation of generic programming in C.

## Step Seven — Implementing Traversal and Search

### Traversal — The Callback Function Pattern

Internally the container is `void *`, so it doesn't know the element type. "How to process each element" must be told to the container by the caller through a callback function — a form of "inversion of control":

```c
typedef void (*DynForEachFn)(void *elem, void *user_data);

DynResult dyn_for_each(DynArray *arr, DynForEachFn fn, void *user_data) {
    if (arr == NULL || fn == NULL) return DYN_ERR_NULL_PTR;
    for (size_t i = 0; i < arr->size; i++) {
        void *elem = (char *)arr->data + i * arr->elem_size;
        fn(elem, user_data);
    }
    return DYN_OK;
}
```

```c
void print_int(void *elem, void *user_data) {
    (void)user_data;
    printf("%d ", *(int *)elem);
}

dyn_for_each(&arr, print_int, NULL);
printf("\n");
```

```text
0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 
```

The callback function pattern is used extensively in the C standard library — the comparison function in `qsort`, `bsearch`, and others all follow this pattern.

### Search — Linear Search

"Comparing for equality" also needs to be provided by the caller:

```c
typedef bool (*DynEqualFn)(const void *elem, const void *target);

DynResult dyn_find(DynArray *arr, const void *target, DynEqualFn eq, size_t *out_index) {
    if (arr == NULL || target == NULL || out_index == NULL) return DYN_ERR_NULL_PTR;
    for (size_t i = 0; i < arr->size; i++) {
        void *elem = (char *)arr->data + i * arr->elem_size;
        if (eq(elem, target)) {
            *out_index = i;
            return DYN_OK;
        }
    }
    return DYN_ERR_OUT_OF_RANGE;
}
```

Time complexity is O(N). If you need faster lookups, you can sort first and then use binary search. C++'s `std::find` uses iterators paired with lambda expressions, which is far more elegant to write than callback functions; C++20's Ranges turn traversal, filtering, and transformation into chained calls.

## C++ Comparison: Design Trade-offs in std::vector

At this point we've hand-rolled a complete dynamic array library. Looking back and systematically comparing with `std::vector`, understanding these design trade-offs is far more important than memorizing APIs.

Using `void *` for generic programming brought us three problems: no type checking, needing to manually pass `elem_size`, and requiring casts inside callback functions. `std::vector` solves all three perfectly with templates — the compiler determines the type `T` at instantiation time, all type checks are completed at compile time, and `elem_size` is automatically calculated. `std::vector`'s destructor automatically frees the internal array whether the function returns normally or exits due to an exception — this is the core idea of RAII: binding resource lifetime to object lifetime. C++11's move semantics make `std::vector` move operations O(1) pointer swaps, whereas in C we can only `memcpy` the entire block of data.

There are two easily confused functions: `reserve` only changes `capacity` without changing `size`, pre-allocating memory without creating new elements; `resize` changes `size`, with extra positions value-initialized and excess elements destroyed. Our C version only implemented `reserve`; `resize` is left as an exercise. Additionally, `std::vector<bool>` uses bit-packing optimization (each element only takes 1 bit), but the trade-off is that you can't take the address of individual elements. C++17's `std::span` provides a non-owning view over contiguous memory and is an extremely important composition tool.

## Exercises

The following exercises only provide function signatures and requirement descriptions. The implementation is left blank.

### Exercise 1: Implement resize

`reserve` only changes capacity without changing size, while `resize` needs to change size. When the new size is greater than the old size, the extra positions should be filled with default values.

```c
DynResult dyn_resize(DynArray *arr, size_t new_size, const void *default_val);
```

### Exercise 2: Implement filter

Given a dynamic array and a filter predicate, return a newly created dynamic array containing only the elements that satisfy the condition.

```c
DynResult dyn_filter(DynArray *src, bool (*pred)(const void *, void *), void *user_data, DynArray *out);
```

### Exercise 3: Implement map transformation

Given a dynamic array and a transformation function, apply the transformation function to each element and store the results in a new array to return.

```c
DynResult dyn_map(DynArray *src, void (*transform)(void *, const void *, void *), void *user_data, DynArray *out);
```

### Exercise 4: Implement concatenation

Concatenate two dynamic arrays of the same type into a new dynamic array.

```c
DynResult dyn_concat(const DynArray *a, const DynArray *b, DynArray *out);
```

> **Difficulty Self-Assessment**: If you find the exercises difficult, please review the design思路 of the corresponding sections. Especially for resize — it's essentially a combination of reserve + memset/memcpy. Once you figure out which positions need filling and what values to fill them with, the code will naturally follow.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
