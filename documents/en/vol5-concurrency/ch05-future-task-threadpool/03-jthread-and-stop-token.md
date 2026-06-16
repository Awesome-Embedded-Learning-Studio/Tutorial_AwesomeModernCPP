---
chapter: 5
cpp_standard:
- 20
description: 'Automatic joining threads and cooperative cancellation in C++20: Complete
  usage of stop_source, stop_token, and stop_callback'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- promise 与 packaged_task
reading_time_minutes: 18
related:
- 线程所有权与 RAII
- 线程池设计
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- RAII守卫
- 进阶
title: jthread and Stop Token
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/03-jthread-and-stop-token.md
  source_hash: 80b83f3c158dae7cdfbea2c654447ed483f043668425a77bf856b79280894f26
  translated_at: '2026-06-16T04:05:15.903308+00:00'
  engine: anthropic
  token_count: 3915
---
# jthread and Stop Tokens

Honestly, while writing the previous few articles, I felt quite uneasy using `std::thread`. Every time required manual `join()`, and a single slip-up resulted in a `std::terminate` crash. Stopping a thread mid-way required hacking together custom flag bits—it's 2026, and C++ thread management still feels this "primitive." In the last article, we used `std::atomic` and `std::condition_variable` to build manual async task control, but the underlying thread tools hadn't been upgraded. In this article, we will finally fix this shortcoming.

Before we dive in, a quick note on the environment: all code in this article is based on **C++20** and requires compiler support for the `<thread>` header (GCC 10+, Clang 17+ (libc++ has partial support, full in 20), MSVC 19.28+). If your compiler isn't up to date, upgrade now—there is no fallback for the features covered here.

C++20 finally gives us `std::jthread`, an automatic joining thread wrapper with a built-in cooperative cancellation mechanism. The core of this mechanism consists of three classes: `std::stop_source` (issues a stop request), `std::stop_token` (checks for a stop request), and `std::stop_callback` (registers a stop callback). They can be used independently without `std::jthread`, but they work best together. In this article, we will thoroughly cover this set of tools.

## The Pain Points of std::thread: A Review

Before learning new tools, let's look back at the specific headaches `std::thread` causes. Understanding these pain points explains why C++20 designed `std::jthread` the way it did.

Consider a typical problem scenario. The following code looks fine at first glance—create a thread, do work, join, done.

```cpp
void risky_function() {
    std::thread t([] {
        std::cout << "Working...\n";
    });
    // do some other work
    t.join();
}
```

But what if `t.join()` throws an exception? The control flow jumps to stack unwinding, `t`'s destructor finds the thread still joinable, and `std::terminate` unceremoniously kills the entire process. No error message, no recovery, just a crash. You might think, "I'll just add a try-catch?"—you can, but you must do this everywhere `std::thread` is used. Missing one is a ticking time bomb.

A common fix is to write a custom RAII wrapper that auto-joins in the destructor. We actually did this in the ch01 article. But every project needs its own version, and the destructor's `join()` is a blocking call—if the thread is running a long task, your program hangs when the guard is destroyed, with no way to signal the thread to stop.

These two problems—crashing on forgotten join and inability to signal a thread to stop—are what `std::jthread` solves in one go.

## std::jthread: The Auto-Joining Thread

Now let's look at `std::jthread`. Its name implies "joining"—it tells you its core selling point right there: automatic join upon destruction. Usage is almost identical to `std::thread`, so you can basically swap them blindly:

```cpp
void safe_function() {
    std::jthread jt([] {
        std::cout << "Working...\n";
    });
    // No need for jt.join(); it happens automatically
}
```

You will notice the only difference is replacing `std::thread` with `std::jthread` and removing the `join()` line. But if it only auto-joined, there would be no fundamental difference from a hand-written RAII guard. `std::jthread`'s real killer feature is in its destructor behavior: before joining, it **first calls `request_stop()`**, then `join()`. The pseudo-code looks roughly like this:

```cpp
~jthread() {
    if (joinable()) {
        request_stop();
        join();
    }
}
```

This means `std::jthread` doesn't just dumbly wait for the thread to end; it politely notifies the thread to stop first, then waits. If the thread function responds to this stop request, it can exit gracefully instead of blocking the caller indefinitely during destruction. This is incredibly important—if you've used Java's `Thread.interrupt()` or Go's `context`, you'll find C++20's design follows the same philosophy: don't force kill, cooperate to exit.

> **Warning**: If you hand-wrote a `ThreadGuard` or `ScopedThread` RAII wrapper in ch01, take note—those guards only `join()` in the destructor, they do not `request_stop()`. If your thread function has long blocking operations (like `sleep()`, condition variable waits), a hand-written guard will cause the destructor to block indefinitely. The `std::jthread` `request_stop()` + `join()` combination is the correct approach.

