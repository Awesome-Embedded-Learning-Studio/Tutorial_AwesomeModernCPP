---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C++ thread creation, `join`, `detach`, IDs, and hardware concurrency
  queries to build intuition for your first multithreaded program.
difficulty: beginner
order: 1
platform: host
prerequisites:
- CPU cache 与 OS 线程
reading_time_minutes: 18
related:
- 线程参数与生命周期
- 线程所有权与 RAII
tags:
- host
- cpp-modern
- beginner
- 入门
title: std::thread Basics
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/01-std-thread.md
  source_hash: 59fb3c0dade326543df8870667c72bf887bb59cf9e5ed995cc67c611868ccb2f
  translated_at: '2026-06-24T01:05:58.949700+00:00'
  engine: anthropic
  token_count: 3671
---
# std::thread Basics

In the previous chapter, we discussed the CPU cache hierarchy, the MESI protocol, false sharing, and looked at Linux threading models and the futex mechanism. These constitute the physical stage upon which multithreaded programs run. But knowing what the stage looks like isn't enough; we need to get on it ourselves. This chapter marks our debut: starting with the construction of `std::thread`, we will figure out how to create threads, how to wait for them, how to "detach and forget," and what pitfalls we might encounter during the process.

`std::thread` is the standard thread class introduced in C++11, defined in the `<thread>` header. It is a direct wrapper around operating system threads provided by the C++ Standard Library. On Linux, every `std::thread` object corresponds to a pthread, which in turn maps to a kernel scheduling entity via the `clone()` system call. The 1:1 model we mentioned in the last chapter is exactly what happens here.

## Constructing std::thread in Three Ways

The `std::thread` constructor accepts a **callable object** and an optional list of arguments. C++ provides us with several ways to express "callable," so let's examine them one by one.

### Function Pointer

The most straightforward way is to pass a plain function pointer:

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

`std::thread t(print_hello, 42)` does a few things: First, it packs `print_hello` (the function pointer) and `42` (the argument) into internal storage. Then, it invokes the underlying `pthread_create` (or an equivalent system call) to create a new operating system thread. Finally, the new thread calls `print_hello(42)` with the saved arguments in that separate execution context. Note that the argument `42` is **copied** into the thread's internal storage—we will dive into the details of argument passing in the next post.

### Lambda Expressions

In real-world projects, lambdas are the most common way to create threads because they allow us to define the thread's task directly at the call site, without needing to declare a separate function:

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

This code works correctly, but if you look closely, `[&data, &sum]` captures by reference. While this is perfectly fine in a single-threaded scenario, what happens if the thread is detached or its lifetime extends beyond the scope of `data` and `sum`? This creates fertile ground for dangling references. Let's keep this "smell" in mind; we will systematically dissect it in the next post.

### Function Objects (Functors)

The third method is to pass a class instance that overloads `operator()`:

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

Here is a classic C++ pitfall—if you write `std::thread t(Accumulator(data, result));` directly, the compiler will parse it as a function declaration named `t` (where the parameter type is a pointer to `Accumulator`), rather than a definition of a thread object. This is known as the "most vexing parse" problem. There are several ways to resolve this: use extra braces `std::thread t{Accumulator(data, result)};`, use a lambda `std::thread t([&](){ ... });`, or construct a named object first and pass it in, as shown above.

Each of these three methods has its own use case. Function pointers are suitable for simple, stateless thread functions; lambdas are ideal for defining local logic at the call site and are the most common approach in daily development; functors are appropriate for complex tasks that need to carry state—but be mindful of the lifetime risks associated with reference members. In actual projects, lambdas cover more than 90% of scenarios.

## join() vs detach(): Two Distinct Strategies

Once a thread is created, we must make a decision before its lifetime ends: **join** or **detach**. This decision directly impacts the correctness of the program.

### join: Waiting for the Thread to Finish

`join()` is a blocking call—the current thread will pause there and wait for the target thread to complete execution before proceeding. An analogy would be: you send someone to do a task, you stand there and wait until they finish, and then you continue together. This is the most common pattern, and also the safest.

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

Running this code, we see that the output strictly follows the sequence of Main start -> Worker start -> Worker finish -> Main continue. `join()` guarantees that the thread's execution results are visible to the calling thread when `join` returns—this establishes a happens-before relationship.

### detach: Letting go

`detach()` does exactly the opposite—it "detaches" the thread from the management of the `std::thread` object. Once detached, the thread runs independently in the background (a so-called daemon thread), and the `std::thread` object no longer holds any reference to it. You can no longer join it—the `joinable()` method of the `std::thread` object will return `false`.

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

