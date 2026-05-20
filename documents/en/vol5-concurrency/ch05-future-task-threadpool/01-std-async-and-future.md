---
title: std::async and future
description: Understanding `std::async` launch policies, the blocking semantics of
  `future.get`, and the deferred trap
chapter: 5
order: 1
tags:
- host
- cpp-modern
- intermediate
- 异步编程
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 线程安全队列
related:
- promise 与 packaged_task
- 线程池设计
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/01-std-async-and-future.md
  source_hash: 31367c94a78e8403d9b1a3b9d7e670ce34f2159b63a0a77d59561e4f4e80b375
  translated_at: '2026-05-20T04:42:48.216328+00:00'
  engine: anthropic
  token_count: 4366
---
# std::async and future

To be honest, reaching this chapter was a relief. In the previous chapters, we have been wrestling with `std::thread`, `std::mutex`, and `std::atomic`—these low-level primitives—directly manipulating thread creation, synchronization, and even memory order. Writing this kind of code gets tedious after a while. You have to manage the thread lifecycle yourself, design synchronization mechanisms, shuttle results from child threads back to the main thread, and worry about how to propagate exceptions or what happens if a thread crashes. Repeating this workflow for every concurrent task makes you wonder: is there a way to just say "run this task asynchronously and give me the result," without bothering with the rest?

C++11 does provide such a higher-level abstraction, centered around `std::async` and `std::future`. In this chapter, we will thoroughly clarify the launch policies of `std::async`, and fully grasp the blocking semantics and one-time consumption model of `std::future`. We will focus especially on the classic deferred trap—if you do not understand the behavior of the default policy, your code might run perfectly fine locally, but mysteriously serialize under specific loads in production. I have fallen into this trap myself, so let us break it down step by step.

## std::async: Launching an Asynchronous Task

What we want to do now is start with the most basic usage, get a clear picture of the basic form of `std::async`, and then gradually dive into the policy and behavioral details.

`std::async` is a function template that takes a callable object and a set of arguments, returning a `std::future`—this future is your "receipt" to retrieve the task's return value at some point in the future. It has two overloads: one that accepts a launch policy, and another that uses the default policy. Let us ignore the policy for now and just get it running:

```cpp
#include <future>
#include <iostream>
#include <chrono>

int heavy_computation(int x)
{
    // 模拟耗时计算
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return x * x;
}

int main()
{
    // 异步启动任务
    std::future<int> result = std::async(std::launch::async, heavy_computation, 42);

    std::cout << "任务已提交，主线程继续干活...\n";

    // 在这里主线程可以做其他事情

    int value = result.get();  // 阻塞等待结果
    std::cout << "计算结果: " << value << "\n";
    return 0;
}
```

The first parameter of `std::async` is the launch policy, the second is the callable object to execute, and the subsequent arguments are perfectly forwarded to that callable. The return value is a `std::future<int>`—where the template parameter is the task's return type. If the task returns `void`, you get a `std::future<void>`.

In the code above, `std::launch::async` is an enumerator meaning "launch this task immediately on a new thread." Once you have the future, the main thread is not blocked and can go about its business, only blocking when you call `result.get()` to wait for the task to finish.

## Two Launch Policies

Great, the basic usage works. Now the question arises—what exactly is the deal with `std::async`'s policy? Earlier we always explicitly passed `std::launch::async`, but what if we don't? This is where the first trap we are going to dissect hides.

`std::async` supports two launch policies, specified via the `std::launch` enumeration. `std::launch::async` requires the runtime to create a new thread (or grab one from an internal thread pool) when `std::async` is called, executing the task immediately. If the system temporarily lacks the resources to create a thread, the standard requires the implementation to either create the thread and execute, or throw a `std::system_error`—this is an error condition you need to watch out for. `std::launch::deferred` is completely different—it does not create any new thread, and the task is deferred until you call `get()` or `wait()` on the future, executing synchronously on the calling thread. In other words, if you call `get()` on the main thread, the task runs directly on the main thread, which is essentially no different from a normal function call, just with an extra layer of wrapping.

These two policies can be combined with a bitwise OR. `std::launch::async | std::launch::deferred` is the default policy—when you do not pass the first argument, this is the combination `std::async` uses. This means the implementation has the right to choose whether to go async or deferred, and the standard leaves the decision to the standard library implementers.

