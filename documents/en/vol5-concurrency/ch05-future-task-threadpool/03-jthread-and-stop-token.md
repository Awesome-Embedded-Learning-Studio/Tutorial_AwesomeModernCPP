---
chapter: 5
cpp_standard:
- 20
description: 'Auto-joining threads and cooperative cancellation in C++20: complete usage of stop_source, stop_token, and stop_callback'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- promise and packaged_task
reading_time_minutes: 18
related:
- Thread Ownership and RAII
- Thread Pool Design
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- RAII守卫
- 进阶
title: jthread and Stop Tokens
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/03-jthread-and-stop-token.md
  source_hash: 80b83f3c158dae7cdfbea2c654447ed483f043668425a77bf856b79280894f26
  translated_at: '2026-09-26T08:29:12+00:00'
  engine: anthropic
  token_count: 9000
---
# jthread and Stop Tokens

To be honest, we felt a little guilty using `std::thread` all through the earlier articles. You have to call `join()` manually, one lapse in attention and the program ends in `std::terminate()`, and stopping a thread mid-flight means rolling your own `std::atomic<bool>` flag—it's 2026, and C++ thread management still feels this "primitive". In the previous article we built manual control over asynchronous tasks with `std::promise` and `std::packaged_task`, but the underlying thread tools never got an upgrade, so this article is where we patch that shortcoming.

Before rushing in, a word about the environment: all code in this article is based on **C++20** and requires compiler support for the `<stop_token>` header (GCC 10+, Clang 17+ (libc++ partially supported, fully supported since Clang 20), and MSVC 19.28+ all work). If your compiler is older than that, go upgrade now—there is no downgrade-compatible substitute for what this article covers.

C++20 finally hands us `std::jthread`, a thread wrapper that joins automatically, together with a built-in cooperative cancellation mechanism. The core of that mechanism is three classes: `std::stop_source` (issues stop requests), `std::stop_token` (checks for stop requests), and `std::stop_callback` (registers stop callbacks). They can be used independently, without `std::jthread`, but they pair most conveniently with it. This article walks through the whole toolkit.

## The Pain Points of std::thread: A Review

Before diving into the new stuff, let's look back at what exactly makes `std::thread` such a headache. Only when we understand the pain points does the C++20 design start to make sense.

First, a typical problem scenario. At first glance the code below has nothing wrong with it—create a thread, do some work, join, done.

```cpp
#include <thread>

void worker();
void do_more_work();

void unsafe_example()
{
    std::thread t(worker);
    do_more_work();  // If this throws...
    t.join();        // This line never runs
    // t destructs, thread still joinable -> std::terminate()!
}
```

But what if `do_more_work()` throws? Control flow jumps straight into stack unwinding; when `t` is destroyed the thread is still joinable, so `std::terminate()` unceremoniously takes down the whole process. No error message, no room for recovery—just a crash. You might think, "I'll just add a try-catch, problem solved." That works, but you would have to do it at every single place that uses `std::thread`, and one omission is a ticking time bomb.

A common fix is a hand-written RAII wrapper that joins automatically in its destructor—we actually did exactly that in the ch01 article. But every project has to write its own copy, and that destructor `join()` is a blocking call: if the thread is running a long task, destroying the guard stalls the whole program right there, and there is still no way to notify the thread that it is "time to stop".

These two problems—blowing up when you forget to join, and having no way to tell a thread to stop—are exactly what `std::jthread` solves in one stroke.

## std::jthread: The Auto-Joining Thread

Alright, now let's meet `std::jthread`. The `j` stands for joining—the name already advertises its headline feature: it joins automatically on destruction. Usage is nearly identical to `std::thread`; you can swap it in almost without thinking:

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void worker()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "worker done\n";
}

