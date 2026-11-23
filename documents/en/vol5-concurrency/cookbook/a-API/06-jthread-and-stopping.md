---
title: "jthread and the Stop Protocol: A Quick Reference"
description: "A jthread destructor automatically performs request_stop and join; the thread function gains cancellation ability only by taking a stop_token as its first parameter; the stop_source, stop_token, and stop_callback trio each cover one piece."
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/a-API/06-jthread-and-stopping.md
  source_hash: 1f6731d03f8bf66156b75f1de35d757ee5b5b50484a1d2359525fd6fe8d9a1be
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 2607
---
# jthread and the Stop Protocol: A Quick Reference

To start a thread that can be politely asked to stop, C++20 `std::jthread` plus the stop_token trio is the standard answer. We walk through the interfaces in the order of start, request, callback, and shutdown — one piece of code covers the whole journey.

## Minimal Code

```cpp
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::jthread worker([](std::stop_token token) {   // 第一个参数收 token，jthread 自动传入
        int rounds = 0;
        std::stop_callback cleanup(token, [] {        // 停止请求一到就触发
            std::cout << "cleanup ran\n";
        });
        while (!token.stop_requested()) {
            ++rounds;
            std::this_thread::sleep_for(200ms);
        }
        std::cout << "worker exits after " << rounds << " rounds\n";
    });

    std::this_thread::sleep_for(1100ms);
    worker.request_stop();   // 不写这一行，析构也会自动做：request_stop 再 join
}
```

We compiled and ran the code in a temporary directory (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`, 2026-11-18); the output:

```text
cleanup ran
worker exits after 6 rounds
```

The round count and the order of the two output lines float with machine scheduling; both the worker's sleep duration and the scheduling precision affect how many rounds fit. What we can actually pin down is the shape: request_stop triggers the callback, the worker exits after observing the stop request at a checkpoint, and main no longer needs a hand-written join.

## Key Points

- `std::jthread` (C++20, `<thread>`): if still joinable at destruction, it performs `request_stop()` and then `join()`. The extra half it ships over std::thread is precisely the stop request.
- Declare the thread function's first parameter as `std::stop_token`, and jthread automatically passes its internal token in. A function that takes no token still runs — it simply has no cancellation ability. If you want cancellation, take the token.
- The trio shares one stop state: `stop_source` is the writing end (`request_stop`), `stop_token` is the reading end (`stop_requested`, `stop_possible`), and `stop_callback` registers a callback. `request_stop()` returns true only on the first call.
- A `stop_callback` callback executes synchronously on the thread that calls request_stop. If the stop already happened by the time of registration, the callback runs immediately on that registering thread — so keep long-running work out of the callback; the one you block is whoever asked for the stop.
- One stop_source can spawn multiple tokens, so a single request stops several threads at once. `t.get_stop_source()` fetches the writing end; `t.get_stop_token()` fetches the reading end.
- A thread blocked in a sleep or on a condition variable does not wake up because of request_stop, unless the waiting interface accepts a stop_token (for example the token-aware overloads of condition_variable_any, which the main text covers in [Waiting Also Needs an Exit Path](../../chapter03-wait-and-coordinate/06-ending-a-wait)).

## Common Mistakes

- A hand-written RAII thread wrapper that joins but never calls request_stop: the thread sits inside a long sleep, and destruction blocks until it wakes up on its own. Switch to jthread, or let the thread function take a token.
- Assuming request_stop makes the thread exit immediately: stopping is cooperative; the thread halts only after it checks stop_requested. So poll once per busy-loop iteration and swap blocking calls for token-aware versions.
- Doing heavy work or taking more locks inside the callback: the callback runs on the requester's thread of execution, so heavy work drags in the very thread that asked for the stop — you are blocking your own road.
- A stop_callback object dying before its stop_source: the registration dangles. Keep the callback in the scope of the thread being stopped; its destructor unregisters it automatically and the ordering stays correct by construction.

## Back to the Main Text

The registration races of the trio and an exit-path checklist are fully laid out in the main text. The companion chapter is Letting Work End in Order: std::jthread and Cooperative Cancellation — see [the companion chapter](../../chapter01-start-concurrency/06-jthread-and-stopping).
