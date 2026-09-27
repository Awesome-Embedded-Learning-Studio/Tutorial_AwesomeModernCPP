---
chapter: 99
cpp_standard:
- 20
- 23
description: 'Language support for stackless coroutines: functions can suspend
  execution and resume later, enabling lazy evaluation and asynchronous flows'
difficulty: intermediate
order: 16
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- coroutine
title: Coroutines
translation:
  source: documents/cpp-reference/core-language/16-coroutines.md
  source_hash: bdc3c51d93a42214d946edc3c59157fd270161480ffcc3b24a0913a975e6e663
  translated_at: '2026-09-27T01:49:39+00:00'
  engine: anthropic
  token_count: 700
---
<!--
Reference Card Template
Used for feature cheat-sheet pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Coroutines (C++20)

## In a Nutshell

A language mechanism that lets a function suspend mid-execution and resume later—the infrastructure for implementing patterns such as lazy generators, asynchronous I/O, and state machines.

## Header

`#include <coroutine>` (coroutine support library)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Coroutine handle | `coroutine_handle<promise_type>` | Type-erased coroutine handle, used to resume/destroy |
| Suspend | `co_await expr;` | Suspends the current coroutine, waiting for `expr` to complete |
| Yield a value | `co_yield expr;` | Suspends and returns a value to the caller |
| Return | `co_return expr;` | Final return of the coroutine |
| Promise type | `struct promise_type` | The type that customizes coroutine behavior (must be defined inside the return type) |
| Initial suspend point | `suspend_always initial_suspend()` | Whether the coroutine suspends immediately upon start |
| Final suspend point | `suspend_always final_suspend() noexcept` | Whether the coroutine suspends upon completion (`noexcept` is mandatory) |
| Return object | `get_return_object()` | Creates the object returned to the caller |

## Minimal Example

```cpp
// Standard: C++20
#include <coroutine>
#include <iostream>

struct Generator {
    struct promise_type {
        int current_value;
        auto get_return_object() { return Generator{handle::from_promise(*this)}; }
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        auto yield_value(int v) { current_value = v; return std::suspend_always{}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    using handle = std::coroutine_handle<promise_type>;
    handle coro;
    ~Generator() { if (coro) coro.destroy(); }
    bool next() { coro.resume(); return !coro.done(); }
    int value() { return coro.promise().current_value; }
};

Generator counter() {
    for (int i = 0; i < 3; ++i)
        co_yield i;
}

int main() {
    auto gen = counter();
    while (gen.next())
        std::cout << gen.value() << " "; // 0 1 2
}
```

## Embedded Applicability: Medium

- Stackless coroutines: when suspended, the state lives in a heap-allocated coroutine frame, so memory overhead is controllable
- A good fit for embedded asynchronous I/O, event loops, state machines, and similar patterns, replacing callback hell
- Coroutine frames are heap-allocated by default; a custom `operator new` can redirect them to a static memory pool
- C++20 provides only the language mechanism and minimal library support; practical high-level abstractions (such as `std::generator`) require C++23
- Compiler support still has known ICEs (internal compiler errors); test thoroughly before production use

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 14 | 19.28 |

## See Also

- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- [cppreference: std::coroutine_handle](https://en.cppreference.com/w/cpp/coroutine/coroutine_handle)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
