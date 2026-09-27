---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'std::array laid bare: a zero-overhead aggregate wrapping a C array,
  no pointer decay, std::get and structured bindings, iterators that never invalidate,
  constexpr compile-time lookup tables, and the precise borders against C arrays
  and vector'
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 7
related:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
tags:
- host
- cpp-modern
- intermediate
- array
- 容器
title: 'array: An Aggregate Container with a Compile-Time Fixed Size'
translation:
  source: documents/vol3-standard-library/containers/02-array.md
  source_hash: 7c61645f47239ac6cb379c18978d92de85382501523cd72c6e30c51e6cec442d
  translated_at: '2026-09-26T02:16:28+00:00'
  engine: anthropic
  token_count: 4500
---
# array: An Aggregate Container with a Compile-Time Fixed Size

## What array Really Is: A Zero-Overhead Aggregate Wrapping a C Array

`std::array` is the "modern shell" C++11 fitted onto C arrays. C arrays `T[N]` come with a batch of old ailments: they decay to a pointer when passed to a function (dropping the length), there is no `.size()`, no whole-object copy assignment, and no way to return one from a function. `std::array<T, N>` wraps that contiguous block of memory in a class template and adds the STL interfaces — and this is the key — **it is an aggregate type, with no extra overhead whatsoever**: `sizeof` is identical to the C array, with no virtual functions, no vtable pointer, and no extra members.

```cpp
std::array<int, 5> a = {1, 2, 3, 4, 5};   // size 5 is nailed down at compile time
a.size();        // 5
a[0];            // 1, O(1)
a.data();        // int*, pointing at the underlying contiguous memory
```

That `N` is a template parameter, a compile-time constant. It means the array's size is part of its type — `std::array<int, 5>` and `std::array<int, 6>` are two different types and cannot be assigned to each other. What that cost buys is zero dynamic allocation: the memory an array occupies is exactly that contiguous block of data, living on the stack or in static storage, never touching the heap.

## A Precise Comparison with C Arrays: No Decay, Real Interfaces, a Real Object

Let's count out array's improvements over C arrays one by one. First, **no decay to a pointer**: pass a C array to a function and it decays into `T*`, dropping the length; array is an object, and passing it preserves the full type (N included) — you either pass a `const std::array<T, N>&`, or call `.data()` explicitly to hand a pointer to a C interface. Second, **it has the STL interfaces**: `.size()`, `.empty()`, `.begin()` / `.end()`, `.data()`, `operator[]`, `.at()` — it feeds straight into `<algorithm>` and range-based for. Third, **whole-object copy and assignment**: `auto b = a;` is an element-by-element copy, and an array can also serve as a function return value or a class member — none of which a C array can do.

```cpp
std::array<int, 4> make() { return {1, 2, 3, 4}; }   // a C array cannot do this
auto a = make();
auto b = a;        // whole-object copy, a C array cannot do this
b.fill(0);         // zeroed in one shot
```

But underneath, it is still that same contiguous block of memory. The standard guarantees that array is an aggregate, so `sizeof(std::array<T, N>)` is exactly `sizeof(T) * N` (no extra members, no waste beyond tail padding). There is no added overhead — only added interfaces and type safety.

## The Border with vector: When to Go Fixed-Size

The dividing line between array and vector is a single question: **is the size known at compile time**. If the size can be nailed down at compile time and will never change, use array — zero heap allocation, zero overhead, `constexpr`-capable, and placeable in static storage to save RAM. If the size is only known at runtime, or you need insertions and erasures, use vector.

The price runs both ways: array's size is part of its type (`array<int, 5>` and `array<int, 6>` do not interchange), so a function that wants to accept "an int array of any size" cannot use array (it must reach for `span` or templates); vector has no such restriction, but pays for heap allocation and reallocation. One line: **fixed size, array; variable size, vector**. For the middle ground (size known at runtime but you would rather not hit the heap), you can wait for C++26's `inplace_vector`, or manage a buffer yourself and pair it with a `span`.

## Perks of Being an Aggregate: std::get, Structured Bindings, and the tuple Interface

array is an aggregate, and that earns it a "tuple-like" dividend beyond what C arrays get. `std::get<I>(arr)` fetches an element by compile-time index (returning a reference, with type safety); C++17 structured bindings unpack a small array straight into variables; and `std::tuple_size` and `std::tuple_element` both recognize array, so it slots into generic code that consumes tuple-like types.

