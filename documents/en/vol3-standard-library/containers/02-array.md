---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A Deep Dive into `std::array`: Wrapping C Arrays as Aggregates with
  Zero Overhead, No Pointer Decay, `std::get` and Structured Bindings, Iterators That
  Never Invalidate, `constexpr` Compile-Time Lookup, and the Precise Boundaries with
  C Arrays and `vector`'
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 7
related:
- vector 深入：三指针、扩容与迭代器失效
tags:
- host
- cpp-modern
- intermediate
- array
- 容器
title: 'array: A fixed-size aggregate container determined at compile time'
translation:
  source: documents/vol3-standard-library/containers/02-array.md
  source_hash: 7c61645f47239ac6cb379c18978d92de85382501523cd72c6e30c51e6cec442d
  translated_at: '2026-06-16T05:47:46.107496+00:00'
  engine: anthropic
  token_count: 1333
---
# array: A Fixed-Size Aggregate Container for Compile-Time

## What `array` Actually Is: A Zero-Overhead Aggregate Wrapper for C Arrays

`std::array` is the "modern shell" that C++11 retrofitted for C arrays. C arrays `T[N]` have several old shortcomings: they decay into pointers when passed as arguments (losing length information), lack a `.size()` method, cannot be copied or assigned as a whole, and cannot be used as function return values. `std::array<T, N>` wraps this contiguous memory block in a class template, equipping it with STL interfaces, and—this is the key point—**it is an aggregate type with absolutely zero overhead**: its `sizeof` is identical to that of a C array, and it has no virtual functions, no v-pointers, and no extra members.

```cpp
std::array<int, 5> a = {1, 2, 3, 4, 5};   // 大小 5 在编译期定死
a.size();        // 5
a[0];            // 1，O(1)
a.data();        // int*，指向底层连续内存
```

That `N` is a template parameter, a compile-time constant. This means the array's size is part of its type—`std::array<int, 5>` and `std::array<int, 6>` are two distinct types and cannot be assigned to each other. The trade-off is zero dynamic allocation: the memory occupied by the array is simply that contiguous block of data, residing on the stack or in the static region, without touching the heap.

## Precise Comparison with C Arrays: No Decay, Interfaces, and Object Semantics

Let's enumerate the improvements `std::array` offers over C arrays. First, **it does not decay to a pointer**: passing a C array to a function decays it to `T*`, losing length information; `std::array` is an object that fully preserves its type (including `N`) when passed. We must pass it as `const std::array<T, N>&`, or explicitly call `.data()` to interface with C APIs. Second, **it provides STL interfaces**: `.size()`, `.empty()`, `.begin()` / `.end()`, `.data()`, `operator[]`, and `.at()` allow it to work seamlessly with `<algorithm>` and range-based for loops. Third, **it supports copy and assignment**: `auto b = a;` performs an element-wise copy, and it can be used as a return value or a class member—feats that C arrays cannot accomplish.

```cpp
std::array<int, 4> make() { return {1, 2, 3, 4}; }   // C 数组做不到
auto a = make();
auto b = a;        // 整体拷贝，C 数组做不到
b.fill(0);         // 一把清零
```

However, the underlying data is still that contiguous block of memory. The standard guarantees that `std::array` is an aggregate, so `sizeof(std::array<T, N>)` is exactly equal to `sizeof(T) * N` (no extra members, no wasted space other than potential tail padding). It has zero overhead, simply providing better interfaces and type safety.

## The Boundary with `vector`: When to Use Fixed Size

The dividing line between `array` and `vector` comes down to one question: **Is the size known at compile time?** If the size is fixed at compile time and will not change, use `array`—it offers zero heap allocation, zero overhead, is `constexpr`-friendly, and can be placed in static storage to save RAM. If the size is determined at runtime or requires dynamic resizing, use `vector`.

The trade-offs are balanced: because an `array`'s size is part of its type (`array<int, 5>` and `array<int, 6>` are distinct types), a function cannot accept "an `int` array of any size" using `array` directly (you would need to use `span` or templates). `vector` does not have this restriction, but it incurs heap allocation and reallocation overhead. In short: **use `array` for fixed size, `vector` for dynamic size**. For the middle ground (size known at runtime but avoiding heap allocation), we can look forward to C++26's `inplace_vector`, or manage a buffer manually paired with `span`.

## Privileges of an Aggregate: `std::get`, Structured Bindings, and Tuple-like Interface

Since `std::array` is an aggregate type, it enjoys the benefits of being "tuple-like" in addition to being a C array. `std::get<I>(arr)` allows accessing elements by compile-time index (returning a reference with type safety). C++17 structured bindings allow us to unpack a small `array` directly into variables. Furthermore, `std::tuple_size` and `std::tuple_element` recognize `array`, allowing it to fit seamlessly into generic code that expects tuple-like types.

```cpp
std::array<int, 3> a = {10, 20, 30};
std::get<1>(a);            // 20，编译期下标，类型安全
auto [x, y, z] = a;        // 结构化绑定：x=10, y=20, z=30
static_assert(std::tuple_size_v<decltype(a)> == 3);
```