## Cooperative Cancellation: stop_source, stop_token, stop_callback

Great, now we know `std::jthread` auto-joins. But what does "request stop" actually mean? How does the thread know it was requested? This is what cooperative cancellation solves.

The core idea is simple: you shouldn't "kill" a thread—because you don't know its state, it might hold a lock or be half-way through writing data. You should "request" it to stop, and let the thread decide when to exit at an appropriate time. Think of it as a signaling mechanism: someone raises a red flag saying "please stop," and the thread checks the flag at the start of every loop, exiting gracefully if raised. This mechanism consists of three classes sharing an internal stop-state. `std::stop_source` is the write side, responsible for issuing requests; `std::stop_token` is the read side, responsible for querying status; `std::stop_callback` executes a callback when a request is issued.

### std::stop_source and std::stop_token

Let's start with the write and read sides. `std::stop_source` provides `request_stop()` to issue a stop request and `get_token()` to get the associated `std::stop_token`. `std::stop_token` is a read-only observer with two query methods: `stop_requested()` returns whether a request has been received, and `stop_possible()` returns whether there is an associated stop state. One `std::stop_source` can derive multiple `std::stop_token`s—this will be used later, meaning you can control multiple threads with a single source.

```cpp
void basic_stop_demo() {
    std::stop_source src;
    std::stop_token tok = src.get_token();

    std::cout << "Stop requested: " << tok.stop_requested() << '\n'; // false

    src.request_stop();

    std::cout << "Stop requested: " << tok.stop_requested() << '\n'; // true
}
```

This example shows the basic one-to-one relationship: a `std::stop_source` issues a request, and its associated `std::stop_token` sees it immediately. Note that `request_stop()` can be called multiple times; only the first returns `true`—subsequent calls are safe but don't re-trigger callbacks.

A default-constructed `std::stop_token` has no associated stop state, and `stop_possible()` returns `false`. If you don't need stop capability, you can use a default-constructed empty token to save overhead.

### How std::jthread Passes the stop_token

So, how does `std::jthread`'s internal token communicate with our thread function? The answer is—if your thread function accepts a `std::stop_token` as its first parameter, `std::jthread` automatically passes its internal token in. If the function doesn't accept `std::stop_token`, `std::jthread` degrades into a simple auto-join thread with no cancellation capability. This design is clever—backward compatible; use it if you want, ignore it if you don't.

```cpp
void jthread_auto_stop_demo() {
    std::jthread jt([](std::stop_token st) {
        while (!st.stop_requested()) {
            std::cout << "Working...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        std::cout << "Thread received stop request, exiting.\n";
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    // jt.request_stop() called automatically here
}
```

You'll notice we didn't manually call `request_stop()`—`std::jthread`'s destructor automatically calls `request_stop()` then `join()`. `request_stop()` is also a member of `std::jthread`, calling the internal `std::stop_source`'s method. You can also use `get_stop_source()` or `get_stop_token()` for finer control, like passing the token to other components.

### std::stop_callback: Registering a Stop Callback

Just checking a stop flag isn't enough—sometimes you want to execute cleanup actions the moment a stop request is issued, like closing file handles, releasing network connections, or setting a flag. `std::stop_callback` does exactly this: its constructor accepts a `std::stop_token` and a callable object, triggering the callback when the associated token's `request_stop()` is called.

```cpp
void callback_demo() {
    std::stop_source src;
    std::stop_token tok = src.get_token();

    std::stop_callback cb(tok, [] {
        std::cout << "Stop requested! Cleaning up...\n";
    });

    std::cout << "Main thread sleeping...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Requesting stop...\n";
    src.request_stop(); // Callback triggers here

    std::cout << "Main thread exiting.\n";
}
```

Running this, you'll see output like this: one second of sleep, then `request_stop()` triggers the callback printing "Cleaning up...", and finally the main thread exits.

A few details to watch. First, the callback executes **synchronously** on the thread calling `request_stop()`, not the worker thread—so don't do heavy work in the callback, or you'll block the requester. Second, if the stop is already requested when you register the callback, it runs immediately on the registering thread, so it won't miss the event. Finally, `std::stop_callback`'s destructor automatically unregisters, so when `callback_demo` ends, `cb` is destroyed, avoiding dangling callbacks.

## Practical Patterns for Cooperative Cancellation

Now that we've covered the API, let's see how to use it in real scenarios. We'll look at three common cancellation patterns—from simple to complex—each with its own use case.