```cpp
std::array<int, 3> a = {10, 20, 30};
std::get<1>(a);            // 20, compile-time index, type-safe
auto [x, y, z] = a;        // structured bindings: x=10, y=20, z=30
static_assert(std::tuple_size_v<decltype(a)> == 3);
```

None of this exists on C arrays — they get no `std::get`, and they do not support structured bindings. For those "a fixed handful of values" arrays (a 3D coordinate, an RGB triple), array plus structured bindings is even handier than writing a struct.

## Complexity, Iterator Invalidation, and Exception Safety

Complexity is plain to see: random access via `operator[]` and `.at()` is O(1), traversal is O(n), and there is no growth and no reallocation — because the size is nailed down.

On **iterator invalidation**, array is the most carefree member of the family: its iterators never invalidate. Because array is a fixed-size aggregate, there is no growth and no insertion or erasure (the interface simply has no `push_back` / `insert`), so once you hold an iterator, a reference, or a pointer, it stays valid for as long as the array object itself is alive. Cleaner than vector (reallocation invalidates everything), deque, or list.

One point to watch on exception safety: `.at(i)` performs bounds checking and throws `std::out_of_range` when out of bounds, while `operator[]` checks nothing and an out-of-bounds access is undefined behavior. In a build with exceptions disabled (`-fno-exceptions`, for instance), an out-of-bounds `.at()` degrades into `std::terminate`, so in that setting your only option is `operator[]` plus guaranteeing the index yourself.

## Let's Run It: Zero Overhead and constexpr

Just saying "zero overhead" is not concrete enough — let's run it and see. First, confirm that sizeof really matches the C array:

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

sizeof comes out exactly equal — no extra overhead: array is that contiguous memory with a class slipped over it. And `data()` really does point at the first element, so you can hand it to a C interface or to DMA without worry.

The other big trick up array's sleeve is **constexpr** — it can finish initialization and computation at compile time, with the generated data going straight into the read-only section. A classic use is generating a CRC lookup table at compile time:

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

// computed at compile time, lands in the read-only section; zero runtime cost
constexpr auto crc_table = make_crc_table();
static_assert(crc_table.size() == 256);
static_assert(crc_table[0] == 0x00000000u);   // input 0 gives result 0
```

This 256-entry table is fully computed by the time compilation ends; at runtime the program reads it straight from the read-only section, spending neither RAM nor CPU cycles. This "compile-time lookup table" is the golden array + constexpr combination — a C array with constexpr cannot get this clean (especially once returning by copy is involved).

## Going Further: array in Embedded Systems (DMA / Flash / Stack)

With zero heap allocation, contiguous memory, and constexpr support, array is especially popular in embedded work; here are a few field-tested pointers (off the main storyline, take them as needed). First, **the contiguous-memory guarantee**: the pointer `.data()` returns points at one contiguous stretch of storage, safe to hand to DMA or a HAL, provided the element type is trivially copyable. Second, **static storage to save RAM**: put large arrays under `static` or into `.bss`, and put lookup-table data into flash via `constexpr` — zero RAM spent. Third, **stack depth**: small arrays on the stack are fine, but keep an eye on the stack-depth limits of tasks and ISRs — do not park a big array in a narrow stack.

## A Few Closing Words

array is the modernized shell around the C array: zero overhead, STL interfaces, no decay, a proper object — and, thanks to its aggregate identity, `std::get` and structured bindings as well. Its iterators never invalidate, it goes constexpr, it never touches the heap: as long as the size can be nailed down at compile time, it is a better fit than either the C array or vector. In the next article we meet its "dynamic edition", vector — stepping from fixed to variable, at the price of the heap and reallocation.

Want to run it and see for yourself? Open the online example below (it runs, and you can inspect the assembly):

<OnlineCompilerDemo
  title="array: A Zero-Overhead Aggregate Container and constexpr Lookup Tables"
  source-path="code/examples/vol3/02_array.cpp"
  description="sizeof matches a C array, a constexpr CRC lookup table computed at compile time, structured bindings"
  allow-run
/>

## References

- [std::array — cppreference](https://en.cppreference.com/w/cpp/container/array)
- [Aggregate types — cppreference](https://en.cppreference.com/w/cpp/language/aggregate_initialization)
- [Container iterator invalidation rules summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
