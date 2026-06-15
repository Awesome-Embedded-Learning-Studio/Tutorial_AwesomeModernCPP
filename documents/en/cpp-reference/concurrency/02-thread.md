---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A class representing a single thread of execution, allowing multiple
  functions to execute concurrently.
difficulty: beginner
order: 2
reading_time_minutes: 2
tags:
- host
- mutex
- beginner
title: std::thread
translation:
  engine: anthropic
  source: documents/cpp-reference/concurrency/02-thread.md
  source_hash: 2dd45afcbbdffe639639ad953479852b85567f32504f35a63d37b8863dc3cd20
  token_count: 438
  translated_at: '2026-05-26T10:12:33.175214+00:00'
---
# std::thread (C++11)

## In a Nutshell

A native thread wrapper provided by the C++ standard library. Creating an object immediately launches an underlying OS thread, enabling true multitasking concurrency.

## Header

`#include <thread>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `thread() noexcept;` | Default constructor, not associated with any thread |
| Constructor | `template< class Function, class... Args > explicit thread( Function&& f, Args&&... args );` | Constructs and immediately starts the thread |
| Destructor | `~thread();` | Must be joined or detached before destruction, otherwise calls std::terminate |
| Assignment | `thread& operator=( thread&& other ) noexcept;` | Move assignment |
| Joinable | `bool joinable() const noexcept;` | Checks if the thread is joinable (i.e., associated with an active thread) |
| Join | `void join();` | Blocks the current thread until the target thread finishes execution |
| Detach | `void detach();` | Detaches the thread from the thread object, allowing it to run independently in the background |
| Get ID | `id get_id() const noexcept;` | Returns the thread identifier |
| Hardware concurrency | `static unsigned int hardware_concurrency() noexcept;` | Returns the number of concurrent threads supported by the implementation |

## Minimal Example

```cpp
#include <iostream>
#include <thread>

void task(int n) {
    for (int i = 0; i < n; ++i)
        std::cout << "worker: " << i << "\n";
}

int main() {
    std::thread t(task, 3);
    t.join(); // 阻塞等待线程 t 执行完毕
    std::cout << "done\n";
}
// Standard: C++11
```

## Embedded Applicability: High

- Zero-overhead abstraction; `std::thread` maps directly to an underlying OS thread (such as an RTOS task or POSIX pthread)
- `hardware_concurrency()` can be used at runtime to probe the number of available cores, dynamically determining the thread pool size
- Combined with `std::mutex` and `std::atomic`, we can safely protect shared peripheral registers or global buffers
- Note the OS thread stack overhead (typically a few KB to tens of KB). On MCUs with extremely limited memory, we must precisely control the number of threads and stack sizes

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## See Also

- [cppreference: std::thread](https://en.cppreference.com/w/cpp/thread/thread)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
