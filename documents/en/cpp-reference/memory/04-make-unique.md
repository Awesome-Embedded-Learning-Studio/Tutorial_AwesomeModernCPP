---
chapter: 99
cpp_standard:
- 14
- 17
- 20
- 23
description: Factory function that safely constructs a `unique_ptr`, avoiding the
  exception-safety hazards of using `new` directly
difficulty: beginner
order: 4
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::make_unique
translation:
  source: documents/cpp-reference/memory/04-make-unique.md
  source_hash: c0935716846180f0a64c29cf627d7f54931183a98df2e3e10127fdb0dd8778ae
  translated_at: '2026-09-27T02:13:59+00:00'
  engine: anthropic
  token_count: 620
---
# std::make_unique (C++14)

## In a Nutshell

Safely creates a `std::unique_ptr` — safer than writing `new` directly, and more concise.

## Header

`#include <memory>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct object | `template<class T, class...Args> unique_ptr<T> make_unique(Args&&... args)` | Creates a `unique_ptr` for a non-array type (C++14) |
| Construct array | `template<class T> unique_ptr<T> make_unique(std::size_t size)` | Creates an array of unknown bound, elements are value-initialized (C++14) |
| Fixed-length arrays deleted | `template<class T, class...Args> /* unspecified */ make_unique(Args&&... args) = delete` | Arrays of known bound are explicitly deleted (C++14) |
| Default-initialize object | `template<class T> unique_ptr<T> make_unique_for_overwrite()` | Creates a non-array type, default-initialized (C++20) |
| Default-initialize array | `template<class T> unique_ptr<T> make_unique_for_overwrite(std::size_t size)` | Creates an array of unknown bound, default-initialized (C++20) |

## Minimal Example

```cpp
#include <memory>
#include <cstdio>
// Standard: C++14
struct Foo {
    Foo(int v) : val(v) { std::printf("Foo(%d)\n", val); }
    ~Foo() { std::printf("~Foo()\n"); }
    int val;
};
int main() {
    auto p1 = std::make_unique<Foo>(42);
    auto p2 = std::make_unique<Foo[]>(3);
}
```

## Embedded Applicability: High

- A zero-overhead abstraction: after compilation it is exactly equivalent to using `new` directly
- Expresses exclusive ownership semantics explicitly, preventing resource leaks
- Avoids the exception-safety hazard caused by separating the `new` expression from the `unique_ptr` construction
- Available since C++14, supported by all mainstream embedded compilers

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
