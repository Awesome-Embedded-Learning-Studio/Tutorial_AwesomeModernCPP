---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C++ thread creation, joining, detaching, IDs, and hardware concurrency
  queries, and build intuition for your first multithreaded program.
difficulty: beginner
order: 1
platform: host
prerequisites:
- CPU cache 与 OS 线程
reading_time_minutes: 15
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
  engine: anthropic
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/01-std-thread.md
  source_hash: 3c0b3d21b5ed102e2c133bd511b483dc03343df1fe9967f874496d7c911908d4
  token_count: 3677
  translated_at: '2026-06-15T09:23:40.757337+00:00'
---
# std::thread Basics

In the previous chapter, we discussed the CPU cache hierarchy, the MESI protocol, and false sharing, as well as Linux's threading model and the futex mechanism—these constitute the physical stage where multithreaded programs run. But knowing what the stage looks like isn't enough; we need to get on it and perform. This post marks our first debut: starting with the construction of `std::thread`, we will figure out how to create threads, how to wait for them, how to "let them go," and what pitfalls we might stumble into during these operations.

`std::thread` is the standard thread class introduced in C++11, defined in the `<thread>` header file. It is a direct wrapper of the operating system threads by the C++ Standard Library—on Linux, behind every `std::thread` object lies a pthread, which is mapped to a kernel scheduling entity via the `clone` system call. The 1:1 model we mentioned in the last post is exactly embodied here.

## Constructing std::thread in Three Ways

The `std::thread` constructor accepts a **callable object** and an optional list of arguments. C++ provides us with several ways to express "callable," so let's examine them one by one.

### Function Pointer

The most straightforward way is to pass a plain function pointer:

```cpp
void task(int n) {
    printf("Task %d running\n", n);
}

std::thread t(task, 42); // Pass function pointer and arguments
```

`std::thread` does a few things here: first, it packs `task` (the function pointer) and `42` (the argument) into internal storage; then, it calls the underlying `pthread_create` (or an equivalent system call) to create a new operating system thread; finally, the new thread calls `task` with the saved arguments in that independent execution context. Note that the argument `42` is **copied** into the thread's internal storage—we will dive into the details of argument passing in the next post.

### Lambda Expression

In actual engineering, lambdas are the most common way to create threads because they allow you to define what the thread does directly at the call site without declaring an extra function:

```cpp
int data = 10;
std::thread t([&]() {
    // Capture 'data' by reference
    data += 5;
});
```

This code works, but if you look closely, `data` is captured by reference—while this is perfectly fine in a single-threaded context, what if the thread is detached or its lifetime exceeds the scope of `data`? This becomes a breeding ground for dangling references. Let's keep this "smell" in mind; we will systematically dissect it in the next post.

### Functor

The third way is to pass a class instance that overloads `operator()`:

```cpp
struct Worker {
    void operator()() {
        printf("Working...\n");
    }
};

Worker w;
std::thread t(w); // Pass the functor object
```

Here is a classic C++ trap—if you write `std::thread t(Worker());` directly, the compiler will parse it as a function declaration named `t` (with a parameter type that is a pointer to `Worker`), rather than a definition of a thread object. This is known as the "most vexing parse" problem. There are several solutions: use extra braces `std::thread t{Worker()};`, use a lambda `std::thread t([]{ Worker()(); });`, or construct a named object first and pass it in, as shown above.

Each method has its use cases. Function pointers suit simple, stateless thread functions; lambdas suit defining local logic at the call site and are the most common approach in daily development; functors suit complex tasks that need to carry state—but beware of the lifetime risks introduced by reference members. In real projects, lambdas cover more than 90% of scenarios.

## join() vs detach(): Two Radically Different Strategies

Once a thread is created, we must make a decision before its lifetime ends: **join** or **detach**. This decision directly affects the correctness of the program.

### join: Waiting for the Thread to Finish

