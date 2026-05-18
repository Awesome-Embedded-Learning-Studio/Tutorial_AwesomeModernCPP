---
title: "std::jthread"
description: "Auto-joining thread class that sends a stop request and waits on destruction"
chapter: 99
order: 4
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
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

# std::jthread (C++20)

## In a Nutshell

A thread class with built-in RAII semantics — on destruction it automatically sends a stop request and joins, eliminating crashes from forgotten joins.

## Header

`#include <thread>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct with function | `template<class F> jthread(F&& f, Args&&... args)` | Start a new thread executing f(args...) |
| Construct with stop_token | `template<class F> jthread(F&& f)` | f's first argument receives `std::stop_token` |
| Destructor | `~jthread()` | Request stop + join (if joinable) |
| Request stop | `bool request_stop() noexcept` | Request cooperative stop, returns success |
| Get stop token | `std::stop_token get_stop_token() const noexcept` | Get the thread's stop token |
| Wait for completion | `void join()` | Block until thread finishes |
| Detach thread | `void detach()` | Detach; thread runs independently |
| Is joinable | `bool joinable() const noexcept` | Check if thread can be joined |
| Get ID | `std::thread::id get_id() const noexcept` | Return thread identifier |

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
    std::jthread t(worker); // stop_token is passed automatically
    // t destructor calls request_stop() + join() automatically
} // Output: working... stopped
```

## Embedded Applicability: Medium

- RAII auto-join eliminates the risk of forgotten joins, improving robustness
- `std::stop_token` cooperative cancellation is more disciplined than hand-rolled flag variables
- Requires OS thread support; bare-metal RTOS scenarios need a threading abstraction layer
- Requires C++20 standard library support; GCC 10+ works, but Clang/libc++ support came later (17+)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 17 | 19.28 |

## See Also

- [cppreference: std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
- [cppreference: std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
