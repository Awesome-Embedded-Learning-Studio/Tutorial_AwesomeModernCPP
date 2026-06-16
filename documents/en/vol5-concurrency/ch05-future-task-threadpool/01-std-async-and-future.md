---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Understanding `std::async` launch policies, the blocking semantics of
  `future.get`, and deferred traps
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 线程安全队列
reading_time_minutes: 24
related:
- promise 与 packaged_task
- 线程池设计
tags:
- host
- cpp-modern
- intermediate
- 异步编程
title: std::async and future
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/01-std-async-and-future.md
  source_hash: 997cf478fe14503ffc099c1c2c6bb5d93a6d39d0892c71ec249adc307e251e07
  translated_at: '2026-06-16T04:05:04.365768+00:00'
  engine: anthropic
  token_count: 4361
---
# std::async and future

Writing this chapter, I have to admit, is a bit of a relief. In the previous chapters, we were dealing with low-level primitives like `std::thread`, `std::mutex`, and `std::atomic`, directly manipulating thread creation, synchronization, and even memory ordering. Writing that stuff gets exhausting—you have to manage the thread lifecycle yourself, design synchronization mechanisms, manually move results from worker threads back to the main thread, and worry about how to propagate exceptions or what happens if a thread crashes. Every time you write a concurrent task, you repeat this process. Eventually, you start thinking: isn't there a way to just say "run this task asynchronously and give me the result," and let the system handle the rest?

C++11 does provide this higher-level abstraction, centered on `std::async` and `std::future`. In this chapter, we will thoroughly clarify the launch policies of `std::async` and master the blocking semantics and one-time consumption model of `std::future`, especially the classic deferred trap—if you don't understand the default policy behavior, your code might run fine locally but mysteriously serialize in production under specific loads. I've fallen into this trap myself, so let's break it down step by step.

## std::async: Launching an Asynchronous Task

Our goal now is to start with the most basic usage to understand the fundamental form of `std::async`, and then gradually dive into policy and behavioral details.

`std::async` is a function template that accepts a callable object and a set of arguments, returning a `std::future`—this future acts as your "ticket" to retrieve the task's return value at a later point in time. It has two overloads: one accepts a launch policy, and the other uses the default policy. Let's ignore the policy for a moment and just get it running:

```cpp
#include <future>
#include <iostream>
#include <thread>

int calculate() {
    std::cout << "Working in thread: " << std::this_thread::get_id() << std::endl;
    return 42;
}

int main() {
    // Launch the task, get the future
    std::future<int> fut = std::async(std::launch::async, calculate);

    // Main thread does its own work
    std::cout << "Main thread doing other things..." << std::endl;

    // Get the result (blocks if not ready)
    int result = fut.get();
    std::cout << "Result: " << result << std::endl;
}
```

The first parameter of `std::async` is the launch policy, the second is the callable object to execute, and subsequent arguments are perfectly forwarded to that callable. The return value is a `std::future<T>`—where the template parameter matches the task's return type. If the task returns `int`, you get a `std::future<int>`.

In the code above, `std::launch::async` is an enumeration value meaning "launch this task immediately on a new thread." Once you have the future, the main thread is not blocked and continues on its way until you call `get()`, which waits for the task to complete.

## Two Launch Policies

Great, the basic usage works. Now the question arises—what exactly is the deal with `std::async`'s policy? We explicitly passed `std::launch::async` before, but what if we don't? This hides the first pitfall we need to dissect today.

`std::async` supports two launch policies, specified via the `std::launch` enumeration. `std::launch::async` requires the runtime to create a new thread (or take one from an internal thread pool) immediately upon calling `std::async` and execute the task. If the system temporarily lacks resources to create a thread, the standard requires the implementation to either create the thread or throw `std::system_error`—this is an error condition you need to watch out for. `std::launch::deferred`, on the other hand, is completely different—it creates no new thread. The task is delayed until you call `get()` or `wait()` on the future, executing synchronously on the calling thread. In other words, if you call `get()` on the main thread, the task runs directly on the main thread, essentially no different from a normal function call, just wrapped in an extra layer.

