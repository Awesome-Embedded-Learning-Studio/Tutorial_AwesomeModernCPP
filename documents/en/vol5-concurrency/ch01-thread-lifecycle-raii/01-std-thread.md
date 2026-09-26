---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C++ thread creation, join, detach, thread IDs, and hardware concurrency queries, and build intuition for your first multithreaded program
difficulty: beginner
order: 1
platform: host
prerequisites:
- CPU Cache and OS Threads
reading_time_minutes: 18
related:
- Thread Arguments and Lifetime
- Thread Ownership and RAII
tags:
- host
- cpp-modern
- beginner
- 入门
title: std::thread Basics
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/01-std-thread.md
  source_hash: 59fb3c0dade326543df8870667c72bf887bb59cf9e5ed995cc67c611868ccb2f
  translated_at: '2026-09-26T06:28:07+00:00'
  engine: anthropic
  token_count: 9200
---
# std::thread Basics

In the previous chapter we talked about the CPU cache hierarchy, the MESI protocol, and false sharing, and we looked at Linux's thread model and the futex mechanism—all of it the physical stage on which multithreaded programs perform. But knowing what the stage looks like isn't enough; we have to step onto it and act ourselves. This article is our first appearance: starting from the construction of `std::thread`, we'll work out how threads are created, how to wait for them, how to "let go and walk away", and which pitfalls are one careless step away.

`std::thread` is the standard thread class introduced in C++11, defined in the `<thread>` header. It is the C++ standard library's direct wrapper around operating system threads—on Linux, behind every `std::thread` object stands a pthread, and that pthread is mapped to a kernel scheduling entity through the `clone()` system call. The 1:1 model we mentioned in the previous chapter takes concrete form right here.

## Constructing a std::thread: Three Ways

The constructor of `std::thread` takes a **callable object** plus an optional list of arguments. C++ gives us several ways to express "callable", and we'll look at them one by one.

### Function Pointers

The plainest way is to pass an ordinary function pointer:

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

`std::thread t(print_hello, 42)` does several things: first, it packs `print_hello` (the function pointer) and `42` (the argument) into internal storage; then it calls the underlying `pthread_create` (or an equivalent system call) to create a new operating system thread; finally, in that separate execution context, the new thread calls `print_hello(42)` with the saved argument. Note that the argument `42` is **copied** into the thread's internal storage—we'll expand on the details of argument passing in the next article.

### Lambda Expressions

In real-world engineering, lambdas are the most common way to create threads, because they define what the thread should do right at the call site, with no extra function declaration needed:

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

This code works fine, but look closely: `[&data, &sum]` captures by reference—completely fine in a single-threaded scenario, but what if the thread gets detached, or outlives the scopes of `data` and `sum`? That is a breeding ground for dangling references. Let's note this "smell" for now; the next article takes it apart systematically.

### Function Objects (Functors)

The third way is to pass an instance of a class that overloads `operator()`:

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
    const std::vector<int>& data_;  // Note: reference member
    int& result_;                    // Reference member
};