If you run this code, you likely won't see the "Background task finished" output. This is because the main thread waits only one second before exiting, while the detached thread needs two seconds. When the process exits, all threads (including detached ones) are forcibly terminated without any chance to clean up. This is the biggest risk of `detach`: **you completely lose control over the thread's execution timing**.

So, when should we use `detach`? Honestly, in most application code, `detach` is not a good choice. Suitable scenarios are very limited—for example, a background logging thread whose job is to flush logs from a memory buffer to disk. You might not care when it ends, as long as it eventually writes the data. However, even in this scenario, using a `joinable` thread with an explicit shutdown signal is usually a safer approach.

### Consequences of neither joining nor detaching: `std::terminate`

If you let a `joinable` `std::thread` object reach its destructor without calling `join()` or `detach()`, your program will call `std::terminate()` and crash immediately. This isn't just a suggestion; it is a hard requirement mandated by the standard:

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

The C++ standard is designed this way for a reason. If the destructor silently joined for you, destruction might block—which is something many developers are unwilling to accept (destructors should be fast). If the destructor silently detached, the thread might access references that no longer exist after the object is destroyed—that is undefined behavior, which is worse than a crash. By choosing to immediately `terminate`, the standard forces you to **explicitly make a decision**: you either wait for it to finish (join) or let it go (detach), but you cannot pretend the problem does not exist.

This design philosophy permeates the entire C++ concurrency API: do not do anything implicit that might be surprising, and give the decision-making power to the programmer. The cost is that you must remember to handle thread join/detach on every code path, including exception paths. A common pattern is to use an RAII wrapper—which saves the thread in the constructor and automatically joins in the destructor—we will expand on this topic later in this chapter.

## Thread Identification and Querying

### get_id(): The Thread ID

Every thread has a unique identifier of type `std::thread::id`. You can obtain a specific thread object's ID via `std::thread::get_id()`, or get the current thread's ID via `std::this_thread::get_id()`. `std::thread::id` supports comparison operations and output to `std::ostream`, making it convenient for debugging and logging:

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

Here are a few points to note: the specific value of `std::thread::id` is implementation-defined—the output format may vary across different compilers and platforms (GCC usually outputs a number, while MSVC might output a hexadecimal address), so do not rely on its specific format for logical checks. After calling `join()` or `detach()`, `get_id()` returns a default-constructed `std::thread::id{}`, indicating "no associated thread"—this is identical to the return value of `get_id()` for a default-constructed `std::thread` object.

The most practical use case for `thread::id` is as a key for `std::hash`, allowing us to allocate resources to threads (such as a separate memory pool or log buffer for each thread). We can also use it to detect if the "current thread is the main thread," implementing simple thread-safe assertions.

### native_handle(): Accessing the Native OS Handle

`std::thread` is a standard library abstraction, but sometimes we need to manipulate the underlying operating system thread directly—for example, to set thread priority, CPU affinity, or the thread name. `native_handle()` returns a platform-dependent native thread handle: on Linux it is `pthread_t`, and on Windows it is `HANDLE`.

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

This code is clearly non-portable—it will only compile on platforms that support pthreads. In real-world projects, we usually isolate platform-specific code with `#ifdef`, or abstract it into a platform layer. `native_handle()` provides an "escape hatch" that allows us to interact directly with the operating system when the standard library isn't quite enough.

### hardware_concurrency(): How many cores do I have?

`std::thread::hardware_concurrency()` is a static member function that returns a hint indicating the number of threads that can truly run concurrently on the current system—in most cases, this is the number of logical CPU cores (including hyperthreading).

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

This value is indicative, not guaranteed. If the information is unavailable, the function returns zero. On a CPU with eight cores and 16 threads, it typically returns 16. In containerized environments, it may return the number of cores allocated to the container rather than the total physical cores of the host machine. The most common use case is determining the thread pool size or the number of task shards based on this value—but do not treat it as an exact value. It is best practice to check if the return value is zero before using it.

## Exceptions in Thread Functions

There is one critical rule: **exceptions must never escape a thread function**. If an exception escapes from a thread function (that is, the thread function throws an exception that is not caught internally), `std::terminate()` is called, causing the program to crash immediately.

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

This behavior is actually quite reasonable. Each thread has its own independent call stack, and the exception handling mechanism (stack unwinding, `catch` matching) operates only on the current thread's stack. If an exception penetrates the thread function, it means there is no `catch` block capable of catching it—except for `std::terminate`. The main thread's `try-catch` and the child thread's exception handling exist in two completely isolated worlds.

The correct approach is to handle all possible exceptions within the thread function, or to propagate exception information back to the caller via a mechanism like `std::promise`/`std::future` or `std::exception_ptr`. A simple defensive pattern looks like this:

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

