---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A class representing a single execution thread, allowing multiple functions
  to run concurrently.
difficulty: beginner
order: 2
reading_time_minutes: 2
tags:
- host
- mutex
- beginner
title: std::thread
translation:
  source: documents/cpp-reference/concurrency/02-thread.md
  source_hash: 596c4d1b8b7efabc62972cee475f92ead74fcf9b57734494369ae9f392c8f3b9
  translated_at: '2026-09-26T17:09:14+00:00'
  engine: anthropic
  token_count: 1050
---
# std::thread (C++11)

## In a Nutshell

The C++ Standard Library's native thread wrapper: constructing an object starts the underlying OS thread right away, giving you true multi-task concurrency.

## Header

`#include <thread>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `thread() noexcept;` | Default construction; not associated with any thread |
| Constructor | `template< class Function, class... Args > explicit thread( Function&& f, Args&&... args );` | Constructs and immediately starts a thread |
| Destructor | `~thread();` | Must already be joined or detached before destruction, otherwise std::terminate is called |
| Assignment | `thread& operator=( thread&& other ) noexcept;` | Move assignment |
| Joinable | `bool joinable() const noexcept;` | Checks whether the thread can be joined (i.e., whether it is associated with an active thread) |
| Join | `void join();` | Blocks the current thread and waits for the target thread to finish |
| Detach | `void detach();` | Separates the thread from the thread object so it runs independently in the background |
| Get ID | `id get_id() const noexcept;` | Returns the thread identifier |
| Hardware Concurrency | `static unsigned int hardware_concurrency() noexcept;` | Returns the number of concurrent threads supported by the implementation |

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
    t.join(); // Block until thread t finishes executing
    std::cout << "done\n";
}
// Standard: C++11
```

## Embedded Applicability: High

- Zero abstraction overhead: `std::thread` maps directly onto the underlying OS thread (an RTOS task or a POSIX pthread, for example)
- `hardware_concurrency()` can probe the available core count at runtime to size a thread pool dynamically
- Combined with `std::mutex` and `std::atomic`, it can safely guard shared peripheral registers or global buffers
- Watch out for OS thread stack overhead (typically a few KB to a few tens of KB); on very small-memory MCUs we need to tightly control thread count and stack size

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## See Also

- [cppreference: std::thread](https://en.cppreference.com/w/cpp/thread/thread)

---

*Some content is adapted from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