### Pattern 1: Polling stop_token in a Loop

The simplest pattern is checking `stop_requested()` in the loop condition. If iterations are short (milliseconds), checking in the `while` condition is enough. But if an iteration takes several seconds, you need checkpoints inside the iteration, or you'll have to wait for the current one to finish before responding.

```cpp
void polling_pattern() {
    std::jthread worker([](std::stop_token st) {
        int counter = 0;
        while (!st.stop_requested()) {
            // Quick check
            if (counter % 10 == 0) {
                std::cout << "Working... " << counter << '\n';
            }
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Simulate long work
            if (counter == 50) {
                std::cout << "Long task start...\n";
                std::this_thread::sleep_for(std::chrono::seconds(3));
                // Check again after long task
                if (st.stop_requested()) break;
            }
        }
        std::cout << "Worker exiting cleanly.\n";
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Auto request_stop and join here
}
```

### Pattern 2: condition_variable + stop_token

Pure polling has a problem—many worker threads aren't busy-waiting in a loop but waiting on a condition variable. Simple polling isn't enough here because the thread might be blocked on `wait()` with no chance to check the flag. C++20 added a `wait()` overload for `std::condition_variable_any` that accepts a `std::stop_token`—when a stop request is issued, the wait automatically wakes up, returning `false` to indicate it was stopped, not that the predicate was satisfied.

> **Warning**: Note it's `std::condition_variable_any`, not `std::condition_variable`. The standard committee only added the overload to the former; the latter doesn't support it. If you're using `std::condition_variable`, either switch to `any` or use `std::stop_callback` to manually `notify_all()`.

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

