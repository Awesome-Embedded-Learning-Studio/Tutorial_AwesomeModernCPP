---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C++ thread creation, `join`, `detach`, ID, and hardware concurrency
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
  source_hash: 96e9b35580983cb6f300f47f909402cd60e95253670f4c8b54afda0c8204c952
  translated_at: '2026-06-16T04:02:48.452086+00:00'
  engine: anthropic
  token_count: 3671
---
# std::thread Basics

In the previous chapter, we discussed the CPU cache hierarchy, the MESI protocol, false sharing, and looked at Linux's threading model and the futex mechanism—these are the physical stages where multithreaded programs run. But knowing what the stage looks like isn't enough; we need to get on stage and perform. This post marks our first debut: starting with the construction of `std::thread`, we will figure out how to create threads, how to wait for them, how to "let them go," and what pitfalls we might easily stumble into during operations.

`std::thread` is the standard thread class introduced in C++11, defined in the `<thread>` header file. It is a direct wrapper of the operating system threads by the C++ Standard Library—on Linux, behind every `std::thread` object lies a pthread, which is mapped to a kernel scheduling entity via the `clone` system call. The 1:1 model we mentioned in the last post is embodied right here.

## Constructing std::thread in Three Ways

The `std::thread` constructor accepts a **callable object** and an optional list of arguments. C++ provides us with several ways to express "callable," so let's look at them one by one.

### Function Pointer

The most primitive way is to pass a plain function pointer:

```cpp
void worker(int x) {
    printf("Worker got: %d\n", x);
}

std::thread t(worker, 42); // Pass function pointer and argument
```

`std::thread` does a few things here: first, it packs `worker` (the function pointer) and `42` (the argument) into internal storage; then, it calls the underlying `pthread_create` (or an equivalent system call) to create a new operating system thread; finally, the new thread calls `worker` with the saved arguments in that independent execution context. Note that the argument `42` is **copied** into the thread's internal storage—we will dive into the details of argument passing in the next post.

### Lambda Expression

In actual engineering, lambda is the most common way to create threads because it allows you to define exactly what the thread does right at the call site, without needing to declare an extra function:

```cpp
int data = 10;
std::thread t([&]() {
    // Capture 'data' by reference
    data += 20;
});
```

This code works, but if you look closely, `data` is captured by reference—this is perfectly fine in a single-threaded scenario, but what if the thread is detached or its lifetime exceeds the scope of `data` and `t`? This is a breeding ground for dangling references. Let's keep this "smell" in mind; we will systematically dissect it in the next post.

### Function Object (Functor)

The third way is to pass a class instance that overloads `operator()`:

```cpp
struct Task {
    void operator()() {
        printf("Task running\n");
    }
};

Task task;
std::thread t(task); // Correct: pass a named object
// std::thread t(Task()); // WRONG! Parsed as a function declaration
```

Here is a classic C++ trap—if you write `std::thread t(Task())` directly, the compiler will parse it as a function declaration named `t` (whose parameter type is a pointer to `Task`), rather than the definition of a thread object. This is known as the "most vexing parse" problem. There are several ways to solve this: use extra braces `std::thread t{Task()};`, use a lambda `std::thread t([](){ Task{}(); });`, or construct a named object first and pass it in, as shown above.

Each method has its use case. Function pointers suit simple, stateless thread functions; lambdas suit defining local logic at the call site and are the most common approach in daily development; functors suit complex tasks that need to carry state—but be aware of the lifetime risks brought by reference members. In actual projects, lambdas cover more than 90% of scenarios.

## join() vs detach(): Two Drastically Different Strategies

Once a thread is created, we must make a decision before its lifetime ends: **join** or **detach**. This decision directly affects the correctness of the program.

### join: Wait for the Thread to Finish

`join()` is a blocking call—the current thread stops there and waits until the target thread finishes execution before continuing. An analogy would be: you send someone to do a job, you stand there and wait for them to finish, and then you continue together. This is the most common and safest mode.

