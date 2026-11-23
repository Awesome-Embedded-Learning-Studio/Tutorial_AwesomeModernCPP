---
title: "call_once and once_flag, and the Trade-off with the Meyers Singleton"
description: "call_once guarantees the callable executes exactly once, with retry on failure. Whatever can be written as block-level static initialization goes to the Meyers singleton; only granularity control is left to call_once."
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/03-call-once.md
  source_hash: 2d38864ba34d91b462069f2f4263795518a27ab0b5b217c6f32aa037bcffff6b
  translated_at: '2026-11-22T14:34:00+00:00'
  engine: glm-5.3
  token_count: 2618
---

# call_once and once_flag, and the Trade-off with the Meyers Singleton

A stretch of initialization code must run exactly once when multiple threads arrive concurrently. Two spellings exist: `std::call_once` with `std::once_flag`, or a function-local `static` variable (the Meyers singleton). We look at call_once's shape and its failure-retry behavior, then set a selection criterion.

## Minimal Code

```cpp
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

std::once_flag init_flag;
int attempts = 0;
const int* resource = nullptr;

void ensure_ready() {                       // 失败不置位，下一次调用会重试
    std::call_once(init_flag, [] {
        ++attempts;
        if (attempts < 2) throw std::runtime_error("config not ready");
        resource = new int(42);
    });
}

int main() {
    try {
        ensure_ready();                     // 第一次调用：抛异常，标志不置完成
    } catch (const std::exception& e) {
        std::cout << "failed: " << e.what() << "\n";
    }
    ensure_ready();                         // 第二次调用：重试成功
    std::cout << "attempts " << attempts << ", value " << *resource << "\n";

    std::vector<std::thread> ts;            // 已完成之后，并发的调用全部直接返回
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([] { ensure_ready(); });
    }
    for (auto& t : ts) t.join();
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
failed: config not ready
attempts 2, value 42
```

## Key Points

- Meyers singleton: initialization of a function-local `static` variable is thread-safe since C++11 ([stmt.dcl]: when threads concurrently first pass the declaration, the others wait for the initialization to finish). When initialization is simple and the result is just one object, use it — the code is minimal.
- `std::call_once` (C++11) guarantees: with multiple threads calling simultaneously, the callable executes exactly once, and the other threads wait for it to finish. `once_flag` records the completion state and is neither copyable nor movable.
- If the initialization function throws, `once_flag` does not enter the completed state, the exception propagates to the initiating thread, and the next call_once attempts again. A throwing initialization of a function-local static is likewise treated as unfinished and retried on the next entry into the declaration — so retryability is not what distinguishes the two.
- The real trade-off lies in control granularity: when initialization must be triggered explicitly at a time you designate, must bundle a group of steps, or must place its result anywhere rather than binding to one object's construction, call_once takes the stage.
- Both charge the first passer full initialization time. When initialization is expensive and you want to warm up, trigger call_once explicitly in main — steadier than the initialization suddenly executing on first call.

## Common Mistakes

- Calling call_once on the same flag again while the initialization function is still executing (nested entry): the nested call waits for the outer execution to finish and the thread hangs — a liveness problem, not undefined behavior ([thread.once.callonce]). Recursive entry into a function-local static is the undefined one ([stmt.dcl]). So never call yourself back from inside the initialization function.
- Reading the completed state as "initialization succeeded": on the exception path the flag is not set; code that depends on the initialization result must handle the "not yet succeeded" branch itself — do not wait on the flag alone.
- Assuming the two would each run the initialization once: `once_flag` and the static variable each govern their own completion state. Mixing both mechanisms for one initialization action may execute it twice — pick one and carry it through.

## Back to the Main Text

The trade-off between the Meyers singleton and double-checked locking is written out fully in the main text. Head to the main text's double-checked locking (DCLP) section, inside [The Four Tiers of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders).
