---
title: "Project: fixed_vector<T, N>"
description: "String together everything from the first 9 pieces and implement a compile-time fixed-capacity, contiguous, zero-allocation vector. Full code with tests, then compare it against C++26 std::inplace_vector and EASTL/Boost/Folly counterparts."
chapter: 12
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 容器
  - vector
  - 零开销抽象
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 26]
reading_time_minutes: 20
prerequisites:
  - "CRTP: Static Polymorphism with the Curiously Recurring Template Pattern"
  - "Non-Type Template Parameters: From Integers to C++20 Floats and Class Types"
  - "Class Templates: Members, Dependent Names, and Lazy Instantiation"
related:
  - "Templates, From Scratch: A Code Recipe with Placeholders"
---

# Project: fixed_vector&lt;T, N&gt;

We have reached the point where the concepts from the first 9 pieces should work together. Let us implement a `fixed_vector<T, N>`: a compile-time fixed-capacity, contiguous, **zero-allocation** vector. It pulls together class templates, non-type template parameters, and iterators, and if you like, CRTP for an iterator interface. This is not a thought experiment. The standard library's `std::inplace_vector` (C++26) is its "official" version, and industry had EASTL's `fixed_vector`, Boost's `static_vector`, and Folly's `small_vector` using the same idea long before. We will write a teaching-simplified version, explain each design choice, and compare it against the standard library at the end.

## Goal: What Kind of Container

First, nail down what `fixed_vector` must satisfy.

One, the capacity `N` is fixed at compile time, as a non-type template parameter. Two, elements are stored contiguously, accessible with `operator[]` for random access, with raw pointers serving as iterators. Three, **no heap allocation**, all elements live in the object's own storage, which is especially useful in embedded, real-time, or no-exception-allowed environments. Four, the element count can change dynamically (from 0 to N), which differs from `std::array` that constructs all elements at compile time. `fixed_vector` constructs on demand.

These goals match `std::inplace_vector` exactly. cppreference defines `inplace_vector` as "a dynamically-resizable array with contiguous inplace storage," with a compile-time-fixed capacity of N and elements stored inside the object itself. Our `fixed_vector` is a teaching-scale version of it.

## Skeleton

The template signature is `template <typename T, std::size_t N>`, one type parameter plus one non-type parameter. Storage uses `std::array<T, N>` as the backing, sparing us alignment and raw memory management, with a `size_` tracking the current element count.

```cpp
#include <array>
#include <cstddef>
#include <stdexcept>

template <typename T, std::size_t N>
class FixedVector {
    std::array<T, N> data_{};   // fixed storage, on the stack, no heap allocation
    std::size_t size_ = 0;
public:
    static constexpr std::size_t capacity_v = N;
    // ... member functions
};
```

`data_` is a `std::array<T, N>`, itself contiguous storage. `size_` tracks how many elements are actually in use. `capacity_v` is a static constant exposing the capacity, a typical use of the non-type parameter `N`.

## push_back and Boundary Handling

`push_back` appends an element at the end. The key boundary is capacity exhaustion: what happens beyond `N`. The standard library's `inplace_vector` throws `std::bad_alloc` in that case (note `bad_alloc`, not `out_of_range`, per the `inplace_vector` spec). Our teaching version throws `std::out_of_range` for clarity.

```cpp
constexpr void push_back(const T& value) {
    if (size_ >= N) {
        throw std::out_of_range("FixedVector full");
    }
    data_[size_++] = value;
}
```