This sounds flexible, but the problem lies precisely in this "implementation's choice." Scott Meyers specifically discusses this trap in Item 36 of *Effective Modern C++*: under the default policy, `std::async` might choose deferred, meaning your task might not be running on another thread at all. Worse, the `wait_for()` function of `std::future` returns `std::future_status::deferred` instead of `timeout` when facing a deferred task—if you write a polling loop using `wait_for()` to check if the task is done, hitting a deferred task will cause the loop to wait forever.

Let us look at an example that intuitively demonstrates the difference between the two:

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

    // 策略一：async —— 强制在新线程上执行
    std::cout << "--- std::launch::async ---\n";
    auto f1 = std::async(std::launch::async, compute, 10);
    std::cout << "  [main] future 已创建，任务已在新线程启动\n";
    std::cout << "  [main] 结果: " << f1.get() << "\n\n";

    // 策略二：deferred —— 延迟到 get() 时在调用线程执行
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

When you run this code, you will see that in async mode, the thread ID printed by compute differs from the main thread, while in deferred mode, the thread IDs are the same—because the deferred task executes synchronously on the thread that calls `get()`.

## std::future\<T\>: Retrieving Asynchronous Results

`std::future<T>` is a "one-time result container" provided by the C++ standard library. You can think of it as a read-only, single-use pipe: one end (`std::async`, `std::promise`, or `std::packaged_task`) is responsible for pushing a value in, and the other end (the `std::future` in your hand) is responsible for pulling the value out. The design philosophy of this pipe is very clear—the value can only be extracted once, and once extracted, the pipe is spent.

Let us look back at the core operations provided by future. `get()` is the one you will use the most—it blocks the current thread until the result is ready, then returns the result value; if the task threw an exception, `get()` rethrows that exception (we will cover the exception propagation mechanism in detail later). But there is a key constraint here: `get()` can only be called once; after the call, the future becomes invalid, the shared state is released, and any further operations on it are undefined behavior (typically throwing `std::future_error`).

If you just want to wait for the task to finish without rushing to get the value, use `wait()`—pure blocking wait, no return value, but once the call ends, the result is guaranteed to be ready. A more common scenario is waiting with a timeout: `wait_for()` takes a time duration (like 500ms), `wait_until()` takes an absolute time point, and both return the `std::future_status` enumeration—`ready` means the result is ready, `timeout` means it is still not ready after waiting this long, and `deferred` means the task did not start at all (remember the deferred policy? that is the one). For deferred tasks, `wait_for()` and `wait_until()` immediately return the `deferred` status without actually waiting—we will see later just how problematic this behavior can be.

There is also a helper function `valid()`, used to check whether this future is still associated with a shared state. A default-constructed `std::future`'s `valid()` returns `false`, and it also returns `false` after calling `get()`—if you are unsure whether a future is still usable, calling `valid()` first is a good habit.

Let us use a comprehensive example to tie these operations together:

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

    // 用 wait_for 轮询（演示用，实际中不推荐这种模式）
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

This code checks the task status every 500ms, and calls `get()` to retrieve the value once the task is done. After calling `get()`, `valid()` becomes `false`, indicating that the shared state has been released.

## One-Time Consumption Semantics

The design philosophy of `std::future` is "one-time consumption"—the value in the shared state can only be extracted once. This design manifests on several levels, so let us break them down one by one.

Starting with the return semantics of `get()`. `get()` performs move semantics: for `std::future<int>`, `get()` returns a value copy of `int` (since moving an int is just a copy, it does not matter), but for `std::future<std::string>`, the `std::string` returned by `get()` is moved out of the shared state, and calling `get()` again after the value has been taken is undefined behavior. Notably, the standard library has separate specializations for `std::future<T&>` (reference types) and `std::future<void>`, and their `get()` behaviors differ slightly—the former returns a reference, while the latter only performs a synchronous wait without returning anything.

