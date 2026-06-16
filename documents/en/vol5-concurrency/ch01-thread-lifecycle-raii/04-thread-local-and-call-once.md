---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master thread-local storage and one-time initialization mechanisms to
  write thread-safe lazy initialization and global state.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 线程所有权与 RAII
reading_time_minutes: 19
related:
- 线程参数与生命周期
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: thread_local and call_once
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/04-thread-local-and-call-once.md
  source_hash: 2fa8fb9a7900bd6543b487a4c32aaffa4e5ca175e501c27973e6590ad66cab92
  translated_at: '2026-06-16T04:03:10.921399+00:00'
  engine: anthropic
  token_count: 3454
---
# thread_local and call_once

In the previous article, we used RAII to solve the problems of thread ownership and lifetime management. In this article, we will look at a problem from another dimension: when multiple threads need to access certain "global states," how can we ensure thread safety without sacrificing performance?

The answer lies in two directions. The first direction is to **avoid sharing entirely**—give each thread an independent copy, so they use their own resources, naturally eliminating contention. This is what `thread_local` storage duration is for. The second direction is to **share but initialize only once**—a global object needs to be initialized upon first use, and no matter how many threads trigger initialization simultaneously, it executes only once. This is the responsibility of `std::call_once`. These two tools solve different problems, but they share a common theme: making concurrent code safe during the "initialization" phase.

## thread_local Storage Duration

C++ has several types of storage duration: automatic storage (local variables on the stack), static storage (global variables and `static` local variables), dynamic storage (allocated by `new`/`malloc`), and thread storage. `thread_local` is the specifier for thread storage duration—a variable modified by it has an independent instance in each thread, existing from the thread's creation until its exit.

What does this mean? Suppose you declare a `thread_local int`. If your program has N threads, there are N independent copies of that `int`. Thread A's modifications to its own copy are completely invisible to Thread B—they are different objects in memory with different addresses. From a thread's perspective, a `thread_local` variable acts like a "thread-specific global variable"—it has a lifetime as long as the thread, but each thread has its own copy.

Let's look at a straightforward example—a thread-safe counter that requires no locks:

```cpp
#include <iostream>
#include <thread>
#include <string>

thread_local int counter = 0; // Each thread has its own counter

void task(const std::string& name) {
    for (int i = 0; i < 5; ++i) {
        ++counter;
        std::cout << name << ": " << counter << "\n";
    }
}

int main() {
    std::thread t1(task, "Thread A");
    std::thread t2(task, "Thread B");

    task("Main Thread");

    t1.join();
    t2.join();

    // Main thread's counter is still 0, never touched by other threads
    std::cout << "Main counter final: " << counter << "\n";
}
```

The output will look something like this:

```text
Thread A: 1
Thread A: 2
Main Thread: 1
Thread B: 1
Thread A: 3
Main Thread: 2
...
Main counter final: 0
```

You will notice that Thread A and Thread B each count to 5 without interfering with each other. The main thread's `counter` remains 0—it was never touched by any other thread. Three threads, three independent `counter` instances.

### Initialization Timing of thread_local

`thread_local` variables are initialized **when each thread first uses them (ODR-use)**, not when the program starts. This "lazy initialization" behavior is crucial for several reasons. First, if a `thread_local` variable is never accessed by a specific thread, that thread won't allocate memory or execute initialization for it, avoiding waste. Second, the initialization is thread-safe—the standard guarantees that even if multiple threads access the same `thread_local` variable for the first time simultaneously, each thread's initialization happens only once and without interference. Third, the initialization order of `thread_local` variables relates to their declaration position—within the same translation unit, `thread_local` variables are initialized in declaration order; the order between different translation units is unspecified (similar to the static variable initialization order problem).

This "lazy initialization" characteristic makes `thread_local` perfect for implementing "on-demand allocation" resources—such as per-thread random number generators, memory pools, or log buffers. These resources would require locking if shared globally, but with `thread_local`, they become completely lock-free.

### thread_local vs. Global/Static Variables: Their Lifetimes

To clearly understand where `thread_local` fits, we can compare it with other storage durations. Global variables and `static` member variables have static storage duration—they are initialized when the program starts (or upon first use for `static` local variables inside functions) and destroyed when the program exits. All threads share the same instance. `thread_local` variables also have a lifetime as long as the thread, but each thread has an independent copy—initialized when the thread starts (upon first use) and destroyed when the thread exits. Normal stack variables (automatic storage duration) are created when the function is called and destroyed when it returns. While they are also isolated between threads, their lifetime is too short—they vanish once the function returns.

