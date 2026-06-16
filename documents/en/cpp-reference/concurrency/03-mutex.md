---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Provides exclusive, non-recursive ownership semantics, used to protect
  shared data from simultaneous access by multiple threads.
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
  source_hash: e75c1e68c58c38974751ac83a869d9a5fd51ad23f14eec19de01158150c75d7f
  translated_at: '2026-06-16T03:27:55.403416+00:00'
  engine: anthropic
  token_count: 366
---
# std::mutex (C++11)

## In a Nutshell

The most basic mutex, allowing only one thread to hold it at any given time, used to protect shared data between threads.

## Header

`#include <mutex>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct | `mutex()` | Constructs the mutex |
| Destruct | `~mutex()` | Destroys the mutex |
| Lock | `void lock()` | Locks the mutex, blocks if unavailable |
| Try Lock | `bool try_lock()` | Tries to lock, returns false immediately if unavailable |
| Unlock | `void unlock()` | Unlocks the mutex |
| Native Handle | `native_handle_type native_handle()` | Returns the implementation-defined native handle |

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

## Embedded Suitability: High

- Usually a zero-overhead abstraction; incurs only atomic operation overhead when uncontended.
- Non-copyable and non-movable, with a deterministic memory layout.
- Recommended to use with `lock_guard` to prevent deadlocks caused by exception paths.
- Note: In RTOS environments, ensure that the underlying pthread or OS primitives are available.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.3   | 2010 |

## See Also

- [Tutorial: Mutexes and RAII Guards](../../vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)
- [cppreference: std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
