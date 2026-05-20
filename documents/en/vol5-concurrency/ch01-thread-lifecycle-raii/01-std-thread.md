---
title: std::thread Basics
description: Master C++ thread creation, join, detach, ID, and hardware concurrency
  queries, building intuition for your first multithreaded program.
chapter: 1
order: 1
tags:
- host
- cpp-modern
- beginner
- 入门
difficulty: beginner
platform: host
reading_time_minutes: 20
cpp_standard:
- 11
- 14
- 17
prerequisites:
- CPU cache 与 OS 线程
related:
- 线程参数与生命周期
- 线程所有权与 RAII
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/01-std-thread.md
  source_hash: 7246e2dd9ebe52ccce9af207e9f62e25d593c76366fc2899e336ef2122a7da1a
  translated_at: '2026-05-20T04:33:11.776185+00:00'
  engine: anthropic
  token_count: 3621
---
# std::thread Basics

In the previous chapter, we discussed CPU cache hierarchies, the MESI protocol, false sharing, and looked at the Linux threading model and the futex mechanism—these form the physical stage on which multithreaded programs run. But knowing what the stage looks like isn't enough; we need to step onto it ourselves. This article marks our first time on stage: starting from the construction of `std::thread`, we'll figure out how to create threads, how to wait for them, how to "let them go," and what pitfalls we might stumble into along the way.

`std::thread` is the standard thread class introduced in C++11, defined in the `<thread>` header. It is the C++ standard library's direct wrapper around operating system threads—on Linux, every `std::thread` object backs a pthread, and that pthread maps to a kernel scheduling entity via the `clone()` system call. The 1:1 model we mentioned in the previous article is embodied right here.

## Three Ways to Construct a std::thread

The constructor of `std::thread` accepts a **callable** and an optional list of arguments. C++ provides us with several ways to express a "callable," so let's look at them one by one.

### Function Pointers

The most straightforward approach is to pass a plain function pointer:

```cpp
#include <thread>
#include <iostream>

void print_hello(int id)
{
    std::cout << "Hello from thread " << id << "\n";
}

int main()
{
    std::thread t(print_hello, 42);
    t.join();
    return 0;
}
```

`std::thread t(print_hello, 42)` does a few things: first, it packages `print_hello` (the function pointer) and `42` (the arguments) into internal storage; then, it calls the underlying `pthread_create` (or equivalent system call) to create a new operating system thread; finally, the new thread invokes `print_hello(42)` with the saved arguments in that independent execution context. Note that the argument `42` is **copied** into the thread's internal storage—we'll dive into the details of argument passing in the next article.

### Lambda Expressions

In real-world engineering, lambdas are the most common way to create threads because they let you define the thread's task right at the call site without declaring a separate function:

```cpp
#include <thread>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> data = {1, 2, 3, 4, 5};
    int sum = 0;

    std::thread t([&data, &sum]() {
        for (int v : data) {
            sum += v;
        }
    });

    t.join();
    std::cout << "Sum = " << sum << "\n";
    return 0;
}
```

This code works fine, but if you look closely, `[&data, &sum]` is captured by reference—this is perfectly fine in a single-threaded scenario, but what if the thread is detached or its lifetime extends beyond the scope of `data` and `sum`? This is a breeding ground for dangling references. Let's keep this "smell" in mind; we'll systematically dissect it in the next article.

### Function Objects (Functors)

The third approach is to pass a class instance that overloads `operator()`:

```cpp
#include <thread>
#include <iostream>
#include <vector>

class Accumulator {
public:
    Accumulator(const std::vector<int>& data, int& result)
        : data_(data), result_(result)
    {}

    void operator()() const
    {
        int local_sum = 0;
        for (int v : data_) {
            local_sum += v;
        }
        result_ = local_sum;
    }

private:
    const std::vector<int>& data_;  // 注意：引用成员
    int& result_;                    // 引用成员
};

int main()
{
    std::vector<int> data = {1, 2, 3, 4, 5};
    int result = 0;

    // 注意：这里需要用花括号或 lambda 避免最令人头疼的解析问题
    // std::thread t(Accumulator(data, result));  // 编译错误！被解析为函数声明
    Accumulator acc(data, result);
    std::thread t(acc);  // OK：拷贝 acc 到线程中

    t.join();
    std::cout << "Result = " << result << "\n";
    return 0;
}
```

