---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A Deep Dive into `std::array`: Zero-overhead wrapping of C arrays as
  aggregate types, preventing pointer decay, `std::get` and structured bindings, iterators
  that never invalidate, `constexpr` compile-time lookup tables, and the precise boundaries
  with C arrays and `vector`.'
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
  translated_at: '2026-06-24T01:20:02.301356+00:00'
  engine: anthropic
  token_count: 1333
---
# array: A Fixed-Size Aggregate Container for Compile-Time

## What is array: A Zero-Overhead Aggregate Wrapper for C Arrays

`std::array` is the "modern shell" that C++11 applied to C arrays. C arrays `T[N]` have several old issues: they decay into pointers when passed as arguments (losing length), lack `.size()`, cannot be copied or assigned as a whole, and cannot be used as function return values. `std::array<T, N>` wraps this contiguous memory in a class template, supplements it with STL interfaces, and—this is the key point—**it is an aggregate type with absolutely no extra overhead**: its `sizeof` is identical to that of a C array, and it has no virtual functions, no vtable pointers, and no extra members.

```cpp
std::array<int, 5> a = {1, 2, 3, 4, 5};   // 大小 5 在编译期定死
a.size();        // 5
a[0];            // 1，O(1)
a.data();        // int*，指向底层连续内存
```

That `N` is a template parameter and a compile-time constant. This means the array size is part of the type—`std::array<int, 5>` and `std::array<int, 6>` are two distinct types and cannot be assigned to one another. The trade-off is zero dynamic allocation: the memory occupied by the array is just that contiguous block of data, located on the stack or in the static area, without touching the heap.

## Precise Comparison with C Arrays: No Decay, Interfaces, and Object Semantics

Let's list the improvements `array` offers over C arrays one by one. First, **it does not decay to a pointer**: a C array decays to a `T*` when passed to a function, losing its length information; `array` is an object, so passing it preserves the complete type (including `N`). We must either pass `const std::array<T, N>&` or explicitly call `.data()` to interface with C APIs. Second, **it provides STL interfaces**: `.size()`, `.empty()`, `.begin()` / `.end()`, `.data()`, `operator[]`, and `.at()` allow it to work seamlessly with `<algorithm>` and range-based for loops. Third, **it supports copy and assignment**: `auto b = a;` performs an element-wise copy, and it can be used as a function return value or a class member—feats that C arrays cannot accomplish.

```cpp
std::array<int, 4> make() { return {1, 2, 3, 4}; }   // C 数组做不到
auto a = make();
auto b = a;        // 整体拷贝，C 数组做不到
b.fill(0);         // 一把清零
```

However, the underlying backing is still that same contiguous block of memory. The standard guarantees that `std::array` is an aggregate, so `sizeof(std::array<T, N>)` is exactly equal to `sizeof(T) * N` (no extra members, no wasted space beyond tail padding). It incurs zero overhead, simply providing better interfaces and type safety.

## The Boundary with `vector`: When to Use Fixed Size

The dividing line between `array` and `vector` comes down to one question: **Is the size known at compile time?** If the size is fixed at compile time and won't change, use `array`—zero heap allocation, zero overhead, `constexpr` capable, and can be placed in static storage to save RAM. If the size is determined at runtime or requires dynamic resizing, use `vector`.

The trade-offs are equivalent: the size is part of the `array`'s type (so `array<int, 5>` and `array<int, 6>` are not compatible), meaning a function cannot accept an "int array of any size" using `array` directly (you would need to use `span` or templates); `vector` does not have this limitation, but incurs heap allocation and reallocation overhead. In short: **fixed size means `array`, variable size means `vector`**. For the middle ground (size known at runtime but avoiding heap allocation), we can look forward to C++26's `inplace_vector`, or manage a buffer manually paired with `span`.

## Privileges as an Aggregate Type: `std::get`, Structured Bindings, and Tuple-like Interface

Because `std::array` is an aggregate, it enjoys "tuple-like" benefits beyond those of C arrays. `std::get<I>(arr)` allows accessing elements by compile-time index (returning a reference with type safety); C++17 structured bindings allow us to unpack small arrays directly into variables; and `std::tuple_size` and `std::tuple_element` recognize `array`, allowing it to fit into generic code that expects tuple-like types.

