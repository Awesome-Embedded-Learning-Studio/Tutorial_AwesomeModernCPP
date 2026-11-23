---
title: "Choosing RAII Locks for mutex: lock_guard, unique_lock, scoped_lock"
description: "Pick the RAII guard by two questions: does the lock need releasing midway, and how many locks are taken at once. Default to lock_guard; switch to unique_lock for mid-scope unlocking or condition variables; hand multiple locks to scoped_lock."
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/01-mutex-and-raii.md
  source_hash: 55a9b5f13b6366fb023a6744a208db2ee4bda5bec84d47db949d6f4b1bec223f
  translated_at: '2026-11-22T14:30:30+00:00'
  engine: glm-5.3
  token_count: 2775
---

# Choosing RAII Locks for mutex: lock_guard, unique_lock, scoped_lock

A piece of shared data in hand needs locking, three RAII guards sit before you — which do you take? The choice comes down to two things: does the lock need releasing midway, and how many locks are taken at once. We compile and run all three choices in one program, then add key points and common mistakes.

## Minimal Code

```cpp
#include <iostream>
#include <mutex>
#include <vector>

std::mutex counter_mutex;
int counter = 0;

std::mutex buffer_mutex;
std::vector<int> buffer;

std::mutex account_a, account_b;

void bump() {
    std::lock_guard<std::mutex> lk(counter_mutex);  // 单锁、持到作用域结束
    ++counter;
}

void snapshot() {
    std::unique_lock<std::mutex> lk(buffer_mutex);  // 需要中途解锁
    std::vector<int> copy = buffer;
    lk.unlock();                                    // 临界区到此为止
    std::cout << "snapshot size " << copy.size() << "\n";
}

void move_funds() {
    std::scoped_lock lk(account_a, account_b);      // 一次安全拿两把（C++17）
    std::cout << "moved\n";
}

int main() {
    bump(); bump(); bump();
    {
        std::lock_guard<std::mutex> lk(buffer_mutex);
        buffer = {10, 20, 30};
    }
    snapshot();
    move_funds();
    std::cout << "counter " << counter << "\n";
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
snapshot size 3
moved
counter 3
```

## Key Points

- Default to `std::lock_guard` (C++11). Locks in the constructor, unlocks in the destructor, no extra interfaces — leaving you no room to use it wrong.
- If the lock must be released midway (shrinking the critical section), locked lazily, or paired with `std::condition_variable::wait`, switch to `std::unique_lock` (C++11). The wait family's signatures accept only `unique_lock<std::mutex>`; passing a lock_guard does not even compile.
- To hold two or more locks at once, hand them to `std::scoped_lock` (C++17). The constructor runs a deadlock-avoidance algorithm internally; pass the arguments in any order and it will not deadlock.
- For single-lock scenes, stay with lock_guard. Seeing it, you know there is exactly one lock; seeing scoped_lock, you must think one extra step about whether a second lock exists — readers of the code rest easier on that difference.
- All three are non-copyable. `unique_lock`, however, is movable — lock ownership can transfer across functions; lock_guard and scoped_lock cannot.

## Common Mistakes

- Omitting the variable name comes in two spellings, each with its own trouble. The parentheses form `std::lock_guard<std::mutex>(mtx);` parses as a declaration, and GCC 16.2.1 fails the compile outright — you never even reach the unlocked run. The truly silent one is the braces form `std::lock_guard<std::mutex>{mtx};`: the unnamed temporary destructs and unlocks immediately after construction, and the code that follows runs unlocked; GCC 16.2.1 flags -Wunused-result by default, other implementations not necessarily. So the habit of naming lock objects is worth building.
- Calling `lock()` again on a `std::mutex` the same thread already holds is undefined behavior ([thread.mutex.requirements.mutex]); most implementations manifest it as a hang. If you truly need reentrancy, see the [recursive_mutex and timed_mutex recipe](02-recursive-and-timed-mutex).
- Hand-writing `lock()`/`unlock()` pairs almost necessarily leaks a path — exceptions and early returns both skip the unlock. So C++ Core Guidelines CP.20 stands with the main text: always use RAII.
- Passing `std::defer_lock` to `unique_lock` but forgetting `lock()` before entering the critical section means the lock was never taken — you are effectively running the critical section unlocked.

## Back to the Main Text

The mutex family, the lock-to-data correspondence, and lock granularity are all laid out for you in the main text's How Do We Get the Lost Numbers Back? — std::mutex and the Three Scoped Locks: [How Do We Get the Lost Numbers Back? — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks).
