---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the launch policies of std::async, the blocking semantics
  of future.get, and the deferred trap
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Thread-Safe Queue
reading_time_minutes: 24
related:
- promise and packaged_task
- Thread Pool Design
tags:
- host
- cpp-modern
- intermediate
- 异步编程
title: std::async and future
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/01-std-async-and-future.md
  source_hash: 997cf478fe14503ffc099c1c2c6bb5d93a6d39d0892c71ec249adc307e251e07
  translated_at: '2026-09-26T08:33:16+00:00'
  engine: anthropic
  token_count: 12800
---
# std::async and future

Honestly, getting to this article feels like a chance to catch our breath. In the previous chapters we have been dealing with low-level primitives—`std::thread`, `std::mutex`, `std::atomic`—directly manipulating thread creation, synchronization, even memory ordering. Writing this stuff for long enough genuinely wears you out: you manage the thread lifecycle yourself, design the synchronization machinery yourself, haul the result from the worker thread back to the main thread yourself, and on top of that worry about how exceptions travel back and what happens if a thread dies. Every concurrent task repeats this ritual, and after enough rounds you start to wonder: is there a way to just say "run this task asynchronously and bring me the result"—and otherwise leave me alone?

C++11 does provide exactly this higher-level abstraction, and its core is `std::async` and `std::future`. In this article we will nail down the launch policies of `std::async` and fully digest the blocking semantics and one-time consumption model of `std::future`—above all, the classic deferred trap. If you are unclear about what the default policy does, your code may run perfectly locally and then, once deployed, mysteriously serialize under specific loads. We have stepped in this pit ourselves, so let's take it apart step by step.

## std::async: Launching an Asynchronous Task

What we are going to do is start from the most basic usage, get a feel for the fundamental shape of `std::async`, and then gradually dig deeper into policies and behavioral details.

`std::async` is a function template: it takes a callable object and a set of arguments and returns a `std::future`—that future is your "claim ticket" for picking up the task's return value at some point in the future. There are two overloads: one takes a launch policy, the other uses the default policy. Never mind the policy for now; let's just get something running:

```cpp
#include <future>
#include <iostream>
#include <chrono>

int heavy_computation(int x)
{
    // Simulate a time-consuming computation
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return x * x;
}

int main()
{
    // Launch the task asynchronously
    std::future<int> result = std::async(std::launch::async, heavy_computation, 42);

    std::cout << "任务已提交，主线程继续干活...\n";

    // Here the main thread can do other things

    int value = result.get();  // Blocking wait for the result
    std::cout << "计算结果: " << value << "\n";
    return 0;
}
```

The first argument to `std::async` is the launch policy, the second is the callable to execute, and the remaining arguments are perfectly forwarded to that callable. The return value is a `std::future<int>`—the template parameter is the task's return type. If the task returns `void`, you get a `std::future<void>`.

In the code above, `std::launch::async` is an enumeration value meaning "launch this task immediately on a new thread." Once you hold the future, the main thread is not blocked—it goes on doing whatever it was doing—and only waits for the task when you call `result.get()`.

## Two Launch Policies

Good, the basic usage works. Now the question arises—what exactly is the deal with `std::async`'s policy? So far we have been passing `std::launch::async` explicitly, but what if we don't? Hiding right there is the first pit we want to crack open today.

`std::async` supports two launch policies, specified through the `std::launch` enumeration. `std::launch::async` requires the runtime to create a new thread (or take one from an internal thread pool) at the moment `std::async` is called and execute the task immediately. If the system temporarily has no resources to create a thread, the standard requires the implementation to either create the thread and run, or throw `std::system_error`—an error condition you should keep in mind. `std::launch::deferred` is completely different: it creates no new thread at all. The task is postponed until you call `get()` or `wait()` on the future, and it then executes synchronously on the calling thread. In other words, if you call `get()` on the main thread, the task simply runs on the main thread—no essential difference from an ordinary function call, just with an extra layer of wrapping.

The two policies can be combined with bitwise OR. `std::launch::async | std::launch::deferred` is precisely the default policy—when you don't pass the first argument, this combination is what `std::async` uses. That means the implementation is entitled to choose asynchronous or deferred on its own; the standard hands the decision to the standard library implementers.