```cpp
std::array<int, 3> a = {10, 20, 30};
std::get<1>(a);            // 20，编译期下标，类型安全
auto [x, y, z] = a;        // 结构化绑定：x=10, y=20, z=30
static_assert(std::tuple_size_v<decltype(a)> == 3);
```

None of these features exist on C arrays—C arrays don't get `std::get`, nor do they support structured binding. For those small arrays with a "fixed number of values" (like 3D coordinates or RGB), using `array` with structured binding is actually more convenient than writing a `struct`.

## Complexity, Iterator Invalidation, and Exception Safety

The complexity is straightforward: random access via `operator[]` and `.at()` are both O(1), and traversal is O(n). There is no capacity expansion or reallocation—because the size is fixed.

Regarding **iterator invalidation**, `array` is the least of our worries: iterators never invalidate. Since `array` is a fixed-size aggregate, there is no resizing or insertion/deletion (the interface lacks `push_back` / `insert` entirely). As long as the `array` object itself is alive, any iterators, references, or pointers obtained remain valid. This is cleaner than `vector` (invalidation on resize), `deque`, or `list`.

There is one point to note regarding exception safety: `.at(i)` performs bounds checking and throws `std::out_of_range` if out of bounds; `operator[]` performs no checking, so an out-of-bounds access is undefined behavior (UB). In environments where exceptions are disabled (e.g., `-fno-exceptions`), an out-of-bounds `.at()` degrades to `std::terminate`. Therefore, in such scenarios, we must use `operator[]` and ensure index correctness ourselves.

## Let's Run It: Zero Overhead and constexpr

Just talking about "zero overhead" isn't concrete enough, so let's run some code. First, let's confirm that `sizeof` is truly the same as a C array:

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

The `sizeof` is exactly the same, with zero overhead — `array` is simply that contiguous memory block wrapped in a class. `data()` indeed points to the first element, so we can safely pass it to C interfaces or DMA.

Another major strength of `array` is **constexpr** — it allows initialization and computation to be completed at compile time, placing the generated data directly into the read-only section. A classic use case is generating a CRC lookup table at compile time:

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

This 256-item table is computed at compile time. At runtime, the program reads directly from the read-only section, consuming neither RAM nor CPU cycles. This "compile-time lookup" is a perfect combination of `array` + `constexpr`—achieving this level of cleanliness is difficult with C arrays (especially when copies are involved).

## Extension: `array` in Embedded Systems (DMA / Flash / Stack)

`array` is particularly popular in embedded development due to its zero heap allocation, contiguous memory, and compatibility with `constexpr`. Here are a few practical tips (supplementary details, use as needed):

First, **guaranteed contiguous memory**: the pointer returned by `.data()` points to a contiguous storage block, which can be safely passed to DMA or HAL, provided the element type is trivially copyable. Second, **saving RAM with static storage**: use `static` for large arrays or place them in `.bss`; for lookup tables, use `constexpr` to store them directly in flash, avoiding RAM usage entirely. Third, **stack depth**: small arrays on the stack are fine, but be mindful of stack depth limits in tasks or ISRs—avoid placing large arrays on constrained stacks.

## Wrapping Up

`array` is the modern wrapper for C arrays: zero overhead, STL interfaces, no decay, usable as an object, and compatible with `std::get` and structured binding via its aggregate nature. It offers non-invalidating iterators, `constexpr` support, and zero heap allocation—as long as the size is fixed at compile time, it is a superior choice to both C arrays and `vector`. In the next article, we will examine its "dynamic counterpart," `vector`, moving from fixed size to variable size, at the cost of heap usage and reallocation.

Want to try it out right now? Check out the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="array: Zero-overhead Aggregate Container and Compile-time Lookup"
  source-path="code/examples/vol3/02_array.cpp"
  description="sizeof matches C arrays, constexpr CRC compile-time lookup, structured binding"
  allow-run
/>

## References

- [std::array — cppreference](https://en.cppreference.com/w/cpp/container/array)
- [Aggregate types — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization)
- [Container iterator invalidation rules summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