int main()
{
    std::vector<int> data = {1, 2, 3, 4, 5};
    int result = 0;

    // Note: braces or a lambda are needed here to avoid the most vexing parse
    // std::thread t(Accumulator(data, result));  // Compile error! Parsed as a function declaration
    Accumulator acc(data, result);
    std::thread t(acc);  // OK: copies acc into the thread

    t.join();
    std::cout << "Result = " << result << "\n";
    return 0;
}
```

There is a classic C++ trap here—if you write `std::thread t(Accumulator(data, result));` directly, the compiler parses it as a declaration of a function named `t` (whose parameter is a pointer to `Accumulator`) rather than the definition of a thread object. This is the infamous "most vexing parse" problem. There are several ways out: extra braces with `std::thread t{Accumulator(data, result)};`, a lambda with `std::thread t([&](){ ... });`, or constructing a named object first and passing it in, as above.

Each of the three approaches has its place. Function pointers suit simple, stateless thread functions; lambdas suit defining local logic at the call site and are the most common choice in day-to-day development; functors suit complex tasks that need to carry state—but watch the lifetime risks that reference members bring. In real projects, a lambda covers more than 90% of the scenarios.

## join() vs detach(): Two Radically Different Strategies

Once a thread is created, we must make a decision before its lifetime ends: **join** or **detach**. This decision bears directly on the program's correctness.

### join: Wait for the Thread to Finish

`join()` is a blocking call—the current thread stands still there and only moves on once the target thread finishes executing. By analogy: you send someone off to do a job, you wait in place until they finish, and then you both carry on. This is the most common pattern, and the safest one.

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

Run this code and you'll see the output happen strictly in the order: Main starts -> Worker starts -> Worker finishes -> Main continues. `join()` guarantees that the thread's results become visible to the calling thread when `join` returns—this is a happens-before relationship.

### detach: Let It Go

`detach()` does exactly the opposite—it "peels" the thread out of the `std::thread` object's management. Once peeled away, the thread runs independently in the background (a so-called daemon thread), and the `std::thread` object no longer holds any reference to it. You can't join it anymore either—the `std::thread` object's `joinable()` returns `false`.

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

If you run this code, chances are you won't see the "Background task finished" line—the main thread waits only 1 second before exiting, while the detached thread needs 2. When the process exits, all threads (detached ones included) are forcibly terminated, with no chance to clean up. That is detach's biggest risk: **you have completely lost control over when the thread executes**.

So when should you use detach? Honestly, in most application code detach is not a good choice. The scenarios where it fits are extremely limited—for example a background logging thread whose job is flushing logs from an in-memory buffer to disk, where you don't care when it ends as long as it eventually writes the data out. But even in that scenario, a `joinable` thread paired with an explicit shutdown signal is usually the sounder approach.

### The Consequence of Neither join nor detach: std::terminate

If you neither call `join()` nor `detach()` on a `joinable` `std::thread` object and just let it reach its destructor—your program calls `std::terminate()` and crashes outright. This is not advice; it is hard, standard-mandated behavior:

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
    // Neither join() nor detach()
    // t's destructor calls std::terminate()
    return 0;  // terminate called without an active exception
}
```

The C++ standard is designed this way for a reason. If the destructor silently joined for you, destruction could block—something many developers refuse to accept (destructors should be fast). If the destructor silently detached for you, the thread might access references that no longer exist after the object's destruction—that is undefined behavior, worse than a crash. By choosing to `terminate` outright, the standard forces you to **make the decision explicitly**: either wait for it to finish (join) or let it go (detach), but you cannot pretend the problem doesn't exist.

This design philosophy runs through the entire C++ concurrency API: no implicit, potentially surprising behavior; the decisions belong to the programmer. The price is that you must remember to handle every thread's join/detach on every code path, exception paths included. A common pattern is an RAII wrapper—save the thread on construction, join automatically on destruction—a topic we'll expand on in the rest of this chapter.

## Thread Identification and Queries

### get_id(): The Thread's Identity Number

Every thread has a unique identifier, of type `std::thread::id`. You can get the ID of a thread object via `std::thread::get_id()`, and the ID of the current thread via `std::this_thread::get_id()`. `std::thread::id` supports comparison and output to `std::ostream`, which makes debugging and logging easier:

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

    // After join or detach, get_id() returns a default-constructed id
    std::cout << "After join, worker ID: "
              << t.get_id() << "\n";
    return 0;
}
```

A few things to note: the concrete value of `std::thread::id` is implementation-defined—different compilers and platforms may print it in different formats (GCC usually prints a number, MSVC may print a hexadecimal address), so don't build logic on its exact format. After `join()` or `detach()`, `get_id()` returns a default-constructed `std::thread::id{}`, meaning "not associated with any thread"—the same value a default-constructed `std::thread` object's `get_id()` returns.

The most practical use of `thread::id` is as a key for `std::hash`, for assigning per-thread resources (say, an independent memory pool or log buffer per thread). You can also use it to check "is the current thread the main thread", implementing a simple thread-safety assertion.

### native_handle(): Reaching the Operating System's Native Handle

`std::thread` is a standard library abstraction, but sometimes you need to manipulate the underlying operating system thread directly—setting thread priority, CPU affinity, or the thread name. `native_handle()` returns the platform-dependent native thread handle: `pthread_t` on Linux, `HANDLE` on Windows.

```cpp
#include <thread>
#include <iostream>

