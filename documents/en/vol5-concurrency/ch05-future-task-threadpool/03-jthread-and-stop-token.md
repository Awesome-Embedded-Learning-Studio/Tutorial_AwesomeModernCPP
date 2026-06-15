---
chapter: 5
cpp_standard:
- 20
description: 'C++20 auto-joining threads and cooperative cancellation: complete usage
  of `stop_source`, `stop_token`, and `stop_callback`'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- promise 与 packaged_task
reading_time_minutes: 19
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
title: jthread and Stop Tokens
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch05-future-task-threadpool/03-jthread-and-stop-token.md
  source_hash: 4c2a8ac69cbe613ec907a2cd164e203e962daebcb03fd9f3d30c2c4a483da809
  token_count: 3922
  translated_at: '2026-05-20T04:43:14.571815+00:00'
---
# jthread and Stop Tokens

Honestly, when writing the previous few articles, I felt a bit uneasy using `std::thread`. Every time we had to manually `join()`, a moment of carelessness ended in a crash, and stopping a thread midway required rolling our own flag variables—it's 2026, and C++ thread management is still this "primitive." In the last article, we used `std::promise` and `std::future` to build the ability to manually control asynchronous tasks, but the underlying thread tools haven't been upgraded. So, in this article, we are going to address this shortcoming.

Before diving in, a quick note on the environment: all code in this article is based on **C++20** and requires compiler support for the `<stop_token>` header (GCC 10+, Clang 17+ with partial libc++ support, full support in Clang 20, and MSVC 19.28+). If your compiler isn't new enough, upgrade now—there are no downgrade alternatives for the features covered here.

C++20 finally gave us `std::jthread`, an automatically joining thread wrapper, along with a built-in cooperative cancellation mechanism. The core components of this mechanism are three classes: `std::stop_source` (issues stop requests), `std::stop_token` (checks for stop requests), and `std::stop_callback` (registers stop callbacks). They can be used independently without `std::jthread`, but they are most convenient when paired with it. In this article, we will walk through this entire toolkit.

## The Pain Points of std::thread: A Review

Before learning something new, let's look back at the headaches that `std::thread` actually causes. Only by understanding the pain points can we appreciate why C++20 was designed this way.

First, let's look at a typical problem scenario. The following code seems fine at first glance—create a thread, do some work, join, and we're done.

```cpp
#include <thread>

void worker();
void do_more_work();

void unsafe_example()
{
    std::thread t(worker);
    do_more_work();  // 如果这里抛异常...
    t.join();        // 这行不会执行
    // t 析构，线程仍然 joinable -> std::terminate()!
}
```

But what if `do_some_work()` throws an exception? The program's control flow jumps straight to stack unwinding, and when the `std::thread` destructor finds that the thread is still joinable, `std::terminate` ruthlessly kills the entire process. No error message, no room for recovery—just a crash. You might think, "Can't I just add a try-catch?" You can, but you have to do this everywhere you use `std::thread`, and missing even one is a ticking time bomb.

A common fix is to write a manual RAII wrapper that automatically joins in the destructor. We actually did this in the ch01 article. But every project has to write its own version, and the `join()` in the destructor is a blocking call—if the thread is running a long task, your program will hang when the guard is destroyed, with no way to tell the thread "it's time to stop."

These two problems—crashing if you forget to join, and having no way to tell a thread to stop—are exactly what `std::jthread` aims to solve once and for all.

## std::jthread: The Auto-Joining Thread