Looking at the properties of the future object itself, `std::future` is move-only. You cannot copy a `std::future`, you can only move it—after moving, the original future's `valid()` becomes `false`, and the new future takes over the shared state. This design ensures that at any given time, only one future can access the shared state, fundamentally eliminating the race condition of multiple parties fighting over the same result. Furthermore, there is no mechanism to "reset" a consumed future; if you need to read the same result multiple times, you should use `std::shared_future`—which we will cover in the next chapter.

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

    // 第一次 get() —— 正常
    std::string report = f.get();
    std::cout << "报告: " << report << "\n";

    // 第二次 get() —— 未定义行为！valid() 已经是 false
    // std::string report2 = f.get();  // 千万别这么干

    std::cout << "get() 后 valid() = " << std::boolalpha << f.valid() << "\n";
    return 0;
}
```

This one-time semantics is not a defect but a design choice. The goal of `std::future` is lightweight, one-time result passing, not a repeatedly readable result container. If you need to "broadcast" a result to multiple consumers, C++ provides `std::shared_future` to meet this need—at the cost of additional reference counting overhead.

## The Deferred Policy Trap

We have already mentioned the basic behavior of the deferred policy: the task does not execute asynchronously, but is deferred until you call `wait()` or `get()`, at which point it executes synchronously on the current thread. But the bugs this behavior triggers in real-world engineering are far more common than you would think—and the story does not end here; the real traps are yet to come.

> **Trap Warning**: `std::async` under the default policy is one of the most insidious concurrency traps I have ever stepped into. Local testing is perfectly fine, but once it hits production, you discover that all tasks are serial—because the standard library implementation chose the deferred policy (under the default policy, the implementation has the right to choose either async or deferred, and the standard does not specify the conditions for this choice).

The biggest trap comes from the default policy. When you write `std::async(f, args...)` without specifying a policy, you are using `std::launch::async | std::launch::deferred`, which means the standard library implementation can choose on its own. On some implementations (especially under high load), the standard library might heavily favor the deferred policy. So you think you are doing parallel computation, but in reality, all tasks are executing serially on the main thread—and your tests can never cover the scenario of "the standard library suddenly switching policies."

A particularly dangerous scenario is the "fire-and-forget" pattern—you launch multiple async tasks without immediately calling `get()`, expecting them to finish in parallel in the background. Let us look at this code:

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

    // 启动 4 个"异步"任务（使用默认策略）
    for (int i = 0; i < 4; ++i) {
        futures.push_back(std::async(work, i));  // 默认策略：async | deferred
    }

    // 依次收集结果
    for (auto& f : futures) {
        std::cout << "结果: " << f.get() << "\n";
    }
    return 0;
}
```

If the implementation chooses the deferred policy, these 4 tasks will execute serially on the main thread, taking 4 seconds total instead of the expected 1 second. What is more insidious is that even if the implementation usually chooses async, under certain special conditions (like tight thread resources) it might switch to deferred—your tests can never cover this situation, which is incredibly frustrating.

Immediately following is the second trap, related to `wait_for()`. If you write a timeout loop using `wait_for()` to poll a deferred task, the loop will immediately return the `deferred` status instead of `timeout`. If you do not handle the `deferred` branch (and frankly, many people do ignore it), the loop turns into an infinite loop:

```cpp
// ⚠️ 危险！如果没有处理 deferred 状态，可能永远循环下去
while (f.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
    // 如果任务是 deferred 的，这个循环永远不会退出！
    // 因为 wait_for 对 deferred 任务立刻返回 std::future_status::deferred
}
```

Do not assume this is just an extreme textbook example—I have seen this kind of infinite loop in real projects, and it only triggers under specific loads, making it absolutely maddening to debug. The correct approach is to first check the return value of `wait_for`; if it is `deferred`, directly call `get()` or adopt another strategy:

```cpp
auto status = f.wait_for(std::chrono::milliseconds(100));
if (status == std::future_status::deferred) {
    // 任务被延迟了，直接在当前线程执行
    result = f.get();
} else if (status == std::future_status::ready) {
    result = f.get();
} else {
    // timeout —— 继续等待或做其他事情
}
```

So my advice is simple: **if you truly need asynchronous execution, explicitly specify `std::launch::async`**. The default policy looks flexible—"let the implementation choose for you," how elegant—but in real projects, this flexibility is almost entirely a trap. Scott Meyers also advises in Item 36 of *Effective Modern C++*: if you want to ensure a task is truly executed asynchronously, always explicitly pass `std::launch::async`. It would not be an exaggeration to tape this rule to the edge of your monitor.

## Exception Propagation

So far we have only dealt with scenarios involving normal return values, but in real-world engineering, tasks throwing exceptions is a common occurrence. A major advantage of `std::async` is that it automatically captures exceptions thrown within the task and propagates them to the caller via `std::future`—you do not need to manually design error codes or other error-passing mechanisms.

