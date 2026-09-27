---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Master thread-local storage and one-time initialization to write thread-safe lazy initialization and global state
difficulty: intermediate
order: 4
platform: host
prerequisites:
- Thread Ownership and RAII
reading_time_minutes: 19
related:
- Thread Arguments and Lifetime
tags:
- host
- cpp-modern
- intermediate
- 内存管理
title: thread_local and call_once
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/04-thread-local-and-call-once.md
  source_hash: 2fa8fb9a7900bd6543b487a4c32aaffa4e5ca175e501c27973e6590ad66cab92
  translated_at: '2026-09-26T06:29:40+00:00'
  engine: anthropic
  token_count: 9900
---
# thread_local and call_once

In the previous article, we used RAII to solve the problems of thread ownership and lifetime management. This article turns to a problem on a different axis: when multiple threads need to access some kind of "global state", how do we keep things thread-safe without sacrificing performance?

The answer comes in two directions. The first is to **avoid sharing altogether**—give each thread its own private copy and let everyone use their own; with nothing shared, there is nothing to race over. That is exactly what `thread_local` storage duration is for. The second is to **share, but initialize only once**—some global object needs to be initialized on first use, and no matter how many threads trigger that initialization simultaneously, it must run exactly one time. That is the responsibility of `std::call_once`. The two tools solve two different problems, yet they share one theme: making the "initialization" step of concurrent code safe.

## thread_local Storage Duration

C++ has several kinds of storage duration: automatic storage (local variables on the stack), static storage (global variables and `static` locals), dynamic storage (allocated with `new`/`malloc`), and thread storage. `thread_local` is the specifier for thread storage duration—a variable it decorates has its own independent instance in every thread, alive from the moment the thread is created until it exits.

What does that mean? Suppose you declare `thread_local int counter = 0;`. Your program then has exactly as many independent copies of `counter` as it has threads. A change thread A makes to its own copy is completely invisible to thread B—they are different objects in memory, right down to their addresses. From a thread's perspective, a `thread_local` variable behaves like a "global variable private to this thread"—it lives exactly as long as the thread does, but every thread gets its own copy.

Let's look at the most direct example—a thread-safe counter that needs no locks at all:

```cpp
#include <thread>
#include <iostream>

thread_local int thread_counter = 0;

void increment_and_print(const char* name)
{
    for (int i = 0; i < 5; ++i) {
        ++thread_counter;
        std::cout << name << ": counter = " << thread_counter << "\n";
    }
}

int main()
{
    std::thread t1(increment_and_print, "Thread-A");
    std::thread t2(increment_and_print, "Thread-B");

    t1.join();
    t2.join();

    // The main thread has its own thread_counter copy too
    std::cout << "Main: counter = " << thread_counter << "\n";
    return 0;
}
```

The output looks roughly like this:

```text
Thread-A: counter = 1
Thread-A: counter = 2
Thread-B: counter = 1
Thread-A: counter = 3
Thread-B: counter = 2
...
Main: counter = 0
```

You will notice that `Thread-A` and `Thread-B` each count up to 5 without interfering with each other, while the main thread's `thread_counter` is still 0—no thread ever touched it. Three threads, three independent `thread_counter` instances.

### When thread_local Variables Are Initialized

A `thread_local` variable is initialized at **each thread's first use of it (ODR-use)**, not at program startup. This initialize-on-first-use behavior matters a great deal—it guarantees several things. First, if a `thread_local` variable is never accessed by a given thread, that thread never allocates memory for it or runs its initialization, so nothing is wasted. Second, initialization is thread-safe—the standard guarantees that even when multiple threads first reach the same `thread_local` variable at the same time, each thread's initialization runs exactly once, with no interference between them. Third, the initialization order of `thread_local` variables follows where they are declared—within a single translation unit, `thread_local` variables initialize in declaration order, while the order across translation units is unspecified (much like the static initialization order problem).

This deferred-initialization property makes `thread_local` a natural fit for resources you want allocated on demand—a per-thread random number generator, memory pool, or log buffer, for instance. Shared globally, these resources would need locks; made `thread_local`, they run entirely lock-free.

### thread_local versus Global and static Variables: A Lifetime Comparison