Now let's look at `std::jthread`. Its "j" stands for joining—the name already tells you its core selling point: it automatically joins on destruction. The usage is almost identical to `std::thread`, so you can basically swap them in without thinking:

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
    // 不需要手动 join —— t 析构时自动 join
    return 0;
}
```

You'll notice that the only difference between this code and using `std::thread` is swapping `std::thread` for `std::jthread` and removing the explicit `join()` line. But if it only auto-joined, there would be no fundamental difference from our hand-written RAII guard—the real killer feature of `std::jthread` lies in its destruction behavior: before joining, it **first calls `request_stop()`**, and only then does it `join()`. The pseudocode looks roughly like this:

```cpp
// std::jthread 析构函数的逻辑（简化）
~jthread()
{
    if (joinable()) {
        request_stop();
        join();
    }
}
```

In other words, `std::jthread` doesn't just dumbly wait for the thread to finish on destruction; it politely notifies the thread "it's time to stop" first, and then waits. If the thread function can respond to this stop request, it can exit gracefully, rather than leaving the caller blocked indefinitely during destruction. This is really important—if you've used Java's `interrupt()` or Go's context cancellation, you'll find that the design philosophy behind C++20's approach is exactly the same: don't forcefully kill, but cooperatively exit.

> **Pitfall Warning**: If you already hand-wrote a `ThreadGuard` or `JoiningThread` RAII wrapper in ch01, please note—those hand-written guards only `join()` on destruction, they don't `request_stop()`. If your thread function has long-blocking operations inside (like `sleep_for`, or condition variable waits), the hand-written guard will cause the destructor to block indefinitely. The `std::jthread` combination of `request_stop()` + `join()` is the correct approach.

## Cooperative Cancellation: stop_source, stop_token, stop_callback

Great, now we know that `std::jthread` automatically `join()`s. But what does "requesting a stop" actually mean? How does the thread know it has been requested? This is the problem that cooperative cancellation solves.

The core idea is actually quite simple: you shouldn't "kill" a thread—because you don't know what state it's in, it might be holding a lock, or it might have half-finished writing data—you should "request" it to stop, and then let the thread decide for itself when to exit at an appropriate time. You can think of it as a signaling mechanism: someone raises a red flag saying "please stop," and the thread glances at the red flag at the start of each loop, exiting gracefully if it's raised. This mechanism consists of three classes that share an internal stop-state. `std::stop_source` is the write end, responsible for issuing stop requests; `std::stop_token` is the read end, responsible for querying the stop state; and `std::stop_callback` can execute a piece of callback code when a stop request is issued.

### std::stop_source and std::stop_token

Let's start with the write and read ends. `std::stop_source` provides `request_stop()` to issue a stop request, and `get_token()` to obtain the associated `std::stop_token`. `std::stop_token` is a read-only observer with only two query methods: `stop_requested()` returns whether a stop request has been received, and `stop_possible()` returns whether there is an associated stop state. A single `std::stop_source` can derive multiple `std::stop_token`s—we'll use this later, and it means you can use the same `std::stop_source` to control the stopping of multiple threads simultaneously.

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
    // request_stop() 可以多次调用，只有第一次返回 true

    return 0;
}
```

This example demonstrates the most basic one-to-one relationship: a `std::stop_source` issues a request, and its associated `std::stop_token` can immediately detect it. It's worth noting that `request_stop()` can be called multiple times, but only the first call returns `true`—subsequent calls are safe but won't trigger callbacks again.

A default-constructed `std::stop_source` isn't associated with any stop state, and `stop_possible()` returns `false`. If you truly don't need stop capability, you can construct an empty `std::stop_token` using `std::stop_token{}`, which won't allocate any internal state and saves a bit of overhead.

### How std::jthread Passes the stop_token