That sounds flexible, but the problem lies exactly in this "implementation decides for itself." Scott Meyers devotes Item 36 of *Effective Modern C++* to this trap: under the default policy, `std::async` may choose deferred, which means your task may not be running on another thread at all. Even worse, when `wait_for()` on a `std::future` faces a deferred task, it returns `std::future_status::deferred` rather than `timeout`—if you write a polling loop that uses `wait_for()` to check whether the task is done and it hits a deferred task, that loop will wait forever.

Let's look at an example that shows the difference between the two directly:

```cpp
#include <future>
#include <iostream>
#include <chrono>
#include <thread>

int compute(int x)
{
    std::cout << "  [compute] 在线程 "
              << std::this_thread::get_id() << " 上执行\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return x * 2;
}

void test_launch_policy()
{
    auto main_id = std::this_thread::get_id();
    std::cout << "主线程 ID: " << main_id << "\n\n";

    // Policy 1: async — forced execution on a new thread
    std::cout << "--- std::launch::async ---\n";
    auto f1 = std::async(std::launch::async, compute, 10);
    std::cout << "  [main] future 已创建，任务已在新线程启动\n";
    std::cout << "  [main] 结果: " << f1.get() << "\n\n";

    // Policy 2: deferred — postponed until get(), executed on the calling thread
    std::cout << "--- std::launch::deferred ---\n";
    auto f2 = std::async(std::launch::deferred, compute, 20);
    std::cout << "  [main] future 已创建，任务尚未启动\n";
    std::cout << "  [main] 现在调用 get()...\n";
    std::cout << "  [main] 结果: " << f2.get() << "\n";
}

int main()
{
    test_launch_policy();
    return 0;
}
```

Run this code and you will see that in async mode the thread ID printed by `compute` differs from the main thread's, while in deferred mode the two thread IDs are identical—because the deferred task executes synchronously on the thread that calls `get()`.

## std::future\<T\>: Fetching Asynchronous Results

`std::future<T>` is the "one-time result container" provided by the C++ standard library. You can picture it as a read-only, single-use pipe: one end (`std::async`, `std::promise`, or `std::packaged_task`) is responsible for pushing a value in, and the other end (the `std::future` in your hand) is responsible for taking the value out. The design philosophy of this pipe is crystal clear—the value can be taken away only once, and once taken, the pipe is dead.

Let's walk back through the core operations a future provides. `get()` is the one you will use most: it blocks the current thread until the result is ready, then returns the result value; if the task threw an exception, `get()` rethrows that exception (we will cover the exception propagation mechanism in its own section later). But here is a key constraint: `get()` can be called only once. After the call the future is spent, the shared state is released, and any further operation on it is undefined behavior (typically throwing `std::future_error`).

If you just want to wait for the task to finish without grabbing the value yet, use `wait()`—a pure blocking wait that returns no result, but the result is guaranteed ready once the call returns. More common in practice is waiting with a timeout: `wait_for()` takes a duration (500ms, say), `wait_until()` takes an absolute time point, and both return a `std::future_status` enumeration—`ready` means the result is ready, `timeout` means it still isn't done after waiting that long, and `deferred` means the task never started at all (remember the deferred policy? That one). For a deferred task, `wait_for()` and `wait_until()` return the `deferred` status immediately without truly waiting—we will see later just how much trouble this behavior causes.

There is also a helper function, `valid()`, which checks whether this future is still associated with a shared state. A default-constructed `std::future`'s `valid()` returns `false`, and it also returns `false` after `get()` has been called—if you are not sure whether a future is still usable, calling `valid()` first is a good habit.

Let's tie these operations together with one comprehensive example:

```cpp
#include <future>
#include <iostream>
#include <chrono>

int slow_task()
{
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 42;
}

int main()
{
    std::future<int> f = std::async(std::launch::async, slow_task);

    std::cout << "valid() = " << std::boolalpha << f.valid() << "\n";

    // Poll with wait_for (for demonstration; this pattern is not recommended in practice)
    while (true) {
        auto status = f.wait_for(std::chrono::milliseconds(500));
        if (status == std::future_status::ready) {
            std::cout << "任务就绪!\n";
            break;
        } else if (status == std::future_status::timeout) {
            std::cout << "还在跑...\n";
        } else if (status == std::future_status::deferred) {
            std::cout << "任务被延迟了，不会自动执行\n";
            break;
        }
    }

    if (f.valid()) {
        int result = f.get();
        std::cout << "结果: " << result << "\n";
        std::cout << "get() 后 valid() = " << f.valid() << "\n";
    }
    return 0;
}
```