int main()
{
    std::jthread t(worker);
    // No manual join needed — t joins automatically when destroyed
    return 0;
}
```

As you can see, the only differences from the `std::thread` version are replacing `std::thread` with `std::jthread` and deleting the `t.join()` line. But auto-join alone would be essentially the same as our hand-written RAII guard—`std::jthread`'s real killer move is in its destructor behavior: before joining, it **first calls `request_stop()`**, and only then `join()`. The pseudocode looks roughly like this:

```cpp
// Logic of std::jthread's destructor (simplified)
~jthread()
{
    if (joinable()) {
        request_stop();
        join();
    }
}
```

In other words, `std::jthread` doesn't just sit there at destruction time waiting for the thread to end—it first politely notifies the thread that it is "time to stop", and then waits. If the thread function can respond to that stop request, it can exit gracefully instead of blocking the caller indefinitely in the destructor. This point really matters: if you have used Java's `Thread.interrupt()` or Go's `context.Cancel()`, you will find that the C++20 design follows exactly the same line of thinking—no forced killing, but cooperative exit.

> **Pitfall Warning**: If you already hand-wrote an RAII wrapper like `thread_guard` or `joining_thread` back in ch01, note this—those hand-written guards only `join()` at destruction; they never `request_stop()`. If your thread function contains long blocking operations (a `sleep`, a condition variable wait), a hand-written guard makes the destructor block indefinitely. The `request_stop()` + `join()` combination in `std::jthread` is the correct approach.

## Cooperative Cancellation: stop_source, stop_token, stop_callback

Good—now we know `std::jthread` calls `request_stop()` automatically. But what does "requesting a stop" actually mean? How does the thread find out it has been asked? That is the problem cooperative cancellation solves.

The core idea is quite plain: you should not "kill" a thread—because you don't know what state it is in; it might hold a lock, or be halfway through writing data. Instead, you should "request" it to stop, and let the thread itself decide to exit at a suitable moment. You can think of it as a signaling mechanism: someone raises a red flag saying "please stop"; the thread glances at the flag at the top of each loop iteration, and if the flag is up, it exits gracefully. The mechanism consists of three classes that share an internal stop-state. `std::stop_source` is the writing side, responsible for issuing stop requests; `std::stop_token` is the reading side, responsible for querying the stop state; `std::stop_callback` can run a piece of callback code when a stop request is issued.

### std::stop_source and std::stop_token

Let's start with the writing and reading sides. `std::stop_source` offers `request_stop()` for issuing a stop request and `get_token()` for obtaining an associated `std::stop_token`. `std::stop_token` is a read-only observer with just two query methods: `stop_requested()` returns whether a stop request has been received, and `stop_possible()` returns whether there is an associated stop state. One `stop_source` can derive multiple `stop_token`s—this comes in handy later, because it means a single `stop_source` can control the stopping of several threads at once.

```cpp
#include <stop_token>
#include <iostream>

int main()
{
    std::stop_source source;
    std::stop_token token = source.get_token();

    std::cout << source.stop_requested() << "\n";  // 0
    std::cout << token.stop_requested() << "\n";   // 0

    source.request_stop();

    std::cout << source.stop_requested() << "\n";  // 1
    std::cout << token.stop_requested() << "\n";   // 1
    // request_stop() may be called repeatedly; only the first call returns true

    return 0;
}
```

This example shows the most basic one-to-one relationship: one `stop_source` issues the request, and its associated `stop_token` sees it immediately. Note that `request_stop()` can be called repeatedly—only the first call returns `true`; subsequent calls are safe but never trigger the callbacks again.

A default-constructed `std::stop_token` has no associated stop state, and `stop_possible()` returns `false`. If you truly don't need stopping capability, you can construct an empty `std::stop_source` with `std::nostopstate`—it allocates no internal state, saving a bit of overhead.

### How std::jthread Passes the stop_token

The next question: how does the `stop_source` inside `std::jthread` communicate with our thread function? The answer: if your thread function accepts a `std::stop_token` as its first parameter, `std::jthread` automatically passes its internal token in; if the function does not accept a `stop_token`, `std::jthread` degrades into an ordinary auto-joining thread with no cancellation ability whatsoever. The design is clever—backward compatible, opt-in if you want it, and completely unobtrusive if you don't.

```cpp
#include <thread>
#include <stop_token>
#include <iostream>
#include <chrono>