The mechanism works like this: if the task function throws an exception, the exception is caught and stored in the `std::future`'s shared state; when you call `get()`, the stored exception is rethrown. This means you can use try-catch in the main thread to handle exceptions from child threads, which is no different from handling exceptions thrown by normal function calls.

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
        int result = f1.get();  // 会抛出 std::invalid_argument
        std::cout << "结果: " << result << "\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    // 正常情况
    auto f2 = std::async(std::launch::async, risky_computation, 5);
    try {
        int result = f2.get();
        std::cout << "正常结果: " << result << "\n";  // 输出 25
    } catch (const std::invalid_argument& e) {
        std::cout << "不会执行到这里\n";
    }
    return 0;
}
```

This exception propagation mechanism works equally well for the deferred policy—except that under the deferred policy, the exception is thrown synchronously when `get()` is called, which is no different from a normal function call throwing an exception.

There is a detail to note here—if you never call `get()`, the exception is silently swallowed. More precisely, if the `std::future` destructs before the task has completed (for the async policy), the destructor will block and wait for the task to finish. If the task threw an exception and you never called `get()`, the exception is released along with the shared state—it is not propagated, it does not terminate the program, it is just lost. This is a silent error and is very dangerous. Therefore, **you must always call `get()` on the future returned from `std::async`**, even if you do not need the return value, even if you just want to confirm that the task did not throw an exception.

## Destructor Behavior of Futures Returned by std::async

You might have noticed that in the previous examples, we dutifully saved the future objects and only called `get()` at the very end. But what if you casually write a line like `std::async(std::launch::async, some_task);` without saving the return value? Here we need to specifically mention the destructor behavior of the `std::future` returned by `std::async`, because it is different from an ordinary `std::future`.

When you obtain a `std::future` through other means (like `std::promise`), the future's destructor simply releases the reference to the shared state—if the promise has not yet set a value, the future destructs just like that, without waiting for anything.

But the future returned by `std::async` is special: if the task was launched via `std::launch::async`, and this is the last future referencing that shared state, the destructor will block until the task completes. This is behavior explicitly required by the standard ([futures.async]), designed to prevent the task from becoming an orphaned thread if you throw away the future while it is still running.

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
    // 注意：临时 future 对象在这条语句结束时就会析构
    std::async(std::launch::async, task, 1);  // 析构阻塞到任务完成
    std::async(std::launch::async, task, 2);  // 析构阻塞到任务完成
    std::async(std::launch::async, task, 3);  // 析构阻塞到任务完成
    // 总耗时 3 秒——完全是串行的！
    return 0;
}
```