To see clearly where `thread_local` sits, we can line it up against the other storage durations. Global variables and `static` member variables have static storage duration—initialized at program startup (or on first use, for `static` locals inside functions) and destroyed at program exit—and all threads share the same instance. `thread_local` variables also live exactly as long as a thread, but each thread holds an independent copy—initialized when the thread starts (on first use) and destroyed when the thread exits. Ordinary stack variables (automatic storage duration) are created at function call and destroyed at function return; threads are isolated from each other here too, of course, but the lifetime is simply too short—gone the moment the function returns.

One easily overlooked point is when `thread_local` variables are destroyed. When a thread exits, all of that thread's `thread_local` variables are destroyed in the reverse order of their initialization. That means a `thread_local` variable's destructor runs in the thread's own context—if the destructor reaches into other threads' state, synchronization becomes your problem again. Trickier still: if a `thread_local` variable's destructor triggers access to another `thread_local` variable that has already been destroyed, the behavior is undefined. This "cross-referencing during destruction" problem is one of the sneakiest traps `thread_local` sets.

## Avoiding Inter-Thread Sharing with thread_local

With the basic concepts in place, let's look at a few typical places `thread_local` earns its keep in real code.

### A Thread-Safe Random Number Generator

Random number generators are one of the most classic `thread_local` use cases. The thread safety of `std::rand()` is implementation-defined—not every platform guarantees it. And even if an implementation happens to be thread-safe, its internal state is still shared by all threads, so in a multithreaded environment the results of repeated calls may lack the randomness distribution you expect. As for the random number engines in `<random>` (such as `std::mt19937`), they are not thread-safe—you cannot call the same engine object from multiple threads at the same time. The solution is to give every thread its own engine:

```cpp
#include <random>
#include <thread>
#include <iostream>
#include <vector>

int random_int(int min_val, int max_val)
{
    // Initialized on each thread's first call, reused afterwards
    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<int> dist(min_val, max_val);
    return dist(generator);
}

void generate_numbers(const char* name, int count)
{
    std::cout << name << ": ";
    for (int i = 0; i < count; ++i) {
        std::cout << random_int(1, 100) << " ";
    }
    std::cout << "\n";
}

int main()
{
    std::thread t1(generate_numbers, "Thread-A", 10);
    std::thread t2(generate_numbers, "Thread-B", 10);
    t1.join();
    t2.join();
    return 0;
}
```

Because `generator` is declared `thread_local`, every thread has its own `std::mt19937` instance maintaining its own random state. `std::random_device{}()` exists to seed each thread's generator differently—note that the seed is obtained when the thread first calls `random_int`, not at program startup. So even if two threads start at almost the same moment, they end up with different seeds (as long as `std::random_device` itself is implemented non-deterministically, which holds on most platforms).

### A Thread-Local Memory Pool

In high-performance settings, frequent `new` and `delete` calls can cause serious lock contention—the standard library's memory allocator (usually `ptmalloc2` or `tcmalloc`) has to take locks internally to protect its free lists. A common optimization is to give each thread a small memory pool, so allocations of small objects come straight out of the thread-local pool instead of competing with other threads:

```cpp
#include <vector>
#include <cstddef>

class ThreadLocalPool {
public:
    static ThreadLocalPool& instance()
    {
        thread_local ThreadLocalPool pool;
        return pool;
    }

    void* allocate(std::size_t size)
    {
        if (size <= kBlockSize) {
            if (!free_list_.empty()) {
                void* ptr = free_list_.back();
                free_list_.pop_back();
                return ptr;
            }
            // Carve a block out of the big chunk
            if (current_offset_ + size > kChunkSize) {
                chunks_.emplace_back(new char[kChunkSize]);
                current_offset_ = 0;
            }
            void* ptr = chunks_.back().get() + current_offset_;
            current_offset_ += size;
            return ptr;
        }
        // Allocations larger than the block size fall back to the global allocator
        return ::operator new(size);
    }

    void deallocate(void* ptr, std::size_t size)
    {
        if (size <= kBlockSize) {
            free_list_.push_back(ptr);
        }
        else {
            ::operator delete(ptr);
        }
    }

private:
    ThreadLocalPool() = default;

    static constexpr std::size_t kBlockSize = 256;
    static constexpr std::size_t kChunkSize = 4096;

    std::vector<std::unique_ptr<char[]>> chunks_;
    std::vector<void*> free_list_;
    std::size_t current_offset_{kChunkSize};  // Initial value triggers the first allocation
};
```