The next question is: how does the internal `std::stop_token` of `std::jthread` communicate with our thread function? The answer is—if your thread function accepts a `std::stop_token` as its first parameter, `std::jthread` will automatically pass its internal token in; if the function doesn't accept a `std::stop_token`, `std::jthread` degrades into a plain auto-joining thread with no cancellation capability. This design is very smart—it's backward compatible; use it if you want, and if you don't, it won't get in the way at all.

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
    // t 析构时：先 request_stop()，再 join()
    return 0;
}
```

You'll notice that in this code we didn't manually call `request_stop()`—when `std::jthread` destructs, it automatically calls `request_stop()` first and then `join()`. `request_stop()` is also a member function of `std::jthread`, which under the hood calls `request_stop()` on its internal `std::stop_source`. You can also get the internal `std::stop_source` via `get_stop_source()` for finer control, such as registering additional callbacks or passing the token to other components.

### std::stop_callback: Registering Stop Callbacks

Just being able to check the stop flag isn't enough—sometimes you want to execute some cleanup operations the instant a stop request is issued, like closing file handles, releasing network connections, or setting a certain flag. That's what `std::stop_callback` is for: its constructor accepts a `std::stop_token` and a callable object, and when the associated `std::stop_source` calls `request_stop()`, the callback is triggered.

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

When you run this code, you'll see output similar to this: first a one-second `working...` loop, then `request_stop()` triggers the callback printing `cleanup callback executed`, and finally the worker thread detects `stop_requested()` and exits the loop.

There are a few details to keep in mind here. First, the callback executes **synchronously** on the thread that called `request_stop()`, not on the worker thread—so never do time-consuming operations in the callback, or you'll block the thread that issued the stop request. Second, if the stop request has already been issued when you register the callback, the callback executes immediately on the registering thread, so it won't be missed. Finally, the destructor of `std::stop_callback` automatically unregisters it, so when the `worker` function ends, the `std::stop_callback` destructs, and you don't need to worry about dangling callbacks.

## Practical Patterns for Cooperative Cancellation

At this point, we've cleared up the API-level details. But APIs are just tools; what really matters is how to use them well in real-world scenarios. Next, we'll look at three common cancellation patterns—ranging from simple to complex—each with its own applicable scenarios.

### Pattern 1: Polling stop_token in a Loop

The simplest pattern is to check `stop_requested()` in the loop condition. If each iteration is short (on the order of milliseconds), checking directly in the `while` condition is sufficient; but if each iteration takes several seconds, you need to insert checkpoints inside the iteration as well, otherwise a stop request might arrive and you'd still have to wait for the current iteration to finish before responding. Let's look at the code:

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

The pure polling pattern has a problem—many worker threads aren't busy-waiting in a loop, but are waiting on a condition variable. In this case, simply polling `stop_requested()` isn't enough, because the thread might be blocked on `wait()` and have no chance to check the stop flag. C++20 added a `wait()` overload to `std::condition_variable_any` that accepts a `std::stop_token`—when a stop request is issued, the wait is automatically woken up, and `wait()` returns `false` to indicate it was woken by the stop signal rather than the predicate being satisfied.

> **Pitfall Warning**: Note that this is `std::condition_variable_any`, not `std::condition_variable`. The Committee only added the `std::stop_token` overload to the former; the latter does not support it. If your existing code is already using `std::condition_variable`, either switch to `std::condition_variable_any`, or use the `std::stop_callback` mentioned later to manually `notify_all()`.

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
                // 返回 false 表示被停止请求唤醒
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

The logic of this code is quite straightforward: the worker thread waits on `cv.wait()`, takes out and executes tasks when they are available, and when a stop request is received, `cv.wait()` returns `false`, and the thread `break`s out to finish processing remaining tasks and then exits. `cv.wait()` internally uses a `std::stop_callback` to help you `notify_all()`—if you must use `std::condition_variable` (not `std::condition_variable_any`), you'll have to manually register a callback to `notify_all()`, which achieves the same effect but makes the code more verbose.

### Pattern 3: Using stop_source to Control a Group of Threads

The previous two patterns are both one-to-one—one thread, one stop signal. But in real-world engineering, one-to-many is more common: you have several worker threads and want a single button to stop all of them at once. This is where the ability of a `std::stop_source` to derive multiple `std::stop_token`s comes in.

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
    source.request_stop();  // 一次调用停止所有三个线程

    p1.join();
    p2.join();
    p3.join();
    return 0;
}
```

Here we deliberately used `std::thread` instead of `std::jthread` to demonstrate that `std::stop_source` and `std::stop_token` can be used completely independently of `std::jthread`—you can even use them to control the cancellation of asynchronous tasks in scenarios without threads. In real projects, using a single `std::stop_source` for one-to-many stop control is much cleaner than giving each thread its own `std::stop_source`, and it avoids the synchronization issues of manually managing multiple flags.

## Integrating Stop Tokens into a Thread Pool

The real challenges lie ahead—the previous three patterns are all independent scenarios, but in a real thread pool, you need to simultaneously handle the task queue, condition variables, and the stopping of multiple worker threads, all while ensuring that destruction doesn't deadlock or lose tasks. Using `std::jthread` and `std::stop_token` allows us to manage all of these things very elegantly. Let's look at a simplified but complete implementation:

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

