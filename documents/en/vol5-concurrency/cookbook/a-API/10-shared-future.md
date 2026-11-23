---
title: "shared_future"
description: "The share() conversion makes copies copyable, and get returns a const reference for class types and can be called repeatedly. Multiple threads accessing the same shared state is safe provided each thread holds its own copy."
chapter: 5
order: 10
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/10-shared-future.md
  source_hash: 5eb578a068f9104ee564d89fdc4596af9e2bd04215aeab11a27eed22af53a62c
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 2400
---
# shared_future

Several threads are all waiting for the result of one initialization task. `std::future`'s get can be called only once; the first caller drains it and everyone else is left stranded. `std::shared_future` (C++11) reshapes "waiting on the same result" into something copyable and repeatedly readable. The `std::promise` in the code is the writing end of a future; in this piece it just plays the starting gun. We run a broadcast program where three threads fetch the configuration at the same time.

## Minimal Code

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
    std::promise<std::string> gate;
    std::shared_future<std::string> ready = gate.get_future().share();  // 原来的 future 随之失效

    std::vector<std::thread> ts;
    std::atomic<int> ok{0};
    for (int id = 0; id < 3; ++id) {
        ts.emplace_back([ready, &ok] {          // 每个线程带走一份自己的副本
            if (ready.get() == "mode=prod") {   // get 对类类型返回 const 引用，可反复调用
                ok.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(100ms);
    gate.set_value("mode=prod");               // 一次 set_value，三个等待者全部放行

    for (auto& t : ts) t.join();
    std::cout << "workers got the value: " << ok.load() << "/3\n";
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
workers got the value: 3/3
```

## Key Points

- `std::future::share()` converts the future into a shared_future and invalidates the original. A shared_future is copyable, and every copy points to the same shared state.
- get() returns a const reference for class types and does not consume the state, so the same copy can be called repeatedly. The same holds for exceptions: an exception stored by `set_exception` is rethrown verbatim by get on every copy — no separate error channel needed.
- The safety clause the cppreference shared_future page spells out: multiple threads accessing the same shared state is safe provided each thread goes through its own copy. So we give each thread a copy to hold — do not share one object.
- It is a one-shot broadcast: set_value can happen only once. Compared with a condition variable it saves the lock, the predicate, and the spurious-wakeup handling; the price is that there is no slot for a second broadcast.
- A rule of thumb for choosing: if the result itself must be delivered, use shared_future. If it is merely a go signal and the scenario repeats, a latch or a condition variable fits better.

## Common Mistakes

- Multiple threads concurrently calling get on the same shared_future object: the standard only guarantees access through per-thread copies; concurrent access to the same object carries no guarantee. Hand each thread its own copy, one per thread.
- The producer path failing and ending without calling set_exception: the waiters never receive a result and hang forever. Failure must also land in the shared state, so the waiters have something to see.
- Trying to pass by copy out of std::future habit: std::future is move-only and the compile fails. To distribute to several threads, share() first, then copy.
- Treating shared_future as a notifier you can set repeatedly: a second set_value from the writing end throws `std::future_error` outright; you need a fresh promise to rebuild the result chain.

## Back to the Main Text

Where shared_future lands in the main text, plus the one-shot semantics of future, are both written up there. Go see it: Running Computations with std::async ([the full piece](../../chapter04-organizing-tasks/01-launch-with-async)).
