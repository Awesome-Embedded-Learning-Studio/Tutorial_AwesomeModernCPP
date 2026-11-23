---
title: "Letting Work End in an Orderly Way: std::jthread and the Stop Trio"
description: "Wire the stop request into the wind-down path via std::jthread's destructor, get clear on the stop_source, stop_token, and stop_callback trio, and look back at the shortcomings of atomic-flag polling."
chapter: 5
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/06-jthread-and-stopping.md
  source_hash: 81d630ab923f5f81fc84ab198596d5edc8b1b25dc59589fdc8432780688fdd9f
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 15415
---
# Letting Work End in an Orderly Way: std::jthread and the Stop Trio

The protagonist spoiled at the end of the last article takes the stage here: `std::jthread`, whose destructor first requests a stop, then joins. "Being able to wait" the last article already had; what C++20 adds is "being able to call it off" — and "calling it off" has itself become a protocol with definitions and synchronization relationships. The races around callback registration, and a checklist for auditing exit paths one by one, belong to the next article.

`std::jthread` lives in `<thread>` and the stop tokens in `<stop_token>`, both facilities since C++20. The outputs below are readings from single runs; rerun in the same environment and the millisecond figures and thread IDs will jitter slightly. Codebases still before C++20 can only take the old road of atomic-flag polling, which gets its own dedicated section later.

Swap `std::thread` for `std::jthread`, delete the `join()` line, and the program runs the same:

<OnlineCompilerDemo
  title="jthread: no join written, the destructor waits automatically"
  source-path="code/examples/vol5/chapter01/37_jthread_auto_join.cpp"
  description="std::jthread's destructor joins for you; without writing a line, no thread is lost."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// jthread:不写 join,析构自动等
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;

void worker() {
    std::this_thread::sleep_for(100ms);
    std::println("worker done");
}

int main() {
    std::jthread t(worker);
    // 不写 join，析构时自动等
}
```

</OnlineCompilerDemo>

We got one line, `worker done`. To this point there is no difference from the last article's destructor-focused `JoiningThread` snippet: both wait for the thread to end in the destructor. The difference is the step before the waiting. The working draft [thread.jthread.cons] prescribes for `~jthread()`: when `joinable()` is true, call `request_stop()`, then call `join()` (since C++20). `JoiningThread` does only the second half; jthread does it all.

This order is not just a sentence in the documentation — run it and you can see it. We let the worker loop until it sees the stop request, while the main thread never calls `request_stop()`; it just lets the object leave the scope:

<OnlineCompilerDemo
  title="jthread requests the stop automatically"
  source-path="code/examples/vol5/chapter01/38_stop_token_basic.cpp"
  description="On leaving the scope the destructor requests the stop first and then waits; the worker's loop can see the stop token."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// jthread 自动请求停止:离开作用域,worker 的循环看得见
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

void worker(std::stop_token token) {
    std::stop_callback cb(
        token, [] { std::println("[callback] running on thread {}", std::this_thread::get_id()); });
    int ticks = 0;
    while (!token.stop_requested()) {
        ++ticks;
        std::this_thread::sleep_for(20ms);
    }
    std::println("[worker] saw the request after {} ticks, thread {}", ticks,
                 std::this_thread::get_id());
}

int main() {
    std::println("[main] thread {}", std::this_thread::get_id());
    {
        std::jthread t(worker);
        std::this_thread::sleep_for(150ms);
        std::println("[main] leaving the scope, no request_stop call");
    }
    std::println("[main] destructor returned");
}
```

</OnlineCompilerDemo>

The five lines we got look like this (thread IDs vary by machine):

```text
[main] thread 125682764838784
[main] leaving the scope, no request_stop call
[callback] running on thread 125682764838784
[worker] saw the request after 8 ticks, thread 125682757662400
[main] destructor returned
```