There is a classic C++ trap here—if you write `std::thread t(Accumulator(data, result));` directly, the compiler will parse it as a function declaration named `t` (with a parameter type of pointer to `Accumulator`), rather than the definition of a thread object. This is the so-called "most vexing parse" problem. There are several ways to solve it: use extra braces `std::thread t{Accumulator(data, result)};`, use a lambda `std::thread t([&](){ ... });`, or construct a named object first and pass it in, as shown above.

Each approach has its own use cases. Function pointers are suitable for simple, stateless thread functions; lambdas are ideal for defining local logic at the call site and are the most common approach in day-to-day development; functors are good for complex tasks that need to carry state—but watch out for the lifetime risks introduced by reference members. In real projects, lambdas cover over 90% of use cases.

## join() vs detach(): Two Radically Different Strategies

After creating a thread, we must make a decision before its lifetime ends: **join** or **detach**. This decision directly affects the correctness of the program.

### join: Waiting for the Thread to Finish

`join()` is a blocking call—the current thread stops right there and waits until the target thread finishes executing before continuing. As an analogy: you send someone to do a job, you stand there and wait until they finish, and then you both move on together. This is the most common pattern, and also the safest.

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void slow_work()
{
    std::cout << "Worker: starting...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Worker: done.\n";
}

int main()
{
    std::cout << "Main: launching thread\n";
    std::thread t(slow_work);
    std::cout << "Main: waiting for thread...\n";
    t.join();
    std::cout << "Main: thread finished, continuing\n";
    return 0;
}
```

When you run this code, you'll see the output happen in a strict order: Main starts -> Worker starts -> Worker finished -> Main continues. `join()` guarantees that the thread's execution results are visible to the calling thread when `join` returns—this is a happens-before relationship.

### detach: Letting Go

`detach()` does the exact opposite—it "detaches" the thread from the management of the `std::thread` object. After detachment, the thread runs independently in the background (a so-called daemon thread / background thread), and the `std::thread` object no longer holds any reference to it. You can't join it anymore either—the `joinable()` of the `std::thread` object will return `false`.

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void background_task()
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Background task finished\n";
}

int main()
{
    std::thread t(background_task);
    t.detach();

    std::cout << "Main: detached thread, sleeping 1 second...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Main: exiting\n";
    return 0;
}
```

If you run this code, you'll most likely not see the "Background task finished" output—because the main thread waits only one second before exiting, while the detached thread needs two seconds. When a process exits, all threads (including detached ones) are forcibly terminated with no chance to clean up. This is the biggest risk of detach: **you completely lose control over the thread's execution timing**.

So when should you use detach? To be honest, in most application code, detach is not a good choice. The scenarios where it fits are very limited—for example, a background logging thread whose job is to flush logs from an in-memory buffer to disk. You don't care when it finishes, as long as it eventually writes the data out. But even in this scenario, using a `joinable` thread paired with an explicit shutdown signal is usually a more robust approach.

### The Consequence of Neither join nor detach: std::terminate

If you have a `std::thread` `joinable` object and you neither call `join()` nor call `detach()`, letting it naturally reach its destructor—your program will call `std::terminate()` and crash outright. This isn't a suggestion; it's a hard requirement mandated by the standard:

```cpp
#include <thread>
#include <iostream>

void some_work()
{
    std::cout << "Working...\n";
}

int main()
{
    std::thread t(some_work);
    // 没有 join() 也没有 detach()
    // t 析构时调用 std::terminate()
    return 0;  // terminate called without an active exception
}
```

