---
chapter: 99
cpp_standard:
- 20
- 23
description: A thread class that joins automatically — on destruction it sends a stop
  request and waits for the thread to exit.
difficulty: beginner
order: 4
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::jthread
translation:
  source: documents/cpp-reference/concurrency/04-jthread.md
  source_hash: 5ae176e38032d0427ba6a0a1278d7aaff51389a3ad5cf3afc08b7d37909a35d7
  translated_at: '2026-09-26T17:08:22+00:00'
  engine: anthropic
  token_count: 850
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a lean, structured format and do not need a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::jthread (C++20)

## One-Liner

A thread class with built-in RAII semantics — on destruction it automatically sends a stop request and joins, wiping out the crashes caused by forgetting to join.

## Header

`#include <thread>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Construct (with function) | `template<class F> jthread(F&& f, Args&&... args)` | Starts a new thread executing f(args...) |
| Construct (with stop_token) | `template<class F> jthread(F&& f)` | f's first parameter receives `std::stop_token` |
| Destructor | `~jthread()` | Requests stop + joins (if joinable) |
| Request stop | `bool request_stop() noexcept` | Requests cooperative stop, returns whether it succeeded |
| Get stop token | `std::stop_token get_stop_token() const noexcept` | Gets the current thread's stop token |
| Wait for completion | `void join()` | Blocks until the thread finishes |
| Detach thread | `void detach()` | Detaches; the thread then runs independently |
| Is joinable | `bool joinable() const noexcept` | Checks whether the thread is joinable |
| Get ID | `std::thread::id get_id() const noexcept` | Returns the thread identifier |

## Minimal Example

```cpp
// Standard: C++20
#include <iostream>
#include <thread>

void worker(std::stop_token st) {
    while (!st.stop_requested()) {
        std::cout << "working...\n";
    }
    std::cout << "stopped\n";
}

int main() {
    std::jthread t(worker); // stop_token is passed in automatically
    // t's destructor automatically calls request_stop() + join()
} // Output: working... stopped
```

## Embedded Applicability: Medium

- The RAII automatic join eliminates the hazard of forgetting to join, improving code robustness.
- The `std::stop_token` cooperative cancellation mechanism is more disciplined than hand-rolled flag variables.
- Depends on OS thread support; bare-metal RTOS scenarios require a thread abstraction layer alongside it.
- Requires C++20 standard library support; available from GCC 10+, but Clang/libc++ support came later (17+).

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 17 | 19.28 |

## See Also

- [cppreference: std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
- [cppreference: std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
