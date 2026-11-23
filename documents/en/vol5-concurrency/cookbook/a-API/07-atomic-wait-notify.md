---
title: "atomic_wait and atomic_notify"
description: "wait(old) compares against the old value rather than the target, and the standard permits spurious wakeups. Notify only after the value is updated; the free function atomic_wait is equivalent to the member wait."
chapter: 5
order: 7
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
  source: documents/vol5-concurrency/cookbook/a-API/07-atomic-wait-notify.md
  source_hash: cf1c867ddd4dbd9dac4d8266db7f9add3f539279d24be10084a809a5eb46dc85
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 2000
---
# atomic_wait and atomic_notify

Busy-waiting on an atomic flag burns CPU, while a mutex plus condition variable feels heavy. C++20 equips atomic variables with waiting and notification: suspend while the value is unchanged, wake when a notification arrives. We run a program where the main thread waits for a worker thread to finish initialization, with both the member-function and free-function shapes on display.

## Minimal Code

```cpp
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

std::atomic<int> state{0};

int main() {
    std::thread producer([] {
        std::this_thread::sleep_for(100ms);
        state.store(42, std::memory_order_release);
        state.notify_one();                    // 值更新之后才通知
    });

    state.wait(0, std::memory_order_acquire);  // 还是 0 就挂起；不再是 0 立即返回
    std::cout << "got " << state.load() << "\n";

    producer.join();
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
got 42
```

## Key Points

- The semantics of `state.wait(old)` (C++20): atomically read the current value; if it equals old, suspend, otherwise return immediately. The parameter is "the old value I last saw", not "the target value I want to wait for".
- `notify_one` and `notify_all` wake threads waiting on that atomic object. The standard permits spurious wakeups, so the robust shape is `while (flag.load(acquire) == not-ready-value) flag.wait(...)` — recheck once after waking; we do not gamble on luck.
- The free functions `std::atomic_wait(&obj, old)` and `std::atomic_notify_one(&obj)` (C++20, `<atomic>`) are equivalent to the member functions; you will find uses for them in generic code.
- On Linux this maps to futex (the kernel interface for suspending and waking threads on user-space addresses); on Windows it maps to WaitOnAddress. Implementations without such primitives fall back to a mutex plus condition variable — the interface stays the same, but the benefit of suspension drops to zero.
- It only solves "wait for one value to change". The wait has no timeout version; multi-variable conditions and timed waits remain condition-variable territory.

## Common Mistakes

- Passing the wait parameter as the target value: to wait for true you might write `flag.wait(true)` — but when the value is already true it sails through without waiting, and when it is false you are waiting for "no longer equal to false". So to wait for true, pass false.
- Placing notify before store: the waiter wakes and still reads the old value, so it can only go back to sleep for another round. Notify after the value changes; do not reverse the order.
- Betting correctness on "how many wakeups happen": notify_one may wake nobody (if nobody is waiting), and a spurious wakeup may wake one extra time. We place our bets on rechecking the value, not on the number of notifications.

## Back to the Main Text

The trade-off between busy-waiting and suspension with wait and notify, plus the boundaries of spinlocks, are all covered in the main text How Far Atomic Operations Take You — keep reading there: [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve).
