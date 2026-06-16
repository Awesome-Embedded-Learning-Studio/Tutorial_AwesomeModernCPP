---
chapter: 99
cpp_standard:
- 14
- 17
- 20
- 23
description: Factory function to safely construct `unique_ptr`, avoiding exception
  safety issues caused by direct use of `new`.
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
  translated_at: '2026-06-16T03:29:45.760204+00:00'
  engine: anthropic
  token_count: 425
---
# std::make_unique (C++14)

## In a Nutshell

Safely creates `unique_ptr` objects. It is safer and more concise than writing `new` directly.

## Header

`<memory>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct object | `template<class T, class... Args> unique_ptr<T> make_unique( Args&&... args );` | Creates a `unique_ptr` for a non-array type (C++14) |
| Construct array | `template<class T> unique_ptr<T> make_unique( size_t size );` | Creates an array of unknown bound, elements are value-initialized (C++14) |
| Fixed-length arrays deleted | `template<class T> unique_ptr<T> make_unique( size_t size ) = delete;` | Arrays of known bound are explicitly deleted (C++14) |
| Default initialize object | `template<class T> unique_ptr<T> make_unique( );` | Creates a non-array type, default initialized (C++20) |
| Default initialize array | `template<class T> unique_ptr<T> make_unique( size_t size );` | Creates an array of unknown bound, default initialized (C++20) |

## Minimal Example

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Widget {
    std::string name;
    Widget(std::string n) : name(n) {
        std::cout << "Widget " << name << " created.\n";
    }
    ~Widget() {
        std::cout << "Widget " << name << " destroyed.\n";
    }
};

int main() {
    // 1. 创建单个对象
    // 1. Create a single object
    auto w1 = std::make_unique<Widget>("Sensor-1");

    // 2. 创建数组 (C++14)
    // 2. Create an array (C++14)
    const size_t N = 3;
    auto arr = std::make_unique<Widget[]>(N);
    // Note: Elements are value-initialized (default ctor called)

    // 3. 使用 reset 替换对象
    // 3. Replace the managed object using reset
    w1.reset(new Widget("Sensor-2")); // Old object destroyed, new one created

    // 4. 移动语义
    // 4. Move semantics
    auto w2 = std::move(w1); // w1 becomes nullptr
    if (!w1) {
        std::cout << "w1 is empty.\n";
    }
}
```

## Embedded Applicability: High

- Zero-overhead abstraction; compiled code is completely equivalent to using `new` directly.
- Explicitly expresses exclusive ownership semantics, avoiding resource leaks.
- Avoids the exception safety risk caused by the separation of the `new` expression and the `unique_ptr` constructor.
- Available since C++14; supported by mainstream embedded compilers.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