Each time, the temporary `std::future` object returned by `std::async` is destructed at the end of the statement, and the destruction blocks until the task completes. So even though you wrote three lines of `std::async`, the actual execution is strictly serial. To achieve true parallelism, you need to store the futures in a container, wait until all are launched, and then collect the results one by one:

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

    // 先全部启动
    for (int i = 1; i <= 3; ++i) {
        futures.push_back(std::async(std::launch::async, task, i));
    }

    // 再统一等待
    for (auto& f : futures) {
        f.get();  // 总耗时约 1 秒——三个任务并行执行
    }
    return 0;
}
```

This destructor behavior is a "signature" design of `std::async` that often trips up beginners. You must always keep this in mind: the destructor of a future returned by `std::async` will block—if you casually ignore the return value, your "parallel" code becomes serial.

## Comparing std::future and std::thread: How to Choose?

At this point, we can compare `std::async`/`std::future` with `std::thread`, and clarify the selection strategy along the way.

When using `std::thread` to execute asynchronous tasks, you need to design the result-passing mechanism yourself—for example, using shared variables with a mutex, global variables with atomics, or condition variables. Exception handling is also entirely your responsibility—exceptions thrown in child threads are not automatically propagated back to the main thread; you have to manually catch them and pass them through some mechanism. Thread management is also manual: you must choose between `join()` or `detach()`; forget to do so, and you trigger `std::terminate`.

Using `std::async` is much more worry-free: return values are automatically passed via `std::future`, exceptions are automatically propagated, and the future's destructor waits for the task to complete (no orphaned threads). The cost is that you lose fine-grained control over the thread—you cannot set thread priority, thread affinity, or thread names, and you do not even know which thread the task is actually running on.

So the selection logic is actually quite clear. If you need to run a computational task with clear inputs and outputs, where tasks are relatively independent, you need exception propagation, and you do not care which thread the task runs on—typical examples include parallel data processing, parallel file I/O, or offloading a time-consuming computation from the main thread—use `std::async`. `std::async` is suited for exactly that "throw out a task, get back a result" scenario. However, `std::async` is not suitable for scenarios requiring frequent thread creation and destruction—each `std::launch::async` might create a new thread, and the system overhead is not insignificant.

If you need a persistent background worker thread—like a background listening thread, an event loop, or a situation where you need to set thread attributes (priority, affinity, etc.)—use `std::thread`, but it requires you to handle all synchronization and error passing yourself, resulting in noticeably more code.

If you need to run a large number of short tasks, that is the domain of thread pools. A thread pool pre-creates a set of worker threads, and tasks are submitted to a queue to be picked up and executed by the workers. This avoids the overhead of frequently creating and destroying threads, and also lets you control the concurrency level (maximum thread count, task queue size, etc.). The C++ standard library currently does not provide a thread pool, so you need to implement one yourself or use a third-party library—we will cover the design and implementation of thread pools in detail in later chapters.

## Exercise: Parallel Computation Using std::async

### Exercise 1: Parallel Summation

Given a `std::vector<int>` containing 10 million random integers, use `std::async` to split it into 4 segments for parallel summation, then aggregate the results. Compare the execution time of the single-threaded version and the multi-threaded version.

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>

// 将 data[begin, end) 区间求和
long long partial_sum(const std::vector<int>& data, std::size_t begin, std::size_t end)
{
    return std::accumulate(data.begin() + begin, data.begin() + end, 0LL);
}

int main()
{
    constexpr std::size_t kDataSize = 10'000'000;
    constexpr int kNumTasks = 4;

    // 生成随机数据
    std::vector<int> data(kDataSize);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 100);
    for (auto& x : data) {
        x = dist(rng);
    }

    // 多线程版本
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

    // 单线程版本（用于验证）
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

Note that we use `std::cref(data)` to pass a read-only reference to the data—because `std::async`'s arguments are passed by value by default. Without `std::cref`, the entire vector would be copied, wasting both memory and time. `std::cref` is a reference wrapper that allows arguments passed by value to actually pass a reference without copying.

### Exercise 2: Verifying the Deferred Trap

Modify the code from Exercise 1 to run using `std::launch::async`, `std::launch::deferred`, and the default policy respectively. Compare the execution times of the three. Observe whether the execution time of the deferred version is close to that of the single-threaded version.

### Exercise 3: Exception Propagation Verification

Write a `std::async` task that throws a custom exception. Use try-catch in the main thread to catch it and verify that the exception type and message content are consistent.

## Summary

At this point, we have walked through the core mechanisms of `std::async` and `std::future` in their entirety. `std::async` provides a higher-level way to launch asynchronous tasks than `std::thread`, automatically handling return value passing and exception propagation, which is indeed much less hassle. `std::future<T>` is the standard channel for retrieving asynchronous results; operations like `get()`, `wait()`, and `wait_for()` have very straightforward names, but the semantics behind them (especially the one-time consumption of get and the behavior of wait_for under the deferred status) are something you need to keep firmly in mind.

Let us reiterate a few key points: the default launch policy (`async | deferred`) is a trap to be wary of, as the implementation might choose the deferred policy causing tasks to execute serially; `wait_for()` immediately returns the `deferred` status for deferred tasks, and a polling loop that does not handle this branch will turn into an infinite loop; the destructor of the future returned by `std::async` blocks until the task completes, so casually ignoring the return value will turn your parallel code into serial code. If you need true asynchronous execution, explicitly pass `std::launch::async`—it would not be an exaggeration to tape this rule to the edge of your monitor.

In the next chapter, we will look at `std::promise` and `std::packaged_task`—they are the "other end" of `std::future`, giving you more flexible control over value setting and task encapsulation. Once you clearly understand the semantics on the future end, understanding the promise end will follow naturally.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

## References

- [std::async — cppreference](https://en.cppreference.com/w/cpp/thread/async)
- [std::future — cppreference](https://en.cppreference.com/w/cpp/thread/future)
- [std::launch — cppreference](https://en.cppreference.com/w/cpp/thread/launch)
- [Effective Modern C++, Item 35, 36 — Scott Meyers](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [Async Tasks in C++11: Not Quite There Yet — Bartosz Milewski](https://bartoszmilewski.com/2011/10/10/async-tasks-in-c11-not-quite-there-yet/)
- [The Promises and Challenges of std::async — DZone](https://dzone.com/articles/the-promises-and-challenges-of-stdasync-task-based)
