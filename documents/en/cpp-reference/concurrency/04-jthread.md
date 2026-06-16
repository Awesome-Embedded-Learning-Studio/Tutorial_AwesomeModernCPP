---
chapter: 99
cpp_standard:
- 20
- 23
description: A thread class that automatically joins; sends a stop request and waits
  for the thread to exit upon destruction.
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
  translated_at: '2026-06-16T03:28:09.989065+00:00'
  engine: anthropic
  token_count: 529
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::jthread (C++20)

## In a Nutshell

A thread class with built-in RAII semantics—automatically sends a stop request and joins on destruction, eliminating crashes caused by forgetting to join.

## Header

`#include <thread>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct (with function) | `template<class F> jthread(F&& f, Args&&... args)` | Starts a new thread executing f(args...) |
| Construct (with stop_token) | `template<class F> jthread(F&& f)` | f's first argument receives `std::stop_token` |
| Destructor | `~jthread()` | Requests stop + join (if joinable) |
| Request stop | `bool request_stop() noexcept` | Requests cooperative stop, returns success status |
| Get stop token | `std::stop_token get_stop_token() const noexcept` | Gets the current thread's stop token |
| Wait for completion | `void join()` | Blocks waiting for thread to finish |
| Detach thread | `void detach()` | Detaches, thread runs independently |
| Is joinable | `bool joinable() const noexcept` | Checks if thread is joinable |
| Get ID | `std::thread::id get_id() const noexcept` | Returns thread identifier |

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

- RAII automatic join eliminates the risk of forgetting to join, improving code robustness.
- `std::stop_token` cooperative cancellation mechanism is more standardized than manual flag variables.
- Relies on OS thread support; bare-metal RTOS scenarios require a thread abstraction layer.
- Requires C++20 standard library support; available in GCC 10+, but Clang/libc++ support came later (17+).

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 17 | 19.28 |

## See Also

- [cppreference: std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
- [cppreference: std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