There's a good reason the C++ standard is designed this way. If the destructor silently joined for you, destruction could block—which is something many developers don't want to accept (destructors should be fast). If the destructor silently detached for you, the thread might access references to objects that no longer exist after destruction—that's undefined behavior, which is worse than a crash. The standard chooses to call `terminate` directly, forcing you to **explicitly make a decision**: either wait for it to finish (join), or let it go (detach), but you can't pretend this problem doesn't exist.

This design philosophy permeates the entire C++ concurrency API: don't do implicit, surprising things; leave the decision to the programmer. The trade-off is that you must remember to handle join/detach on every code path, including exception paths. A common pattern is to use an RAII wrapper—save the thread on construction, and automatically join on destruction—we'll expand on this topic later in this chapter.

## Thread Identification and Queries

### get_id(): A Thread's ID Card

Every thread has a unique identifier of type `std::thread::id`. You can get a thread object's ID via `std::thread::get_id()`, or get the current thread's ID via `std::this_thread::get_id()`. `std::thread::id` supports comparison operations and output to `std::ostream`, making it convenient for debugging and logging:

```cpp
#include <thread>
#include <iostream>

void worker()
{
    std::cout << "Worker thread ID: "
              << std::this_thread::get_id() << "\n";
}

int main()
{
    std::thread t(worker);
    std::cout << "Main thread ID: "
              << std::this_thread::get_id() << "\n";
    std::cout << "Worker's thread ID (from main): "
              << t.get_id() << "\n";
    t.join();

    // join 或 detach 后，get_id() 返回默认构造的 id
    std::cout << "After join, worker ID: "
              << t.get_id() << "\n";
    return 0;
}
```

A few things to note: the specific value of `std::thread::id` is implementation-defined—different compilers and platforms may output different formats (GCC usually outputs a number, MSVC might output a hexadecimal address), so don't rely on its specific format for logic decisions. After `join()` or `detach()`, `get_id()` returns a default-constructed `std::thread::id{}`, meaning "not associated with any thread"—this is the same return value as `get_id()` on a default-constructed `std::thread` object.

The most practical use case for `thread::id` is as a key in a `std::hash` to allocate per-thread resources (like a separate memory pool or log buffer for each thread). You can also use it to detect whether "the current thread is the main thread," implementing simple thread-safe assertions.

### native_handle(): Touching the OS Native Handle

`std::thread` is a standard library abstraction, but sometimes you need to manipulate the underlying operating system thread directly—such as setting thread priority, CPU affinity, or the thread name. `native_handle()` returns a platform-dependent native thread handle: on Linux it's a `pthread_t`, and on Windows it's a `HANDLE`.

```cpp
#include <thread>
#include <iostream>

// 注意：以下代码是 Linux 专用的
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

void set_high_priority(std::thread& t)
{
#ifndef _WIN32
    sched_param param;
    param.sched_priority = 10;  // 较高的优先级（具体值取决于调度策略）
    pthread_setschedparam(t.native_handle(), SCHED_RR, &param);
#endif
}

int main()
{
    std::thread t([]() {
        std::cout << "High priority thread running\n";
    });
    set_high_priority(t);
    t.join();
    return 0;
}
```

This code is obviously non-portable—it will only compile on platforms that support pthread. In real projects, you'd typically isolate platform-specific code using `#ifdef`, or abstract it into a platform layer. `native_handle()` gives you an "escape hatch," letting you deal directly with the operating system when the standard library isn't enough.

### hardware_concurrency(): How Many Cores Do I Have

`std::thread::hardware_concurrency()` is a static member function that returns a hint indicating the number of threads that can truly execute concurrently on the current system—in most cases, this is the number of logical CPU cores (including hyperthreading).

```cpp
#include <thread>
#include <iostream>

int main()
{
    unsigned int cores = std::thread::hardware_concurrency();
    std::cout << "Hardware concurrency: " << cores << "\n";
    return 0;
}
```

This value is a hint, not a guarantee. If the information is unavailable, the function returns 0. On an 8-core, 16-thread CPU, it typically returns 16. In container environments, it might return the number of CPU cores allocated to the container rather than the total cores of the physical machine. The most common use is to decide the size of a thread pool or the number of task partitions based on it—but don't treat it as an exact value; it's best to check if the return value is 0 before using it.

