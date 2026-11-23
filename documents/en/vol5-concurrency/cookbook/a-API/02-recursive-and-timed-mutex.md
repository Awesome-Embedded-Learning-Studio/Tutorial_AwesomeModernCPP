---
title: "recursive_mutex and timed_mutex"
description: "Same-thread reentrancy uses recursive_mutex's counting semantics; time-limited locking uses timed_mutex's try_lock_for/until. Both are fallbacks — the default remains the plain mutex."
chapter: 5
order: 2
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
  source: documents/vol5-concurrency/cookbook/a-API/02-recursive-and-timed-mutex.md
  source_hash: e91c9e85042b3f881eeca548a93ca81c7b1084981e9fd28edb0d3b9bca005a30
  translated_at: '2026-11-22T14:32:00+00:00'
  engine: glm-5.3
  token_count: 2436
---

# recursive_mutex and timed_mutex

The two variants each govern one thing: the same lock needing to be taken twice by the same thread inside a recursive call chain belongs to `std::recursive_mutex`; not wanting to wait forever for a lock belongs to `std::timed_mutex`. We run a minimal piece of code for each, then look at why I do not recommend them as the default choice.

## Minimal Code

```cpp
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::recursive_mutex walk_mutex;
std::timed_mutex log_mutex;

int walk(int n) {
    std::lock_guard<std::recursive_mutex> lk(walk_mutex);  // 每层各加一次，计数递增
    if (n == 0) return 0;
    return n + walk(n - 1);                                // 同一线程再次加锁，合法
}

int main() {
    std::cout << "walk(5) = " << walk(5) << "\n";          // walk(5) = 15

    log_mutex.lock();                                      // 主线程占住不放
    std::thread waiter([] {
        if (log_mutex.try_lock_for(std::chrono::milliseconds(100))) {
            std::cout << "got the lock\n";
            log_mutex.unlock();
        } else {
            std::cout << "timed out\n";                    // 100ms 内没等到
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    log_mutex.unlock();                                    // 放得太迟，等待方已经超时
    waiter.join();
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
walk(5) = 15
timed out
```

## Key Points

- `recursive_mutex` keeps an internal level count: each lock by the same thread increments it, each unlock decrements it, and the lock is truly released only when it reaches zero. Other threads still cannot enter during that time — no need to worry about them freeloading.
- `timed_mutex` adds `try_lock_for` (wait a duration) and `try_lock_until` (wait until a time point) over mutex; when the deadline arrives without the lock, it returns false and your program takes its own fallback path.
- `std::recursive_timed_mutex` (C++11) is the two combined; its real-world appearance rate is very low — knowing it exists is enough.
- Recursive locking usually reports a design problem: an interface has mixed "logic that should be called while holding the lock" and "the lock-free internal implementation" into one layer. The sturdier fix is to split out the lock-free internal implementation and let the outer interface do the locking.
- Locks with timeouts deal with the system clock, and their overhead is usually higher than a plain mutex. So with no timeout need, use mutex — do not pay extra for "might come in handy".

## Common Mistakes

- Swapping `std::mutex` for `recursive_mutex` to "fix" a deadlock: deadlock is a symptom of scrambled lock responsibilities; swapping the lock only stops the program from hanging — the data races and logic errors stay right where they were, waiting for you.
- Calling unlock one time too few leaves the level count above zero and the lock never released; every other thread can only queue up, and what you see while troubleshooting is a crowd of threads stuck on the same lock.
- Treating `try_lock_for`'s return value as success without branching on it: when it returns false, the critical section is not in your hands at all.
- lock and unlock on `recursive_mutex` by the same thread must balance strictly; transferring ownership across threads is undefined — do not count on the standard to catch you.

## Back to the Main Text

The main text's table specifically covered where the two variants belong, and here is the promised recipe — read them side by side: [How Do We Get the Lost Numbers Back? — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks).
