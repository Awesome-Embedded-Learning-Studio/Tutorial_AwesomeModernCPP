---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A class representing a single execution thread, allowing concurrent execution
  of multiple functions.
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
  translated_at: '2026-06-16T03:27:54.437506+00:00'
  engine: anthropic
  token_count: 443
---
# std::thread (C++11)

## In a Nutshell

A native thread wrapper provided by the C++ Standard Library. Creating an object immediately launches the underlying OS thread, enabling true multi-task concurrency.

## Header

`#include <thread>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `thread() noexcept;` | Default constructor, does not associate with any thread |
| Constructor | `template< class Function, class... Args > explicit thread( Function&& f, Args&&... args );` | Constructs and immediately starts the thread |
| Destructor | `~thread();` | Must be joined or detached before destruction, otherwise calls std::terminate |
| Assignment | `thread& operator=( thread&& other ) noexcept;` | Move assignment |
| Joinable | `bool joinable() const noexcept;` | Checks if the thread is joinable (i.e., associated with an active thread) |
| Join | `void join();` | Blocks the current thread until the target thread finishes execution |
| Detach | `void detach();` | Detaches the thread from the thread object, allowing it to run independently in the background |
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
    t.join(); // 阻塞等待线程 t 执行完毕
    std::cout << "done\n";
}
// Standard: C++11
```

## Embedded Applicability: High

- Zero abstraction overhead; `std::thread` maps directly to underlying OS threads (such as RTOS tasks or POSIX pthreads)
- `hardware_concurrency()` can be used to probe available core count at runtime to dynamically determine thread pool size
- Combined with `std::mutex` and `std::atomic`, it can safely protect shared peripheral registers or global buffers
- Note the OS thread stack overhead (typically several KB to tens of KB). On MCUs with extremely limited memory, we must precisely control the number of threads and stack size

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## See Also

- [cppreference: std::thread](https://en.cppreference.com/w/cpp/thread/thread)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