## Exceptions in Thread Functions

There is a very important rule here: **exceptions should never escape a thread function**. If an exception escapes from a thread function (i.e., the thread function throws an exception but it isn't caught inside the thread), `std::terminate()` will be called and the program will crash outright.

```cpp
#include <thread>
#include <iostream>
#include <stdexcept>

void unsafe_worker()
{
    throw std::runtime_error("Oops, something went wrong!");
    // 异常逃逸线程函数 -> std::terminate()
}

int main()
{
    try {
        std::thread t(unsafe_worker);
        t.join();  // 永远到不了这里
    } catch (const std::exception& e) {
        // 这个 catch 捕获不到线程里的异常！
        // 线程函数中的异常和主线程的 try-catch 是完全隔离的
        std::cout << "Caught: " << e.what() << "\n";
    }
    return 0;
}
```

This behavior actually makes sense. Each thread has its own independent call stack, and the exception handling mechanism (stack unwinding, catch matching) only works on the current thread's stack. If an exception pierces through the thread function, it means there's no catch block to receive it—except for `std::terminate`. The main thread's `try-catch` and the child thread's exception handling are two completely isolated worlds.

The correct approach is to handle all possible exceptions inside the thread function, or pass the exception information back to the caller through some mechanism (`std::promise`/`std::future`, `std::exception_ptr`). A simplest defensive pattern looks like this:

```cpp
#include <thread>
#include <iostream>
#include <stdexcept>
#include <functional>

void safe_worker(std::function<void()> task)
{
    try {
        task();
    } catch (const std::exception& e) {
        // 在线程内部处理异常，或者记录下来
        std::cerr << "Thread caught exception: "
                  << e.what() << "\n";
    } catch (...) {
        std::cerr << "Thread caught unknown exception\n";
    }
}

int main()
{
    std::thread t(safe_worker, []() {
        throw std::runtime_error("Oops!");
    });
    t.join();  // OK：异常在线程内部被捕获，程序不会 terminate
    std::cout << "Main continues normally\n";
    return 0;
}
```

In later chapters, we'll introduce `std::async` and `std::promise`/`std::future`, which provide more elegant ways to propagate child thread exceptions back to the main thread. But in scenarios where we use `std::thread` directly, the "catch-all inside the thread" pattern above is the most basic defensive measure.

## Basic Pattern: Spawn Threads, Join on Scope Exit

With the knowledge above, we can summarize a most basic thread usage pattern: spawn a thread for each subtask, and join all threads before the current scope exits. Expressed in code:

```cpp
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>

void process_range(const std::vector<int>& input,
                   std::vector<int>& output,
                   std::size_t start,
                   std::size_t end)
{
    for (std::size_t i = start; i < end; ++i) {
        // 模拟一个计算密集型操作
        output[i] = input[i] * input[i];
    }
}

int main()
{
    constexpr std::size_t kDataSize = 10'000'000;
    constexpr unsigned int kNumThreads = 4;

    std::vector<int> input(kDataSize);
    std::vector<int> output(kDataSize);

    // 初始化输入数据
    for (std::size_t i = 0; i < kDataSize; ++i) {
        input[i] = static_cast<int>(i);
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    std::size_t chunk_size = kDataSize / kNumThreads;

    // 派生线程
    for (unsigned int i = 0; i < kNumThreads; ++i) {
        std::size_t start = i * chunk_size;
        std::size_t end = (i == kNumThreads - 1)
                              ? kDataSize
                              : start + chunk_size;
        threads.emplace_back(process_range,
                             std::cref(input),
                             std::ref(output),
                             start,
                             end);
    }

    // 在作用域退出前 join 所有线程
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  end_time - start_time);

    std::cout << "Processed " << kDataSize << " elements in "
              << ms.count() << " ms using "
              << kNumThreads << " threads\n";
    return 0;
}
```

The execution flow of this code is clear: split the data into N chunks, hand each chunk to a thread for processing, and then the main thread waits for all worker threads to finish. `threads.emplace_back(...)` constructs the thread objects directly in the vector, avoiding extra moves. The final for loop joins them one by one, ensuring all threads have finished executing before exiting.

There is a detail worth noting here: `output` is passed by reference into each thread (via `std::ref`), but different threads write to different ranges of `output`—there's no overlap, so it won't produce a data race. This "partitioned parallelism" pattern is one of the easiest ways to write correct code in multithreaded programming: as long as you ensure each thread only touches its own portion of data, you don't need any synchronization mechanism.

But this pattern has a problem—if a thread's `process_range` function throws an exception, the destructor of `threads` will be called during stack unwinding, and as we mentioned earlier, destructing a `joinable` thread calls `std::terminate`. To solve this problem, we need to use RAII to wrap the join logic, ensuring correct join even if an exception occurs. We'll implement this improved version in the upcoming "Thread Ownership and RAII" article.

## Summary

In this article, we completed a comprehensive review of the basic interface of `std::thread`. We saw three ways to construct threads—function pointers, lambdas, and functors—whose essence is passing in a callable object and arguments. `join()` and `detach()` are two radically different thread management strategies: join means "wait for me to finish before you go," and detach means "you go first, I'll clean up on my own." If you do nothing and let a `std::thread` destruct, the standard will callously call `std::terminate`—this is C++ using the harshest possible way to remind you: thread lifetimes must be explicitly managed.

We also learned about thread identification (`get_id()`), native handles (`native_handle()`), and hardware concurrency queries (`hardware_concurrency()`), as well as a rule that's easy to overlook but crucial: exceptions should not escape thread functions, or they will trigger `std::terminate`.

Finally, we established a basic parallel processing pattern: data partitioning + multithreaded processing + join one by one. This pattern works well in simple scenarios, but it doesn't handle exception safety and RAII—those are the problems we're going to solve next.

In the next article, we'll dive into a deeper topic: the thread argument passing mechanism. We'll see how the decay-copy semantics of `std::thread` work, why `std::ref` is a double-edged sword, and what kind of disaster strikes when detach and reference capture are combined. The real pitfalls lie ahead.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Parallel Array Transformation

Given a `std::vector<double>`, use `std::thread` to compute the square root of each element. Requirements:

1. Use `std::thread::hardware_concurrency()` to get the core count, and decide how many threads to create based on it
2. Each thread processes a range of the array
3. After all threads finish, print the first 10 results for verification

Hint: Watch out for the case where `hardware_concurrency()` might return 0, and handle the situation where the array size isn't evenly divisible by the number of threads.

### Exercise 2: Verify terminate Behavior

Write a program that intentionally lets a `std::thread` `joinable` destruct without calling `join()` or `detach()`. Run the program and observe the output when `std::terminate` is called. Then wrap this code with `try-catch` in a `main` function, and see if you can "catch" this terminate—the answer is: no, `std::terminate` cannot be caught by a regular `try-catch`; it is a forced termination of the program.

### Exercise 3: Thread ID Mapping

Write a program that creates N threads (for example, four), where each thread stores its `std::this_thread::get_id()` into a shared `std::map<std::thread::id, int>` (the key is the thread ID, the value is the thread number 0-3). Because multiple threads writing to a map simultaneously is a data race, we'll keep it simple for now: have each thread output its result to `std::cout`, and the main thread records it. The purpose of this exercise is to get you familiar with the basic usage of `std::thread::id`.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::thread::detach — cppreference](https://en.cppreference.com/w/cpp/thread/thread/detach)
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency)
- [C++ Core Guidelines: CP.20 — Use RAII, never plain `lock()`/`unlock()`](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [What does `decay_copy` in the constructor of `std::thread` do? — StackOverflow](https://stackoverflow.com/questions/67947814/what-does-decay-copy-in-the-constructor-in-a-stdthread-object-do)