This simplified memory pool shows the classic performance play for `thread_local`: `thread_local ThreadLocalPool pool` guarantees every thread its own pool, and small-object allocation and deallocation complete entirely on the local thread with no synchronization at all. Of course, this is a teaching example—in production you should use a mature memory allocator (such as `jemalloc` or `tcmalloc`), which already build thread-local caching on the same idea internally. But understanding the role `thread_local` plays here is a big help when writing high-performance concurrent code.

## std::call_once and std::once_flag

That wraps up the "one copy per thread" scenario; now we turn to the scenario where "all threads share one copy, but it is initialized exactly once".

`std::call_once` is C++11's one-time initialization mechanism. You hand it a `std::once_flag` and a callable, and it guarantees that no matter how many threads call `call_once` at the same time, the callable executes exactly once—the first thread to arrive performs the initialization and the others wait for it to finish. The mechanism is extremely useful for implementing singletons, initializing global configuration, lazy loading, and similar scenarios.

### Basic Usage

```cpp
#include <mutex>
#include <iostream>
#include <thread>

std::once_flag init_flag;
int* shared_resource = nullptr;

void ensure_initialized()
{
    std::call_once(init_flag, []() {
        std::cout << "Initializing shared resource...\n";
        shared_resource = new int(42);
    });
}

void use_resource(const char* thread_name)
{
    ensure_initialized();
    std::cout << thread_name << ": resource = " << *shared_resource << "\n";
}

int main()
{
    std::thread t1(use_resource, "Thread-A");
    std::thread t2(use_resource, "Thread-B");
    std::thread t3(use_resource, "Thread-C");

    t1.join();
    t2.join();
    t3.join();

    delete shared_resource;
    return 0;
}
```

In the output you will find "Initializing shared resource..." appears exactly once—whatever order the three threads get scheduled in, the initialization code runs a single time. The `std::once_flag` records whether initialization has completed, and `call_once` checks that flag on every call. If initialization hasn't started yet, the first thread runs it; if it is in progress, the other threads block and wait; if it is done, every thread skips straight through.

### call_once and Exception Retry

`std::call_once` has one crucial behavior: if the initialization function (the callable) throws an exception, `call_once` does not mark the `once_flag` as "done". That means the next time a thread calls `call_once`, the initialization is attempted again. The design makes complete sense—if initialization failed (say, opening a file failed or a network connection timed out), you don't want every subsequent thread to assume "already initialized" and then run on invalid state.

```cpp
#include <mutex>
#include <iostream>
#include <stdexcept>

std::once_flag config_flag;
bool config_loaded = false;
int attempt_count = 0;

void load_config()
{
    ++attempt_count;
    std::cout << "Attempt " << attempt_count << ": loading config...\n";

    if (attempt_count < 3) {
        // Simulate the first two attempts failing
        throw std::runtime_error("Config file not ready");
    }

    config_loaded = true;
    std::cout << "Config loaded successfully\n";
}

void worker(const char* name)
{
    try {
        std::call_once(config_flag, load_config);
        std::cout << name << ": using config\n";
    }
    catch (const std::exception& e) {
        std::cout << name << ": init failed - " << e.what() << "\n";
    }
}
```

In this example, the first two calls to `call_once` make `load_config` throw, so the `once_flag` is never marked as completed and the next call attempts the initialization again. Once the third attempt succeeds, every subsequent call skips the initialization outright. This retry-after-exception behavior is a major advantage of `call_once` over the Meyers singleton—we will compare the two in detail shortly.

## The Meyers Singleton: A static Local in Function Scope

Since C++11, `static` local variables in function scope carry a very important guarantee: **their initialization is thread-safe**. If multiple threads first reach the declaration of a `static` variable at the same time, exactly one of them performs the initialization while the others wait. This is the so-called "Meyers singleton" (named after Scott Meyers, who popularized the idiom in *Effective C++*):

