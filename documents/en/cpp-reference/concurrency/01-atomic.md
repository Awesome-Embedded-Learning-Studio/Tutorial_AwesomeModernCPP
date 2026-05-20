---
title: std::atomic
description: Lock-free atomic operation types for safe data sharing between multiple
  threads without data races
chapter: 99
order: 1
tags:
- host
- atomic
- intermediate
difficulty: intermediate
cpp_standard:
- 11
- 14
- 17
- 20
- 23
---
# std::atomic (C++11)

## In a Nutshell

A template class that guarantees read and write operations are indivisible, preventing data races when multiple threads concurrently access the same variable.

## Header

`#include <atomic>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Constructor | `atomic() noexcept = default;` | Default constructor (value is uninitialized) |
| Assignment | `T operator=(T desired) noexcept;` | Atomically writes the given value |
| Read | `operator T() const noexcept;` | Atomically reads and returns the current value |
| Store | `void store(T desired, memory_order order = memory_order_seq_cst) noexcept;` | Atomic write |
| Load | `T load(memory_order order = memory_order_seq_cst) const noexcept;` | Atomic read |
| Exchange | `T exchange(T desired, memory_order order = memory_order_seq_cst) noexcept;` | Atomically replaces the old value and returns it |
| Compare-and-exchange | `bool compare_exchange_weak(T& expected, T desired, ...) noexcept;` | Weak CAS, may spuriously fail |
| Compare-and-exchange | `bool compare_exchange_strong(T& expected, T desired, ...) noexcept;` | Strong CAS, fails only on a genuine mismatch |
| Atomic add | `T fetch_add(T arg, memory_order order = memory_order_seq_cst) noexcept;` | Atomically adds and returns the old value (integer/pointer) |
| Lock-free check | `bool is_lock_free() const noexcept;` | Checks if the current type is lock-free |

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

## Embedded Applicability: High

- Properly aligned integer and pointer types typically map directly to hardware atomic instructions, with zero extra overhead
- `is_lock_free()` allows runtime confirmation of whether the implementation is truly lock-free, avoiding implicit system calls
- Replaces bulky mutexes, ideal for lightweight state synchronization between interrupts and the main loop
- Overly large custom structures may degrade into internally locked implementations, which we must carefully avoid

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## See Also

- [Tutorial: Corresponding Chapter](../../../vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
