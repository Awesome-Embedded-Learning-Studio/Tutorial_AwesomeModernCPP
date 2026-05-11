---
title: std::shared_ptr
description: A smart pointer that shares object ownership through reference counting
chapter: 2
order: 0
tags:
- host
- cpp-modern
- intermediate
difficulty: intermediate
cpp_standard:
- 11
- 14
- 17
- 20
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

# std::shared_ptr（C++11）

## In a Nutshell

Multiple smart pointers can jointly own the same object; the object is automatically released only when the last owner is destroyed or reset.

## Header

`#include <memory>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construction | `shared_ptr()` | Constructs a null pointer (default) |
| Construction (factory) | `template<class T, class... Args> shared_ptr<T> make_shared(Args&&... args)` | Allocates and constructs an object (C++11) |
| Reset | `void reset()` | Releases ownership of the currently managed object |
| Get raw pointer | `T* get() const noexcept` | Returns the stored pointer |
| Dereference | `T& operator*() const noexcept` | Dereferences the stored pointer |
| Arrow operator | `T* operator->() const noexcept` | Access members through the pointer |
| Reference count | `long use_count() const noexcept` | Returns the number of shared_ptrs sharing the object |
| Boolean conversion | `explicit operator bool() const noexcept` | Checks if it manages a non-null object |
| Swap | `void swap(shared_ptr& r) noexcept` | Swaps the managed objects of two shared_ptrs |

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

## Embedded Applicability: Medium

- Internally maintains a control block and atomic reference count, incurring extra memory and CPU overhead
- Copy operations are inherently thread-safe, making it suitable for sharing resources across tasks
- Use with caution on MCUs with extremely limited RAM and Flash; prefer unique_ptr

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBD | TBD | TBD |

## See Also

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