// Note: the following code is Linux-specific
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

void set_high_priority(std::thread& t)
{
#ifndef _WIN32
    sched_param param;
    param.sched_priority = 10;  // A higher priority (the exact value depends on the scheduling policy)
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

This code is clearly not portable—it only compiles on platforms with pthread support. In real projects, platform-specific code is usually isolated with `#ifdef` or abstracted into a platform layer. `native_handle()` gives you an "escape hatch" for talking straight to the operating system when the standard library isn't enough.

### hardware_concurrency(): How Many Cores Do I Have

`std::thread::hardware_concurrency()` is a static member function that returns a hint about the number of threads the current system can genuinely execute concurrently—in most cases, the CPU's logical core count (hyperthreads included).

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

This value is a hint, not a guarantee. If the information is unavailable, the function returns 0. On an 8-core, 16-thread CPU it usually returns 16. In container environments it may return the number of CPU cores allocated to the container rather than the physical machine's total. The most common use is sizing a thread pool or deciding how many task shards to split work into—but don't treat it as an exact value, and it's wise to check whether it returned 0 before using it.

## Exceptions in Thread Functions

Here is a rule of utmost importance: **exceptions must never escape the thread function**. If an exception escapes the thread function (that is, the thread function throws and nothing inside the thread catches it), `std::terminate()` is called and the program crashes outright.

```cpp
#include <thread>
#include <iostream>
#include <stdexcept>

void unsafe_worker()
{
    throw std::runtime_error("Oops, something went wrong!");
    // The exception escapes the thread function -> std::terminate()
}

int main()
{
    try {
        std::thread t(unsafe_worker);
        t.join();  // Never reached
    } catch (const std::exception& e) {
        // This catch cannot catch the exception from the thread!
        // Exceptions in the thread function are completely isolated from the main thread's try-catch
        std::cout << "Caught: " << e.what() << "\n";
    }
    return 0;
}
```

This behavior is actually quite reasonable. Every thread has its own call stack, and the exception machinery (stack unwinding, catch matching) works only on the current thread's stack. If an exception punches through the thread function, no catch block can receive it—except `std::terminate`. The main thread's `try-catch` and the child thread's exception handling are two completely isolated worlds.

The right approach is to handle every possible exception inside the thread function, or to pass exception information back to the caller through some mechanism (`std::promise`/`std::future`, `std::exception_ptr`). The simplest defensive pattern looks like this:

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
        // Handle the exception inside the thread, or log it
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
    t.join();  // OK: the exception is caught inside the thread; the program does not terminate
    std::cout << "Main continues normally\n";
    return 0;
}
```

In later chapters we'll introduce `std::async` and `std::promise`/`std::future`, which offer more elegant ways to relay a child thread's exception back to the main thread. But when using `std::thread` directly, the "catch-all inside the thread" pattern above is the most basic line of defense.

## A Basic Pattern: Spawn Threads, Join on Scope Exit

With the knowledge above, we can distill the most basic thread-usage pattern: spawn one thread per subtask, and join all threads before the current scope exits. Expressed in code:

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
        // Simulate a compute-intensive operation
        output[i] = input[i] * input[i];
    }
}

int main()
{
    constexpr std::size_t kDataSize = 10'000'000;
    constexpr unsigned int kNumThreads = 4;

    std::vector<int> input(kDataSize);
    std::vector<int> output(kDataSize);

    // Initialize the input data
    for (std::size_t i = 0; i < kDataSize; ++i) {
        input[i] = static_cast<int>(i);
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    std::size_t chunk_size = kDataSize / kNumThreads;

    // Spawn threads
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

    // Join all threads before leaving the scope
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

The execution flow of this code is clear: split the data into N chunks, hand each chunk to a thread, and the main thread waits for all worker threads to finish. `threads.emplace_back(...)` constructs the thread objects directly inside the vector, avoiding extra moves. The final `for` loop joins one by one, ensuring every thread has finished before exit.

One detail deserves attention: `output` is passed by reference to each thread (via `std::ref`), but different threads write different ranges of `output`—no overlap, hence no data race. This "partitioned parallelism" pattern is one of the easiest ways to write correct multithreaded code: as long as each thread touches only its own share of the data, you need no synchronization mechanism at all.

But this pattern has a problem—if some thread's `process_range` throws, the destructor of `threads` runs during stack unwinding, and as we said earlier, destroying a `joinable` thread calls `std::terminate`. To fix this, we need to wrap the join logic in RAII to guarantee a correct join even when an exception occurs. We'll implement that improved version in the upcoming "Thread Ownership and RAII" article.

## Run It Online

Try the three ways to construct a `std::thread`, thread ID queries, and partitioned parallel data processing online:

<OnlineCompilerDemo
  title="std::thread Basics"
  source-path="code/examples/vol5/09_std_thread.cpp"
  description="Explore the function pointer, lambda, and functor ways to construct threads, plus partitioned data parallelism"
  allow-run
/>

## Summary

In this article we completed a full tour of the basic `std::thread` interface. We saw three ways to construct a thread—function pointer, lambda, and functor—all of which boil down to passing in a callable object and arguments. `join()` and `detach()` are two radically different thread management strategies: join says "wait for me to finish before you go", detach says "you go ahead, I'll wrap up on my own". If you do nothing and let a `std::thread` destruct, the standard will call `std::terminate` without mercy—C++ reminding you in the sternest possible way that a thread's lifecycle must be managed explicitly.

We also covered thread identification (`get_id()`), the native handle (`native_handle()`), and hardware concurrency queries (`hardware_concurrency()`), plus one easily overlooked but vital rule: exceptions must not escape the thread function, or `std::terminate` fires.

Finally, we established a basic parallel processing pattern: partition the data + process with multiple threads + join one by one. This pattern works well in simple scenarios, but it doesn't handle exception safety or RAII—that is the problem we solve next.

The next article moves into deeper territory: how arguments are passed to threads. We'll see how `std::thread`'s decay-copy semantics work, why `std::ref` is a double-edged sword, and what kind of disaster detach combined with by-reference capture can unleash. The real pitfalls lie ahead.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) under `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Parallel Array Transformation