In later chapters, we will introduce `std::async` and `std::promise`/`std::future`, which provide a more elegant way to propagate exceptions from child threads back to the main thread. However, in scenarios where we use `std::thread` directly, the "catch-all inside the thread" pattern shown above is the most basic defensive measure.

## Basic Pattern: Spawn Threads, Join on Scope Exit

With the knowledge we have gained so far, we can summarize a most basic threading pattern: we spawn a thread for each subtask, and join all threads before the current scope exits. Expressed in code, this is:

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

The execution flow of this code is straightforward: we split the data into N chunks, assign each chunk to a thread for processing, and then the main thread waits for all worker threads to finish. `threads.emplace_back(...)` constructs the thread object directly inside the `vector`, avoiding unnecessary moves. The final `for` loop joins the threads one by one, ensuring that all threads have completed execution before exiting.

There is a noteworthy detail here: `output` is passed by reference to each thread (via `std::ref`), but different threads write to different ranges of `output`—there is no overlap, so no data race occurs. This "partitioned parallelism" pattern is one of the easiest ways to write correct multithreaded code: as long as we ensure each thread only touches its own share of the data, we don't need any synchronization mechanisms.

However, this pattern has a flaw: if the `process_range` function of a thread throws an exception, the destructor of `threads` will be called during stack unwinding. As we mentioned earlier, the destructor of a `joinable` thread calls `std::terminate`. To solve this, we need to wrap the join logic using RAII to ensure correct joining even if an exception occurs. We will implement this improved version in the upcoming article on "Thread Ownership and RAII".

## Run Online

Experience the three ways to construct a `std::thread`, query thread IDs, and perform partitioned parallel processing:

<OnlineCompilerDemo
  title="std::thread Basics"
  source-path="code/examples/vol5/09_std_thread.cpp"
  description="Explore function pointers, lambdas, and functors for thread construction and partitioned parallel processing"
  allow-run
/>

## Summary

In this article, we completed a comprehensive overview of the basic `std::thread` interface. We looked at three ways to construct threads—function pointers, lambdas, and functors—whose essence is passing a callable object and arguments. `join()` and `detach()` represent two distinct thread management strategies: join means "wait for me to finish," while detach means "go ahead, I'll clean up myself." If you let a `std::thread` be destroyed without doing anything, the standard will mercilessly call `std::terminate`—this is C++ using the strictest possible way to remind you: the thread lifecycle must be managed explicitly.

We also learned about thread identification (`get_id()`), native handles (`native_handle()`), and hardware concurrency queries (`hardware_concurrency()`), as well as a rule that is easily overlooked but critical: exceptions should not escape the thread function, otherwise `std::terminate` will be triggered.

Finally, we established a basic parallel processing pattern: data partitioning + multithreading + individual joins. This pattern works well in simple scenarios, but it lacks exception safety and RAII—problems we will solve next.

In the next article, we will dive into a deeper topic: the thread argument passing mechanism. We will see how the decay-copy semantics of `std::thread` work, why `std::ref` is a double-edged sword, and what kind of disaster occurs when `detach` is combined with reference captures. The real pitfalls lie ahead.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Parallel Array Transformation

Given a `std::vector<double>`, use `std::thread` to calculate the square root of each element. Requirements:

1. Use `std::thread::hardware_concurrency()` to get the number of cores and determine the number of threads based on that.
2. Each thread processes a segment of the array.
3. After all threads finish, print the first 10 results for verification.

**Hint:** Pay attention to the case where `hardware_concurrency()` might return 0, and how to handle situations where the array size is not divisible by the number of threads.

### Exercise 2: Verify Terminate Behavior

Write a program that intentionally allows a `joinable` `std::thread` to be destroyed without calling `join()` or `detach()`. Run the program and observe the output when `std::terminate` is called. Then, wrap this code in `main` with a `try-catch` block to see if you can "catch" this terminate—the answer is: no, `std::terminate` cannot be caught by ordinary `try-catch` blocks; it is a forced termination of the program.

### Exercise 3: Thread ID Mapping

Write a program that creates N threads (for example, four), where each thread stores its own `std::this_thread::get_id()` into a shared `std::map<std::thread::id, int>` (key is the thread ID, value is the thread number 0-3). Since multiple threads writing to the map simultaneously is a data race, we will keep it simple for now: each thread outputs the result to `std::cout`, and the main thread records it. The purpose of this exercise is to familiarize you with the basic usage of `std::thread::id`.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::thread::detach — cppreference](https://en.cppreference.com/w/cpp/thread/thread/detach)
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency)
- [C++ Core Guidelines: CP.20 — Use RAII, never plain `lock()`/`unlock()`](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [What does `decay_copy` in the constructor of `std::thread` do? — StackOverflow](https://stackoverflow.com/questions/67947814/what-does-decay-copy-in-the-constructor-in-a-stdthread-object-do)