```cpp
std::thread t([]{
    std::cout << "Worker started\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Worker finished\n";
});

std::cout << "Main joining...\n";
t.join(); // Block until worker finishes
std::cout << "Main continuing\n";
```

Running this code, you will see the output strictly in the order of Main starts -> Worker starts -> Worker finishes -> Main continues. `join()` guarantees that the thread's execution results are visible to the calling thread when `join()` returns—this is a happens-before relationship.

### detach: Let It Go

`detach()` does the exact opposite—it "strips" the thread from the management of the `std::thread` object. After detaching, the thread runs independently in the background (a so-called daemon thread/background thread), and the `std::thread` object no longer holds any reference to it. You can't join it anymore—the `joinable()` method of the `std::thread` object will return `false`.

```cpp
std::thread t([]{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Background task finished\n";
});

t.detach(); // "Fire and forget"
std::cout << "Main thread exiting...\n";
std::this_thread::sleep_for(std::chrono::seconds(1));
return 0; // Process exits, background thread killed
```

If you run this code, you likely won't see the "Background task finished" output—because the main thread only waited 1 second before exiting, while the detached thread needs 2 seconds. When the process exits, all threads (including detached ones) are forcibly terminated without any chance to clean up. This is the biggest risk with detach: **you completely lose control over the thread's execution timing**.

So when should you use detach? Honestly, in most application code, detach is not a good choice. Its suitable scenarios are very limited—such as a background logging thread whose job is to flush logs from a memory buffer to disk; you don't care when it ends, as long as it eventually writes the data out. But even in this scenario, using a `joinable` thread with an explicit shutdown signal is usually a safer approach.

### Consequences of Neither Joining nor Detaching: std::terminate

If you let a `joinable` `std::thread` object's destructor run without calling `join()` or `detach()`, your program will call `std::terminate()` and crash immediately. This isn't a suggestion; it's a hard behavior mandated by the standard:

```cpp
void do_work() {
    std::thread t([]{ /* ... */ });
    // Forgot join/detach? std::terminate is called here!
}
```

The C++ standard is designed this way for a reason. If the destructor silently helped you `join`, destruction might block—something many developers don't want (destructors should be fast). If the destructor silently helped you `detach`, the thread might access references that no longer exist after the object is destroyed—this is undefined behavior, which is worse than a crash. The standard chooses to call `std::terminate` immediately to force you to **make an explicit decision**: either wait for it to finish (join) or let it go (detach), but you can't pretend this problem doesn't exist.

This design philosophy runs through the entire C++ concurrency API: do nothing implicit or surprising, and give the decision power to the programmer. The cost is that you must remember to handle the thread's join/detach on every code path, including exception paths. A common pattern is to use an RAII wrapper—save the thread on construction, and automatically join on destruction—we will expand on this topic later in this chapter.

## Thread Identification and Query

### get_id(): The Thread's ID Number

Every thread has a unique identifier, of type `std::thread::id`. You can get a thread object's ID via `get_id()`, or get the current thread's ID via `std::this_thread::get_id()`. `std::thread::id` supports comparison operations and output to `std::ostream`, which is convenient for debugging and logging:

```cpp
std::thread t([]{
    std::cout << "Worker ID: " << std::this_thread::get_id() << "\n";
});

std::cout << "Main ID: " << std::this_thread::get_id() << "\n";
std::cout << "Thread object ID: " << t.get_id() << "\n";
```

A few points to note: the specific value of `std::thread::id` is implementation-defined—different compilers and platforms may output different formats (GCC usually outputs a number, MSVC might output a hex address), so don't rely on its specific format for logic checks. After `join()` or `detach()`, `get_id()` returns a default-constructed `std::thread::id`, indicating "not associated with any thread"—which is the same as the return value of `get_id()` for a default-constructed `std::thread` object.

The most practical scenario for `std::thread::id` is using it as a key in a `std::map` to allocate resources for threads (such as a separate memory pool or log buffer per thread). It can also be used to detect if the "current thread is the main thread," implementing simple thread-safe assertions.

