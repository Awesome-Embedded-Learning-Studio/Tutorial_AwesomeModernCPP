---
chapter: 99
cpp_standard:
- 23
description: A sorted associative container based on contiguous storage, a cache-friendly
  alternative to `std::map`.
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
  source_hash: 3c25b6b314501ba0464571012499d72ea13369ca9e8301928607d46a4096fcde
  translated_at: '2026-06-16T03:28:31.958631+00:00'
  engine: anthropic
  token_count: 501
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag Usage Rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::flat_map (C++23)

## In a nutshell

An ordered associative container that uses a contiguous array instead of a red-black tree—faster lookups (cache-friendly) and more compact memory, but with O(n) insertion/deletion.

## Header

`#include <flat_map>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Access Element | `V& operator[](const K& key)` | Access by key; inserts default value if not found |
| Find | `iterator find(const K& key)` | Returns an iterator to the element |
| Insert | `pair<iterator, bool> insert(const value_type&)` | Inserts a key-value pair |
| Erase | `size_t erase(const K& key)` | Removes an element by key |
| Element Count | `size_t size() const` | Returns the number of elements |
| Is Empty | `bool empty() const` | Checks if the container is empty |
| Clear | `void clear()` | Removes all elements |
| Iterate | `iterator begin()` / `end()` | Traverse in key order |
| Lower/Upper Bound | `iterator lower_bound(const K&)` | Ordered search for boundaries |
| Contains | `bool contains(const K& key) const` | (Since C++20) Checks if a key exists |

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

- Contiguous storage is CPU cache-friendly; lookup performance on small datasets is far superior to `std::map`
- No node allocator overhead and less memory fragmentation, suitable for embedded environments with limited heap space
- Insertion/deletion is O(n), making it unsuitable for large, frequently modified datasets
- Compiler support is still evolving (GCC 15+, Clang 20+, MSVC 19.51+); evaluate toolchains for production use

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 15 | 20 | 19.51 |

## See Also

- [cppreference: std::flat_map](https://en.cppreference.com/w/cpp/container/flat_map)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