These two policies can be combined using bitwise OR. `std::launch::async | std::launch::deferred` is the default policy—when you don't pass the first argument, `std::async` uses this combination. This means the implementation has the right to choose whether to run asynchronously or deferred; the standard delegates this decision to the standard library implementers.

This sounds flexible, but the problem lies precisely in this "implementation choice." Scott Meyers dedicated Item 36 in *Effective Modern C++* to this pitfall: under the default policy, `std::async` might choose `deferred`, meaning your task might not run on another thread at all. Even worse, the `wait_for()` function of `std::future` returns `std::future_status::deferred` instead of `timeout` or `ready` for deferred tasks—if you write a polling loop using `wait_for` to check if a task is done, and you hit a deferred task, that loop will spin forever.

Let's look at an example that直观ly shows the difference between the two:

```cpp
#include <future>
#include <iostream>
#include <thread>

void compute() {
    std::cout << "[Task] Thread ID: " << std::this_thread::get_id() << std::endl;
}

int main() {
    std::cout << "[Main] Thread ID: " << std::this_thread::get_id() << std::endl;

    // 1. async policy
    auto f1 = std::async(std::launch::async, compute);
    f1.get();

    std::cout << "---" << std::endl;

    // 2. deferred policy
    auto f2 = std::async(std::launch::deferred, compute);
    f2.get(); // Task executes here, in main thread
}
```

Running this code, you will see that in `async` mode, the thread ID printed by `compute` differs from the main thread, while in `deferred` mode, the thread IDs are identical—because the deferred task executes synchronously on the thread that called `get()`.

## std::future\<T\>: Fetching Asynchronous Results

`std::future<T>` is the "one-time result container" provided by the C++ Standard Library. You can think of it as a read-only, single-use pipe: one end (`std::promise`, `std::packaged_task`, or `std::async`) is responsible for putting a value in, and the other end (the `std::future` in your hand) is responsible for taking it out. The design philosophy is very clear—the value can be taken out only once; once taken, the pipe is defunct.

