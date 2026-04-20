# Building a Dynamic Array from Scratch — Implementing a Container from Zero

When writing C programs, one of the most painful things is that array sizes must be determined at compile time. You want to store 10 items, so you declare `int arr[10]`; later the requirement changes to 100, and you have to go back, modify the code, and recompile. Even worse, in many cases you have no idea how many items will arrive at runtime — how many records a user enters, how many packets the network receives, how many samples a sensor collects — these can only be determined at runtime.

`malloc` does solve the uncertain-size problem, but it only handles allocation, not growth — once it is full and you want to keep adding, you must manually `realloc`, manage capacity yourself, and handle errors yourself. `malloc/realloc/free` and `size` variables scattered throughout your codebase quickly become a maintenance nightmare. In Python you can casually write `list.append(x)`, and in C++ you have `std::vector` — both automatically grow. But the C standard library has no such thing, so we must build one ourselves.

Today we will build a complete dynamic array library from scratch. Along the way, we will clarify data structure design, memory growth and shrinkage strategies, and error handling patterns. Finally, we will compare our work with C++'s `std::vector` to see how the standard library handles these same problems.

> **Learning Objectives**
>
> - [ ] Understand the necessity of the dynamic array's size/capacity/data three-field design
> - [ ] Master the 2x growth strategy and its amortized O(1) complexity analysis
> - [ ] Understand shrinkage timing choices to avoid frequent realloc calls
> - [ ] Master the enum return code error handling pattern
> - [ ] Be able to independently design a complete CRUD API
> - [ ] Understand the internal mechanisms of `std::vector` and their correspondence to our C implementation

## Environment Notes