void cond_var_pattern() {
    std::jthread producer([](std::stop_token st) {
        int i = 0;
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::cout << "Producing " << i << '\n';
            i++;
        }
    });

    std::queue<int> tasks;
    std::mutex m;
    std::condition_variable_any cv;

    std::jthread consumer([&](std::stop_token st) {
        while (true) {
            int data;
            // wait returns false if stop requested
            if (!cv.wait(m, st, [&]{ return !tasks.empty(); })) {
                std::cout << "Consumer stopped.\n";
                break;
            }

            data = tasks.front();
            tasks.pop();
            std::cout << "Consuming " << data << '\n';
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    // Auto request_stop triggers cv wakeup
}
```

The logic is straightforward: the consumer waits on `cv`. When a task arrives, it processes it. When a stop request occurs, `wait()` returns `false`, and the thread finishes remaining tasks and exits. Internally, `wait()` uses `std::stop_callback` to call `notify_all()` for you. If you must use `std::condition_variable`, you'd need to manually register a callback to `notify_all()`, which is more verbose.

### Pattern 3: Controlling a Group of Threads with stop_source

The previous two patterns are one-to-one—one thread, one stop signal. But in real engineering, one-to-many is common: you have several worker threads and want one button to stop them all. This leverages `std::stop_source`'s ability to derive multiple tokens.

```cpp
void group_control_demo() {
    std::stop_source global_src;
    std::stop_token token = global_src.get_token();

    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([token, i] {
            while (!token.stop_requested()) {
                std::cout << "Thread " << i << " working\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::cout << "Thread " << i << " stopped\n";
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Stopping all threads...\n";
    global_src.request_stop(); // Stop all at once
    // jthreads auto-join
}
```

I intentionally used `std::jthread` here to show that `std::stop_source` and `std::stop_token` can be used completely independently of `std::jthread`—you can even use them without threads to control async task cancellation. In real projects, using `std::stop_source` for one-to-many control is much cleaner than setting individual flags for each thread, avoiding manual synchronization of multiple flags.

## Integrating Stop Tokens into a Thread Pool

The real challenge is ahead—previous patterns were isolated, but in a real thread pool, you need to handle task queues, condition variables, stopping multiple workers, and ensure no deadlocks or lost tasks on destruction. Using `std::jthread` and `std::stop_token` allows us to manage all this elegantly. Let's look at a simplified but complete implementation:

```cpp
#include <thread>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>
#include <functional>

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop_source_(std::nostopstate) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(m_);
                        // Wait with stop_token support
                        if (!cv_.wait(lock, st, [this] {
                            return !tasks_.empty();
                        })) {
                            // Stop requested
                            break;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            }, stop_source_.get_token());
        }
    }

    ~ThreadPool() {
        // 1. Request stop
        stop_source_.request_stop();
        // 2. Wake up everyone waiting
        cv_.notify_all();
        // 3. Join all threads (jthread does this automatically)
    }

    template<typename F>
    void enqueue(F&& f) {
        {
            std::lock_guard lock(m_);
            tasks_.push(std::forward<F>(f));
        }
        cv_.notify_one();
    }

private:
    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex m_;
    std::condition_variable_any cv_;
    std::stop_source stop_source_;
};
```

Let's break down the design.

First, the constructor—we use an independent `std::stop_source` (member `stop_source_`), not the one inside `std::jthread`. We pass the same token to each worker via `stop_source_.get_token()` in the lambda capture. This is necessary because all workers must share the same stop signal—if each `std::jthread` used its own internal token, we'd have to call `request_stop()` on each one individually, which is tedious and error-prone.

Next, the destructor—first call `request_stop()`, then `notify_all()`, and finally let the `std::jthread`s join. You might ask, since `request_stop()` triggers `cv_.wait()` to return, why the extra `notify_all()`? Theoretically, `request_stop()` is enough, but explicit `notify_all()` is clearer intent and ensures we don't rely on specific implementation timing—what if there's a race between `request_stop()` and the last `wait()`? An extra line buys certainty.

Finally, a point of confusion: since the lambda accepts a `std::stop_token` parameter, `std::jthread`'s internal token isn't used here. `std::jthread`'s destructor still does `request_stop()` + `join()`, but its internal token affects its own passed argument (which we ignore). The real control comes from our manual `stop_source_.request_stop()` at the start of the destructor.

## Where We Are

In this article, we started from the pain points of `std::thread`, covered `std::jthread`'s auto-join semantics, the `std::stop_source`/`std::stop_token`/`std::stop_callback` cooperative cancellation mechanism, and finally strung them all together in a thread pool. Looking back, C++20's design is simple—don't force kill threads, signal them to exit gracefully. But behind this simple design, it solves the two biggest headaches from the `std::thread` era: crashing on forgotten join and inability to signal stops.

Next, we will integrate these tools to build a more complete thread pool—with task priorities, dynamic thread counts, and work stealing. With the foundation of `std::jthread` and stop tokens, the rest will be much easier. Correctness first, performance second—this principle never changes.

## Exercises

### Exercise 1: Interruptible Worker with Stop Token

Implement a `Worker` class that runs a background thread printing the current time every 500ms. Use `std::jthread` and `std::stop_token`. When a stop request is received, print "shutting down" and exit. Use `std::stop_callback` to print "cleanup callback executed" on stop. In `main()`, create the worker, run for 3 seconds, then stop it via `request_stop()`. Hint: The callback runs on the thread calling `request_stop()`, so don't do heavy work there.

### Exercise 2: Improve the Thread Pool

Based on the `ThreadPool` code above, make these improvements:

1. On destruction, clear unexecuted tasks in the queue (print discarded task IDs) before stopping workers.
2. Add a `pending_count()` method returning the number of waiting tasks.
3. Use `std::stop_callback` instead of manual `notify_all()`—register a callback in the worker loop to notify the condition variable. Hint: Think about the `std::stop_callback`'s lifetime—it must remain valid for the entire `ThreadPool` duration.

### Exercise 3: Combining Multiple stop_sources

Assume you have two groups of worker threads, each with its own `std::stop_source`. Design a mechanism allowing you to stop one group independently, or stop all simultaneously, with requests being one-way. Hint: Keep individual `std::stop_source`s for each group, plus an extra "global" `std::stop_source`. Workers must check both tokens—exiting if either receives a request. `std::stop_token` has no "combine" operation, so you might need to check `stop_requested()` in the loop condition.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `examples/jthread_demo.cpp`.

## References

- [std::jthread -- cppreference](https://en.cppreference.com/cpp/thread/jthread)
- [std::stop_token -- cppreference](https://en.cppreference.com/cpp/thread/stop_token)
- [std::stop_source -- cppreference](https://en.cppreference.com/cpp/thread/stop_source)
- [std::stop_callback -- cppreference](https://en.cppreference.com/cpp/thread/stop_callback)
- [std::condition_variable_any::wait -- cppreference](https://en.cppreference.com/cpp/thread/condition_variable_any/wait)
- [std::jthread and cooperative cancellation with stop token -- nextptr](https://www.nextptr.com/tutorial/ta1588653702/stdjthread-and-cooperative-cancellation-with-stop-token)
- [Cooperative Interruption of a Thread in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/cooperative-interruption-of-a-thread-in-c20/)
- [Better worker threads with C++23 cooperative thread interruption -- twdev.blog](https://twdev.blog/2023/06/stop_source/)
- [Interrupt Politely -- Herb Sutter](https://www.drdobbs.com/interrupt-politely/225700115)
