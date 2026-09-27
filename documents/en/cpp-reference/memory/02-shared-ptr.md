---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: A smart pointer that shares object ownership via reference counting
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
  translated_at: '2026-09-27T02:13:20+00:00'
  engine: anthropic
  token_count: 500
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards follow a compact, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::shared_ptr (C++11)

## In a Nutshell

Multiple smart pointers can jointly own the same object; the object is released automatically only when the last owner is destroyed or reset.

## Header

`#include <memory>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Construct | `shared_ptr()` | Constructs an empty pointer (default) |
| Construct (factory) | `template<class T, class... Args> shared_ptr<T> make_shared(Args&&... args)` | Allocates and constructs the object (C++11) |
| Reset | `void reset()` | Releases ownership of the currently managed object |
| Get raw pointer | `T* get() const noexcept` | Returns the stored pointer |
| Dereference | `T& operator*() const noexcept` | Dereferences the stored pointer |
| Arrow access | `T* operator->() const noexcept` | Accesses members through the pointer |
| Reference count | `long use_count() const noexcept` | Returns the number of shared_ptr instances sharing the object |
| Boolean conversion | `explicit operator bool() const noexcept` | Checks whether it manages a non-null object |
| Swap | `void swap(shared_ptr& r) noexcept` | Exchanges the objects managed by two shared_ptr instances |

## Minimal Example

```cpp
#include <iostream>
#include <memory>
struct Foo { Foo() { std::cout << "Foo()\n"; } ~Foo() { std::cout << "~Foo()\n"; } };
int main() {
    std::shared_ptr<Foo> p1 = std::make_shared<Foo>();
    std::shared_ptr<Foo> p2 = p1; // reference count becomes 2
    std::cout << "count: " << p1.use_count() << "\n";
    p1.reset(); // count: 1
    p2.reset(); // destroys Foo
}
```

## Embedded Suitability: Medium

- Maintains an internal control block and an atomic reference count, which adds memory and CPU overhead
- Copying itself is thread-safe, making it suitable for sharing resources across tasks
- Use with caution on MCUs with extremely limited RAM and Flash; prefer `unique_ptr` instead

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBD | TBD | TBD |

## See Also

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