None of these features exist for C arrays—C arrays cannot be used with `std::get` and do not support structured binding. For small arrays holding a "fixed set of values" (like 3D coordinates or RGB values), using `array` combined with structured binding is often more convenient than defining a custom struct.

## Complexity, Iterator Invalidation, and Exception Safety

The complexity is straightforward: random access via `operator[]` and `.at()` is O(1), and traversal is O(n). There is no capacity expansion or reallocation—because the size is fixed at compile time.

**Iterator invalidation** is the least of our worries with `array`: iterators never invalidate. Since `array` is a fixed-size aggregate, there is no resizing or insertion/deletion (the interface lacks `push_back` / `insert` entirely). As long as the `array` object itself is alive, any iterators, references, or pointers obtained from it remain valid. This is cleaner than `vector` (where iterators invalidate on resize), `deque`, or `list`.

Regarding exception safety, there is one point to note: `.at(i)` performs bounds checking and throws `std::out_of_range` if out of bounds; `operator[]` performs no checking, so an out-of-bounds access is undefined behavior (UB). In environments where exceptions are disabled (e.g., with `-fno-exceptions`), an out-of-bounds `.at()` degrades to `std::terminate`. Therefore, in such scenarios, we must use `operator[]` and ensure index correctness ourselves.

## Let's Run It: Zero Overhead and constexpr

Simply claiming "zero overhead" isn't concrete enough, so let's verify it. First, we confirm that `sizeof` is truly identical to that of a C array:

```cpp
#include <array>
#include <iostream>

int main()
{
    int raw[8];
    std::array<int, 8> arr;
    std::cout << "sizeof(int[8])        = " << sizeof(raw) << '\n';
    std::cout << "sizeof(array<int,8>)  = " << sizeof(arr) << '\n';
    std::cout << "data() 指向首元素？   " << (arr.data() == &arr[0]) << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/array_sizeof /tmp/array_sizeof.cpp && /tmp/array_sizeof
```

```text
sizeof(int[8])        = 32
sizeof(array<int,8>)  = 32
data() 指向首元素？   1
```

The `sizeof` is exactly the same, with zero overhead—`array` is simply that contiguous memory block wrapped in a class. `data()` correctly points to the first element, so we can safely pass it to C interfaces or DMA.

Another major strength of `array` is **constexpr**—it allows initialization and computation at compile time, placing the generated data directly into the read-only section. A classic use case is generating a CRC lookup table at compile time:

```cpp
#include <array>
#include <cstdint>

constexpr std::array<uint32_t, 256> make_crc_table()
{
    std::array<uint32_t, 256> t{};
    for (std::size_t i = 0; i < 256; ++i) {
        uint32_t crc = static_cast<uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
        t[i] = crc;
    }
    return t;
}

// 编译期算完，进只读段；运行时零开销
constexpr auto crc_table = make_crc_table();
static_assert(crc_table.size() == 256);
static_assert(crc_table[0] == 0x00000000u);   // 输入 0，结果 0
```

This 256-item table is computed at compile time, so at runtime we read directly from the read-only section. It consumes no RAM and costs no CPU cycles. This "compile-time lookup" is a perfect combination of `array` + `constexpr`—C arrays with `constexpr` can't achieve this level of cleanliness (especially when returning by copy).

## Extension: `array` in Embedded Systems (DMA / Flash / Stack)

Because `array` involves zero heap allocation, guarantees contiguous memory, and works with `constexpr`, it is particularly popular in embedded systems. Here are a few practical points to keep in mind (supplementary to the main topic, use as needed). First, **contiguous memory guarantee**: the pointer returned by `.data()` points to contiguous storage, which can be safely passed to DMA or HAL, provided the element type is trivially copyable. Second, **saving RAM with static storage**: use `static` for large arrays or place them in `.bss`; for lookup tables, use `constexpr` to place them directly in flash, saving RAM. Third, **stack depth**: small arrays on the stack are fine, but be mindful of task / ISR stack depth limits—don't place large arrays on a narrow stack.

## Wrapping Up

`array` is a modern wrapper for C arrays: zero overhead, STL interfaces, no decay, usable as an object, and compatible with `std::get` and structured binding due to its aggregate nature. It offers non-invalidating iterators, `constexpr` support, and zero heap allocation—as long as the size is fixed at compile time, it is a better choice than both C arrays and `vector`. In the next article, we will look at its "dynamic version," `vector`, moving from fixed to variable size at the cost of the heap and reallocation.

Want to try it out right now? Open the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="array: Zero-Overhead Aggregate Container and constexpr Lookup"
  source-path="code/examples/vol3/02_array.cpp"
  description="sizeof consistent with C arrays, constexpr CRC compile-time lookup, structured binding"
  allow-run
/>

## References

- [std::array — cppreference](https://en.cppreference.com/w/cpp/container/array)
- [Aggregate types — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization)
- [Container iterator invalidation rules summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
