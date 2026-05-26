---
title: std::jthread
description: A thread class that automatically joins, sending a stop request and waiting
  for the thread to exit upon destruction.
chapter: 99
order: 4
tags:
- host
- cpp-modern
- beginner
difficulty: beginner
cpp_standard:
- 20
- 23
translation:
  source: documents/cpp-reference/concurrency/04-jthread.md
  source_hash: 086dce477a31c89263c97393ead9b091f5497a690feb634821b0da44a9904475
  translated_at: '2026-05-26T10:13:10.774510+00:00'
  engine: anthropic
  token_count: 526
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::jthread (C++20)

## In a Nutshell

A thread class with built-in RAII semantics — automatically sends a stop request and joins on destruction, completely eliminating crashes caused by forgetting to join.

## Header

`#include <thread>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor (with function) | `template<class F> jthread(F&& f, Args&&... args)` | Starts a new thread to execute f(args...) |
| Constructor (with stop_token) | `template<class F> jthread(F&& f)` | The first parameter of f receives `std::stop_token` |
| Destructor | `~jthread()` | Requests stop + join (if joinable) |
| Request stop | `bool request_stop() noexcept` | Requests cooperative stop, returns whether the request succeeded |
| Get stop token | `std::stop_token get_stop_token() const noexcept` | Gets the stop token of the current thread |
| Wait for completion | `void join()` | Blocks until the thread finishes |
| Detach thread | `void detach()` | Detaches, the thread runs independently |
| Is joinable | `bool joinable() const noexcept` | Checks if the thread is joinable |
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
    std::jthread t(worker); // 自动传入 stop_token
    // t 析构时自动 request_stop() + join()
} // 输出: working... stopped
```

## Embedded Applicability: Medium

- RAII automatic join eliminates the risk of forgetting to join, improving code robustness
- `std::stop_token` cooperative cancellation mechanism is more standard than manual flag variables
- Relies on OS thread support; bare-metal and RTOS scenarios require a thread abstraction layer
- Requires C++20 standard library support; available since GCC 10+, but Clang/libc++ support came later (17+)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 17 | 19.28 |

## See Also

- [cppreference: std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
- [cppreference: std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
