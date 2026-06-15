---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
description: A smart pointer with exclusive ownership, releasing resources automatically
  with zero overhead.
difficulty: beginner
order: 1
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::unique_ptr
translation:
  engine: anthropic
  source: documents/cpp-reference/memory/01-unique-ptr.md
  source_hash: 7fe4ab2885f8549ff78763d8cf4284d65bfef9fe9592628ccaebe2d210402900
  token_count: 506
  translated_at: '2026-05-26T10:17:15.894143+00:00'
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::unique_ptr (C++11)

## In a Nutshell

A smart pointer that manages the lifetime of dynamic objects through exclusive ownership semantics, automatically destroying the object when it leaves scope, and having the exact same size as a raw pointer.

## Header

`#include <memory>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Create object | `template<class T> unique_ptr<T> make_unique(Args&&... args)` | (C++14) Exception-safe creation of unique_ptr |
| Constructor | `constexpr unique_ptr(pointer p = pointer())` | Takes ownership of a raw pointer |
| Destructor | `~unique_ptr()` | Destroys the managed object |
| Release ownership | `pointer release() noexcept` | Relinquishes ownership and returns the raw pointer |
| Reset pointer | `void reset(pointer p = pointer())` | Destroys the current object and takes ownership of a new pointer |
| Get raw pointer | `pointer get() const noexcept` | Returns the managed raw pointer |
| Check if empty | `explicit operator bool() const noexcept` | Determines whether an object is held |
| Dereference | `T& operator*() const` | Accesses the managed object |
| Member access | `T* operator->() const` | Accesses members via pointer |
| Array subscript | `T& operator[](size_t i) const` | (Array specialization) Accesses array elements |

## Minimal Example

```cpp
// Standard: C++14
#include <iostream>
#include <memory>
struct Foo { ~Foo() { std::cout << "destroyed\n"; } };
int main() {
    std::unique_ptr<Foo> p = std::make_unique<Foo>();
    std::unique_ptr<Foo> q = std::move(p); // 转移所有权
    std::cout << std::boolalpha << (p == nullptr) << "\n"; // true
} // "destroyed"
```

## Embedded Applicability: High

- Zero-overhead abstraction: compiles to the same size as a raw pointer, with no additional memory footprint
- Deterministic destruction: releases immediately when the scope ends, meeting embedded requirements for real-time performance and deterministic memory
- Perfectly supports the pImpl idiom, hiding implementation details and shortening compilation dependency chains
- Introduces no control block, avoiding the thread safety and memory fragmentation overhead of `shared_ptr`

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9   | 2010 |

## See Also

- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