void cancellable_worker(std::stop_token token)
{
    while (!token.stop_requested()) {
        std::cout << "working...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "worker: stop requested, exiting\n";
}

int main()
{
    std::jthread t(cancellable_worker);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    t.request_stop();
    // When t is destroyed: request_stop() first, then join()
    return 0;
}
```

Notice that we never call `join()` manually in this code—when `t` is destroyed, it automatically does `request_stop()` and then `join()`. `request_stop()` is also a member function of `std::jthread`; underneath, it calls `request_stop()` on the internal `stop_source`. You can also obtain the internal `stop_source` through `t.get_stop_source()` for finer control—for example, registering extra callbacks or passing the token to other components.

### std::stop_callback: Registering a Stop Callback

Just being able to check a stop flag is not enough—sometimes you want some cleanup to run the instant a stop request is issued: close file handles, release network connections, set some flag. That is exactly what `std::stop_callback` is for: its constructor takes a `std::stop_token` and a callable object, and when the associated `stop_source` calls `request_stop()`, the callback is fired.

```cpp
#include <stop_token>
#include <iostream>
#include <thread>
#include <chrono>

void worker(std::stop_token token)
{
    int counter = 0;
    std::stop_callback cb(token, [&counter]() {
        std::cout << "stop callback fired! counter was: "
                  << counter << "\n";
    });

    while (!token.stop_requested()) {
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "worker exiting\n";
}

int main()
{
    std::jthread t(worker);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();
    return 0;
}
```

When you run this code, you will see output along these lines: roughly one second of the `working...` loop, then `request_stop()` fires the callback which prints `stop callback fired!`, and finally the worker thread notices `stop_requested()` and leaves the loop.

A few details here deserve attention. First, the callback executes **synchronously on the thread that calls `request_stop()`**, not on the worker thread—so never do anything time-consuming inside the callback, or you will block whichever thread issued the stop request. Second, if the stop request was already issued before you registered the callback, the callback executes immediately on the registering thread—it is never missed. Finally, `std::stop_callback`'s destructor unregisters automatically, so when the `worker` function ends, `cb` is destroyed—no dangling callbacks to worry about.

## Practical Patterns for Cooperative Cancellation

At this point we have the API-level machinery sorted out. But an API is just a tool; what really matters is how to use it well in real scenarios. Next we look at three common cancellation patterns—from simple to complex, each with scenarios where it fits.

### Pattern 1: Polling stop_token in a Loop

The simplest pattern is checking the `stop_token` in the loop condition. If each iteration is short (on the order of milliseconds), checking in the `while` condition is enough; but if a single iteration runs for several seconds, you need to insert checkpoints inside the iteration as well, otherwise a stop request has to wait for the current iteration to finish before it can be answered. Here's the code:

```cpp
void polling_worker(std::stop_token token)
{
    int iteration = 0;
    while (!token.stop_requested()) {
        process_batch(iteration);
        ++iteration;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "processed " << iteration << " batches\n";
}
```

### Pattern 2: condition_variable + stop_token

Pure polling has a problem: many worker threads don't busy-wait in a loop—they wait on a condition variable. Plain `stop_token` polling falls short there, because the thread may be blocked in `cv.wait()` and never gets a chance to check the stop flag. C++20 added a `wait` overload to `std::condition_variable_any` that accepts a `std::stop_token`—when a stop request is issued, the wait is woken automatically, and `wait` returns `false` to indicate it was woken by the stop signal rather than by the predicate being satisfied.

> **Pitfall Warning**: Note that it is `condition_variable_any`, not `condition_variable`. The standards committee only added the `stop_token` overload to the former; the latter doesn't support it. If the code you're working with already uses `condition_variable`, either switch to `condition_variable_any`, or use `stop_callback` as mentioned later to `notify` manually.

```cpp
#include <thread>
#include <stop_token>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <iostream>
#include <chrono>

class TaskWorker
{
public:
    TaskWorker()
        : thread_([this](std::stop_token token) { run(token); })
    {}

    void submit(int task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(task);
        }
        cv_.notify_one();
    }

private:
    void run(std::stop_token token)
    {
        while (!token.stop_requested()) {
            int task = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // Returning false means we were woken by the stop request
                if (!cv_.wait(lock, token,
                              [this] { return !tasks_.empty(); })) {
                    drain_queue();
                    break;
                }
                task = tasks_.front();
                tasks_.pop();
            }
            std::cout << "processing task: " << task << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    void drain_queue()
    {
        while (!tasks_.empty()) {
            int task = tasks_.front();
            tasks_.pop();
            std::cout << "draining task: " << task << "\n";
        }
    }

    std::mutex mutex_;
    std::queue<int> tasks_;
    std::condition_variable_any cv_;
    std::jthread thread_;
};
```

The logic here is really quite straightforward: the worker waits on `cv_.wait(lock, token, predicate)`, takes out and executes a task when one arrives, and when a stop request comes in, `wait` returns `false`, the thread calls `drain_queue()` to finish the remaining tasks, and then exits. Internally, `condition_variable_any` simply uses `stop_callback` to do the `notify` for you—if you must use `condition_variable` (not `_any`), you have to manually register a callback to call `notify_all()`, which achieves the same effect but makes the code wordier.

### Pattern 3: Controlling a Group of Threads with stop_source

The first two patterns are both one-to-one—one thread, one stop signal. But in real projects, one-to-many is more common: you have several worker threads and want a single button to stop them all at once. This is where the ability of a `stop_source` to derive multiple `stop_token`s comes in.

```cpp
#include <stop_token>
#include <thread>
#include <iostream>
#include <chrono>

void data_processor(std::stop_token token, int id)
{
    while (!token.stop_requested()) {
        std::cout << "processor " << id << " working\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::cout << "processor " << id << " stopped\n";
}

int main()
{
    std::stop_source source;
    std::thread p1(data_processor, source.get_token(), 1);
    std::thread p2(data_processor, source.get_token(), 2);
    std::thread p3(data_processor, source.get_token(), 3);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    source.request_stop();  // One call stops all three threads

    p1.join();
    p2.join();
    p3.join();
    return 0;
}
```

We deliberately used `std::thread` instead of `std::jthread` here, to demonstrate that `stop_source` and `stop_token` can be used completely independently of `std::jthread`—you can even use them to control the cancellation of asynchronous tasks in settings with no threads at all. In real projects, using one `std::stop_source` for one-to-many stop control is much cleaner than giving each thread its own `std::atomic<bool>`, and it avoids the synchronization headaches of manually managing multiple flags.

## Integrating Stop Tokens into a Thread Pool

The real pitfalls come later—the three patterns above are isolated scenarios, but in a real thread pool you have to handle the task queue, the condition variable, and the stopping of multiple worker threads all at once, while also ensuring that destruction neither deadlocks nor loses tasks. With `stop_source` and `stop_token`, all of this can be managed in a very elegant, unified way. Let's look at a simplified but complete implementation:

```cpp
#include <thread>
#include <stop_token>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <functional>
#include <vector>
#include <iostream>

class SimpleThreadPool
{
public:
    explicit SimpleThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back(
                [this, token = stop_source_.get_token()]() {
                    worker_loop(token);
                });
        }
    }

    ~SimpleThreadPool()
    {
        stop_source_.request_stop();
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    void submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_loop(std::stop_token token)
    {
        while (!token.stop_requested()) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!cv_.wait(lock, token,
                              [this] { return !tasks_.empty(); })) {
                    break;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
    std::condition_variable_any cv_;
    std::stop_source stop_source_;
    std::vector<std::jthread> workers_;
};
```

Let's break down the design of this code.

First, the constructor—we use a standalone `std::stop_source` (the member variable `stop_source_`) rather than relying on the one inside `std::jthread`. In the lambda capture list, `token = stop_source_.get_token()` passes the same token to every worker thread. The reason is that all worker threads must share the same stop signal—if each `jthread` used its own `stop_source`, you would have to call `request_stop()` on them one by one, which is tedious and easy to miss.

Next, the destructor—first call `stop_source_.request_stop()`, then `cv_.notify_all()`, and finally `join()` each worker. You might ask: since `request_stop()` already makes `condition_variable_any`'s `wait` return, why the extra `notify_all()`? True, in theory `request_stop()` alone is enough, but an explicit `notify_all()` expresses the intent more clearly and ensures we don't depend on implementation-specific timing—what if there is a race between `request_stop()` and `wait`? One extra line of `notify_all()` in exchange for determinism—worth it.

Finally, a point that is easy to confuse: because the lambda does not accept a `std::stop_token` parameter, the internal `stop_source` of `std::jthread` is never used here. The `jthread` destructor still does `request_stop()` + `join()`, but its internal `request_stop()` affects the `jthread`'s own `stop_source`, which has nothing to do with the token we passed to `worker_loop`. What actually controls the workers' exit is the manual `stop_source_.request_stop()` at the beginning of the thread pool's destructor.

## Where We Are

In this article we started from the pain points of `std::thread`, walked through `std::jthread`'s auto-join semantics and the `stop_source`/`stop_token`/`stop_callback` cooperative cancellation mechanism, and finally strung them all together in a thread pool. Looking back, the C++20 design is really quite simple—don't forcibly kill a thread; send it a signal and let it exit gracefully on its own. But behind that simple design, it solves the two problems that pained us most in the `std::thread` era: blowing up when you forget to join, and having no way to notify a thread to stop.

In the next article we will integrate these tools and build a more complete thread pool—with task priorities, dynamic thread counts, and work stealing. With `jthread` and stop tokens as a foundation, what comes next will go much more smoothly. Correctness first, then performance—that principle has not changed.

## Exercises

### Exercise 1: An Interruptible Worker with a Stop Token

Implement an `InterruptibleWorker` class that runs a worker thread internally and prints the current time every 500 ms. Requirements: use `std::jthread` and `std::stop_token`; the thread prints "shutting down" after receiving a stop request and then exits; register a callback with `std::stop_callback` that prints "cleanup callback executed" when the stop happens. In `main()`, create the worker and stop it via `request_stop()` after running for 3 seconds. Hint: the `std::stop_callback` callback executes on the thread that calls `request_stop()`—do not do anything time-consuming inside the callback.

### Exercise 2: Reworking the Thread Pool

Based on the `SimpleThreadPool` code above, make the following improvements: on destruction, first clear out the unexecuted tasks in the queue (printing the number of each discarded task), and only then stop the worker threads; add a `size()` method that returns how many tasks are currently waiting in the queue; replace the manual `notify_all` call with `std::stop_callback`—register a callback before the worker thread's loop begins that notifies the condition variable. Hint: think about the lifetime of the `std::stop_callback`—it needs to stay valid for the entire duration of `worker_loop`.

### Exercise 3: Combining Multiple stop_sources

Suppose you have two groups of worker threads, each group with its own `std::stop_source`. Design a mechanism such that any single group can be stopped individually, all threads can be stopped at once, and stop requests are one-way. Hint: you can keep a separate `std::stop_source` for each group, and additionally maintain one "global" `std::stop_source`. Worker threads need to check both tokens—exiting when either token receives a stop request. `std::stop_token` itself has no "combine" operation, so you may need to check `token_a.stop_requested() || token_b.stop_requested()` in the loop condition.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

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