`join()` is a blocking call—the current thread stops there and waits for the target thread to finish execution before continuing. The analogy is: you send someone to do a job, you stand there and wait until they are done, and then you continue together. This is the most common and safest pattern.

```cpp
void worker() {
    printf("Worker started\n");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    printf("Worker finished\n");
}

int main() {
    printf("Main start\n");
    std::thread t(worker);
    t.join(); // Block until worker finishes
    printf("Main continues\n");
}
```

Running this code, you will see the output strictly in the order: Main start -> Worker started -> Worker finished -> Main continues. `join()` guarantees that the thread's execution results are visible to the calling thread when `join()` returns—this is a happens-before relationship.

### detach: Letting Go

`detach()` does the exact opposite—it "detaches" the thread from the management of the `std::thread` object. After detaching, the thread runs independently in the background (a so-called daemon thread/background thread), and the `std::thread` object no longer holds any reference to it. You can't join it anymore—the `joinable()` method of the `std::thread` object will return `false`.

```cpp
int main() {
    std::thread t([]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        printf("Background task finished\n");
    });

    t.detach(); // Detach from the object
    printf("Main thread exiting soon...\n");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

If you run this code, you likely won't see the "Background task finished" line—because the main thread waits only one second before exiting, while the detached thread needs two. When the process exits, all threads (including detached ones) are forcibly terminated without any chance for cleanup. This is the biggest risk of `detach`: **you completely lose control over the thread's execution timing**.

So when should you use `detach`? Honestly, in most application code, `detach` is not a good choice. Its suitable scenarios are very limited—such as a background logging thread whose job is to flush logs from a memory buffer to disk. You don't care when it finishes, as long as it eventually writes the data out. But even in this scenario, using a `joinable` thread with an explicit shutdown signal is usually a safer approach.

### The Consequence of Neither Joining nor Detaching: std::terminate

If you let a `joinable` `std::thread` object's destructor run without calling `join()` or `detach()`, your program will call `std::terminate()` and crash immediately. This isn't a suggestion; it's a hard requirement mandated by the standard:

```cpp
int main() {
    std::thread t([]() {
        printf("Running...\n");
    });
    // Forgot join/detach -> std::terminate called here
    return 0;
}
```

The C++ standard is designed this way for a reason. If the destructor silently joined for you, destruction might block—which many developers don't want (destructors should be fast). If the destructor silently detached for you, the thread might access references that no longer exist after the object is destroyed—that is undefined behavior, which is worse than a crash. The standard chooses to call `std::terminate` immediately to force you to **make an explicit decision**: either wait for it to finish (join) or let it go (detach), but you can't pretend this problem doesn't exist.

This design philosophy runs through the entire C++ Concurrency API: do nothing implicit or surprising, and give the decision power to the programmer. The cost is that you must remember to handle the thread's join/detach on every code path, including exception paths. A common pattern is to use an RAII wrapper—save the thread on construction, and automatically join on destruction—we will expand on this topic later in this chapter.

## Thread Identification and Queries

### get_id(): The Thread's ID Number

Every thread has a unique identifier of type `std::thread::id`. You can get a thread object's ID via `get_id()`, or get the current thread's ID via `std::this_thread::get_id()`. `std::thread::id` supports comparison operations and output to `std::ostream`, which is convenient for debugging and logging:

```cpp
std::thread t([]() {
    printf("Worker ID: %s\n",
           std::this_thread::get_id().operator std::string().c_str()); // Simplified for demo
});

printf("Main ID: %s\n",
       std::this_thread::get_id().operator std::string().c_str());
