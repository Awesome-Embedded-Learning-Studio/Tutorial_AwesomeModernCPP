---
title: "atomic_signal_fence"
description: "An ordering barrier between ordinary code and a signal handler on the same thread; it only constrains compiler reordering and emits no CPU instructions. For inter-thread synchronization, use atomic_thread_fence."
chapter: 5
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/09-atomic-signal-fence.md
  source_hash: 7f15fb43bfe1b4ab3ced8d80874d0345d4826fef7117e0493f1b882522dd16f3
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 2525
---
# atomic_signal_fence

A signal handler can drop in between any two instructions of the execution flow. You want some plain write to be guaranteed complete before the handler reads it, but the handler and the main flow run on the same thread of execution — the CPU level is already consistent there, and the only thing to guard against is the compiler moving instructions around. `std::atomic_signal_fence` (C++11) exists for exactly this: a compiler-only barrier that emits not a single CPU instruction.

## Minimal Code

```cpp
#include <atomic>
#include <csignal>
#include <iostream>

std::atomic<bool> notified{false};
volatile std::sig_atomic_t snapshot = 0;
int payload = 0;                          // 普通变量

void handler(int) {
    if (notified.load(std::memory_order_relaxed)) {
        std::atomic_signal_fence(std::memory_order_acquire);  // 下面的读不许提前
        snapshot = payload;               // volatile sig_atomic_t：信号处理器里安全的写
    }
}

int main() {
    std::signal(SIGUSR1, handler);

    payload = 42;                            // 普通写
    std::atomic_signal_fence(std::memory_order_release);  // 它不许挪到通知之后
    notified.store(true, std::memory_order_relaxed);

    std::raise(SIGUSR1);                     // handler 在本线程执行完才返回
    std::cout << "snapshot " << snapshot << "\n";   // snapshot 42
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
snapshot 42
```

Our demo sends the signal synchronously with `std::raise`, so the handler and the main flow execute in order on the same thread of execution. In the asynchronous-signal scenario the handler drops in at an arbitrary instruction boundary, and what the two fences block is precisely the compiler's freedom to move things.

## Key Points

- `std::atomic_signal_fence(order)` (C++11, `<atomic>`) establishes ordering between ordinary code and a signal handler within the same thread. Its semantics mirror `atomic_thread_fence`, except that it emits no CPU instructions and only blocks compiler reordering (the cppreference page says exactly this).
- The two sides are symmetric in shape: the writing side is a plain write, then signal_fence(release), then a relaxed atomic store; the handler's reading side is a relaxed load, then signal_fence(acquire), then the read of the plain data. Drop the fence on either end and the ordering breaks — so hang it on both.
- It is useless for inter-thread synchronization: different threads run on different cores, and the CPU barrier that scenario needs is not provided. For cross-thread ordering, use `atomic_thread_fence` or atomic operations with memory ordering.
- The objects you can safely touch inside a signal handler are limited: `volatile std::sig_atomic_t`, lock-free atomic objects, plus atomic_signal_fence itself. For the value of a plain object as seen in the handler, the standard gives only unspecified ([intro.execution], a C++11 clause; the current draft has since moved sections). Reading payload in the demo relies on fence ordering plus empirical verification as a backstop — do not lean on it in production code.

## Common Mistakes

- Using atomic_signal_fence for synchronization between two threads: it emits not one CPU instruction, so on weakly ordered architectures the other thread simply never sees your writes.
- Calling functions that are not async-signal-safe, such as iostream or malloc, inside a signal handler: the handler may drop in right in the middle of a malloc with the lock in hand — the program deadlocks on the spot.
- Hanging a fence on only one end: the release on the write side and the acquire on the read side must appear as a pair; miss either one and the compiler is entitled to move things, leaving your ordering made of paper.
- Treating the fence as an operation on some variable: it marks a position in the execution flow and takes no object parameter — there is no such mechanism as "fence variable X".

## Back to the Main Text

The three ways to wire up fences, the x86 instruction comparison, and the volatile boundaries — a section in the main text awaits you: The Four Levels of Memory Ordering: From Sequential Consistency to relaxed ([the full section](../../chapter05-atomic-memory-model/04-choosing-memory-orders)).