This code checks the task status once every 500ms, and once the task completes it calls `get()` to fetch the value. After calling `get()`, `valid()` becomes `false`, showing that the shared state has been released.

## One-Time Consumption Semantics

The design philosophy of `std::future` is "one-time consumption"—the value in the shared state can be taken away only once. This design shows up at several levels; let's break them down one by one.

Start with the return semantics of `get()`. `get()` performs a move: for `std::future<int>`, `get()` returns a copy of the `int` value (moving an int is the same as copying it, so it makes no difference), but for `std::future<std::string>`, the `std::string` returned by `get()` is moved out of the shared state—once the value has been taken, calling `get()` again is undefined behavior. Note that the standard library has dedicated specializations for `std::future<T&>` (reference type) and `std::future<void>`, whose `get()` behavior differs slightly—the former returns a reference, and the latter only performs the synchronization wait and returns nothing.

Looking at the future object itself, `std::future` is move-only. You cannot copy a `std::future`; you can only move it—after the move, the original future's `valid()` becomes `false`, and the new future takes over the shared state. This design guarantees that at any moment only one future can access the shared state, eliminating at the root the race condition of multiple parties fighting over the same result. And there is no mechanism whatsoever to "reset" an already-consumed future; if you need to read the same result multiple times, you should use `std::shared_future`—which we will cover in the next article.

```cpp
#include <future>
#include <iostream>
#include <string>

std::string generate_report()
{
    return "这是一份详细的分析报告";
}

int main()
{
    std::future<std::string> f = std::async(std::launch::async, generate_report);

    // First get() — fine
    std::string report = f.get();
    std::cout << "报告: " << report << "\n";

    // Second get() — undefined behavior! valid() is already false
    // std::string report2 = f.get();  // never do this

    std::cout << "get() 后 valid() = " << std::boolalpha << f.valid() << "\n";
    return 0;
}
```

This one-time semantic is not a defect but a design choice. The goal of `std::future` is lightweight, one-time delivery of a result, not a repeatedly readable result container. If you need to "broadcast" one result to multiple consumers, C++ provides `std::shared_future` to meet that need—at the cost of extra reference-counting overhead.

## The deferred Policy Trap

Earlier we already mentioned the basic behavior of the deferred policy: the task does not execute asynchronously but is postponed until you call `wait()` or `get()`, at which point it executes synchronously on the current thread. Yet the bugs this behavior causes in real engineering are far more numerous than you would think—and we are not done here; the real pits are still ahead.

> **Pitfall warning**: `std::async` under the default policy is one of the most insidious concurrency pits we have ever stepped in. Everything is normal in local testing, and only in production do you discover that all the tasks were serial—because the standard library implementation chose the deferred policy (under the default policy the implementation is entitled to choose async or deferred on its own; the standard does not specify the conditions for the choice).

The biggest trap comes from the default policy. When you write `std::async(f, args...)` without specifying a policy, you are using `std::launch::async | std::launch::deferred`, which means the standard library implementation can choose on its own. On some implementations (especially under high load), the standard library may choose the deferred policy heavily. So you believe you are doing parallel computation while in reality all the tasks executed serially on the main thread—and your tests can never cover the scenario of "the standard library suddenly switching policies."

A particularly dangerous scenario is the "fire-and-forget" pattern—you launch multiple async tasks, don't call `get()` right away, and expect them to finish running in parallel in the background. Let's look at this code:

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <chrono>

int work(int id)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "任务 " << id << " 完成\n";
    return id * 10;
}