All code examples in this article compile and run in a standard C environment. When compiling, we recommend always including `-Wall -Wextra` — dynamic array implementation involves extensive pointer arithmetic and `memcpy/memmove` calls, and compiler warnings can help you catch many potential issues.

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
依赖：无
```

## Step One — Understand What a Dynamic Array Actually Is

From a physical storage perspective, a dynamic array is still a contiguous block of memory, no different from a plain array. The key difference is that a dynamic array separates "used space" from "reserved space" and uses a pointer for indirect access, so it can swap in a larger block when needed. You can think of it as a warehouse that can automatically "move to a bigger building" — when the shelves are full, you switch to a warehouse with more shelves, move all the old goods over, and to the outside world the address changed but the interface for storing and retrieving goods remains the same.

Let us start with the simplest prototype:

```c
typedef struct {
    void* data;          // 连续内存块
    size_t size;         // 当前有多少个元素
} DynamicArray;
```

`data` points to contiguous memory allocated on the heap, and `size` records the current element count. But you will notice a fatal problem: we are using `void*`, so we do not know how large each element is. For a `int` array the stride is 4 bytes, for `double` it is 8 bytes, and a custom struct could be dozens of bytes. Without element size information, we cannot locate the Nth element at all.

So we need to add `capacity` and `element_size`:

```c
typedef struct _DynamicArray_ {
    void* data;              // 连续内存块（存储实际数据）
    size_t size;             // 当前元素个数
    size_t capacity;         // 当前分配的总容量（元素个数计）
    size_t element_size;     // 单个元素的字节大小
} DynamicArray;
```

The four fields each have their own role: `data` manages "where it lives," `size` manages "how many are used," `capacity` manages "how many total slots," and `element_size` manages "how big each slot is." With `element_size`, locating the address of the `i`th element is `(char*)data + i * element_size` — we must first cast to `char*` because `char` is exactly 1 byte, making the pointer arithmetic a precise byte offset. Doing addition or subtraction directly on `void*` will cause a compiler error (the C standard does not allow it, although GCC permits it as an extension, but it is not portable).

> ⚠️ **Pitfall Warning**
> `size` is "how many valid elements actually exist," while `capacity` is "how many elements this memory block can hold at most." `size <= capacity`. If you use `capacity` instead of `size` as the upper bound during iteration, you will read uninitialized garbage data.

The internal data layout of `std::vector` is almost identical to ours, except that the template parameter `T` replaces the `void*` + `element_size` combination, and type safety is guaranteed at compile time. `sizeof(std::vector<int>)` is 24 bytes in most implementations — three 8-byte fields (pointer + size + capacity) — and `element_size` does not need to be stored after template instantiation.

## Step Two — Establish an Error Handling System

Before writing functional code, let us solve an engineering problem: what do we do when a function fails? The laziest approach is to call `exit(-1)` on error — this is common in teaching code, but in real engineering it is a disaster. You cannot just kill the entire server process because one `push_back` failed, right?

We use an enum to establish a clear error code system:

```c
typedef enum _DynamicArrayStatus_ {
    kSuccess            = 0,    // 正常执行
    kNullPointer        = -1,   // 传入了 NULL 指针
    kOutOfMemory        = 1,    // 内存分配失败
    kIndexOutOfRange    = -2,   // 下标越界
    kInvalidOperation   = -3    // 非法操作（如对空数组 pop）
} DynamicArrayStatus;
```

Every function returns `DynamicArrayStatus`, and the caller can check whether the operation succeeded and why it failed. We can pair this with a helper macro to output friendly error messages:

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

Separating error message display from error code generation is an even better approach — the caller might want to write errors to a log file instead of printing to the terminal, or might want to clean up resources after an error. Enum return codes give the caller complete control.

## Step Three — Implement Creation and Destruction

### Creation — Factory Function

In object-oriented languages this is called a constructor; in C we call it a factory function — it "produces" an initialized object and returns it to the caller.

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

After allocating the struct memory, we must immediately check the `malloc` return value — accessing `arr->data` without checking will cause an immediate segfault. We set a minimum capacity of 8 as a rule of thumb; too small leads to frequent growth, and too large wastes memory.

> ⚠️ **Pitfall Warning**
> Note the presence of `free(arr)`. This is a very classic resource leak scenario: the struct allocation succeeded, but the data area allocation failed. If you simply `return NULL` without `free(arr)`, that struct memory is leaked forever. This situation of "partially allocating resources but failing in subsequent steps" is one of the most error-prone aspects of C memory management.

Usage:

```c
DynamicArray* nums = dynamic_array_create(16, sizeof(int));
if (nums == NULL) {
    fprintf(stderr, "Failed to create dynamic array\n");
    return -1;
}
```

Use `sizeof(int)` instead of hardcoding `4` — the size of `int` may differ across platforms, while `sizeof` is computed at compile time with no runtime overhead.

### Destruction — Release Order Must Not Be Reversed

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

The release order must not be reversed — if you `free(arr)` first, then `arr->data` is accessing already-freed memory (Use After Free). Another issue is that after `destroy`, the `arr` pointer itself does not become `NULL`; it still points to that freed memory. C function parameters are pass-by-value, so we can only rely on the caller to manually set it to NULL:

```c
dynamic_array_destroy(nums);
nums = NULL;  // 手动置 NULL，防止后续误用
```

C++'s RAII mechanism solidifies this create/destroy pairing at the language level — the destructor is automatically called when the object leaves scope, so memory absolutely cannot leak. In our C version, every step of resource management relies on human discipline.

## Step Four — Get Capacity Management Right

### Growth — The 2x Strategy

When `size == capacity`, the array is full, and inserting another element requires growth. The question is: how much to grow? If we add 1 each time, inserting N elements consecutively requires N `realloc` calls, with a total copy volume of 1 + 2 + ... + N = O(N²), which is completely unacceptable. Doubling — doubling the capacity each time it fills — requires only about log₂(N) growth operations, with a total copy volume of ≈ 2N = O(N), amortized to O(1) per insertion. It is like moving houses — instead of buying one more box each time, you double the house area each time — the move itself is exhausting, but averaged over each day it is barely noticeable.

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

`realloc` will try to expand in place at the original location; if that fails, it finds a larger block on the heap and copies the old data over. In either case, the returned pointer points to valid memory, and the old data is intact.

> ⚠️ **Pitfall Warning**
> `realloc` may return a different address! You must use `arr->data = new_data` to update the pointer. If you write `realloc(arr->data, ...)` without capturing the return value, you lose the new address after the move, and the memory at the old address has already been freed — a double disaster.

### Shrinkage — Avoiding Thrashing

If an array once grew to 10,000 elements and was later trimmed to just 10, the memory for 9,990 elements is wasted. But shrinkage timing is much trickier than growth — consider an array oscillating between 100 and 50: shrinking at 50, then immediately needing to insert again, growing back to 100 — this back-and-forth is the classic "thrashing" problem. Our strategy is to shrink to `size` but keep a minimum capacity of 8, called explicitly by the user:

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

`shrink_to_fit` is typically only called when you are "certain there will be no more significant growth," such as after data loading is complete. The C++ standard does not mandate that `std::vector`'s growth factor must be 2x — MSVC uses 1.5x, while libstdc++ and libc++ use 2x. A factor of 1.5x yields better memory utilization but slightly more growth operations.

## Step Five — Implement Element Access

We provide two access methods: a fast version without bounds checking (similar to `std::vector::operator[]`) and a safe version with bounds checking (similar to `std::vector::at()`).

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

The safe version returns a copy into the caller's buffer, because C has no concept of references and the data area is `void*`, so the function cannot directly return a value of the correct type. This is indeed much more cumbersome than C++'s `vec.at(i)`, but it is the price of generic programming in C.

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

## Step Six — Implement Insertion and Deletion

### push_back — Append to the End

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

The destination address of `memcpy` is `(char*)arr->data + arr->size * arr->element_size` — skipping all existing elements to reach the first empty slot. Thanks to the 2x growth strategy, N consecutive `push_back` calls take O(N) total time, amortized to O(1).

Let us verify the growth behavior:

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

The initial capacity of 4 is raised to the minimum of 8, and after inserting 20 elements it undergoes two growth operations: 8 -> 16 -> 32.

### pop_back — Remove from the End

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

The "deleted" element still sits in memory and will be overwritten on the next `push_back`.

> ⚠️ **Pitfall Warning**
> We do not trigger shrinkage after `pop_back` — if you `pop` and immediately `push` again, the shrinkage was wasted. Shrinkage should be explicitly invoked by the caller via `shrink_to_fit`. `std::vector::pop_back` follows the same design.

### insert and erase — Middle Insertion and Deletion

`insert` needs to shift all elements after the insertion point back by one position, while `erase` shifts them forward by one to overwrite the deleted element. Both must use `memmove` instead of `memcpy` — because the source and destination memory regions overlap, `memcpy`'s behavior in overlapping cases is undefined.

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

`std::vector::push_back` gained an rvalue reference overload in C++11, accepting move semantics to avoid deep copies. Our C version can only do a shallow copy via `memcpy` — if elements contain dynamically allocated memory (such as strings pointing to `malloc`-allocated buffers), shallow copies will cause double free crashes. This is a fundamental limitation of generic programming in C.

## Step Seven — Implement Traversal and Search

### Traversal — The Callback Function Pattern

The container's interior is `void*`, so it does not know the element type. Therefore, "how to process each element" must be communicated to the container by the caller through a callback function — a form of "inversion of control":

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

The callback function pattern is used extensively in the C standard library — the comparison function in `qsort`, and `bsearch` all follow this pattern.

### Search — Linear Search

"Comparing for equality" also needs to be provided by the caller:

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

Time complexity is O(N). For faster lookups, you can sort first and then use binary search. C++'s `std::find` uses iterators paired with a lambda expression, which is far more elegant than callback functions; C++20's Ranges turn traversal, filtering, and transformation into chained calls.

## C++ Comparison: std::vector's Design Trade-offs

At this point we have built a complete dynamic array library from scratch. Looking back and systematically comparing with `std::vector`, understanding these design trade-offs is far more important than memorizing APIs.

Using `void*` for generics brought us three problems: no type checking, needing to manually pass `element_size`, and requiring casts inside callback functions. `std::vector<T>` solves all three perfectly with templates — the compiler determines the type `T` at instantiation time, all type checking is completed at compile time, and `sizeof(T)` is automatically calculated. `std::vector`'s destructor automatically releases the internal array whether the function returns normally or exits due to an exception — this is the core idea of RAII: binding resource lifetimes to object lifetimes. C++11's move semantics make `vec2 = std::move(vec1)` an O(1) pointer swap, while in C we can only `memcpy` the entire data block.

There are two easily confused functions: `reserve(n)` only changes `capacity` without changing `size`, pre-allocating memory without creating new elements; `resize(n)` changes `size`, with extra positions value-initialized and excess elements destroyed. Our C version only implements `reserve`, leaving `resize` as an exercise. Additionally, `std::vector<bool>` uses bit-packing optimization (each `bool` takes only 1 bit), but at the cost of not being able to take the address of individual elements. C++17's `std::span<T>` provides a non-owning view over contiguous memory and is an extremely important composition tool.

## Exercises

The following exercises provide only function signatures and requirement descriptions, with the implementation left blank.

### Exercise 1: Implement resize

`reserve` only changes capacity without changing size, while `resize` needs to change size. When the new size is greater than the old size, the extra positions should be filled with default values.

```c
/// @brief 改变动态数组的元素个数
/// @param default_value 指向默认值的指针（用于填充新增位置），可以为 NULL（填零）
DynamicArrayStatus dynamic_array_resize(
    DynamicArray* arr,
    size_t new_size,
    const void* default_value
);
// TODO: 自行实现
```

### Exercise 2: Implement filter

Given a dynamic array and a filter predicate, return a newly created dynamic array containing only the elements that satisfy the condition.

```c
/// @brief 根据谓词过滤动态数组的元素
DynamicArray* dynamic_array_filter(
    const DynamicArray* arr,
    int (*pred)(const void* element)
);
// TODO: 自行实现
```

### Exercise 3: Implement map transformation

Given a dynamic array and a transformation function, apply the transformation function to each element and store the results in a new array to return.

```c
/// @brief 对动态数组的每个元素应用变换函数
/// @param out_element_size 输出数组的元素大小（可能与输入不同）
DynamicArray* dynamic_array_map(
    const DynamicArray* arr,
    void (*transform)(const void* in, void* out),
    size_t out_element_size
);
// TODO: 自行实现
```

### Exercise 4: Implement concatenation

Concatenate two dynamic arrays of the same type into a new dynamic array.

```c
/// @brief 将两个动态数组拼接成一个新的动态数组
DynamicArray* dynamic_array_concat(
    const DynamicArray* arr1,
    const DynamicArray* arr2
);
// TODO: 自行实现
```

> **Difficulty Self-Assessment**: If you find the exercises difficult, please review the design思路 of the corresponding sections. Especially for resize — it is essentially a combination of reserve + memset/memcpy. Once you figure out which positions need filling and what values to fill them with, the code will come naturally.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
