---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Provides exclusive, non-recursive ownership semantics to protect shared
  data from simultaneous access by multiple threads.
difficulty: beginner
order: 3
reading_time_minutes: 1
tags:
- host
- mutex
- beginner
title: std::mutex
translation:
  source: documents/cpp-reference/concurrency/03-mutex.md
  source_hash: ad3622b587c66d17f0ce3019c67c2ae7e0b74aa5e62d30b3f3b70e30f2f61b0e
  translated_at: '2026-11-22T16:41:57.097806+00:00'
  engine: glm-5.3-refresh
  token_count: 1458
---
# std::mutex (C++11)

## In a Nutshell

The most basic kind of mutex: only one thread may hold it at any moment, making it the tool of choice for protecting data shared across threads.

## Header File

`#include <mutex>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct | `mutex()` | Constructs the mutex |
| Destruct | `~mutex()` | Destroys the mutex |
| Lock | `void lock()` | Locks the mutex; blocks if it is unavailable |
| Try lock | `bool try_lock()` | Attempts to lock; returns false immediately if unavailable |
| Unlock | `void unlock()` | Unlocks the mutex |
| Native handle | `native_handle_type native_handle()` | Returns the underlying, implementation-defined native handle |

## Minimal Example

```cpp
#include <iostream>
#include <mutex>
#include <thread>

int counter = 0;
std::mutex m;

void increment() {
    std::lock_guard<std::mutex> lock(m);
    ++counter;
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << counter << '\n'; // 输出: 2
}
```

## Embedded Applicability: High

- Typically a zero-overhead abstraction: when uncontended, the only cost is that of an atomic operation
- Neither copyable nor movable, with an explicit and controllable memory layout
- Best paired with `lock_guard` to keep exception paths from causing a deadlock
- Note: in RTOS environments, make sure the underlying pthread or OS primitives are available

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.3   | 2010 |

## See Also

- [Tutorial: mutex and RAII Locks](../../vol5-concurrency/chapter02-sharing-data/02-mutex-and-locks.md)
- [cppreference: std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