Note the whole function is `constexpr`. In C++20 this means `push_back` can execute at compile time (as long as `T`'s operations are constant expressions). All members of `fixed_vector` can be made `constexpr`, the same property that makes `std::array` a good fit for compile-time computation.

## Element Access and Iterators

`operator[]` forwards straight to the underlying `std::array`, with no bounds check (matching `std::vector::operator[]`; use `at()` for checked access).

```cpp
constexpr T& operator[](std::size_t i) { return data_[i]; }
constexpr const T& operator[](std::size_t i) const { return data_[i]; }
constexpr std::size_t size() const { return size_; }
```

Iterators are the most elegant part of this implementation. Because elements are contiguous, a raw pointer `T*` is inherently a type that satisfies the random-access iterator requirements (supports `*`, `++`, `+n`, comparison). So `begin()` and `end()` just return pointers, with no custom iterator class.

```cpp
constexpr T* begin() { return data_.data(); }
constexpr T* end() { return data_.data() + size_; }
constexpr const T* begin() const { return data_.data(); }
constexpr const T* end() const { return data_.data() + size_; }
```

`data_.data()` returns a pointer to the first element of the backing array, and `end()` points one past the current last element. With this pair of `begin/end`, range-for loops, `std::sort`, `std::find`, and other standard algorithms all work directly on `fixed_vector`, because they only need the iterator interface, and a raw pointer satisfies it. This is the STL's "iterators unify containers and algorithms" philosophy in action.

## Full Code and Tests

Stitch the above together and add a `main` to run it.

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
    std::cout << "sizeof(int*) = " << sizeof(int*) << " (contrast: a dynamic vector holds at least 3 pointers)\n";
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 fixed_vector.cpp -o fixed_vector && ./fixed_vector
size = 5 capacity = 8
elements: 10 20 30 40 50
v[2] = 30
sizeof(FixedVector<int,8>) = 40
sizeof(int*) = 8 (contrast: a dynamic vector holds at least 3 pointers)
```

Check a few key results. `size = 5 capacity = 8`: 5 elements pushed, capacity 8. The range-for loop prints `10 20 30 40 50`, confirming the raw-pointer iterators work. `v[2] = 30`, `operator[]` random access is fine. The most telling line is `sizeof(FixedVector<int,8>) = 40`: 8 `int`s take 32 bytes, plus 8 bytes for `size_`, exactly 40, with **no heap pointer** inside. By contrast, `std::vector` holds at least three pointers (data, capacity, size) plus a heap allocation.

## Why Zero Allocation Matters

No heap pointer in `sizeof` means all of `fixed_vector`'s storage lives inside the object itself. That has practical benefits.

Predictable performance. No heap allocation means no allocator overhead or memory fragmentation, and construction and destruction are deterministic. On hot paths in real-time systems or game engines, a single `std::vector` heap allocation can be a few microseconds of jitter; `fixed_vector` has none.

Clearer exception-safety bounds. `fixed_vector` only throws on capacity exhaustion (`push_back` past N), unlike `std::vector` which can throw `bad_alloc` on reallocation. In environments that disable exceptions or the heap (many embedded projects), `fixed_vector` works where `std::vector` does not.

Cache friendliness. Elements are contiguous and inside the object, an access pattern that is very friendly to the CPU cache, like `std::array` and `std::vector`.

## Comparison with std::inplace_vector (C++26)

`std::inplace_vector` is the standard-library version of this idea. Its feature-test macro `__cpp_lib_inplace_vector` (current value `202603L`) corresponds to **C++26** (early proposals targeted C++23, but it landed in C++26). Its design closely matches our `fixed_vector`: compile-time-fixed capacity, contiguous storage, no heap allocation, elements constructed on demand.

The standard version is far more complete than our teaching one. It has a full set of member functions: `emplace_back`, `try_push_back` (when full, does not throw and returns an empty `std::optional<reference>`), `unchecked_push_back` (no check, caller guarantees room, for hot paths), `insert`, `erase`, `resize`, and more. The exhaustion policy is also more nuanced: `push_back` throws `std::bad_alloc` when full, `try_push_back` returns an empty optional when full, and `unchecked_push_back` assumes room and just appends. This "throw / try / unchecked" three-tier API is a mature pattern for industrial container design.

Industry had counterparts before this. EASTL (EA's STL replacement) has `fixed_vector`, Boost.Container has `static_vector`, and Folly (Facebook) has `small_vector` (with small-buffer optimization). Each has its own emphasis, but the core is the same: contiguous storage plus a compile-time or semi-compile-time capacity, avoiding heap allocation. Our `FixedVector` extracts the core skeleton to explain it; once you understand it, reading these industrial implementations becomes easy.

## Directions to Extend

This teaching version is missing a few pieces, good directions for practice.

Add `try_push_back` and `unchecked_push_back`, matching the `inplace_vector` three-tier API. `try_push_back` returns `std::optional<reference>`, an empty optional when full; `unchecked_push_back` assumes room, skipping the check.

Use aligned raw memory instead of `std::array` to construct on demand. Right now `std::array<T, N>` default-constructs all N elements even if you use only 3; a real `inplace_vector` uses an `alignas(T)` raw byte array and placement-new only on `push_back`, skipping useless construction. This involves `std::optional`, placement new, and manual destruction, closer to the standard library implementation.

Add a CRTP interface to the iterator. If you want `fixed_vector`'s iterator to support custom behavior (say, bounds checking in debug mode), you can write an iterator base with CRTP. This ties piece 9's CRTP to the iterator here.

---

With that, part one of this volume, "Template Basics (C++11-14)," is complete. Starting from a code recipe, through the compilation model of function templates, lazy instantiation of class templates, the pattern matching of specialization and partial specialization, non-type parameters, two-phase name lookup and ADL, hidden friends and Barton-Nackman, alias templates, and CRTP static polymorphism, finally welding them all together with `fixed_vector`. Part two (Modern Template Techniques, C++17) continues with type traits, SFINAE, `if constexpr`, variadic templates, fold expressions, and perfect forwarding, completing the metaprogramming toolbox. Part three (C++20-23) adds concepts, requires, and reflection, turning TMP from dark arts into code a person can write. On the templates road, we have only just set out.
