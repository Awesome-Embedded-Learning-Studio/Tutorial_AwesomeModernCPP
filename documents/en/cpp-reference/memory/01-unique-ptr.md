---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
description: A smart pointer with exclusive ownership that automatically releases
  resources with zero overhead.
difficulty: beginner
order: 1
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::unique_ptr
translation:
  source: documents/cpp-reference/memory/01-unique-ptr.md
  source_hash: 7e058fe01e0c40e6959f9b71d6dea58e9d3092bf096acc043880458c9518eac5
  translated_at: '2026-09-27T02:13:25+00:00'
  engine: anthropic
  token_count: 1000
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use 'host')
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::unique_ptr (C++11)

## In a Nutshell

A smart pointer that manages the lifetime of dynamic objects through exclusive ownership semantics: it destroys the object automatically when it goes out of scope, and it is exactly the same size as a raw pointer.

## Header

`#include <memory>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Create object | `template<class T> unique_ptr<T> make_unique(Args&&... args)` | (C++14) Creates a unique_ptr in an exception-safe manner |
| Constructor | `constexpr unique_ptr(pointer p = pointer())` | Takes over a raw pointer |
| Destructor | `~unique_ptr()` | Destroys the managed object |
| Release ownership | `pointer release() noexcept` | Relinquishes ownership and returns the raw pointer |
| Reset pointer | `void reset(pointer p = pointer())` | Destroys the current object and takes over a new pointer |
| Get raw pointer | `pointer get() const noexcept` | Returns the managed raw pointer |
| Check if empty | `explicit operator bool() const noexcept` | Tests whether it holds an object |
| Dereference | `T& operator*() const` | Accesses the managed object |
| Member access | `T* operator->() const` | Accesses members through the pointer |
| Array subscript | `T& operator[](size_t i) const` | (Array specialization) Accesses array elements |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>
#include <memory>
struct Foo { ~Foo() { std::cout << "destroyed\n"; } };
int main() {
    std::unique_ptr<Foo> p = std::make_unique<Foo>();
    std::unique_ptr<Foo> q = std::move(p); // Transfers ownership
    std::cout << std::boolalpha << (p == nullptr) << "\n"; // true
} // "destroyed"
```

## Embedded Applicability: High

- Zero-overhead abstraction: compiles to the same size as a raw pointer, with no extra memory footprint
- Deterministic destruction: releases immediately at end of scope, matching embedded demands for real-time behavior and deterministic memory usage
- Fully supports the pImpl idiom, hiding implementation details and shortening compilation dependency chains
- Introduces no control block, avoiding the thread-safety and memory-fragmentation overhead of `shared_ptr`

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9   | 2010 |

## See Also

- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