Let's break down the design ideas behind this code.

First, look at the constructor—we use a separate `std::stop_source` (the member variable `stop_src`), rather than relying on the one inside `std::jthread`. We pass the same token to each worker thread through `stop_src.get_token()` in the lambda capture list. The reason for this is that all worker threads must share the same stop signal—if each `std::jthread` used its own `std::stop_source`, you'd have to call `request_stop()` on them one by one, which is tedious and easy to miss.

Next, look at the destructor—it first calls `stop_src.request_stop()`, then `cv.notify_all()`, and finally `join()`s each thread one by one. You might ask, since `request_stop()` triggers `cv.wait()` to return `false`, why do we need the extra `notify_all()`? Indeed, theoretically `request_stop()` alone would suffice, but explicitly calling `notify_all()` is a clearer expression of intent, and it ensures we don't rely on specific implementation timing—what if there's a race condition between `request_stop()` and `cv.wait()`? Writing one extra line of `notify_all()` in exchange for determinism is worth it.

Finally, a point that's easy to confuse: because the lambda doesn't accept a `std::stop_token` parameter, the internal `std::stop_source` of `std::jthread` isn't used here. The destruction of `std::jthread` will still do `request_stop()` + `join()`, but its internal `std::stop_token` affects its own `std::stop_source`, which is completely unrelated to the token we passed to `cv.wait()`. What actually controls the worker threads' exit is the `stop_src.request_stop()` we manually called at the beginning of the destructor.

## Where We Are

In this article, starting from the pain points of `std::thread`, we walked through the auto-join semantics of `std::jthread`, the cooperative cancellation mechanism of `std::stop_source`/`std::stop_token`/`std::stop_callback`, and finally strung them all together in a thread pool. Looking back, the design philosophy behind C++20's approach is actually quite simple—don't forcefully kill threads, but send them a signal to exit gracefully on their own. But behind this simple design, it solves the two most headache-inducing problems from the `std::thread` era: crashing if you forget to join, and having no way to notify a thread to stop.

In the next article, we will integrate these tools to build a more complete thread pool—with task priorities, dynamic thread counts, and work stealing. With the foundation of `std::jthread` and stop tokens, the subsequent steps will be much smoother. Correctness first, performance second—this principle hasn't changed.

## Exercises

### Exercise 1: Interruptible Worker Thread with Stop Token

Implement an `InterruptibleWorker` class that runs a worker thread internally, printing the current time every 500ms. Requirements: use `std::jthread` and `std::stop_token`; when the thread receives a stop request, it should print "shutting down" and then exit; use `std::stop_callback` to register a callback that prints "cleanup callback executed" when stopped. In `main()`, create the worker, let it run for three seconds, and then stop it via `request_stop()`. Hint: the callback of `std::stop_callback` executes on the thread that called `request_stop()`, so don't do time-consuming operations in the callback.

### Exercise 2: Refactoring the Thread Pool

Based on the `ThreadPool` code above, make the following improvements: clear unexecuted tasks in the queue upon destruction (print the task numbers of the discarded tasks) before stopping the worker threads; add a `pending_task_count()` method that returns the number of tasks currently waiting in the queue; replace the manual `notify_all()` call with a `std::stop_callback`—register a callback before the worker thread's loop starts to notify the condition variable. Hint: think about the lifetime of `std::stop_callback`—it needs to remain valid for the entire duration of the `std::jthread`.

### Exercise 3: Combining Multiple stop_sources

Suppose you have two groups of worker threads, each with its own `std::stop_source`. Design a mechanism that allows you to stop a single group individually, stop all threads simultaneously, and ensures that stop requests are one-way. Hint: you can keep a separate `std::stop_source` for each group, and additionally maintain a "global" `std::stop_source`. Worker threads need to check both tokens simultaneously—exiting when either token receives a stop request. `std::stop_token` itself doesn't have a "combine" operation, so you might need to check `token.stop_requested() || global_token.stop_requested()` in the loop condition.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

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