```cpp
#include <iostream>
#include <thread>

class Singleton {
public:
    static Singleton& instance()
    {
        static Singleton inst;  // Thread-safe initialization
        return inst;
    }

    void do_work()
    {
        std::cout << "Singleton working\n";
    }

private:
    Singleton()
    {
        std::cout << "Singleton constructed\n";
    }

    // Forbid copying and moving
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};

void use_singleton(const char* name)
{
    std::cout << name << ": accessing singleton\n";
    Singleton::instance().do_work();
}

int main()
{
    std::thread t1(use_singleton, "Thread-A");
    std::thread t2(use_singleton, "Thread-B");
    t1.join();
    t2.join();
    return 0;
}
```

"Singleton constructed" is printed only once, no matter how many threads call `instance()` concurrently. The C++11 standard ([stmt.dcl] paragraph 4) states it explicitly: if control flow enters the declaration of a `static` local variable in multiple threads simultaneously, one of them executes the initialization and the others block and wait. The compiler and the runtime library deliver this guarantee together—on GCC and Clang it is typically implemented through the two ABI functions `__cxa_guard_acquire` / `__cxa_guard_release`, whose underlying machinery resembles `call_once`.

The Meyers singleton is the simplest, safest way to implement the singleton pattern. No manual locking, no `std::call_once`, no `std::atomic`—the compiler takes care of everything for you. If your singleton's initialization cannot fail (won't throw), the Meyers singleton is the best choice.

## When call_once Beats the Meyers Singleton

If the Meyers singleton is this good, why do we still need `std::call_once`? The key differences are **granularity of control** and **exception handling**.

A Meyers singleton's initialization is welded to the variable's declaration—you can't do preparatory work before it, and you can't pick a different strategy after it fails. `call_once` hands you full control: the initialization function can be an ordinary function or a lambda whose contents you decide freely; the initialization can access external state (reading a configuration file path, connecting to a database); and if it fails (throws), subsequent calls can retry.

A subtler difference is the "location" of initialization. A Meyers singleton initializes when the `instance()` function is first called—and that moment may not be the one you want. Perhaps you would rather initialize all global resources explicitly right after the program starts, instead of an expensive initialization suddenly firing in the middle of handling some request. `call_once` lets you put that initialization logic anywhere—invoke it proactively at the top of `main()`, or lazy-load it when truly needed; it is entirely up to you.

There is also a very practical scenario: if your "singleton" is not one object but a set of initialization steps (bringing up the logging system, the configuration manager, the database connection pool, and so on), `call_once` can bundle all of those steps into a single function. A Meyers singleton can only initialize one object—to initialize several things you would need a separate `static` local for each, which is not flexible.

To sum up the selection strategy: if your initialization logic is simple, cannot fail, and only needs to initialize one object, the Meyers singleton is the best choice—concise, safe, zero overhead. If you need more flexible control—initialization may fail and need retries, must access external state, or must initialize a group of resources rather than a single object—`call_once` is the better tool.

## thread_local and Dynamically Loaded Libraries

`thread_local` is very reliable in ordinary use, but scenarios involving dynamically linked libraries (shared libraries / DLLs) come with a few things to watch out for.

The root of the problem lies in lifetime management of `thread_local` variables. Each thread's `thread_local` variables must be destroyed when that thread exits, which requires registering a destruction callback. In the main program, this registration is performed by the C++ runtime when a `thread_local` variable is first accessed. In a dynamically loaded library, the situation gets more complicated—the library may be loaded or unloaded at any time, and the destruction callbacks for its `thread_local` variables need to be cleaned up before the library is unloaded.

On Linux (glibc + GCC/Clang), `thread_local` variables in shared libraries usually work fine—the `__cxa_thread_atexit` function is responsible for registering the thread-exit destruction callbacks, and it handles library unloading correctly. In the Windows DLL model, however, `thread_local` inside DLLs has been problematic for a long time—when a DLL unloads, the destruction callbacks for `thread_local` variables of already-exited threads can point into code sections that are no longer valid, causing crashes. Only fairly recent MSVC versions (VS 2017 and later) support `thread_local` in DLLs reasonably well.

