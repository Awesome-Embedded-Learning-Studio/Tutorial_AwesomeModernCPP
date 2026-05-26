---
title: std::flat_map
description: A sorted associative container based on contiguous storage, a cache-friendly
  alternative to `std::map`
chapter: 99
order: 8
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 23
translation:
  source: documents/cpp-reference/containers/08-flat-map.md
  source_hash: bbb5226ff887c9e3581041bf5e974bb22024ac2dae49d89c8973eaff10604140
  translated_at: '2026-05-26T10:14:35.567727+00:00'
  engine: anthropic
  token_count: 498
---
<!--
. `std::flat_map` (C++23)
-->

## One-Liner

An ordered map that replaces the red-black tree with a contiguous array — faster lookups (cache-friendly) and more compact memory, but O(n) insertion/deletion.

## Header

`#include <flat_map>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Access element | `V& operator[](const K& key)` | Access by key; inserts a default value if not present |
| Find | `iterator find(const K& key)` | Returns an iterator to the element |
| Insert | `pair<iterator, bool> insert(const value_type&)` | Inserts a key-value pair |
| Erase | `size_t erase(const K& key)` | Erases an element by key |
| Element count | `size_t size() const` | Returns the number of elements |
| Check if empty | `bool empty() const` | Checks whether the container is empty |
| Clear | `void clear()` | Removes all elements |
| Iterate | `iterator begin()` / `end()` | Traverse in key order |
| Lower/upper bound | `iterator lower_bound(const K&)` | Find ordered boundaries |
| Contains | `bool contains(const K& key) const` | (Available since C++20) Checks if a key exists |

## Minimal Example

```cpp
// Standard: C++23
#include <flat_map>
#include <iostream>

int main() {
    std::flat_map<int, const char*> m;
    m[1] = "one";
    m[3] = "three";
    m[2] = "two";

    for (const auto& [k, v] : m) {
        std::cout << k << ": " << v << "\n";
    }
    // 1: one  2: two  3: three  (按键序排列)

    std::cout << std::boolalpha << m.contains(2) << "\n"; // true
}
```

## Embedded Applicability: Medium

- Contiguous storage is CPU cache-friendly; lookup performance on small datasets far exceeds `std::map`
- No node allocator overhead and less memory fragmentation, making it suitable for embedded environments with limited heap space
- O(n) insertion/deletion makes it unsuitable for large, frequently modified datasets
- Compiler support is still ongoing (GCC 15+, Clang 20+, MSVC 19.51+); evaluate your toolchain before using in production

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 15 | 20 | 19.51 |

## See Also

- [cppreference: std::flat_map](https://en.cppreference.com/w/cpp/container/flat_map)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