Compare the ID on the callback line with the `[main]` line: the two numbers match. The stop request was issued by the main thread inside the destructor, not by some background thread on its behalf. The worker's ID differs from both; it loops on its own flow of execution, exits when it sees `stop_requested()` turn true, and only then can the destructor's join collect it. Without the request_stop step, the worker's loop condition would never fail, and the destructor would wait forever — same as the join-only `JoiningThread`.

A request, once sent, only does anything for threads that can see a token. The `std::jthread` constructor tries two call forms. If inserting the internal token as the first argument after the callable compiles, it uses the with-token call; if not, it calls without the token (the Effects clause of [thread.jthread.cons], since C++20). Let's write a thread function that accepts no token and try:

<OnlineCompilerDemo
  title="The destructor waits: the worker must finish first"
  source-path="code/examples/vol5/chapter01/39_jthread_dtor_waits.cpp"
  description="The worker sleeps a full 300 milliseconds with nobody requesting a stop, and the destructor waits the full 300 — waiting is the destructor's bottom line."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// jthread 析构等待:worker 睡满 300ms,析构就等满 300ms
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
static clock_type::time_point t0;

int main() {
    t0 = clock_type::now();
    {
        std::jthread t([] {
            std::this_thread::sleep_for(300ms);
            std::println("[worker] finished its own work, nobody asked it to");
        });
        std::this_thread::sleep_for(50ms);
        std::println(
            "[main] leaving the scope at {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count());
    }
    std::println(
        "[main] destructor returned at {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count());
}
```

</OnlineCompilerDemo>

```text
[main] leaving the scope at 50 ms
[worker] finished its own work, nobody asked it to
[main] destructor returned at 301 ms
```

The main thread left the scope at 50 milliseconds, yet the destructor did not return until 301. The ~250 milliseconds in between were not spent in request_stop. request_stop sends the request and returns; join was waiting for the worker to finish its own 300-millisecond sleep, and all we could do was wait along. The request truly went out, but the worker holds no token, nobody was watching, so the request might as well never have been sent. That is what cooperative stopping means: it does not kill threads, it only sends requests.

jthread carries three more entry points — `request_stop()`, `get_stop_source()`, `get_stop_token()` — all forwarding to that internal `stop_source`. For finer-grained use, say passing the same token to several components, you fetch from these. Next section we look at the trio itself.

## The stop_source / stop_token / stop_callback Trio

One piece of stop state that everyone can see, managed by the standard library — and the trio are its three access roles. `std::stop_source` is the writing end: `request_stop()` issues the request, and `get_token()` derives the reading end from it. The reading end is `std::stop_token`, holding two queries: `stop_requested()` asks whether the request has come, and `stop_possible()` asks whether it is associated with a stop state at all. `std::stop_callback` handles notification: hang a callable on it, and it executes when the request arrives. All three are declared in `<stop_token>`, since C++20.

Before wiring in any thread, let's run the trio's question-and-answer empty-handed:

<OnlineCompilerDemo
  title="State queries on stop_source and stop_token"
  source-path="code/examples/vol5/chapter01/40_stop_source_states.cpp"
  description="Of two request_stop calls only one returns true; a default token and a nostopstate source cannot stop anything."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// stop_source 与 stop_token 的状态查询:两次 request_stop 只有一次为真
#include <print>
#include <stop_token>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();

    std::println("before: source.stop_possible()={} token.stop_possible()={}",
                 source.stop_possible(), token.stop_possible());
    std::println("before: source.stop_requested()={} token.stop_requested()={}",
                 source.stop_requested(), token.stop_requested());
    std::println("request_stop() #1 -> {}", source.request_stop());
    std::println("request_stop() #2 -> {}", source.request_stop());
    std::println("after:  token.stop_requested()={}", token.stop_requested());

    std::stop_token empty;
    std::println("default token: stop_possible()={} stop_requested()={}", empty.stop_possible(),
                 empty.stop_requested());

    std::stop_source silent(std::nostopstate);
    std::println("nostopstate source: stop_possible()={} request_stop() -> {}",
                 silent.stop_possible(), silent.request_stop());
}
```

</OnlineCompilerDemo>

```text
before: source.stop_possible()=true token.stop_possible()=true
before: source.stop_requested()=false token.stop_requested()=false
request_stop() #1 -> true
request_stop() #2 -> false
after:  token.stop_requested()=true
default token: stop_possible()=false stop_requested()=false
nostopstate source: stop_possible()=false request_stop() -> false
```

Several things in the output deserve a line-by-line look. A default-constructed source immediately owns stop state: `stop_possible()` is true. `request_stop()` is safe to call any number of times — only the first returns true, and the second call will not re-trigger callbacks. Once issued, a request is never withdrawn. A default-constructed token is disengaged: no associated state, and both queries return false. A source constructed with `std::nostopstate` also has no state, and its `request_stop()` returns false. Tokens can be copied and compared; tokens derived from the same source compare equal, which is why we can pass them around as parameters without losing identity.

jthread already holds a `stop_source` internally — `t.request_stop()` forwards exactly to it. To use the internal one, call `t.get_stop_source()`. If you only use jthread and never touch the trio, the destructor step still issues the request. To make one stop cover several threads, you have to take a source yourself and hand tokens out from it. The demo below, to show that the trio does not depend on jthread, spins up its own source and runs one-to-many with three `std::thread`s:

<OnlineCompilerDemo
  title="One source stops a group of threads"
  source-path="code/examples/vol5/chapter01/41_one_source_many_threads.cpp"
  description="Three processors share the same token; one request_stop, and each sees it and wraps up on its own."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 一个 stop_source 停一组线程:一次 request_stop,三条 processor 各自看见
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

void processor(std::stop_token token, int id) {
    int rounds = 0;
    while (!token.stop_requested()) {
        ++rounds;
        std::this_thread::sleep_for(50ms);
    }
    std::println("processor {} stopped after {} rounds", id, rounds);
}

int main() {
    std::stop_source source;
    std::vector<std::thread> pool;
    for (int id = 1; id <= 3; ++id) {
        pool.emplace_back(processor, source.get_token(), id);
    }
    std::this_thread::sleep_for(150ms);
    std::println("one request_stop() call");
    source.request_stop();
    for (auto& t : pool)
        t.join();
    std::println("all joined");
}
```

</OnlineCompilerDemo>

```text
one request_stop() call
processor 2 stopped after 3 rounds
processor 3 stopped after 3 rounds
processor 1 stopped after 3 rounds
all joined
```

One request, and all three threads saw it on their next loop pass. A stop request is addressed to the stop state, not to any particular thread. Tokens handed out by one source all read the same state; whoever holds a token can see the request. The order in which the three threads print will differ on every run — the timing of each check is decided by the scheduler, the same interleaving we saw in A First std::thread Program. With a few more threads, characters within one line can tangle together too — we met `std::cout`'s little problem back then as well.

Querying alone is not enough. Sometimes the moment the request arrives is exactly the best moment to close a file, break a connection, or wake a waiter — enter `std::stop_callback`. It registers a callable that executes when the request arrives. Let's run the registration timing:

<OnlineCompilerDemo
  title="Which thread the callback runs on"
  source-path="code/examples/vol5/chapter01/42_callback_thread.cpp"
  description="A callback registered before the request runs on the requesting thread; one registered after runs in place, right there."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// stop_callback 在哪个线程上跑:请求前注册的回调跑在请求方,请求后注册的当场跑
#include <print>
#include <stop_token>
#include <thread>

int main() {
    std::stop_source source;
    std::stop_token token = source.get_token();
    std::println("main is thread {}", std::this_thread::get_id());

    std::println("registering a callback, before request");
    std::stop_callback cb(
        token, [] { std::println("  [callback] runs on thread {}", std::this_thread::get_id()); });
    std::println("registration returned");

    std::println("calling request_stop");
    source.request_stop();
    std::println("request_stop returned");

    std::println("registering another callback after the request");
    std::stop_callback late(
        token, [] { std::println("  [late] runs on thread {}", std::this_thread::get_id()); });
    std::println("late registration returned");
}
```

</OnlineCompilerDemo>

```text
main is thread 129462478755712
registering a callback, before request
registration returned
calling request_stop
  [callback] runs on thread 129462478755712
request_stop returned
registering another callback after the request
  [late] runs on thread 129462478755712
late registration returned
```

Both callbacks ran on the main thread, matching the `main is thread` ID. The first executed synchronously inside `request_stop()` — the callback returns, and only then does `request_stop()`. By the second registration the request had already been sent. The standard specifies that if the request has already been issued at the moment of registration, the callback executes right there in the constructor, without entering the callback list (the registration clause of [stoptoken.concepts], since C++20). That is why it printed before "late registration returned" — we can see with our own eyes that it did not wait for some later moment. Put the [callback] and [late] lines side by side and you get one practical sentence: registering a callback does not guarantee "later" — it may run on the spot. So don't do time-consuming work in a callback; it spends the requesting thread's time. The callback object unregisters automatically on destruction, and the fine print of unregistration includes a blocking behavior — the easiest place to get it wrong, which we will look at together later when we cover stop_callback's registration timing and races.

## The Stop Protocol's Precursor: atomic-Flag Polling and Its Shortcomings (Extended)

This section carries the (Extended) marker; skipping it does not affect the main line of this article. But this old approach is everywhere in existing code, and knowing it tells you exactly which shortcomings the stop tokens patched.

Since C++11 the standard library has had `std::thread`, but stopping had no ready-made facility. The common practice of the day was to put up your own `std::atomic<bool>` as a stop flag: the worker glances at it every loop, and the main thread sets it. Let's run it:

<OnlineCompilerDemo
  title="The stop protocol's precursor: atomic-flag polling"
  source-path="code/examples/vol5/chapter01/43_atomic_flag_polling.cpp"
  description="Polling every 20 milliseconds does stop, but noticing is delayed — this is exactly the old road the stop trio replaces."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 停止协议的前身:atomic 标志加轮询,20ms 一问
#include <atomic>
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> stop_flag{false};
    auto t0 = std::chrono::steady_clock::now();

    std::thread worker([&] {
        long polls = 0;
        while (!stop_flag.load()) {
            ++polls;
            std::this_thread::sleep_for(20ms);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        std::println("worker noticed at {} ms after {} polls", ms, polls);
    });

    std::this_thread::sleep_for(50ms);
    stop_flag.store(true);
    std::println("flag set at 50 ms");
    worker.join();
}
```

</OnlineCompilerDemo>

```text
flag set at 50 ms
worker noticed at 60 ms after 3 polls
```

The main thread set the flag at 50 milliseconds; the worker only noticed at around 60, on its third poll. Response latency is decided by the polling interval: with a 20-millisecond interval, the worst case is 20 extra milliseconds. Shrink the interval to push the latency down, and the thread's idle spinning rises along with it; grow the interval to save CPU, and stopping turns sluggish. This trade-off is part of the shape of polling, dictated by the interval — no amount of tidy code dodges it.

Polling has one harder problem: it cannot wake a thread that is blocked in a wait. We let the worker wait on a condition variable while the main thread only flips the flag, with no notify:

<OnlineCompilerDemo
  title="Polling misses the window: no notification means sleeping it out"
  source-path="code/examples/vol5/chapter01/44_poll_miss_window.cpp"
  description="The flag is set mid-sleep, and with nobody to wake it wait_for must sleep the full 500 milliseconds — exactly the hole the stop trio patches."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 轮询错过窗口:标志在 sleep 中间置位,wait_for 还得睡满整个超时
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> stop_flag{false};
    std::mutex m;
    std::condition_variable cv;

    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(m);
        auto t0 = std::chrono::steady_clock::now();
        bool woke = cv.wait_for(lock, 500ms, [&] { return stop_flag.load(); });
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        std::println("wait_for returned {} after {} ms", woke, ms);
    });

    std::this_thread::sleep_for(50ms);
    stop_flag.store(true);
    std::println("flag set to true at 50 ms, nobody notifies");
    worker.join();
}
```

</OnlineCompilerDemo>

```text
flag set to true at 50 ms, nobody notifies
wait_for returned true after 500 ms
```

The waking end is the same story: a flag by itself wakes nobody. The flag turned true at 50 milliseconds, yet the worker still slept the full 500 before coming back. The returned true is the predicate's result. The so-called predicate is that bool-returning judgment in wait's third argument. The standard is explicit on this point: the return value says whether the predicate held, unrelated to the timeout and unrelated to whoever did the waking. So treating the return value as a "stop request received" signal doesn't hold up either. Add one `notify_all()` to the same program and the wait ends at 50 milliseconds — interrupting a wait relies on notification, not on the flag itself.

The third shortcoming is semantics. Several things you must agree on yourself: Who may set this flag? Once set, who is responsible for waking? Does the flag outlive the thread? Do two threads reading the value count as synchronized? Two modules each with their own stop condition — how are they merged? Is there a "stop possible" state you can query? All of it is verbal agreement. Every thread, and every maintainer taking over, has to know it; miss one clause and you get an intermittent failure. `std::atomic<bool>` does not reach this layer. Under relaxed ordering it does not even guarantee that "data written before the flag write is visible to the thread that reads the flag." relaxed is the loosest memory-ordering tier: it only guarantees that this variable's own reads and writes don't tangle, and nothing else.

Stop tokens took these agreements into the standard. Between `request_stop()` returning true and `stop_requested()` returning true on another thread, there is a synchronization relationship. Between registering a callback and that callback's execution, there is also a synchronization relationship (the synchronization clause of [stoptoken.concepts], since C++20). You don't have to memorize the agreements one by one — the library turned them into clauses. Where C++20 is unavailable, atomic-flag polling still works; we just have to write down every one of the agreements above ourselves, and check them ourselves.

## Reference Resources

While writing this article I drew on one book, C++ Concurrency in Action by Anthony Williams, taking its chapter progression around threads and cooperative cancellation. The examples and prose are this author's own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against cppreference and the working draft, with clause numbers attached to the entries.

- [std::jthread (cppreference)][cppref-jthread] — the auto-joining thread, since C++20.
- [std::jthread::~jthread (cppreference)][cppref-jthread-dtor] — requests a stop on destruction, then joins.
- [std::stop_source (cppreference)][cppref-source] — the writing end: issues the stop request and derives tokens.
- [std::stop_token (cppreference)][cppref-token] — the reading end: queries the request and the state.
- [std::stop_callback (cppreference)][cppref-callback] — registering and unregistering stop callbacks.
- [C++ working draft: jthread construction and destruction (thread.jthread.cons)][draft-jthread] — the two constructor call forms and the destructor's ordering.
- [C++ working draft: stop-token concepts clause (stoptoken.concepts)][draft-stop] — registration, unregistration, synchronization relationships, and the semantics of request_stop.

[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[cppref-jthread-dtor]: https://en.cppreference.com/w/cpp/thread/jthread/~jthread
[cppref-source]: https://en.cppreference.com/w/cpp/thread/stop_source
[cppref-token]: https://en.cppreference.com/w/cpp/thread/stop_token
[cppref-callback]: https://en.cppreference.com/w/cpp/thread/stop_callback
[draft-jthread]: https://eel.is/c++draft/thread.jthread.cons
[draft-stop]: https://eel.is/c++draft/stoptoken.concepts