```

A few things to note: the specific value of `std::thread::id` is implementation-defined—different compilers and platforms may output different formats (GCC usually outputs a number, MSVC might output a hexadecimal address), so don't rely on its specific format for logic checks. After `join()` or `detach()`, `get_id()` returns a default-constructed `std::thread::id`, indicating "no associated thread"—this is the same as the return value of `get_id()` on a default-constructed `std::thread` object.

The most practical use for `std::thread::id` is as a key in a `std::map` to allocate resources for threads (e.g., a separate memory pool or log buffer per thread). It can also be used to detect if the "current thread is the main thread," implementing simple thread-safe assertions.

### native_handle(): Touching the OS Native Handle

`std::thread` is a Standard Library abstraction, but sometimes you need to manipulate the underlying operating system thread directly—such as setting thread priority, CPU affinity, or the thread name. `native_handle()` returns a platform-specific native thread handle: `pthread_t` on Linux, `HANDLE` on Windows.

```cpp
std::thread t([]() {});
pthread_t native_t = t.native_handle(); // Linux specific
// Set thread priority...
```

This code is clearly non-portable—it will only compile on platforms supporting pthread. In actual projects, platform-specific code is usually isolated with `#ifdef` or abstracted into a platform layer. `native_handle()` gives you an "escape hatch" to deal directly with the operating system when the Standard Library isn't enough.

### hardware_concurrency(): How Many Cores Do I Have

`hardware_concurrency()` is a static member function that returns a hint indicating the number of threads that can truly run concurrently on the current system—in most cases, this is the number of logical CPU cores (including hyperthreading).

```cpp
unsigned int cores = std::thread::hardware_concurrency();
printf("Concurrent threads supported: %u\n", cores);
```

This value is a hint, not a guarantee. If the information is unavailable, the function returns 0. On an 8-core, 16-thread CPU, it usually returns 16. In container environments, it might return the number of cores allocated to the container rather than the physical machine's total. The most common use is to decide the size of a thread pool or the number of task shards—but don't treat it as an exact value; it's best to check if the return value is 0 before using it.

## Exceptions in Thread Functions