int main()
{
    std::vector<std::future<int>> futures;

    // Launch 4 "async" tasks (using the default policy)
    for (int i = 0; i < 4; ++i) {
        futures.push_back(std::async(work, i));  // default policy: async | deferred
    }

    // Collect the results one by one
    for (auto& f : futures) {
        std::cout << "结果: " << f.get() << "\n";
    }
    return 0;
}
```

If the implementation chose the deferred policy, these 4 tasks will execute serially on the main thread, taking 4 seconds total instead of the expected 1 second. More insidiously, even if the implementation usually chooses async, under certain special conditions (thread resources running tight, for instance) it may still switch to deferred—your tests can never cover that, and that is genuinely annoying.

Right on its heels comes a second trap, related to `wait_for()`. If you write a timeout loop using `wait_for()` to poll a deferred task, the call returns the `deferred` status immediately rather than `timeout`. If you don't handle the `deferred` branch (and honestly, many people really do ignore it), the loop turns into an infinite loop:

```cpp
// ⚠️ Dangerous! If the deferred status is not handled, this may loop forever
while (f.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
    // If the task is deferred, this loop never exits!
    // Because wait_for returns std::future_status::deferred immediately for deferred tasks
}
```

Never assume this is just an extreme textbook example—we have seen this exact infinite loop in real projects, and it only triggers under specific loads, which makes tracking it down genuinely blood-pressure-raising. The correct approach is to check the return value of `wait_for` first; if it is `deferred`, call `get()` directly or adopt some other strategy:

```cpp
auto status = f.wait_for(std::chrono::milliseconds(100));
if (status == std::future_status::deferred) {
    // The task was deferred; execute it directly on the current thread
    result = f.get();
} else if (status == std::future_status::ready) {
    result = f.get();
} else {
    // timeout — keep waiting or do something else
}
```

So our advice is simple: **if you truly need asynchronous execution, explicitly specify `std::launch::async`**. The default policy looks flexible—"let the implementation choose for you," how elegant—but in real projects this flexibility is almost nothing but pits. Scott Meyers advises the same in Item 36 of *Effective Modern C++*: if you want to ensure a task is truly executed asynchronously, always explicitly pass `std::launch::async`. This is a sentence worth taping to the edge of your monitor.

## Exception Propagation

So far we have been dealing only with scenarios of normal return values, but in real engineering, tasks throwing exceptions is commonplace. A major advantage of `std::async` is that it automatically captures exceptions thrown inside the task and propagates them to the caller via `std::future`—you don't need to hand-design error codes or other error-passing mechanisms.

The mechanism works like this: if the task function throws an exception, the exception is caught and stored in the shared state of the `std::future`; when you call `get()`, the stored exception is rethrown. This means you can handle the worker-thread exception in the main thread with try-catch, no different from handling an exception thrown by an ordinary function call.

```cpp
#include <future>
#include <iostream>
#include <stdexcept>

int risky_computation(int x)
{
    if (x < 0) {
        throw std::invalid_argument("参数不能为负数");
    }
    return x * x;
}

int main()
{
    auto f1 = std::async(std::launch::async, risky_computation, -5);

    try {
        int result = f1.get();  // Throws std::invalid_argument
        std::cout << "结果: " << result << "\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    // Normal case
    auto f2 = std::async(std::launch::async, risky_computation, 5);
    try {
        int result = f2.get();
        std::cout << "正常结果: " << result << "\n";  // prints 25
    } catch (const std::invalid_argument& e) {
        std::cout << "不会执行到这里\n";
    }
    return 0;
}
```

This exception propagation mechanism works equally well for the deferred policy—except that under the deferred policy the exception is thrown synchronously at the `get()` call, no different from an ordinary function call throwing.

There is a detail to note here—if you never call `get()`, the exception is silently swallowed. More precisely, if the `std::future` destructs before the task has completed (for the async policy), the destructor blocks waiting for the task to finish. If the task threw an exception and you never called `get()`, the exception is released along with the shared state—it does not propagate, it does not terminate the program, it is simply lost. That is a silent error, and a very dangerous one. So, **always call `get()` on the future returned by `std::async`**, even if you don't need the return value, even if you only want to confirm that the task didn't throw.

## Destructor Behavior of the Future Returned by std::async

You may have noticed that in the earlier examples we dutifully saved the future object and only called `get()` at the end. But what if you casually write a line `std::async(std::launch::async, some_task);` and don't save the return value? This is the place to specifically call out the destructor behavior of the `std::future` returned by `std::async`, because it is not the same as an ordinary `std::future`.

When you obtain a `std::future` through other means (from a `std::promise`, say), destroying the future merely releases the reference to the shared state—if the promise hasn't set a value yet and the future just destructs like this, it waits for nothing.

But the future returned by `std::async` is special: if the task was launched via `std::launch::async`, and this is the last future referencing that shared state, the destructor blocks until the task completes. This is behavior the standard explicitly requires ([futures.async]), and its purpose is to prevent you from discarding the future while the task is still running, leaving the task an orphan thread.

This means the following code is actually serial:

```cpp
#include <future>
#include <iostream>
#include <chrono>

void task(int id)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "任务 " << id << " 完成\n";
}