### native_handle(): Touching the OS Native Handle

`std::thread` is a standard library abstraction, but sometimes you need to manipulate the underlying operating system thread directly—such as setting thread priority, CPU affinity, or the thread name. `native_handle()` returns a platform-dependent native thread handle: on Linux it's `pthread_t`, on Windows it's `HANDLE`.

```cpp
std::thread t([]{ /* work */ });

// Linux specific: set thread name
pthread_setname_np(t.native_handle(), "MyWorker");
```

This code is clearly non-portable—it will only compile on platforms supporting pthread. In actual projects, platform-specific code is usually isolated with `#if defined` macros, or abstracted into a platform layer. `native_handle()` gives you an "escape hatch" to deal directly with the operating system when the standard library isn't enough.

### hardware_concurrency(): How Many Cores Do I Have

`hardware_concurrency()` is a static member function that returns a hint value indicating the number of threads that can truly run concurrently on the current system—in most cases, this is the number of logical CPU cores (including hyperthreading).

```cpp
unsigned int cores = std::thread::hardware_concurrency();
std::cout << "Concurrent threads supported: " << cores << "\n";
```

This value is advisory, not guaranteed. If the information is unavailable, the function returns 0. On an 8-core 16-thread CPU, it usually returns 16. In a container environment, it might return the number of cores allocated to the container rather than the physical machine's total cores. The most common use is to decide the size of a thread pool or the number of task shards based on it—but don't treat it as an exact value; it's best to check if the return value is 0 before using it.

## Exceptions in Thread Functions

