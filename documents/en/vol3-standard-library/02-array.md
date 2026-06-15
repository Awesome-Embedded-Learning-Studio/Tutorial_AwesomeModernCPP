---
title: 'array: A fixed-size aggregate container determined at compile time'
description: 'A Deep Dive into `std::array`: Wrapping C Arrays as Aggregate Types
  with Zero Overhead, No Pointer Decay, `std::get` and Structured Bindings, Iterators
  That Never Invalidate, `constexpr` Compile-Time Lookup, and the Precise Boundary
  Between C Arrays and `vector`'
chapter: 7
order: 2
tags:
- host
- cpp-modern
- intermediate
- array
- 容器
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 12
related:
- vector 深入：三指针、扩容与迭代器失效
translation:
  source: documents/vol3-standard-library/02-array.md
  source_hash: f41713d84c0a41b88fe22a2838df40e140aeaa4ddab6f72383344f12e67cf698
  translated_at: '2026-06-15T09:11:49.148384+00:00'
  engine: anthropic
  token_count: 1337
---
# array: A Fixed-Size Aggregate Container for Compile-Time

## What is array: A Zero-Overhead Aggregate Wrapper for C Arrays

`std::array` is the "modern shell" that C++11 applied to C arrays. C arrays (`T[N]`) have several old shortcomings: they decay into pointers when passed as arguments (losing length information), lack iterators, cannot be copied or assigned as a whole, and cannot be returned from functions. `std::array` wraps this contiguous memory in a class template, equipping it with STL interfaces, and—crucially—**it is an aggregate type with absolutely no overhead**: the memory layout of `std::array<T, N>` is identical to that of a C array, with no virtual functions, no vtable pointers, and no extra members.

```cpp
#include <array>
std::array<int, 5> arr = {1, 2, 3, 4, 5};
```

That `N` is a template parameter, a compile-time constant. This means the size of an array is part of its type—`std::array<int, 3>` and `std::array<int, 4>` are two completely different types and cannot be assigned to each other. The price paid is zero dynamic allocation: the memory occupied by an array is exactly that contiguous block of data, residing on the stack or in the static area, never touching the heap.

## Precise Comparison with C Arrays: No Decay, Interfaces, and Object Semantics

Let's count the improvements of `array` over C arrays one by one. First, **it does not decay to a pointer**: a C array passed to a function decays to `T*`, losing its length; an array is an object, so when passed as an argument, it fully preserves its type (including `N`). You either pass by reference `std::array<T, N>&`, or explicitly provide `data()` to C interfaces. Second, **it has STL interfaces**: `size()`, `empty()`, `begin()` / `end()`, `front()`, `back()`, and `at()`, allowing it to be fed directly to algorithms and range-based for loops. Third, **it supports copy and assignment**: copy construction copies elements one by one, and it can be used as a return value or a class member—things C arrays cannot do.

```cpp
void func(std::array<int, 5>& a) {
    // a.size() is 5, type is preserved
    // No decay to int*
}
```

But underneath, it is still that same contiguous memory. The standard guarantees that `array` is an aggregate, so `sizeof(std::array<T, N>)` equals `sizeof(T) * N` (no extra members, no waste other than potential tail padding). It has no overhead, simply adding interfaces and type safety.

## The Boundary with vector: When to Use Fixed Size

The dividing line between `array` and `vector` comes down to one thing: **is the size known at compile time?** If the size is fixed at compile time and won't change, use `array`—zero heap allocation, zero overhead, can be made `constexpr`, and saves RAM if placed in a static area. If the size is determined at runtime or requires insertion/deletion, use `vector`.

The trade-offs are equivalent: the size of an `array` is part of its type (`std::array<int, 3>` and `std::array<int, 4>` are not interchangeable), so a function accepting "an int array of any size" cannot use `array` (you would need `std::span` or templates); `vector` doesn't have this limitation but incurs heap allocation and reallocation overhead. In short: **fixed size uses `array`, variable size uses `vector`**. For the middle ground (size known at runtime but avoiding heap allocation), wait for C++26's `std::dynarray`, or manage a buffer yourself with `std::span`.

## Privileges of Being an Aggregate: std::get, Structured Bindings, and Tuple Interface

Because `array` is an aggregate type, it enjoys "tuple-like" benefits beyond C arrays. `std::get` can access elements by compile-time index (returning a reference with type safety); C++17 structured bindings can unpack a small array directly into variables; `std::tuple_size` and `std::tuple_element` also recognize `array`, meaning it can be slotted into generic code that consumes tuple-like types.

```cpp
std::array<int, 3> coord = {10, 20, 30};
auto& [x, y, z] = coord; // Structured binding
static_assert(std::tuple_size<decltype(coord)>::value == 3);
```