int main()
{
    // Note: the temporary future object is destroyed at the end of this statement
    std::async(std::launch::async, task, 1);  // destructor blocks until the task finishes
    std::async(std::launch::async, task, 2);  // destructor blocks until the task finishes
    std::async(std::launch::async, task, 3);  // destructor blocks until the task finishes
    // Total elapsed: 3 seconds — completely serial!
    return 0;
}
```

Each temporary `std::future` object returned by `std::async` is destroyed at the end of the statement, and that destruction blocks until the task completes. So although you wrote three lines of `std::async`, the actual execution is strictly serial. For true parallelism, you need to store the futures in a container and collect them one by one after all are launched:

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <chrono>

void task(int id)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "任务 " << id << " 完成\n";
}

int main()
{
    std::vector<std::future<void>> futures;

    // Launch all of them first
    for (int i = 1; i <= 3; ++i) {
        futures.push_back(std::async(std::launch::async, task, i));
    }

    // Then wait for all of them together
    for (auto& f : futures) {
        f.get();  // Total elapsed: about 1 second — the three tasks execute in parallel
    }
    return 0;
}
```

This destructor behavior is a "signature" design of `std::async`, and it regularly trips up newcomers. You must always keep this in the back of your mind: the destructor of the future returned by `std::async` blocks—if you casually ignore the return value, the "parallel" code you wrote becomes serial.

## std::future vs std::thread: How to Choose

At this point we can put `std::async`/`std::future` and `std::thread` side by side for comparison, and straighten out the selection strategy while we are at it.

When you run an asynchronous task with `std::thread`, you need to design the result-passing mechanism yourself—for example a shared variable plus a mutex, a global variable plus an atomic, or a condition variable. Exception handling is entirely your own business too—exceptions thrown in the worker thread do not travel back to the main thread automatically; you must catch them manually and pass them along through some mechanism. Thread management is manual operation as well: you must choose between `join()` and `detach()`, and forgetting triggers `std::terminate`.

With `std::async`, life is much easier: the return value is passed automatically via `std::future`, exceptions propagate automatically, and the future's destructor waits for the task to complete (no orphan threads). The price is that you lose fine-grained control over the thread—you cannot set thread priority, cannot set thread affinity, cannot give the thread a name, and you don't even know which thread the task actually ran on.

So the selection logic is actually quite clear. If you are running a computational task with well-defined inputs and outputs, the tasks are relatively independent, you need exception propagation, and you don't care which thread the task runs on—typical cases being parallel data processing, parallel file I/O, or offloading a time-consuming computation from the main thread—use `std::async`. What `std::async` fits is exactly the "throw out a task, get back a result" kind of scenario. But `std::async` is not suitable for scenarios that frequently create and destroy threads—every `std::launch::async` may create a brand-new thread, and that system overhead is not small.

If you need a resident background worker thread—a background listener thread, an event loop, or situations where you need to set thread attributes (priority, affinity, and so on)—use `std::thread`, but it requires you to handle all synchronization and error passing yourself, with noticeably more code.

If you need to run a large number of short tasks, that is the home turf of thread pools. A thread pool pre-creates a set of worker threads, and tasks are submitted to a queue from which worker threads pull them out and execute them. This avoids the overhead of frequently creating and destroying threads, and it lets you control the degree of concurrency (maximum thread count, task queue size, and so on). The C++ standard library currently does not provide a thread pool, so you need to implement one yourself or use a third-party library—we will cover the design and implementation of thread pools in detail in later chapters.

## Exercises: Parallel Computation with std::async

### Exercise 1: Parallel Summation

Given a `std::vector<int>` containing 10 million random integers, use `std::async` to split it into 4 segments and sum them in parallel, then aggregate the results. Compare the elapsed time of the single-threaded version and the multi-threaded version.

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>