Here is a very important rule: **exceptions should never escape a thread function**. If an exception escapes from a thread function (i.e., the thread function throws an exception but isn't caught inside the thread), `std::terminate` will be called, and the program will crash immediately.

```cpp
std::thread t([]{
    throw std::runtime_error("Oops!"); // Uncaught exception!
});
// t.join(); // If we don't join, terminate is called in destructor
// If we do join, terminate is called inside join()
t.join();
```

This behavior is actually quite reasonable. Each thread has its own independent call stack, and the exception handling mechanism (stack unwinding, catch matching) only works on the current thread's stack. If an exception pierces through the thread function, it means there is no catch block to catch it—except `std::terminate`. The main thread's `try-catch` and the child thread's exception handling are two completely isolated worlds.

The correct approach is to handle all possible exceptions inside the thread function, or pass exception information back to the caller via some mechanism (`std::exception_ptr`/`std::promise`, `std::future`). A simple defensive pattern looks like this:

```cpp
std::thread t([]{
    try {
        // Do work that might throw
    } catch (const std::exception& e) {
        // Log error or store state
        std::cerr << "Thread error: " << e.what() << "\n";
    }
});
```

In later chapters, we will introduce `std::promise` and `std::future`/`std::shared_future`, which provide a more elegant way to pass child thread exceptions back to the main thread. But in scenarios using `std::thread` directly, the "catch-all inside the thread" pattern above is the most basic defensive measure.

## Basic Pattern: Spawn Threads, Join on Scope Exit

With the knowledge above, we can summarize a most basic thread usage pattern: spawn a thread for each subtask, and join all threads before the current scope exits. Expressed in code:

```cpp
std::vector<int> data(1000);
std::vector<std::thread> threads;
unsigned int num_threads = std::thread::hardware_concurrency();
if (num_threads == 0) num_threads = 2; // Fallback

size_t chunk_size = data.size() / num_threads;

for (unsigned int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&, i] {
        size_t start = i * chunk_size;
        size_t end = (i == num_threads - 1) ? data.size() : (i + 1) * chunk_size;
        for (size_t j = start; j < end; ++j) {
            data[j] *= 2; // Process data
        }
    });
}

// Join all threads
for (auto& t : threads) {
    t.join();
}
```

The execution flow of this code is clear: split the data into N parts, hand each part to a thread for processing, and then the main thread waits for all worker threads to finish. `emplace_back` constructs the thread object directly in the vector, avoiding extra moves. The final for loop joins one by one, ensuring all threads have finished execution before exiting.

There is a detail worth noting here: `data` is passed into each thread by reference (via `&`), but different threads write to different ranges of `data`—there is no overlap, so no data race occurs. This "partitioned parallelism" pattern is one of the easiest ways to write correct code in multithreaded programming: as long as you ensure each thread only touches its own share of data, no synchronization mechanism is needed.

But this pattern has a problem—if a thread's lambda function throws an exception, the `std::thread` destructor will be called during stack unwinding, and as we said earlier, destroying a `joinable` thread calls `std::terminate`. To solve this, we need to wrap the join logic with RAII to ensure correct join even if an exception occurs. We will implement this improved version in the upcoming post on "Thread Ownership and RAII."

## Run Online

Experience the three construction methods of `std::thread`, thread ID queries, and data-partitioned parallel processing online:

<OnlineCompilerDemo
  title="std::thread Basics"
  source-path="code/examples/vol5/09_std_thread.cpp"
  description="Experience function pointer, lambda, and functor thread construction methods and data-partitioned parallelism"
  allow-run
/>

## Summary

In this post, we completed a comprehensive review of the basic interface of `std::thread`. We saw three ways to construct a thread—function pointer, lambda, and functor—whose essence is passing a callable object and arguments. `join()` and `detach()` are two drastically different thread management strategies: join is "wait for me to finish before leaving," detach is "you go first, I'll finish up." If you do nothing and let `std::thread` destruct, the standard will mercilessly call `std::terminate`—this is C++ using the strictest way to remind you: thread lifetimes must be managed explicitly.

We also learned about thread identification (`get_id`), native handles (`native_handle`), and hardware concurrency queries (`hardware_concurrency`), as well as a rule that is easily overlooked but crucial: exceptions should not escape thread functions, otherwise `std::terminate` is triggered.

Finally, we established a basic parallel processing pattern: data partitioning + multithreading + join one by one. This pattern works well in simple scenarios, but it lacks exception safety and RAII—this is the problem we need to solve next.

In the next post, we will dive into a deeper topic: the thread argument passing mechanism. We will see how the decay-copy semantics of `std::thread` work, why `std::ref` is a double-edged sword, and what disasters happen when detach and reference capture combine. The real traps are ahead.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `vol5/09_std_thread.cpp`.

## Exercises

### Exercise 1: Parallel Array Transformation

Given a `std::vector<int>`, use `std::thread` to calculate the square root of each element. Requirements:

1. Use `std::thread::hardware_concurrency` to get the core count and decide how many threads to spawn.
2. Each thread processes a segment of the array.
3. After all threads finish, print the first 10 results for verification.

Hint: Watch out for the case where `hardware_concurrency` might return 0, and how to handle the array size not being divisible by the number of threads.

### Exercise 2: Verify Terminate Behavior

Write a program that intentionally lets a `std::thread`'s destructor run without calling `join()` or `detach()`. Run the program and observe the output when `std::terminate` is called. Then, wrap this code in a `main` function with a `try-catch(...)`, and see if you can "catch" this terminate—Answer: No, `std::terminate` cannot be caught by a normal `try-catch`; it is the forced termination of the program.

### Exercise 3: Thread ID Mapping

Write a program that creates N threads (e.g., 4), and each thread stores its `std::thread::id` in a shared `std::map` (key is thread ID, value is thread number 0-3). Since multiple threads writing to a map at the same time is a data race, let's handle it simply for now: each thread outputs the result to `std::cout`, and the main thread records it. The purpose of this exercise is to familiarize you with the basic usage of `std::thread::id`.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::thread::detach — cppreference](https://en.cppreference.com/w/cpp/thread/thread/detach)
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency)
- [C++ Core Guidelines: CP.20 — Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [What does decay-copy in the constructor of a std::thread object do? — StackOverflow](https://stackoverflow.com/questions/67947814/what-does-decay-copy-in-the-constructor-in-a-stdthread-object-do)