Given a `std::vector<double>`, use `std::thread` to take the square root of every element. Requirements:

1. Use `std::thread::hardware_concurrency()` to get the core count and decide from it how many threads to spawn
2. Each thread processes one range of the array
3. After all threads finish, print the first 10 results for verification

Hint: watch out for the case where `hardware_concurrency()` returns 0, and handle an array size that doesn't divide evenly by the thread count.

### Exercise 2: Verifying terminate Behavior

Write a program that deliberately lets a `joinable` `std::thread` destruct without calling `join()` or `detach()`. Run it and observe the output when `std::terminate` is invoked. Then wrap that code in `main` with a `try-catch` and see whether you can "catch" the terminate—the answer is no: `std::terminate` cannot be caught by an ordinary `try-catch`; it is a forced termination of the program.

### Exercise 3: Thread ID Mapping

Write a program that creates N threads (say 4), where each thread stores its own `std::this_thread::get_id()` into a shared `std::map<std::thread::id, int>` (the key is the thread ID, the value is the thread's number 0-3). Since multiple threads writing the map at the same time is a data race, keep it simple for now: each thread prints its result to `std::cout`, and the main thread records it. The goal of this exercise is to get you comfortable with the basic use of `std::thread::id`.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::thread::detach — cppreference](https://en.cppreference.com/w/cpp/thread/thread/detach)
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency)
- [C++ Core Guidelines: CP.20 — Use RAII, never plain `lock()`/`unlock()`](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [What does `decay_copy` in the constructor of `std::thread` do? — StackOverflow](https://stackoverflow.com/questions/67947814/what-does-decay-copy-in-the-constructor-in-a-stdthread-object-do)
