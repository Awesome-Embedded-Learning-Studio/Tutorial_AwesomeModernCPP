---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
- 26
description: 'Chains together everything learned in the previous nine pieces of this
  volume to implement a compile-time fixed-length, contiguous-storage, zero-dynamic-allocation
  vector. Complete code plus a real run, then a comparison against C++26 std::inplace_vector
  and the EASTL/Boost/Folly counterparts'
difficulty: intermediate
order: 10
platform: host
prerequisites:
- 'CRTP: Static Polymorphism with the Curiously Recurring Template Pattern'
- 'Non-Type Template Parameters: From Integers to C++20 Floats and Class Types'
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
reading_time_minutes: 9
related:
- 'Templates, From Scratch: A Code Recipe with Placeholders'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 容器
- vector
- 零开销抽象
title: 'Capstone Project: fixed_vector<T, N>'
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/10-fixed-vector.md
  source_hash: 834b81e850b6ea29c9d334c348fb5b96767cd5f1d887cfbd9c7f45fa0aa2b1d5
  translated_at: '2026-09-26T04:32:13+00:00'
  engine: anthropic
  token_count: 5000
---
# Capstone Project: fixed_vector&lt;T, N&gt;

Having come this far, it is time for the concepts from the previous nine pieces of this volume to get a joint workout. We are going to implement a `fixed_vector<T, N>`: a vector with a compile-time fixed capacity, contiguous storage, and **zero dynamic allocation**. It puts class templates, non-type template parameters, and iterators to work together — and, if you are willing, CRTP to give the iterators extra interface. This exercise is not idle fantasy: the standard library's `std::inplace_vector` (C++26) is its "official edition", and industry had long since been running the same idea in EASTL's `fixed_vector`, Boost's `static_vector`, and Folly's `small_vector`. We will write a simplified teaching version, explain every piece of the design, and close by lining it up against the standard library.

## The Goal: What Kind of Container

Let us first pin down exactly what `fixed_vector` has to satisfy.

First, the capacity `N` is fixed at compile time, as a non-type template parameter. Second, elements are stored contiguously, accessible at random via `operator[]`, and bare pointers can serve as iterators. Third, **no heap memory is allocated**: every element lives in the object's own storage, which is especially useful in embedded systems, real-time systems, and environments where exception-throwing allocation is banned. Fourth, the number of elements can change dynamically (from 0 to N) — and this is where it differs from `std::array`, which constructs all of its elements at compile time; `fixed_vector` constructs them on demand.

These goals are exactly those of `std::inplace_vector`. cppreference defines `inplace_vector` as "a dynamically-resizable array with contiguous inplace storage": the capacity is fixed at compile time and equals N, and the elements live inside the object itself. Our `fixed_vector` is a teaching miniature of it.

## Implementation Skeleton

The template signature is `template <typename T, std::size_t N>` — one type parameter plus one non-type parameter. Storage uses `std::array<T, N>` as the backend, sparing us from managing alignment and raw memory ourselves, plus a `size_` that records the current number of elements.

```cpp
#include <array>
#include <cstddef>
#include <stdexcept>

template <typename T, std::size_t N>
class FixedVector {
    std::array<T, N> data_{};   // fixed-size storage, on the stack, no heap allocation
    std::size_t size_ = 0;
public:
    static constexpr std::size_t capacity_v = N;
    // ... member functions
};
```

`data_` is a `std::array<T, N>`, which is contiguous storage in itself, and `size_` tracks how many elements have been packed in so far. `capacity_v` is a static constant that exposes the capacity to the outside — a textbook use of the non-type parameter `N`.

## push_back and Boundary Handling

`push_back` appends one element at the tail. The critical boundary is capacity exhaustion: what to do when `N` is exceeded. The standard library's `inplace_vector` throws `std::bad_alloc` in that situation (note: `bad_alloc`, not `out_of_range` — that is what the `inplace_vector` specification says). Our teaching version takes the easy road and throws `std::out_of_range`; a clear semantic is all we need.