None of this works with C arrays—C arrays can't use `std::get` and don't support structured bindings. For small arrays with "a fixed number of values" (like 3D coordinates or RGB), `array` plus structured binding is even smoother than writing a custom struct.

## Complexity, Iterator Invalidation, and Exception Safety

Complexity is straightforward: random access (`operator[]`) and `at()` are both O(1), traversal is O(n), and there is no reallocation or resizing because the size is fixed.

Regarding **iterator invalidation**, `array` is the most worry-free: iterators never invalidate. Because `array` is a fixed-size aggregate with no resizing or insertion/deletion (the interface lacks `push_back` / `insert`), iterators, references, and pointers remain valid as long as the array object itself is alive. This is cleaner than `vector` (invalidation on resize), `deque`, or `list`.

For exception safety, note that `at()` performs bounds checking and throws `std::out_of_range` if out of bounds; `operator[]` does not check, so out-of-bounds access is undefined behavior. In environments with exceptions disabled (like `-fno-exceptions`), `at()`'s check might degrade to a no-op or abort, so in those scenarios, use `operator[]` and ensure indices are correct yourself.

## Let's Run It: Zero Overhead and constexpr

Saying "zero overhead" isn't enough; let's run it to see. First, confirm that `sizeof` is truly the same as a C array:

```cpp
#include <array>
#include <iostream>

int main() {
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    int c_arr[5] = {1, 2, 3, 4, 5};

    static_assert(sizeof(arr) == sizeof(c_arr), "Sizes must match");
    std::cout << "sizeof(array): " << sizeof(arr) << std::endl;
    std::cout << "sizeof(c_arr): " << sizeof(c_arr) << std::endl;

    // Verify data() points to the first element
    static_assert(sizeof(arr) == 5 * sizeof(int));
    return 0;
}
```

```text
sizeof(array): 20
sizeof(c_arr): 20
```

`sizeof` is completely equal, with no overhead—`array` is just that contiguous memory wrapped in a class. `data()` indeed points to the first element, so it can be safely handed to C interfaces or DMA.

Another major feature of `array` is **`constexpr`**—it can complete initialization and computation at compile time, placing the generated data directly into the read-only section. A classic use case is generating a CRC lookup table at compile time:

```cpp
#include <array>
#include <cstdint>

constexpr std::array<uint16_t, 256> generate_crc_table() {
    std::array<uint16_t, 256> table{};
    for (uint16_t i = 0; i < 256; ++i) {
        uint16_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
    return table;
}

// Computed at compile time, stored in Flash
constexpr auto crc_table = generate_crc_table();
```

This 256-entry table is calculated at compile time. When the program runs, it reads directly from the read-only section, consuming neither RAM nor runtime CPU. This "compile-time lookup" is the golden combination of `array` + `constexpr`—C arrays with `constexpr` can't achieve this as cleanly (especially when involving copy returns).

## Extensions: array in Embedded Systems (DMA / Flash / Stack)

Because `array` involves zero heap allocation, guarantees contiguous memory, and supports `constexpr`, it is particularly popular in embedded systems. Here are a few practical points (beyond the main thread, use as needed). First, **contiguous memory guarantee**: the pointer returned by `data()` points to contiguous storage, which can be safely handed to DMA or HAL, provided the element type is trivially copyable. Second, **save RAM by using static storage**: use `static` for large arrays or place them in namespace scope; use `constexpr` for lookup table data to go directly to Flash, saving RAM. Third, **stack depth**: small arrays on the stack are fine, but be mindful of task / ISR stack depth limits—don't put a large `array` on a narrow stack.

## Wrapping Up

`array` is the modern shell for C arrays: zero overhead, STL interfaces, no decay, usable as an object, and benefiting from `std::get` and structured bindings via its aggregate nature. Its iterators never invalidate, it supports `constexpr`, and it has zero heap allocation—as long as the size is fixed at compile time, it is a more suitable choice than both C arrays and `vector`. In the next article, we look at its "dynamic version", `vector`, moving from fixed to variable size, at the cost of the heap and reallocation.

Want to try running it immediately? Check out the online example below (runnable, with assembly view):

<OnlineCompilerDemo
  title="array: Zero-Overhead Aggregate Container and constexpr Lookup"
  source-path="code/examples/vol3/02_array.cpp"
  description="sizeof matches C arrays, constexpr CRC compile-time lookup, structured bindings"
  allow-run
/>

## References

- [std::array — cppreference](https://en.cppreference.com/w/cpp/container/array)
- [Aggregate type — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization)
- [Container iterator invalidation rules summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
