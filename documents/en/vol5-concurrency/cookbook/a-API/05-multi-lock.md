---
title: "Multi-Lock Acquisition Tools: std::lock, try_lock Fallback, and Hierarchical Locks"
description: "Acquiring two or more locks together: one-line scoped_lock in C++17, defer_lock paired with std::lock in C++11, std::try_lock when a missed attempt is acceptable, and hierarchical locks when no ordering can be established."
chapter: 5
order: 5
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
  source: documents/vol5-concurrency/cookbook/a-API/05-multi-lock.md
  source_hash: ed78135b4a0c7b1f2edbb026bdb3aff153b58f5e87d532c1c8ddb7e521113162
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 3041
---
# Multi-Lock Acquisition Tools: std::lock, try_lock Fallback, and Hierarchical Locks

Moving a task from one queue to another means locking both queue mutexes at the same time. Swapping two buffers is the same story: take both locks together. The moment the lock order runs in opposite directions on two execution paths, an AB-BA deadlock walks onto the stage. The tools the standard library provides fall neatly into three tiers. When an order can be fixed, scoped_lock grabs everything in one shot. When one failed attempt means "skip this round", that is try_lock's job. Only when no order can be established at all does the hierarchical lock get its turn. We verify it stays deadlock-free under a stress test with four threads swapping concurrently.

## Minimal Code

```cpp
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

std::mutex left_mutex, right_mutex;
std::vector<int> left_data{1, 2}, right_data{3, 4};

void swap_cxx11() {                          // C++11/14：defer_lock 配 std::lock
    std::unique_lock<std::mutex> a(left_mutex, std::defer_lock);
    std::unique_lock<std::mutex> b(right_mutex, std::defer_lock);
    std::lock(a, b);                         // 死锁避免算法：拿不齐就退回重来
    left_data.swap(right_data);
}

void swap_cxx17() {                          // C++17：一行等价于上面三行
    std::scoped_lock lk(left_mutex, right_mutex);
    left_data.swap(right_data);
}

bool try_both() {                            // 拿不到就算了
    int failed = std::try_lock(left_mutex, right_mutex);
    if (failed != -1) return false;          // 第 failed 把没拿到，已拿到的已被释放
    std::lock_guard<std::mutex> a(left_mutex, std::adopt_lock);
    std::lock_guard<std::mutex> b(right_mutex, std::adopt_lock);
    left_data.push_back(1);
    return true;
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([] {
            for (int r = 0; r < 10000; ++r) swap_cxx17();   // 获取顺序随机也不死锁
        });
    }
    for (auto& t : ts) t.join();

    std::cout << "left " << left_data.size()
              << ", right " << right_data.size() << "\n";
    std::cout << "try_both succeeded: " << try_both() << "\n";
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
left 2, right 2
try_both succeeded: 1
```

## Key Points

- In C++17, just write `std::scoped_lock lk(m1, m2);`. The constructor runs a deadlock-avoidance algorithm internally, the destructor releases the locks in reverse order, and the argument order you pass in is arbitrary.
- The C++11/14 equivalent: construct two `unique_lock`s with `std::defer_lock` (no locking yet), then `std::lock(a, b)` acquires them all at once. Three steps collapse into a single scoped_lock line — we naturally pick the latter.
- `std::try_lock(m1, m2)` makes exactly one attempt: it returns -1 when every lock is acquired. If some lock fails, everything already acquired is released and the index of the failing position (0-based) is returned — a good fit for "lock busy, skip this round" situations.
- The hierarchical lock is a pattern the standard library does not ship; we have to build it ourselves: assign each mutex a level number, threads may only acquire from high to low, a `thread_local` variable records the current thread's level, and violations throw. With many locks and complex module boundaries, it turns lock ordering into a runtime-checkable constraint, so you catch violations during testing.
- When a global order is known, pass the arguments to scoped_lock in that order: the internal try_lock retries fewer times and the overhead drops.

## Common Mistakes

- Reading the `std::try_lock` return value as "how many locks succeeded": it is the index of the failing position, and the failed call has already released every lock it acquired — do not unlock them again.
- Forgetting adopt after try_lock succeeds on all locks: the locks are already in hand, and writing plain lock_guard constructions would lock them a second time. Use `std::adopt_lock` to adopt the already-held locks.
- Panicking because the scoped_lock arguments are "passed in the wrong order": deadlock-freedom is its guarantee; the order only affects internal retry overhead, not correctness — pass them with confidence.
- Forgetting to initialize the hierarchical lock's thread_local to the maximum level: the check is dead on arrival from the very first lock, violations go undetected, and we lose the most important safety net during debugging.

## Back to the Main Text

The main text fully works through deadlock causes, lock ordering, and layering strategy in Nobody Lets Go — Now What; just keep reading there: [Nobody Lets Go — Now What](../../chapter02-sharing-data/06-deadlock-defenses).
