---
title: "std::flat_map"
description: "Cache-friendly ordered associative container based on contiguous storage, alternative to std::map"
chapter: 99
order: 8
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [23]
---

<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::flat_map (C++23)

## In a Nutshell

An ordered map backed by a contiguous array instead of a red-black tree -- faster lookups (cache-friendly), more compact memory, but O(n) insertion and deletion.

## Header

`#include <flat_map>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Access element | `V& operator[](const K& key)` | Access by key; inserts a default value if not present |
| Find | `iterator find(const K& key)` | Returns an iterator to the element |
| Insert | `pair` `<iterator, bool>` `insert(const value_type&)` | Insert a key-value pair |
| Erase | `size_t erase(const K& key)` | Remove element by key |
| Element count | `size_t size() const` | Returns the number of elements |
| Check empty | `bool empty() const` | Checks whether the container is empty |
| Clear | `void clear()` | Removes all elements |
| Iterate | `iterator begin()` / `end()` | Traverse in key order |
| Lower/upper bound | `iterator lower_bound(const K&)` | Ordered boundary lookup |
| Contains | `bool contains(const K& key) const` | (Since C++20) Check whether a key exists |

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
    // 1: one  2: two  3: three  (sorted by key)

    std::cout << std::boolalpha << m.contains(2) << "\n"; // true
}
```

## Embedded Applicability: Medium

- Contiguous storage is CPU cache-friendly; lookup performance for small datasets far exceeds `std::map`
- No per-node allocator overhead, less memory fragmentation -- suitable for embedded environments with limited heap space
- Insertion/deletion is O(n); not suitable for large datasets with frequent modifications
- Compiler support is still evolving (GCC 15+, Clang 20+, MSVC 19.51+); evaluate your toolchain before production use

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 15 | 20 | 19.51 |

## See Also

- [cppreference: std::flat_map](https://en.cppreference.com/w/cpp/container/flat_map)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
