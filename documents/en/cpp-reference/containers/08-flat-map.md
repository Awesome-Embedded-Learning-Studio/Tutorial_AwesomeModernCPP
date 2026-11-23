---
chapter: 99
cpp_standard:
- 23
description: A sorted associative container built on contiguous storage — a cache-friendly
  alternative to `std::map`
difficulty: beginner
order: 8
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::flat_map
translation:
  source: documents/cpp-reference/containers/08-flat-map.md
  source_hash: 0ac175f510184fd43b425a45874f9d9663f9390392c7000606ab856af1cc0559
  translated_at: '2026-11-22T16:41:57.150143+00:00'
  engine: glm-5.3-refresh
  token_count: 2008
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a lean, structured format with no narrative style required.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::flat_map (C++23)

## In a nutshell

A sorted map that swaps the red-black tree for a contiguous array — faster lookups (cache-friendly), more compact memory, but O(n) insertion and erasure.

## Header

`#include <flat_map>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Access element | `V& operator[](const K& key)` | Access by key; inserts a default value if the key is absent |
| Find | `iterator find(const K& key)` | Returns an iterator to the element |
| Insert | `pair<iterator, bool> insert(const value_type&)` | Inserts a key-value pair |
| Erase | `size_t erase(const K& key)` | Removes the element with the given key |
| Element count | `size_t size() const` | Returns the number of elements |
| Is empty | `bool empty() const` | Checks whether the container is empty |
| Clear | `void clear()` | Removes all elements |
| Iteration | `iterator begin()` / `end()` | Traverses in key order |
| Lower/upper bound | `iterator lower_bound(const K&)` | Finds boundaries in sorted order |
| Contains | `bool contains(const K& key) const` | (Available since C++23) Checks whether a key exists |

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

- Contiguous storage is CPU-cache-friendly; lookups on small datasets far outperform `std::map`
- No per-node allocator overhead and less memory fragmentation — a good fit for embedded environments with tight heap space
- Insertion/erasure is O(n), so it is unsuitable for large datasets that change frequently
- Compiler support is still landing (GCC 15+, Clang 20+, MSVC STL not yet); evaluate your toolchain before production use

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 15 | 20 | 19.51 |

## See Also

- [cppreference: std::flat_map](https://en.cppreference.com/w/cpp/container/flat_map)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
