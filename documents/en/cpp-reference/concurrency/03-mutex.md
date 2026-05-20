---
title: std::mutex
description: Provides exclusive, non-recursive ownership semantics to protect shared
  data from simultaneous access by multiple threads.
chapter: 99
order: 3
tags:
- host
- mutex
- beginner
difficulty: beginner
cpp_standard:
- 11
- 14
- 17
- 20
- 23
---
# std::mutex (C++11)

## In a Nutshell

The most basic mutex, allowing only one thread to hold it at a time, used to protect shared data across multiple threads.

## Header

`#include <mutex>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct | `mutex()` | Constructs the mutex |
| Destruct | `~mutex()` | Destroys the mutex |
| Lock | `void lock()` | Locks the mutex, blocks if unavailable |
| Try lock | `bool try_lock()` | Tries to lock the mutex, returns false immediately if unavailable |
| Unlock | `void unlock()` | Unlocks the mutex |
| Native handle | `native_handle_type native_handle()` | Returns the underlying implementation-defined native handle |

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

- Typically a zero-overhead abstraction, incurring only atomic operation overhead when uncontended
- Non-copyable and non-movable, with a clear and controllable memory layout
- We recommend using it with `lock_guard` to avoid deadlocks on exception paths
- Note: in an RTOS environment, we must ensure that the underlying pthread or OS primitives are available

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.3   | 2010 |

## See Also

- [Tutorial: Mutexes and RAII Guards](../../../vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md)
- [cppreference: std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