An easily overlooked point is the destruction timing of `thread_local` variables. When a thread exits, all `thread_local` variables for that thread are destroyed in reverse order of initialization. This means the destructor of a `thread_local` variable executes within the context of that thread—if you access another thread's state in the destructor, you must be careful about synchronization. Even trickier, if the destructor of a `thread_local` variable triggers access to another `thread_local` variable that has already been destroyed, the behavior is undefined. This "cross-reference in destructor" problem is one of the subtlest traps of `thread_local`.

## Avoiding Inter-Thread Sharing with thread_local

Now that we understand the basic concepts, let's look at a few typical application scenarios for `thread_local` in practice.

### Thread-Safe Random Number Generator

Random number generators are one of the most classic use cases for `thread_local`. The thread safety of `rand()` is implementation-defined and not guaranteed on all platforms. Moreover, even if an implementation happens to be thread-safe, its internal state is still shared by all threads, and results in a multi-threaded environment might lack the random distribution you expect. Random number engines in `<random>` (like `std::mt19937`) are not thread-safe—you cannot call the same engine object in multiple threads simultaneously. The solution is to give each thread an independent engine:

```cpp
#include <iostream>
#include <random>
#include <thread>
#include <string>

void generate_numbers(const std::string& name) {
    // Each thread has its own engine and distribution
    thread_local std::mt19937 engine(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);

    for (int i = 0; i < 5; ++i) {
        std::cout << name << " generated: " << dist(engine) << "\n";
    }
}

int main() {
    std::thread t1(generate_numbers, "Thread A");
    std::thread t2(generate_numbers, "Thread B");

    t1.join();
    t2.join();
}
```

`engine` is declared as `thread_local`, so each thread has its own `std::mt19937` instance, maintaining its own random state. `std::random_device{}` is used to provide a different seed for each thread's generator—note that this seed is obtained when the thread first calls `generate_numbers`, not at program startup. So even if two threads start almost simultaneously, they will get different seeds (assuming `std::random_device` itself is non-deterministic, which is true on most platforms).

### Thread-Local Memory Pool

In high-performance scenarios, frequent calls to `new` and `delete` can cause severe lock contention—because the standard library's allocator (usually `malloc` or `ptmalloc`) needs to lock internally to protect the free list. A common optimization is to give each thread a small memory pool, allocating small objects directly from the thread-local pool without competing with other threads:

```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <memory>

// Simplified thread-local memory pool
class ThreadLocalPool {
    struct Block { Block* next; };
    Block* free_list = nullptr;

public:
    void* allocate(size_t size) {
        if (free_list) {
            void* ptr = free_list;
            free_list = free_list->next;
            return ptr;
        }
        return ::operator new(size); // Fallback to global new
    }

    void deallocate(void* ptr) {
        Block* block = static_cast<Block*>(ptr);
        block->next = free_list;
        free_list = block;
    }
};

thread_local ThreadLocalPool pool; // Each thread has its own pool

void worker(int id) {
    std::vector<void*> ptrs;
    for (int i = 0; i < 100; ++i) {
        ptrs.push_back(pool.allocate(sizeof(int)));
    }
    for (void* p : ptrs) {
        pool.deallocate(p);
    }
    std::cout << "Thread " << id << " done.\n";
}

int main() {
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
}
```

This simplified memory pool demonstrates the typical usage of `thread_local` in performance optimization: `thread_local` ensures each thread has its own independent memory pool, so allocation and deallocation of small objects happen entirely locally without any synchronization. Of course, this is just a teaching example—in production, you should use mature allocators (like `mimalloc`, `jemalloc`), which already implement similar thread-local caching internally. But understanding the role `thread_local` plays here is very helpful for writing high-performance concurrent code.

## std::call_once and std::once_flag

Having covered the "one copy per thread" scenario, let's look at the "all threads share one copy but initialize only once" scenario.

`std::call_once` is a one-time initialization mechanism provided by C++11. You give it a `std::once_flag` and a callable object, and it guarantees that no matter how many threads call `std::call_once` simultaneously, the callable object is executed only once—the first arriving thread executes the initialization, while the others wait for it to complete. This mechanism is very useful for implementing singletons, global configuration initialization, lazy loading, and so on.

### Basic Usage

```cpp
#include <iostream>
#include <mutex>
#include <thread>

std::once_flag init_flag;
int shared_resource = 0;

void init_resource() {
    std::cout << "Initializing shared resource...\n";
    shared_resource = 42; // Expensive initialization
}

void worker() {
    std::call_once(init_flag, init_resource);
    std::cout << "Using resource: " << shared_resource << "\n";
}

int main() {
    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);

    t1.join();
    t2.join();
    t3.join();
}
```

In the output, you will find "Initializing shared resource..." appears only once—regardless of the scheduling order of the three threads, the initialization code executes only once. `std::once_flag` records whether initialization is complete, and `std::call_once` checks this flag on each call. If initialization hasn't started, the first thread executes it; if it's in progress, other threads block and wait; if it's complete, all threads skip directly.

### call_once and Exception Retry

