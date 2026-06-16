---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: smart pointer that shares object ownership via reference counting
difficulty: intermediate
order: 0
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: std::shared_ptr
translation:
  source: documents/cpp-reference/memory/02-shared-ptr.md
  source_hash: 3252b3a305fa4aa9ed0a548616f96cae11805003b299ed0d20c374ebbcb7fb42
  translated_at: '2026-06-16T03:29:35.690924+00:00'
  engine: anthropic
  token_count: 496
---
<!--
Reference Card Template
Used for feature quick reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards follow a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for all reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::shared_ptr (C++11)

## In a Nutshell

Multiple smart pointers can share ownership of the same object. The object is automatically released only when the last owner is destroyed or reset.

## Header

`#include <memory>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `shared_ptr()` | Constructs an empty pointer (default) |
| Constructor (Factory) | `template<class T, class... Args> shared_ptr<T> make_shared(Args&&... args)` | Allocates and constructs an object (C++11) |
| Reset | `void reset()` | Releases ownership of the currently managed object |
| Get Raw Pointer | `T* get() const noexcept` | Returns the stored pointer |
| Dereference | `T& operator*() const noexcept` | Dereferences the stored pointer |
| Arrow Operator | `T* operator->() const noexcept` | Access members via pointer |
| Reference Count | `long use_count() const noexcept` | Returns the number of shared_ptr owners sharing the object |
| Boolean Conversion | `explicit operator bool() const noexcept` | Checks if a non-null object is managed |
| Swap | `void swap(shared_ptr& r) noexcept` | Swaps objects managed by two shared_ptr instances |

## Minimal Example

```cpp
#include <iostream>
#include <memory>
struct Foo { Foo() { std::cout << "Foo()\n"; } ~Foo() { std::cout << "~Foo()\n"; } };
int main() {
    std::shared_ptr<Foo> p1 = std::make_shared<Foo>();
    std::shared_ptr<Foo> p2 = p1; // 引用计数变为 2
    std::cout << "count: " << p1.use_count() << "\n";
    p1.reset(); // count: 1
    p2.reset(); // 析构 Foo
}
```

## Embedded Suitability: Medium

- Maintains an internal control block and atomic reference counts, incurring extra memory and CPU overhead.
- Copy operations are thread-safe, making it suitable for sharing resources between multiple tasks.
- Use with caution on MCUs with extremely limited RAM and Flash; prefer `unique_ptr` where possible.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBD | TBD | TBD |

## See Also

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