Here is a very important rule: **exceptions should never escape a thread function**. If an exception escapes from a thread function (i.e., the thread function throws an exception but it isn't caught inside the thread), `std::terminate` is called, and the program crashes immediately.

```cpp
void risky_task() {
    throw std::runtime_error("Oops!");
}

int main() {
    std::thread t(risky_task);
    t.join(); // std::terminate called inside join
    return 0;
}
```

This behavior is actually quite reasonable. Each thread has its own independent call stack, and the exception handling mechanism (stack unwinding, catch matching) only works on the current thread's stack. If an exception pierces through the thread function, it means no catch block can catch it—except `std::terminate`. The main thread's `try-catch` and the child thread's exception handling are two completely isolated worlds.

The correct approach is to handle all possible exceptions inside the thread function, or pass exception information back to the caller via some mechanism (`std::exception_ptr`/`std::current_exception`, `std::promise`). A simple defensive pattern looks like this:

```cpp
std::thread t([]() {
    try {
        // Do work
    } catch (const std::exception& e) {
        // Log or store error
    }
});
```

In later chapters, we will introduce `std::exception_ptr` and `std::promise`/`std::future`, which provide more elegant ways to pass child thread exceptions back to the main thread. But in scenarios using `std::thread` directly, this "catch-all inside the thread" pattern is the most basic defensive measure.

## Basic Pattern: Spawn Threads, Join on Scope Exit

With the knowledge above, we can summarize a most basic thread usage pattern: spawn a thread for each subtask, and join all threads before the current scope exits. Expressed in code:

```cpp
std::vector<int> data(1000);
std::vector<std::thread> threads;
const int thread_count = 4;
const int chunk_size = data.size() / thread_count;

for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&, i] { // Capture by reference, capture i by value
        int start = i * chunk_size;
        int end = (i == thread_count - 1) ? data.size() : (start + chunk_size);
        for (int j = start; j < end; ++j) {
            data[j] *= 2;
        }
    });
}

for (auto& t : threads) {
    t.join();
}
```

The execution flow of this code is clear: split the data into N parts, hand each part to a thread for processing, and the main thread waits for all worker threads to finish. `emplace_back` constructs the thread object directly in the vector, avoiding extra moves. The final for loop joins one by one, ensuring all threads have finished execution before exiting.

There is a detail worth noting: `data` is passed into each thread by reference (via `&`), but different threads write to different ranges of `data`—no overlap, so no data race occurs. This "partitioned parallelism" pattern is one of the easiest ways to write correct code in multithreaded programming: as long as you ensure each thread only touches its own share of data, you don't need any synchronization mechanisms.

But this pattern has a problem—if a thread's lambda throws an exception, the `std::thread` destructors in the `threads` vector will be called during stack unwinding, and as we said earlier, destroying a `joinable` thread calls `std::terminate`. To solve this, we need to wrap the join logic with RAII to ensure correct joining even if exceptions occur. We will implement this improved version in the upcoming post on "Thread Ownership and RAII."

## Run Online

Experience the three construction methods of `std::thread`, thread ID queries, and data partitioned parallel processing online:

<OnlineCompilerDemo
  title="std::thread Basics"
  source-path="code/examples/vol5/09_std_thread.cpp"
  description="Experience function pointers, lambdas, and functors, plus data partitioned parallelism"
  allow-run
/>

## Summary

In this post, we completed a comprehensive review of the basic interface of `std::thread`. We saw three ways to construct threads—function pointers, lambdas, and functors—whose essence is passing a callable object and arguments. `join()` and `detach()` are two radically different thread management strategies: join is "wait for me to finish," detach is "you go first, I'll clean up." If you do nothing and let a `std::thread` destruct, the standard will mercilessly call `std::terminate`—this is C++ using the strictest way to remind you: thread lifetimes must be explicitly managed.

We also learned about thread identification (`get_id`), native handles (`native_handle`), and hardware concurrency queries (`hardware_concurrency`), as well as a rule that is easily overlooked but crucial: exceptions should not escape thread functions, or `std::terminate` will be triggered.

Finally, we established a basic parallel processing pattern: data partitioning + multithreading + joining one by one. This pattern works well in simple scenarios, but it lacks exception safety and RAII—which is what we need to solve next.

In the next post, we will dive into a deeper topic: the thread argument passing mechanism. We will see how the decay-copy semantics of `std::thread` work, why `std::reference_wrapper` is a double-edged sword, and what disasters happen when `detach` combines with reference capture. The real traps are ahead.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `vol5/09_std_thread`.

## Exercises

### Exercise 1: Parallel Array Transformation

Given a `std::vector<double>`, use `std::thread` to calculate the square root of each element. Requirements:

1. Use `hardware_concurrency` to get the core count and decide how many threads to spawn.
2. Each thread processes a segment of the array.
3. After all threads finish, print the first 10 results for verification.

Hint: Watch out for `hardware_concurrency` possibly returning 0, and how to handle cases where the array size isn't divisible by the thread count.

### Exercise 2: Verify Terminate Behavior

Write a program that intentionally lets a `joinable` `std::thread`'s destructor run without calling `join()` or `detach()`. Run the program and observe the output when `std::terminate` is called. Then, wrap this code in a `try-catch(...)` block in the `main` function to see if you can "catch" this terminate—the answer is: no, `std::terminate` cannot be caught by a normal `try-catch`, it is a forced termination of the program.

### Exercise 3: Thread ID Mapping

Write a program that creates N threads (e.g., 4), where each thread stores its `std::thread::id` into a shared `std::map` (key is thread ID, value is thread number 0-3). Since multiple threads writing to a map simultaneously is a data race, let's handle it simply for now: each thread outputs the result to `std::cout`, and the main thread records it. The purpose of this exercise is to familiarize you with the basic usage of `std::thread::id`.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::thread::detach — cppreference](https://en.cppreference.com/w/cpp/thread/thread/detach)
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency)
- [C++ Core Guidelines: CP.20 — Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [What does decay-copy in the constructor in a std::thread object do? — StackOverflow](https://stackoverflow.com/questions/67947814/what-does-decay-copy-in-the-constructor-in-a-stdthread-object-do)