// Sum the range data[begin, end)
long long partial_sum(const std::vector<int>& data, std::size_t begin, std::size_t end)
{
    return std::accumulate(data.begin() + begin, data.begin() + end, 0LL);
}

int main()
{
    constexpr std::size_t kDataSize = 10'000'000;
    constexpr int kNumTasks = 4;

    // Generate random data
    std::vector<int> data(kDataSize);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 100);
    for (auto& x : data) {
        x = dist(rng);
    }

    // Multi-threaded version
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<long long>> futures;
    std::size_t chunk = kDataSize / kNumTasks;

    for (int i = 0; i < kNumTasks; ++i) {
        std::size_t begin = i * chunk;
        std::size_t end = (i == kNumTasks - 1) ? kDataSize : (i + 1) * chunk;
        futures.push_back(
            std::async(std::launch::async, partial_sum,
                       std::cref(data), begin, end));
    }

    long long total = 0;
    for (auto& f : futures) {
        total += f.get();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       end_time - start)
                       .count();

    std::cout << "并行求和结果: " << total << "\n";
    std::cout << "耗时: " << elapsed << " us\n";

    // Single-threaded version (for verification)
    start = std::chrono::high_resolution_clock::now();
    long long single = std::accumulate(data.begin(), data.end(), 0LL);
    end_time = std::chrono::high_resolution_clock::now();
    elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                  end_time - start)
                  .count();

    std::cout << "单线程结果: " << single << "\n";
    std::cout << "耗时: " << elapsed << " us\n";
    std::cout << "结果一致: " << std::boolalpha << (total == single) << "\n";
    return 0;
}
```

Note the use of `std::cref(data)` to pass a read-only reference to the data—because `std::async`'s arguments are passed by value by default, and without `std::cref` the entire vector would be copied, wasting both memory and time. `std::cref` is a reference wrapper that lets an argument passed by value carry a reference along without copying.

### Exercise 2: Verifying the deferred Trap

Modify the code from Exercise 1 to run with `std::launch::async`, `std::launch::deferred`, and the default policy respectively, and compare the elapsed times of the three. Observe whether the deferred version's elapsed time is close to the single-threaded version's.

### Exercise 3: Exception Propagation Verification

Write a `std::async` task and have it throw a custom exception. Catch it in the main thread with try-catch, and verify that the exception type and message content match.

## Summary

With this article, we have walked through the core mechanisms of `std::async` and `std::future` in full. `std::async` provides a way to launch asynchronous tasks at a higher level than `std::thread`, automatically handling return-value delivery and exception propagation—a genuine load off your mind. `std::future<T>` is the standard channel for fetching asynchronous results; operations like `get()`, `wait()`, and `wait_for()` have very plain names, but the semantics behind them (especially the one-time consumption of get and the behavior of wait_for under the deferred status) need to be kept firmly in mind.

A few key points, emphasized once more: the default launch policy (`async | deferred`) is a trap to stay alert to—the implementation may choose the deferred policy, causing tasks to execute serially; `wait_for()` returns the `deferred` status immediately for deferred tasks, and a polling loop that doesn't handle this branch becomes an infinite loop; and the destructor of the future returned by `std::async` blocks until the task completes, so casually ignoring the return value turns your parallel code into serial. If you need truly asynchronous execution, explicitly pass `std::launch::async`—this rule, too, is worth taping to the edge of your monitor.

In the next article we will look at `std::promise` and `std::packaged_task`—they are the "other end" of `std::future`, letting you control value setting and task packaging more flexibly. Once the semantics on the future side are clear, understanding the promise side follows naturally.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

## References

- [std::async — cppreference](https://en.cppreference.com/w/cpp/thread/async)
- [std::future — cppreference](https://en.cppreference.com/w/cpp/thread/future)
- [std::launch — cppreference](https://en.cppreference.com/w/cpp/thread/launch)
- [Effective Modern C++, Item 35, 36 — Scott Meyers](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [Async Tasks in C++11: Not Quite There Yet — Bartosz Milewski](https://bartoszmilewski.com/2011/10/10/async-tasks-in-c11-not-quite-there-yet/)
- [The Promises and Challenges of std::async — DZone](https://dzone.com/articles/the-promises-and-challenges-of-stdasync-task-based)
