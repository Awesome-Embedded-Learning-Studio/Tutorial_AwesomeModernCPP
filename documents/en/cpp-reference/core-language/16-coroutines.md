---
title: "Coroutines (Basics)"
description: "Stackless coroutine language support: functions can suspend execution and resume later for lazy evaluation and async flows"
chapter: 99
order: 16
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
difficulty: intermediate
cpp_standard: [20, 23]
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

# Coroutines (Basics) (C++20)

## In a Nutshell

A language mechanism that allows functions to suspend (suspend) mid-execution and resume (resume) later -- the foundational infrastructure for implementing lazy generators, asynchronous I/O, state machines, and other patterns.

## Header

`#include <coroutine>` (coroutine support library)

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Coroutine handle | `coroutine_handle<promise_type>` | Type-erased coroutine handle, used to resume/destroy |
| Suspend | `co_await expr;` | Suspends the current coroutine, waiting for `expr` to complete |
| Yield value | `co_yield expr;` | Suspends and returns a value to the caller |
| Return | `co_return expr;` | Final return from the coroutine |
| Promise type | `struct promise_type` | Type that customizes coroutine behavior (must be defined in the return type) |
| Initial suspend point | `suspend_always initial_suspend()` | Whether to immediately suspend when the coroutine starts |
| Final suspend point | `suspend_always final_suspend() noexcept` | Whether to suspend when the coroutine ends (`noexcept` is required) |
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

- Stackless coroutines: when suspended, state is stored in a heap-allocated coroutine frame, keeping memory overhead manageable
- Well-suited for implementing embedded async I/O, event loops, state machines, and similar patterns, replacing callback hell
- Coroutine frames are heap-allocated by default, but can be redirected to static memory pools via custom `operator new`
- C++20 provides only the language mechanism and minimal library support; practical high-level abstractions (e.g., `std::generator`) require C++23
- Compiler support still has known ICE (internal compiler error) issues; thorough testing is needed for production use

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 14 | 19.28 |

## See Also

- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- [cppreference: std::coroutine_handle](https://en.cppreference.com/w/cpp/coroutine/coroutine_handle)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