`std::call_once` has a critical behavior: if the initialization function (the callable object) throws an exception, `std::call_once` does not mark the `std::once_flag` as "completed." This means the next time a thread calls `std::call_once`, initialization will be attempted again. This design is very reasonable—if initialization fails (e.g., file open failure, network connection timeout), you don't want all subsequent threads to think "it's already initialized" and then use an invalid state.

```cpp
#include <iostream>
#include <mutex>
#include <thread>

std::once_flag init_flag;
int attempt_count = 0;

void risky_init() {
    ++attempt_count;
    std::cout << "Attempt " << attempt_count << "...\n";
    if (attempt_count < 3) {
        throw std::runtime_error("Not ready yet");
    }
    std::cout << "Initialization succeeded!\n";
}

void worker() {
    try {
        std::call_once(init_flag, risky_init);
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }
}

int main() {
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();

    // Retry from main thread
    worker();
}
```

In this example, the first two calls to `std::call_once` cause `risky_init` to throw an exception, so `init_flag` is not marked as complete, and the next call retries initialization. Only after the third success do all subsequent calls skip initialization. This "retry on exception" behavior is a significant advantage of `std::call_once` over the Meyers singleton—we will compare them in detail shortly.

## Meyers Singleton: Static Local Variables in Function Scope

Since C++11, `static` local variables in function scope have a very important guarantee: **their initialization is thread-safe**. If multiple threads simultaneously reach the declaration of a `static` local variable for the first time, only one thread will execute the initialization, and the others will wait. This is known as the "Meyers singleton" (named after Scott Meyers, who popularized this pattern in *Effective C++):

```cpp
#include <iostream>
#include <mutex>
#include <thread>

class Singleton {
public:
    static Singleton& getInstance() {
        static Singleton instance; // Magic static
        return instance;
    }

    void doSomething() { std::cout << "Working...\n"; }

private:
    Singleton() {
        std::cout << "Singleton constructed\n";
        // Simulate expensive init
    }
    ~Singleton() = default;
    // Delete copy/move
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};

void worker() {
    Singleton& s = Singleton::getInstance();
    s.doSomething();
}

int main() {
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
}
```

"Singleton constructed" will only output once, no matter how many threads call `getInstance()` simultaneously. The C++11 standard ([stmt.dcl] p4) explicitly states: if control enters the declaration of a `static` local variable while multiple threads are active, one thread executes initialization and the others block. This guarantee is implemented jointly by the compiler and runtime library—on GCC and Clang, it is usually implemented through the `__cxa_guard_acquire` / `__cxa_guard_release` ABI functions, using a mechanism similar to `std::call_once` underneath.