```cpp
constexpr void push_back(const T& value) {
    if (size_ >= N) {
        throw std::out_of_range("FixedVector full");
    }
    data_[size_++] = value;
}
```

Note that the entire function is marked `constexpr`. In C++20 this means `push_back` can execute at compile time (as long as `T`'s operations are all constant expressions). Every member of `fixed_vector` can be made `constexpr`, which is the same suitability for compile-time computation that `std::array` enjoys.

## Element Access and Iterators

`operator[]` forwards directly to the underlying `std::array` with no bounds checking (consistent with `std::vector::operator[]`; if you want the check, use `at()`).

```cpp
constexpr T& operator[](std::size_t i) { return data_[i]; }
constexpr const T& operator[](std::size_t i) const { return data_[i]; }
constexpr std::size_t size() const { return size_; }
```

The iterators are the most elegant part of this implementation. Because the elements are stored contiguously, the bare pointer `T*` is natively a type that satisfies the random access iterator requirements (it supports `*`, `++`, `+n`, comparisons). So `begin()` and `end()` simply return pointers, and there is no need to define an iterator class of our own.

```cpp
constexpr T* begin() { return data_.data(); }
constexpr T* end() { return data_.data() + size_; }
constexpr const T* begin() const { return data_.data(); }
constexpr const T* end() const { return data_.data() + size_; }
```

`data_.data()` returns a pointer to the first element of the underlying array, and `end()` points "one past the last current element". With this pair of `begin/end`, range-based for loops and standard algorithms such as `std::sort` and `std::find` can be applied to `fixed_vector` directly, because all they ask for is the iterator interface — and bare pointers satisfy it exactly. This is the STL design philosophy of "iterators unifying containers and algorithms" showing up in the flesh.

## The Complete Code and a Real Run

Assemble the pieces above, add a `main`, and run it once.

```cpp
#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>

template <typename T, std::size_t N>
class FixedVector {
    std::array<T, N> data_{};
    std::size_t size_ = 0;
public:
    static constexpr std::size_t capacity_v = N;

    constexpr void push_back(const T& value) {
        if (size_ >= N) throw std::out_of_range("FixedVector full");
        data_[size_++] = value;
    }
    constexpr T& operator[](std::size_t i) { return data_[i]; }
    constexpr const T& operator[](std::size_t i) const { return data_[i]; }
    constexpr std::size_t size() const { return size_; }

    constexpr T* begin() { return data_.data(); }
    constexpr T* end() { return data_.data() + size_; }
    constexpr const T* begin() const { return data_.data(); }
    constexpr const T* end() const { return data_.data() + size_; }
};

int main() {
    FixedVector<int, 8> v;
    for (int i = 1; i <= 5; ++i) v.push_back(i * 10);

    std::cout << "size = " << v.size() << " capacity = " << decltype(v)::capacity_v << "\n";
    std::cout << "elements: ";
    for (auto x : v) std::cout << x << " ";
    std::cout << "\n";
    std::cout << "v[2] = " << v[2] << "\n";
    std::cout << "sizeof(FixedVector<int,8>) = " << sizeof(FixedVector<int, 8>) << "\n";
    std::cout << "sizeof(int*) = " << sizeof(int*) << " (对比:动态 vector 至少含 3 个指针)\n";
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 fixed_vector.cpp -o fixed_vector && ./fixed_vector
size = 5 capacity = 8
elements: 10 20 30 40 50
v[2] = 30
sizeof(FixedVector<int,8>) = 40
sizeof(int*) = 8 (对比:动态 vector 至少含 3 个指针)
```

Let us check the key results. `size = 5 capacity = 8`: five elements packed in, capacity 8. The range-based for prints `10 20 30 40 50`, which shows the bare-pointer iterators work. `v[2] = 30`: `operator[]` random access is fine. The most convincing line is `sizeof(FixedVector<int,8>) = 40`: eight `int`s occupy 32 bytes, plus the 8 bytes of `size_`, exactly 40 — and **not a single heap pointer** inside. By contrast, a `std::vector` holds at least three pointers (data pointer, capacity, size), plus one heap allocation on top.

## Zero Dynamic Allocation: Why It Matters

A `sizeof` with no heap pointers means all of `fixed_vector`'s storage sits inside the object itself. That brings several practical benefits.

Predictable performance. No heap allocation means no allocator overhead and no memory fragmentation, and construction and destruction are deterministic. On the hot paths of real-time systems and game engines, a single `std::vector` heap allocation can be a jitter of several microseconds; `fixed_vector` has none of that.

A clear exception-safety boundary. `fixed_vector` throws only when the capacity is exhausted (`push_back` past the N limit), unlike `std::vector`, which may throw `bad_alloc` on allocation failure while growing. In environments where exceptions or the heap are disabled (many embedded projects), `fixed_vector` works and `std::vector` does not.

Cache friendliness. The elements are contiguous and inside the object itself, an access pattern the CPU cache loves — on this point it is just like `std::array` and `std::vector`.

## Comparison with std::inplace_vector (C++26)

`std::inplace_vector` is the standard-library edition of this idea. Its feature-test macro is `__cpp_lib_inplace_vector` (current value `202603L`), and it corresponds to **C++26** (early proposals aimed at C++23; it finally landed in C++26). Its design matches our `fixed_vector` closely: compile-time fixed capacity, contiguous storage, no heap allocation, elements constructed on demand.

The standard-library version is far more complete than our teaching one. It carries a full set of member functions: `emplace_back`, `try_push_back` (when full, no throw — returns an empty `std::optional<reference>`), `unchecked_push_back` (no check; the caller guarantees there is room — for performance-critical paths), `insert`, `erase`, `resize`, and so on. Its policy for a full container is more finely tiered too: `push_back` throws `std::bad_alloc` when full, `try_push_back` returns an empty optional when full, and `unchecked_push_back` assumes there is room and writes directly. This three-tier "throwing / try / unchecked" API is a mature paradigm of industrial-grade container design.

Industry had counterparts long before this. EASTL (EA's STL replacement) has `fixed_vector`, Boost.Container has `static_vector`, and Folly (Facebook) has `small_vector` (with small-buffer optimization). Each has its own emphasis, but the core is the same: "contiguous storage + compile-time (or semi-compile-time) capacity + avoiding heap allocation". Our `FixedVector` extracts the most essential skeleton and explains it; once you understand it, reading those industrial implementations becomes easy.

## Directions to Extend From Here

This teaching version is still missing a few pieces, left as directions for your own practice.

Add `try_push_back` and `unchecked_push_back`, aligning with `inplace_vector`'s three-tier API. `try_push_back` returns a `std::optional<reference>`, an empty optional when full (no exception thrown); `unchecked_push_back` assumes there is room and saves the check.

Replace the `std::array` with aligned raw memory to achieve true "construct on demand". As it stands, `std::array<T, N>` default-constructs all N elements even if you use only 3; the real `inplace_vector` uses an `alignas(T)` array of raw bytes and constructs each element with placement new only when `push_back` happens, skipping the useless constructions. This part involves std::optional, placement new, and manual destruction — a practice run closer to how the standard library is actually implemented.

Give the iterators a CRTP interface. If you want `fixed_vector`'s iterators to support some custom behavior (bounds checking in a debug mode, for example), you can write an iterator base class with CRTP. That combines piece 9's CRTP with the iterators here.

---

With this, Part 1 of this volume, "Template Basics (C++11-14)", is complete. We started from a code recipe, went through the compilation model of function templates, the lazy instantiation of class templates, the pattern matching of specialization and partial specialization, non-type parameters, two-phase name lookup and ADL, hidden friends and Barton-Nackman, alias templates, and CRTP static polymorphism, and finally welded all of it together with `fixed_vector`. Part 2 (modern template techniques, C++17) goes on to cover type traits, SFINAE, `if constexpr`, variadic templates, fold expressions, and perfect forwarding, completing the metaprogramming toolbox; Part 3 (C++20-23) then brings in concepts, requires, and reflection, turning TMP from black magic into code a human can write. On the road of templates, we have only just gotten started.