Let's look back at the core operations provided by `future`. `get()` is what you'll use most—it blocks the current thread until the result is ready, then returns the result value; if the task threw an exception, `get()` rethrows that exception (we'll cover exception propagation later). But there is a critical constraint: `get()` can be called only once. After the call, the future becomes invalid, the shared state is released, and any further operation on it is undefined behavior (usually throwing `std::future_error`).

If you just want to wait for the task to finish without needing the value immediately, use `wait()`—pure blocking wait, returns nothing, but guarantees the result is ready upon return. A more common scenario is waiting with a timeout: `wait_for()` accepts a time duration (like 500ms), and `wait_until()` accepts an absolute time point. Both return a `std::future_status` enumeration—`ready` means the result is available, `timeout` means it's not ready after waiting, and `deferred` means the task hasn't even started yet (remember the deferred policy? That's the one). For deferred tasks, `wait_for()` and `wait_until()` return the `deferred` status immediately without actually waiting—a behavior we'll see how tricky it can be later.

There's also a helper function `valid()`, used to check if the future still associates with a shared state. A default-constructed `std::future`'s `valid()` returns `false`, and it also returns `false` after calling `get()`—if you aren't sure whether a future is still usable, calling `valid()` first is a good habit.

Let's string these operations together in a comprehensive example:

```cpp
#include <future>
#include <iostream>
#include <chrono>

int work() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 100;
}

int main() {
    std::future<int> fut = std::async(std::launch::async, work);

    // Polling check every 500ms
    while (true) {
        auto status = fut.wait_for(std::chrono::milliseconds(500));
        if (status == std::future_status::ready) {
            std::cout << "Task completed!" << std::endl;
            break;
        } else {
            std::cout << "Not yet ready..." << std::endl;
        }
    }

    int result = fut.get();
    std::cout << "Result: " << result << std::endl;
    std::cout << "Future valid? " << std::boolalpha << fut.valid() << std::endl;
}
```

This code checks the task status every 500ms. After the task completes, it calls `get()` to retrieve the value. After calling `get()`, `fut.valid()` becomes `false`, indicating the shared state has been released.

## One-Time Consumption Semantics

The design philosophy of `std::future` is "one-time consumption"—the value in the shared state can be retrieved only once. This design is evident at several levels; let's break them down one by one.

Starting with the return semantics of `get()`. `get()` performs move semantics: for `std::future<int>`, `get()` returns a copy of the `int` value (since moving an int is just a copy, it doesn't matter), but for `std::future<std::string>`, the `string` returned by `get()` is moved out of the shared state. Once the value is taken, calling `get()` again is undefined behavior. Notably, the standard library has specializations for `std::future<T&>` (reference type) and `std::future<void`; their `get()` behavior differs slightly—the former returns a reference, while the latter only performs a synchronous wait and returns nothing.

Looking at the future object itself, `std::future` is move-only. You cannot copy a `std::future`, you can only move it—after moving, the original future's `valid()` becomes `false`, and the new future takes over the shared state. This design ensures that only one future can access the shared state at any given time, fundamentally eliminating race conditions where multiple parties fight for the same result. Furthermore, there is no mechanism to "reset" an already consumed future. If you need to read the same result multiple times, you should use `std::shared_future`—which we will cover in the next chapter.

```cpp
std::future<std::string> fut = std::async([] {
    return std::string("Hello");
});

// Move the future
std::future<std::string> fut2 = std::move(fut);
// fut.valid() is now false
// fut2.valid() is true
```

This one-time semantic is not a defect but a design choice. `std::future`'s goal is lightweight, one-time result passing, not a reusable result container. If you need to "broadcast" a result to multiple consumers, C++ provides `std::shared_future` to meet that need—at the cost of extra reference counting overhead.

## The Trap of the deferred Policy

We've already mentioned the basic behavior of the `deferred` policy: the task doesn't execute asynchronously but is delayed until you call `get()` or `wait()`, executing synchronously on the current thread. But this behavior causes far more bugs in actual engineering than you might think—and that's not all, the real trap is yet to come.

> **Pitfall Warning**: `std::async` with the default policy is one of the most insidious concurrency pitfalls I've encountered. Local testing is fine, but in production, you realize all tasks are serial—because the standard library implementation chose the `deferred` policy (under the default policy, the implementation is free to choose either async or deferred, and the standard doesn't specify the selection criteria).

The biggest trap comes from the default policy. When you write `std::async(task)` without specifying a policy, you are using `std::launch::async | std::launch::deferred`. This means the standard library implementation can choose freely. On some implementations (especially under high load), the standard library might heavily favor the `deferred` policy. So you think you are doing parallel computing, but actually, all tasks are executing serially on the main thread—and your tests will never cover the scenario where "the standard library suddenly switches policies."

A particularly dangerous scenario is the "fire-and-forget" pattern—you launch multiple async tasks without immediately calling `get()`, expecting them to finish in parallel in the background. Let's look at this code:

```cpp
#include <future>
#include <iostream>
#include <vector>

void task(int id) {
    std::cout << "Task " << id << " start" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Task " << id << " done" << std::endl;
}

int main() {
    // Expecting 4 tasks to run in parallel (total 1s)
    std::async(std::launch::async, task, 1);
    std::async(std::launch::async, task, 2);
    std::async(std::launch::async, task, 3);
    std::async(std::launch::async, task, 4);

    std::this_thread::sleep_for(std::chrono::seconds(5));
}
```

If the implementation chooses the `deferred` policy, these 4 tasks will execute serially on the main thread, taking 4 seconds total instead of the expected 1 second. Even more insidiously, even if the implementation usually chooses `async`, under certain special conditions (like thread resource exhaustion), it might switch to `deferred`—your tests will never cover this, which is frustrating.

Immediately following is the second trap, related to `wait_for()`. If you write a timeout loop using `wait_for()` to poll a deferred task, the loop will immediately return the `deferred` status instead of `timeout` or `ready`. If you don't handle the `deferred` branch (honestly, many people do ignore it), the loop becomes an infinite loop:

```cpp
// Dangerous polling loop
auto fut = std::async(std::launch::deferred, []{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 42;
});

while (true) {
    auto status = fut.wait_for(std::chrono::milliseconds(100));
    if (status == std::future_status::ready) {
        break; // Never reached for deferred!
    }
    // If status is deferred, we loop forever
}
```

Don't assume this is just an extreme textbook example—I've seen this exact infinite loop in real projects, and it only triggers under specific loads, which is maddening to debug. The correct approach is to check the return value of `wait_for()` first; if it is `deferred`, call `get()` directly or adopt another strategy:

```cpp
auto status = fut.wait_for(std::chrono::milliseconds(100));
if (status == std::future_status::deferred) {
    // Force synchronous execution
    fut.get();
} else if (status == std::future_status::ready) {
    // Result available
    fut.get();
}
```

So my suggestion is simple: **if you truly need asynchronous execution, explicitly specify `std::launch::async`**. The default policy looks flexible—"let the implementation choose for you," how elegant—but this flexibility is almost entirely pitfalls in actual projects. Scott Meyers also suggests in Item 36 of *Effective Modern C++*: if you want to ensure a task is truly executed asynchronously, always explicitly pass `std::launch::async`. It's worth sticking this rule on your monitor.

## Exception Propagation

So far, we've only dealt with scenarios involving normal return values, but in actual engineering, tasks throwing exceptions is common. A major advantage of `std::async` is that it automatically captures exceptions thrown within the task and propagates them to the caller via `std::future`—you don't need to manually design error codes or other error passing mechanisms.

The mechanism works like this: if the task function throws an exception, the exception is caught and stored in the shared state of the `std::future`. When you call `get()`, the stored exception is rethrown. This means you can handle child thread exceptions in the main thread using try-catch, just like handling exceptions from normal function calls.

```cpp
#include <future>
#include <iostream>

int risky_task() {
    throw std::runtime_error("Something went wrong!");
    return 0;
}

int main() {
    std::future<int> fut = std::async(std::launch::async, risky_task);

    try {
        int result = fut.get();
    } catch (const std::runtime_error& e) {
        std::cout << "Caught exception: " << e.what() << std::endl;
    }
}
```

This exception propagation mechanism is equally effective for the `deferred` policy—except that under the `deferred` policy, the exception is thrown synchronously at the call to `get()`, no different from a normal function call throwing an exception.

There is a detail to note here—if you never call `get()`, the exception is silently swallowed. More precisely, if the `std::future` destructs before the task is complete (for the `async` policy), the destructor blocks waiting for the task to finish. If the task threw an exception and you never called `get()`, the exception is released along with the shared state—it won't propagate, won't terminate the program, it's just gone. This is a silent error and very dangerous. Therefore, **you must call `get()` on the future returned from `std::async`**, even if you don't need the return value, just to confirm the task didn't throw an exception.

## Destructor Behavior of std::async Returned Futures

You might have noticed that in the previous examples, we dutifully saved the future object and only called `get()` at the end. But what if you just write a line of `std::async(...)` and don't save the return value? Here we need to specifically mention the destructor behavior of the `std::future` returned by `std::async`, because it differs from a normal `std::future`.

When you obtain a `std::future` through other means (like `std::promise::get_future()`), the future's destruction merely releases the reference to the shared state—if the promise hasn't set a value yet, the future just destructs without waiting for anything.

But the `std::future` returned by `std::async` is special: if the task was launched via `std::launch::async` (or the default policy where async is chosen), and this is the last future referencing that shared state, the destructor blocks until the task is complete. This is behavior explicitly required by the standard ([futures.async]), designed to prevent the task from becoming an orphan thread if you discard the future while it's still running.

This means the following code is actually serial:

```cpp
// Serial execution, NOT parallel!
std::async(std::launch::async, []{ std::this_thread::sleep_for(std::chrono::seconds(1)); });
std::async(std::launch::async, []{ std::this_thread::sleep_for(std::chrono::seconds(1)); });
std::async(std::launch::async, []{ std::this_thread::sleep_for(std::chrono::seconds(1)); });
```

Each temporary `std::future` object returned by `std::async` is destructed at the end of the statement, and the destruction blocks until the task is complete. So although you wrote three lines of `std::async`, the actual execution is strictly serial. To achieve true parallelism, you need to store the futures in a container and collect them sequentially after all are launched:

```cpp
std::vector<std::future<void>> futs;
futs.push_back(std::async(std::launch::async, []{ /* ... */ }));
futs.push_back(std::async(std::launch::async, []{ /* ... */ }));
futs.push_back(std::async(std::launch::async, []{ /* ... */ }));

// Wait for all
for (auto& f : futs) {
    f.get();
}
```

This destructor behavior is a "feature" of `std::async` that often trips up newcomers. You must keep this in mind: the destructor of the future returned by `std::async` will block—if you casually ignore the return value, your "parallel" code becomes serial.

## std::future vs std::thread: How to Choose?

At this point, we can compare `std::async`/`std::future` with `std::thread` and clarify the selection strategy.

When using `std::thread` to execute asynchronous tasks, you need to design the result passing mechanism yourself—using shared variables with mutexes, global variables with atomics, or condition variables. Exception handling is also entirely your responsibility—exceptions thrown in child threads won't automatically propagate back to the main thread; you must catch them manually and pass them through some mechanism. Thread management is also manual: you must choose between `join()` or `detach()`, forgetting triggers `std::terminate`.

Using `std::async` is much more worry-free: return values are passed automatically via `std::future`, exceptions propagate automatically, and the future's destructor waits for task completion (no orphan threads). The cost is you lose fine-grained control over the thread—you can't set thread priority, affinity, or name, and you don't even know which thread the task is running on.

So the logic for selection is actually quite clear. If you are running a computational task with clear inputs and outputs, tasks are relatively independent, you need exception propagation, and you don't care which thread runs the task—typical examples include parallel data processing, parallel file I/O, or offloading a time-consuming calculation from the main thread—use `std::async`. `std::async` is suited for "throw a task out, get a result back" scenarios. However, `std::async` is not suitable for scenarios requiring frequent thread creation and destruction—each `std::async` might create a new thread, which carries significant system overhead.

If you need a persistent background worker thread—background listener threads, event loops, or cases requiring thread attributes (priority, affinity, etc.)—use `std::thread`, but you need to handle all synchronization and error passing yourself, which results in significantly more code.

If you need to run a large number of short tasks, that is the domain of thread pools. A thread pool pre-creates a set of worker threads, and tasks are submitted to a queue to be executed by worker threads. This avoids the overhead of frequent thread creation and destruction and allows you to control concurrency (max threads, queue size, etc.). The C++ Standard Library currently does not provide a thread pool, so you need to implement one yourself or use a third-party library—we will cover the design and implementation of thread pools in detail in later chapters.

## Exercise: Parallel Computation with std::async

### Exercise 1: Parallel Summation

Given a `std::vector<int>` containing 10 million random integers, use `std::async` to split it into 4 segments for parallel summation, and finally aggregate the results. Compare the time taken by the single-threaded version and the multi-threaded version.

```cpp
#include <algorithm>
#include <async>
#include <chrono>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>
#include <future>

int main() {
    std::vector<int> data(10'000'000);
    std::generate(data.begin(), data.end(), std::rand);

    // Single-threaded baseline
    auto start = std::chrono::high_resolution_clock::now();
    long long sum_single = std::accumulate(data.begin(), data.end(), 0LL);
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Single thread: " << sum_single
              << ", Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms\n";

    // Multi-threaded
    start = std::chrono::high_resolution_clock::now();
    size_t chunk = data.size() / 4;

    // Use std::ref to pass read-only reference
    std::future<long long> f1 = std::async(std::launch::async, [&data, chunk] {
        return std::accumulate(data.begin(), data.begin() + chunk, 0LL);
    });

    std::future<long long> f2 = std::async(std::launch::async, [&data, chunk] {
        return std::accumulate(data.begin() + chunk, data.begin() + 2 * chunk, 0LL);
    });

    std::future<long long> f3 = std::async(std::launch::async, [&data, chunk] {
        return std::accumulate(data.begin() + 2 * chunk, data.begin() + 3 * chunk, 0LL);
    });

    std::future<long long> f4 = std::async(std::launch::async, [&data, chunk] {
        return std::accumulate(data.begin() + 3 * chunk, data.end(), 0LL);
    });

    long long sum_multi = f1.get() + f2.get() + f3.get() + f4.get();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Multi thread: " << sum_multi
              << ", Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms\n";
}
```

Note that we use `std::ref` (or a lambda capture by reference) to pass a read-only reference to the data—because `std::async`'s parameters are passed by value by default. Without `std::ref`, the entire vector would be copied, wasting both memory and time. `std::reference_wrapper` (via `std::ref`) allows passing by reference without copying when the parameter expects by value.

### Exercise 2: Verify the deferred Trap

Modify the code from Exercise 1 to run using `std::launch::async`, `std::launch::deferred`, and the default policy respectively. Compare the time taken by all three. Observe whether the time taken by the `deferred` version is close to the single-threaded version.

### Exercise 3: Exception Propagation Verification

Write a `std::async` task that throws a custom exception. Catch it in the main thread using try-catch and verify that the exception type and message content match.

## Summary

At this point, we have thoroughly walked through the core mechanisms of `std::async` and `std::future`. `std::async` provides a higher-level way to launch asynchronous tasks than `std::thread`, automatically handling return value passing and exception propagation, which saves a lot of worry. `std::future` is the standard channel for retrieving asynchronous results. Operations like `get()`, `wait()`, and `wait_for()` have straightforward names, but the semantics behind them (especially the one-time consumption of `get` and the behavior of `wait_for` with the `deferred` status) need to be kept in mind.

Let me reiterate a few key points: the default launch policy (`std::launch::async | std::launch::deferred`) is a trap to be wary of; the implementation might choose the `deferred` policy causing tasks to execute serially. `wait_for()` returns the `deferred` status immediately for deferred tasks; a polling loop that doesn't handle this branch becomes an infinite loop. The destructor of the future returned by `std::async` blocks until the task is complete; casually ignoring the return value turns your parallel code into serial code. If you need true asynchronous execution, explicitly pass `std::launch::async`—this rule is worth sticking on your monitor.

In the next chapter, we will look at `std::promise` and `std::packaged_task`—they are the "other end" of `std::future`, allowing you more flexible control over value setting and task encapsulation. Once you understand the semantics on the future side, understanding the promise side will follow naturally.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `examples/future_async`.

## References

- [std::async — cppreference](https://en.cppreference.com/w/cpp/thread/async)
- [std::future — cppreference](https://en.cppreference.com/w/cpp/thread/future)
- [std::launch — cppreference](https://en.cppreference.com/w/cpp/thread/launch)
- [Effective Modern C++, Item 35, 36 — Scott Meyers](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [Async Tasks in C++11: Not Quite There Yet — Bartosz Milewski](https://bartoszmilewski.com/2011/10/10/async-tasks-in-c11-not-quite-there-yet/)
- [The Promises and Challenges of std::async — DZone](https://dzone.com/articles/the-promises-and-challenges-of-stdasync-task-based)