The Meyers singleton is the simplest and safest way to implement the singleton pattern. No manual locking, no `std::once_flag`, no `std::call_once`—the compiler handles everything for you. If your singleton initialization cannot fail (won't throw exceptions), the Meyers singleton is the best choice.

## When call_once is Better Than Meyers Singleton

Since the Meyers singleton is so good, why do we still need `std::call_once`? The key difference lies in **control granularity** and **exception handling**.

Meyers singleton initialization is tied to the variable declaration—you cannot do preparatory work before initialization, nor can you choose a different strategy if initialization fails. `std::call_once`, however, gives you full control: the initialization function can be a normal function or lambda, and you decide its contents freely; initialization can access external state (like reading a config file path, connecting to a database); if initialization fails (throws an exception), subsequent calls can retry.

A more subtle difference is the "location" of initialization. Meyers singleton initialization happens when the `getInstance()` function is called for the first time—this timing might not be what you want. You might prefer to explicitly initialize all global resources after program startup, rather than triggering a sudden, time-consuming initialization in the middle of a request. `std::call_once` allows you to place this initialization logic anywhere—you can call it proactively at the start of `main()`, or lazy-load only when truly needed, entirely under your control.

There is also a practical scenario: if your "singleton" is not a single object but a set of initialization steps (like initializing the logging system, configuration manager, database connection pool, etc.), `std::call_once` can package all these steps in one function. The Meyers singleton can only initialize one object—to initialize multiple things, you would need to write a `static` local variable for each, which is less flexible.

To summarize the selection strategy: if your initialization logic is simple, won't fail, and only needs to initialize one object, the Meyers singleton is the best choice—concise, safe, zero overhead. If you need more flexible control—initialization might fail, needs retry, needs to access external state, or needs to initialize a group of resources rather than a single object—`std::call_once` is the more suitable tool.

## thread_local and Dynamically Loaded Libraries

`thread_local` is very reliable in normal use, but there are issues to be aware of in scenarios involving dynamically loaded libraries (shared library / DLL).

The root of the problem lies in the lifetime management of `thread_local` variables. Each thread's `thread_local` variables need to be destroyed when the thread exits, which requires registering a destructor callback. In the main program, this registration is done by the C++ runtime when the `thread_local` variable is first accessed. In dynamically loaded libraries, however, the situation becomes more complex—the library can be loaded or unloaded at any time, and the destructor callbacks for `thread_local` variables need to be cleaned up before the library is unloaded.

On Linux (glibc + GCC/Clang), support for `thread_local` variables in dynamic libraries usually works fine—the `__cxa_thread_atexit` function is responsible for registering destructor callbacks on thread exit and handles library unloading correctly. However, in the Windows DLL model, the behavior of `thread_local` in DLLs has been problematic for a long time—when a DLL is unloaded, the destructor callbacks for `thread_local` variables of already exited threads would point to invalid code segments, causing crashes. It wasn't until relatively recent MSVC versions (VS 2017 and later) that support for `thread_local` in DLLs became more robust.

If you need to write cross-platform library code that might be dynamically loaded, pay attention to the following points when using `thread_local`. First, ensure your target platform's compiler support for `thread_local` in dynamic libraries is complete. Second, if the destructor of a `thread_local` variable has side effects (like releasing locks, writing files, notifying other threads), be especially careful—these destructors might not execute in the order you expect when the library is unloaded. Finally, in some embedded or special environments (like WebAssembly, certain RTOSes), support for `thread_local` may be incomplete or entirely absent—if your code needs to run on these platforms, it's better to implement thread-local storage using other methods.

## Summary

In this article, we discussed two mechanisms for handling "initialization" problems in concurrent environments. `thread_local` provides independent copies of variables for each thread, fundamentally eliminating data sharing—suitable for scenarios like random number generators, memory pools, and log buffers where "each thread has its own copy." Its initialization is lazy (on first use), thread-safe, and destruction occurs when the corresponding thread exits.

`std::call_once` combined with `std::once_flag` provides the guarantee that "all threads share one copy, but initialize only once." It is more flexible than the Meyers singleton—supporting exception retries, initializing non-object resources (like a set of function calls), and triggering initialization at any location. If your initialization logic is simple and won't fail, the Meyers singleton is still the first choice—it's more concise and requires no extra `std::once_flag` variable. The two are not mutually exclusive but complementary tools; the choice depends on your specific needs.

With this, the four articles of ch01 are complete. We started from the basic usage of `std::thread`, covered parameter passing, lifetime management, RAII wrappers, thread ownership, and thread-local storage and one-time initialization. These are the foundation for subsequent content—when we discuss mutexes, atomic operations, and lock-free programming later, we will frequently use the concepts and tools established in this chapter.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `ch01`.

## Exercises

### Exercise 1: Thread-Safe Configuration Initializer

Implement a `ConfigLoader` class that reads configuration from a file (you can simulate with `std::ifstream`), using `std::call_once` to ensure it initializes only once. Requirements: (1) If file reading fails, it should throw an exception and allow retry; (2) Provide a `get()` method to return the configuration value; (3) Multiple threads can call `get()` simultaneously, but only the first call triggers the file read.

```cpp
// TODO: Implement ConfigLoader
// - std::once_flag flag
// - std::call_once in get()
// - Throw exception on simulated failure
```

### Exercise 2: thread_local Logger

Implement a simple thread-local logger where each thread has its own log buffer (`std::stringstream`), and log writing is lock-free. Provide two methods: `log()` to write messages, and `flush()` to output the buffer content to `std::cout` and clear it. In `main()`, launch 4 threads, have each write 10 log messages and then flush, and observe if the output is thread-safe.

### Exercise 3: Comparing call_once and Meyers Singleton

Implement the same singleton in two ways—one using `std::call_once`, one using the Meyers singleton. Then simulate an expensive initialization in the singleton's constructor (like `std::this_thread::sleep_for`), use 8 threads to access the singleton simultaneously, and measure the performance difference between the two implementations. Think about why the performance might differ. Hint: The Meyers singleton's initialization lock is on the `static` variable itself, while `std::call_once`'s lock is on `std::once_flag`—if multiple threads access simultaneously, the waiting mechanism is similar, but implementation details may vary.

## References

- [thread_local storage — cppreference](https://en.cppreference.com/w/cpp/language/storage_duration#thread_local_storage)
- [std::call_once — cppreference](https://en.cppreference.com/w/cpp/thread/call_once)
- [Magic Statics (C++11 thread-safe statics) — cppreference](https://en.cppreference.com/w/cpp/language/static#Static_local_variables)
- [Effective C++, Item 4: Make sure that objects are initialized before they're used — Scott Meyers](https://www.oreilly.com/library/view/effective-c/0321334876/)
- [Thread-local storage — Wikipedia](https://en.wikipedia.org/wiki/Thread-local_storage)
- [Dynamic Initialization and Destruction in C++ (Itanium C++ ABI)](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#once-ctor)
