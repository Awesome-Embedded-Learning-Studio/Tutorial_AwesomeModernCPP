---
chapter: 99
cpp_standard:
- 20
- 23
description: 'Language support for stackless coroutines: functions can suspend execution
  and resume later, enabling lazy evaluation and asynchronous flows.'
difficulty: intermediate
order: 16
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- coroutine
title: Coroutines (Coroutine Basics)
translation:
  engine: anthropic
  source: documents/cpp-reference/core-language/16-coroutines.md
  source_hash: ff2e9ba415581403b2a336afafa9bf9cc60897e340d72ea513043872017b5cee
  token_count: 677
  translated_at: '2026-05-26T10:16:56.000856+00:00'
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

# Coroutines Basics (C++20)

## One-Liner

A language mechanism that allows functions to suspend mid-execution and resume later — the infrastructure for implementing lazy generators, async I/O, and state machines.

## Header

`#include <coroutine>` (coroutine support library)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Coroutine handle | `coroutine_handle<promise_type>` | Type-erased coroutine handle, used to resume/destroy |
| Suspend | `co_await expr;` | Suspends the current coroutine, waits for `expr` to complete |
| Yield value | `co_yield expr;` | Suspends and returns a value to the caller |
| Return | `co_return expr;` | Final return of the coroutine |
| Promise type | `struct promise_type` | Type that customizes coroutine behavior (must be defined in the return type) |
| Initial suspend point | `suspend_always initial_suspend()` | Whether to suspend immediately when the coroutine starts |
| Final suspend point | `suspend_always final_suspend() noexcept` | Whether to suspend when the coroutine ends (`noexcept` required) |
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

- Stackless coroutines: state is stored in a heap-allocated coroutine frame upon suspension, keeping memory overhead controllable
- Well-suited for implementing embedded async I/O, event loops, and state machines, replacing callback hell
- Coroutine frames are heap-allocated by default, but can be changed to a static memory pool via a custom `operator new`
- C++20 only provides the language mechanism and minimal library support; practical high-level abstractions (such as `std::generator`) require C++23
- Compiler support still has known ICEs (Internal Compiler Errors); production use requires thorough testing

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 14 | 19.28 |

## See Also

- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- [cppreference: std::coroutine_handle](https://en.cppreference.com/w/cpp/coroutine/coroutine_handle)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
