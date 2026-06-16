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
  source: documents/cpp-reference/core-language/16-coroutines.md
  source_hash: bdc3c51d93a42214d946edc3c59157fd270161480ffcc3b24a0913a975e6e663
  translated_at: '2026-06-16T03:29:32.714707+00:00'
  engine: anthropic
  token_count: 680
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for all reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Coroutines Basics (C++20)

## One-Liner

A language mechanism that allows a function to suspend (suspend) at an intermediate point and later resume (resume)—the infrastructure for implementing lazy generators, asynchronous I/O, state machines, and other patterns.

## Header

`<coroutine>` (Coroutine support library)

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Coroutine Handle | `std::coroutine_handle<>` | Type-erased coroutine handle, used to resume/destroy |
| Suspend | `co_await` | Suspends the current coroutine, waiting for the `awaiter` to complete |
| Yield Value | `co_yield` | Suspends and returns a value to the caller |
| Return | `co_return` | Final return of the coroutine |
| Promise Type | `promise_type` | Type that customizes coroutine behavior (must be defined in the return type) |
| Initial Suspend Point | `initial_suspend` | Whether the coroutine suspends immediately upon startup |
| Final Suspend Point | `final_suspend` | Whether the coroutine suspends upon exit (required for `coroutine_handle` to remain valid) |
| Return Object | `get_return_object` | Creates the object returned to the caller |

## Minimal Example

```cpp
#include <coroutine>
#include <iostream>

struct Generator {
    struct Promise {
        int value_;
        Generator get_return_object() { return Generator{std::coroutine_handle<Promise>::from_promise(*this)}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { std::terminate(); }
        std::suspend_always yield_value(int val) { value_ = val; return {}; }
        void return_void() {}
    };

    using promise_type = Promise;
    std::coroutine_handle<Promise> h_;

    Generator(std::coroutine_handle<Promise> h) : h_(h) {}
    ~Generator() { if (h_) h_.destroy(); }

    bool next() {
        h_.resume();
        return !h_.done();
    }

    int value() const { return h_.promise().value_; }
};

Generator mySequence() {
    std::cout << "Start\n";
    co_yield 1;
    std::cout << "Middle\n";
    co_yield 2;
    std::cout << "End\n";
}

int main() {
    auto gen = mySequence();
    while (gen.next()) {
        std::cout << "Got: " << gen.value() << "\n";
    }
    return 0;
}
```

## Embedded Applicability: Moderate

- Stackless coroutines: State is stored in a heap-allocated coroutine frame upon suspension, making memory overhead controllable.
- Suitable for implementing embedded asynchronous I/O, event loops, state machines, and other patterns, replacing callback hell.
- Coroutine frames are heap-allocated by default; this can be changed to static memory pools via custom `operator new`.
- C++20 only provides the language mechanism and minimal library support. Practical high-level abstractions (like `std::generator`) require C++23.
- Compiler support still has known ICEs (Internal Compiler Errors); thorough testing is required for production use.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12  | 14    | 19.28 |

## See Also

- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- [cppreference: std::coroutine_handle](https://en.cppreference.com/w/cpp/coroutine/coroutine_handle)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
