---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Lock-free atomic operation types for safe data sharing between threads
  without data races
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
  source_hash: 96c52cdc0c5404014d06f43297add0d3ba49fdaa0ebb59f2dac7360e20309f29
  translated_at: '2026-11-22T16:40:07.922458+00:00'
  engine: glm-5.3-refresh
  token_count: 2027
---
# std::atomic (C++11)

## In a nutshell

A template class that guarantees read and write operations are indivisible, so that multiple threads accessing the same variable concurrently never produce a data race.

## Header file

`#include <atomic>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Constructor | `atomic() noexcept = default;` | Default construction (value left uninitialized) |
| Assignment | `T operator=(T desired) noexcept;` | Atomically writes the given value |
| Read | `operator T() const noexcept;` | Atomically reads and returns the current value |
| Store | `void store(T desired, memory_order order = memory_order_seq_cst) noexcept;` | Atomic write |
| Load | `T load(memory_order order = memory_order_seq_cst) const noexcept;` | Atomic read |
| Exchange | `T exchange(T desired, memory_order order = memory_order_seq_cst) noexcept;` | Atomically replaces the old value and returns it |
| Compare exchange | `bool compare_exchange_weak(T& expected, T desired, ...) noexcept;` | Weak CAS; may fail spuriously |
| Compare exchange | `bool compare_exchange_strong(T& expected, T desired, ...) noexcept;` | Strong CAS; fails only on a genuine mismatch |
| Atomic add | `T fetch_add(T arg, memory_order order = memory_order_seq_cst) noexcept;` | Atomically adds and returns the previous value (integers/pointers) |
| Lock-free check | `bool is_lock_free() const noexcept;` | Checks whether this type is implemented lock-free |

## Minimal Example

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

std::atomic<int> cnt{0};

int main() {
    std::vector<std::jthread> pool;
    for (int i = 0; i < 10; ++i)
        pool.emplace_back([] { for (int n = 0; n < 10000; ++n) cnt++; });
    std::cout << cnt << '\n'; // 输出 100000
}
```

## Embedded Suitability: High

- Properly aligned integer and pointer types usually map straight onto hardware atomic instructions, with zero extra overhead
- `is_lock_free()` lets us confirm at runtime whether the implementation is truly lock-free, avoiding hidden system calls
- A lean replacement for heavyweight mutexes, well suited to lightweight state synchronization between an interrupt handler and the main loop
- Oversized custom structs may fall back to an internally locked implementation — the main pitfall to watch out for

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: the corresponding chapter](../../vol5-concurrency/chapter05-atomic-memory-model/01-what-atomics-solve.md)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
