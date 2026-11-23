---
title: "The shared_mutex Read-Write Lock: When It Pays, When It's Slower"
description: "Read with shared_lock, write with unique_lock. Worth it only with many readers, long reads, and rare writes; low-contention short critical sections get slower instead, and the standard guarantees no writer preference."
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - mutex
difficulty: intermediate
platform: host
cpp_standard: [17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/04-shared-mutex.md
  source_hash: 00f2343211aa494463b2ed59478ed0b48c47e0bf180095d0ae20399af16ec32c
  translated_at: '2026-11-22T14:36:00+00:00'
  engine: glm-5.3
  token_count: 2721
---

# The shared_mutex Read-Write Lock: When It Pays, When It's Slower

One piece of data, many readers, few writers. A plain mutex makes reads queue behind reads; a read-write lock lets reads run parallel with reads. When `std::shared_mutex` (C++17) earns and when it loses, we settle with a program mixing concurrent reads and writes, then give two judgment conditions.

## Minimal Code

```cpp
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Config {
public:
    std::string get(const std::string& key) const {
        std::shared_lock lk(mutex_);              // 共享锁：多个读同时进来
        auto it = data_.find(key);
        return it == data_.end() ? std::string{} : it->second;
    }
    void set(const std::string& key, std::string value) {
        std::unique_lock lk(mutex_);              // 独占锁：一个写，等所有读退场
        data_[key] = std::move(value);
    }
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

int main() {
    Config cfg;
    cfg.set("mode", "prod");

    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&cfg] {                  // 8 个读线程并发查
            for (int r = 0; r < 10000; ++r) cfg.get("mode");
        });
    }
    ts.emplace_back([&cfg] {                      // 1 个写线程偶尔更新
        for (int r = 0; r < 100; ++r) cfg.set("mode", "prod");
    });
    for (auto& t : ts) t.join();

    std::cout << "mode = " << cfg.get("mode") << "\n";   // mode = prod
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
mode = prod
```

In our run, the write thread finished its updates normally while 8 read threads queried concurrently — no errors, no deadlock.

## Key Points

- The read path uses `std::shared_lock` (C++14); the write path uses `std::unique_lock` or lock_guard. Multiple shared_locks can coexist; while an exclusive lock is on the field, every other request — read or write — must wait.
- `shared_mutex` is C++17. C++14 has only `std::shared_timed_mutex`, which carries the timeout interface. Without timeout needs, choose shared_mutex — the implementation is lighter and the overhead you pay smaller.
- When it earns: many readers, long read critical sections, rare writes. Only when the read critical section stretches long can parallel reading's benefit cover the read-write lock's own management overhead (the reader count bouncing between cores).
- When it loses: few threads, short critical sections. With just one load inside the lock, a plain mutex is usually faster — so measure a round with the real workload before switching.
- The standard guarantees neither writer preference nor scheduling fairness: a writer can be starved by a continuous stream of readers (writer starvation). Where write timeliness matters, do not stake the only write path on shared_mutex.

## Common Mistakes

- Locking again on a lock the same thread already holds (including recursive shared locks): shared_mutex permits a thread to hold only one lock at a time, either shared or exclusive — violating it is undefined behavior, and the standard will not catch you.
- Trying to upgrade a shared_lock to exclusive in place: two threads holding shared locks each wait for the other to let go, neither yields, and a deadlock is cast. The standard practice is to release the shared lock, take the exclusive lock, and cover the in-between changes by re-checking.
- Reading out a pointer or reference and still using it after the shared_lock's scope ends: one later write by the writer thread, and what you hold dangles.
- Taking "many reads, few writes" as a sufficient condition: with only two readers whose reads finish in a blink, switching locks only slows things down — measure before switching all the same.

## Back to the Main Text

Waiting with shared_lock paired with a condition variable — the main text covered condition_variable_any. Turn to the main text and keep reading there: [Waiting for a Condition: From Polling to Condition Variables](../../chapter03-wait-and-coordinate/01-wait-for-a-condition).