If you need to write cross-platform library code that may be dynamically loaded, keep the following in mind when using `thread_local`. First, make sure the compilers on your target platforms fully support `thread_local` in dynamic libraries. Second, be especially careful if a `thread_local` variable's destructor has side effects (releasing locks, writing files, notifying other threads)—when the library unloads, those destructions may not run in the order you expect. Finally, in some embedded or special environments (WebAssembly, certain RTOSes), `thread_local` support may be incomplete or absent altogether—if your code needs to run on these platforms, you are better off implementing thread-local storage some other way.

## Summary

This article discussed two mechanisms for handling "initialization" in a concurrent environment. `thread_local` gives each thread an independent copy of a variable, eliminating data sharing at the root—a natural fit for random number generators, memory pools, log buffers, and other "one per thread" resources. Its initialization is deferred (on first use) and thread-safe, and destruction happens when the owning thread exits.

`std::call_once` paired with `std::once_flag` provides the "all threads share one copy, but initialize only once" guarantee. It is more flexible than the Meyers singleton—it supports retrying after exceptions, can initialize non-object resources (a set of function calls, for example), and can be triggered from any location. If your initialization logic is simple and cannot fail, the Meyers singleton remains the first choice—it is more concise and needs no extra `once_flag` variable. The two are not replacements for each other but complementary tools; which one you choose depends on your concrete needs.

With this, the four articles of ch01 are complete. Starting from the basic usage of `std::thread`, we worked through argument passing, lifetime management, RAII wrappers, thread ownership, and finally thread-local storage and one-time initialization. All of it is the foundation for what comes next—when we discuss mutexes, atomic operations, and lock-free programming later on, we will keep leaning on the concepts and tools established in this chapter.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: A Thread-Safe Configuration Initializer

Implement a `ConfigManager` class that reads its configuration from a file (you can simulate it with `std::getline`) and uses `std::call_once` to guarantee initialization happens exactly once. Requirements: (1) if reading the file fails, it should throw an exception and allow a retry; (2) provide a `get(key)` method that returns the configured value; (3) multiple threads may call `get()` at the same time, but only the first call triggers the file read.

```cpp
// Skeleton code
#include <mutex>
#include <string>
#include <unordered_map>

class ConfigManager {
public:
    static ConfigManager& instance();

    std::string get(const std::string& key) const;

private:
    ConfigManager() = default;
    void load_from_file();

    std::once_flag init_flag_;
    std::unordered_map<std::string, std::string> config_;
};
```

### Exercise 2: A thread_local Logger

Implement a simple thread-local logger: each thread has its own log buffer (a `std::stringstream`), and writing log entries takes no locks. Provide two methods: `log(message)` appends a log entry, and `flush()` writes the buffer's contents to `std::cout` and clears it. In `main()`, start 4 threads; each writes 10 log entries and then flushes. Watch whether the output stays thread-safe.

### Exercise 3: Comparing call_once and the Meyers Singleton

Implement the same singleton both ways—one with `std::call_once`, one as a Meyers singleton. Then simulate an expensive initialization in the singleton's constructor (`std::this_thread::sleep_for(std::chrono::milliseconds(100))`), have 8 threads access the singleton simultaneously, and measure the performance difference between the two implementations. Think about it: why might their performance differ? Hint: the Meyers singleton's initialization lock sits on the `static` variable, while `call_once`'s lock sits on the `once_flag`—when multiple threads arrive at once, the waiting mechanism is the same, but the implementation details may differ.

## References

- [thread_local storage — cppreference](https://en.cppreference.com/w/cpp/language/storage_duration#thread_local_storage)
- [std::call_once — cppreference](https://en.cppreference.com/w/cpp/thread/call_once)
- [Magic Statics (C++11 thread-safe statics) — cppreference](https://en.cppreference.com/w/cpp/language/static#Static_local_variables)
- [Effective C++, Item 4: Make sure that objects are initialized before they're used — Scott Meyers](https://www.oreilly.com/library/view/effective-c/0321334876/)
- [Thread-local storage — Wikipedia](https://en.wikipedia.org/wiki/Thread-local_storage)
- [Dynamic Initialization and Destruction in C++ (Itanium C++ ABI)](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#once-ctor)
