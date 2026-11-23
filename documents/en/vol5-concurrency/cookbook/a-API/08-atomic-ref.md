---
title: "atomic_ref: Atomic Operations on Non-Atomic Variables"
description: "atomic_ref does not own the data; during the reference's lifetime the object must be accessed exclusively through atomic paths: the lifetime must outlast the reference, alignment must meet the bar, and plain reads and writes must not mix in."
chapter: 5
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/08-atomic-ref.md
  source_hash: 0457fc1c9c43dae2e58bc1eb39cf1420315cfd6bebf84da698c0fb82333b2456
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 2289
---
# atomic_ref: Atomic Operations on Non-Atomic Variables

The variable's type is already locked in (an array from a third-party library, a struct that has to interface with C), so it cannot become `std::atomic`, yet concurrent access genuinely needs atomic operations. C++20 `std::atomic_ref` exists for exactly this: leave the object untouched and temporarily swap the access path for an atomic one. We verify with four counters incremented concurrently.

## Minimal Code

```cpp
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    std::vector<int> counters(4, 0);               // 普通 int，运行时才决定要原子加

    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([&counters, i] {
            std::atomic_ref<int> ref(counters[i]);  // 不拥有数据，只是一个原子视图
            for (int n = 0; n < 100000; ++n) {
                ref.fetch_add(1, std::memory_order_relaxed);
            }
        });                                         // ref 在线程函数结束时销毁
    }
    for (auto& t : ts) t.join();

    for (int v : counters) std::cout << v << " ";   // 100000 100000 100000 100000
    std::cout << "\n";
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
100000 100000 100000 100000
```

## Key Points

- `std::atomic_ref<T>` (C++20, `<atomic>`) requires T to be trivially copyable. Its interface matches `std::atomic<T>` almost exactly: load, store, exchange, compare_exchange, fetch_add, with the same memory-order parameters.
- Atomicity holds for the lifetime of the reference: atomic operations on multiple atomic_refs pointing to the same object are atomic with respect to each other. The object's lifetime must cover all atomic_ref instances — do not let the reference outlive the object.
- Two hard constraints, violation of which is undefined behavior: while an atomic_ref exists, the object must be accessed exclusively through atomic_ref instances — plain reads and writes must not mix with the atomic path. Within the same object, two atomic_refs must not concurrently reference overlapping subobjects.
- Alignment: the static constant `std::atomic_ref<T>::required_alignment` gives the minimum alignment requirement. Integer elements in a contiguous array laid out at sizeof(T) spacing satisfy it naturally; objects cast out of raw memory are yours to guarantee.
- It fits situations such as atomic per-slot increments on a statistics array, or deciding after the fact that some member needs atomic access. If a variable needs to be atomic from the start and its type is yours to change, declaring `std::atomic` directly saves the worry.

## Common Mistakes

- One thread goes through atomic_ref while another thread reads the raw variable directly: mixed paths are a data race. Atomicity holds only when everything goes atomic — leave no back door for plain reads and writes.
- Alignment falling short: on some platforms 64-bit atomic operations require 8-byte alignment; members of packed structs need `alignas` padding before you build an atomic_ref on them.
- An atomic_ref outliving the object: for example, caching the reference in a longer-lived container — once the object is destroyed you hold a dangling reference, and your program blows up at some random spot after the free.
- Expecting a const atomic_ref to be read-only: constness is shallow; a const atomic_ref can still modify the referenced value — do not let the type fool you.

## Back to the Main Text

The operation surface of atomic types, plus the boundaries around floating-point and custom types, are all laid out in the main text How Far Atomic Operations Take You — head over: [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve).
