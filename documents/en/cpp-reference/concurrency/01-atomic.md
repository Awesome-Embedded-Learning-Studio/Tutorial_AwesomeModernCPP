---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Lock-free atomic operation types for safe data sharing between threads
  without data races.
difficulty: intermediate
order: 1
reading_time_minutes: 2
tags:
- host
- atomic
- intermediate
title: std::atomic
translation:
  source: documents/cpp-reference/concurrency/01-atomic.md
  source_hash: 59abadaa327489d53b2aad1a01181393ca926fdba9127dd63fe3e0f54fe7fb7c
  translated_at: '2026-06-16T03:27:43.238039+00:00'
  engine: anthropic
  token_count: 505
---
# std::atomic (C++11)

## In a Nutshell

A template class that guarantees read and write operations are indivisible, preventing data races when multiple threads access the same variable concurrently.

## Header File

```cpp
#include <atomic>
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `atomic() noexcept` | Default construction (value is uninitialized) |
| Assignment | `T operator=(T) noexcept` | Atomically write the selected value |
| Read | `T operator T() const noexcept` | Atomically read and return the current value |
| Store | `void store(T, order = memory_order::seq_cst) noexcept` | Atomic write |
| Load | `T load(order = memory_order::seq_cst) const noexcept` | Atomic read |
| Exchange | `T exchange(T, order = memory_order::seq_cst) noexcept` | Atomically replace the old value and return the old value |
| Compare Exchange | `bool compare_exchange_weak(T&, T, order, order) noexcept` | Weak CAS, may spuriously fail |
| Compare Exchange | `bool compare_exchange_strong(T&, T, order, order) noexcept` | Strong CAS, only fails on a true mismatch |
| Atomic Add | `T fetch_add(T, order = memory_order::seq_cst) noexcept` | Atomically add and return the old value (integer/pointer) |
| Lock-free Check | `bool is_lock_free() const noexcept` | Check if the current type is implemented in a lock-free manner |

## Minimal Example

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};

void task() {
    for (int i = 0; i < 1000; ++i) {
        // Atomically increment counter by 1
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

int main() {
    std::thread t1(task);
    std::thread t2(task);

    t1.join();
    t2.join();

    std::cout << "Final counter value: " << counter << '\n';
    // Output: Final counter value: 2000
}
```

## Embedded Applicability: High

- Properly aligned integer and pointer types typically map directly to hardware atomic instructions, resulting in zero overhead.
- `is_lock_free()` allows us to confirm at runtime if the implementation is truly lock-free, avoiding implicit system calls.
- Replaces bulky mutexes, making it ideal for lightweight state synchronization between interrupts and the main loop.
- Excessively large custom structures may fall back to an internal locking implementation, which we must strictly avoid.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: Corresponding Chapter](../../vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
