---
title: thread_local and call_once
description: Master thread-local storage and one-time initialization mechanisms to
  write thread-safe lazy initialization and global state.
chapter: 1
order: 4
tags:
- host
- cpp-modern
- intermediate
- 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 18
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 线程所有权与 RAII
related:
- 线程参数与生命周期
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/04-thread-local-and-call-once.md
  source_hash: 803ce5b7672b1ad64cc16b38b015c7b24da48284e2fde1fa5b3202b23d98ff25
  translated_at: '2026-05-20T04:35:40.114272+00:00'
  engine: anthropic
  token_count: 3459
---
# thread_local and call_once

In the previous article, we used RAII to solve the problems of thread ownership and lifetime management. In this article, we look at a different dimension: when multiple threads need to access certain "global state," how do we ensure thread safety without sacrificing performance?

The answer splits into two directions. The first direction is to **avoid sharing entirely**—give each thread its own independent copy, let them use their own, and competition naturally disappears. This is what ``thread_local`` storage duration does. The second direction is to **share but initialize only once**—a global object needs to be initialized on first use, and no matter how many threads trigger the initialization simultaneously, it executes only once. This is the responsibility of ``std::call_once``. These two tools solve two different problems, but they share a common theme: making concurrent code safe during the "initialization" phase.

## thread_local Storage Duration

C++ has several storage durations: automatic storage (local variables on the stack), static storage (global variables and ``static`` local variables), dynamic storage (allocated via ``new``/``malloc``), and thread storage. ``thread_local`` is the specifier for thread storage duration—variables modified by it have their own independent instance in each thread, existing from thread creation until thread exit.

What does this mean? Suppose you declare a ``thread_local int counter = 0;``. Then, however many threads your program has, that is how many independent ``counter`` copies exist. Thread A's modifications to its own copy are completely invisible to Thread B—they are different objects in memory with different addresses. From a thread's perspective, a ``thread_local`` variable acts like a "thread-private global variable"—its lifetime is as long as the thread, but each thread gets its own copy.

Let's look at the most straightforward example—a thread-safe counter that doesn't need any locks:

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

    // 主线程也有自己的 thread_counter 副本
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

You'll notice that ``Thread-A`` and ``Thread-B`` each count up to 5 without interfering with each other. The main thread's ``thread_counter`` remains 0—it was never touched by any thread. Three threads, three independent ``thread_counter`` instances.

### Initialization Timing of thread_local

A ``thread_local`` variable is initialized **when each thread first uses it (ODR-use)**, not when the program starts. This "initialize on first use" behavior is very important—it guarantees the following: first, if a ``thread_local`` variable is never accessed by a particular thread, that thread won't allocate memory or execute initialization for it, so there's no waste. Second, initialization is thread-safe—the standard guarantees that even if multiple threads simultaneously access the same ``thread_local`` variable for the first time, each thread's initialization executes only once without interfering with the others. Third, the initialization order of ``thread_local`` variables relates to their declaration position—``thread_local`` variables within the same translation unit are initialized in declaration order, while the order across different translation units is unspecified (similar to the static variable initialization order problem).

This "lazy initialization" characteristic makes ``thread_local`` very well-suited for implementing "on-demand allocated" resources—such as per-thread random number generators, memory pools, log buffers, and so on. If these resources were shared globally, they would require locks, but with ``thread_local``, they become completely lock-free.

### thread_local vs. Global/Static Variables: Their Respective Lifetimes

To understand ``thread_local``'s position more clearly, we can compare it with other storage durations. Global variables and ``static`` member variables have static storage duration—they are initialized when the program starts (or on first use, for ``static`` local variables inside functions) and destroyed when the program exits. All threads share the same instance. ``thread_local`` variables also have a lifetime as long as the thread, but each thread has an independent copy—initialized when the thread starts (on first use) and destroyed when the thread exits. Ordinary stack variables (automatic storage duration) are created on function call and destroyed on function return; they are of course also isolated between threads, but their lifetime is too short—they're gone once the function returns.

An easily overlooked point is the destruction timing of ``thread_local`` variables. When a thread exits, all of that thread's ``thread_local`` variables are destroyed in reverse order of their initialization. This means the destructors of ``thread_local`` variables execute within the thread's context—if you access other threads' state inside a destructor, you need to be careful about synchronization issues. Even trickier, if the destructor of a ``thread_local`` variable triggers access to another ``thread_local`` variable (which has already been destroyed), the behavior is undefined behavior (UB). This "cross-reference during destruction" problem is one of ``thread_local``'s most hidden traps.

## Avoiding Inter-Thread Sharing with thread_local

Having understood the basic concepts, let's look at a few typical application scenarios for ``thread_local`` in practice.

### Thread-Safe Random Number Generator

The random number generator is one of the most classic use cases for ``thread_local``. The thread safety of ``std::rand()`` is implementation-defined—not all platforms guarantee it—and even if a particular implementation happens to be thread-safe, its internal state is still shared by all threads, so the results of multiple calls in a multithreaded environment may lack the randomness distribution you expect. The random number engines in ``<random>`` (such as ``std::mt19937``) are not thread-safe—you cannot call the same engine object simultaneously from multiple threads. The solution is to give each thread its own independent engine:

```cpp
#include <random>
#include <thread>
#include <iostream>
#include <vector>

int random_int(int min_val, int max_val)
{
    // 每个线程第一次调用时初始化，后续复用
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

``generator`` is declared as ``thread_local``, so each thread has its own ``std::mt19937`` instance, each maintaining its own random state. ``std::random_device{}()`` is used to provide a different seed for each thread's generator—note that this seed is obtained when the thread first calls ``random_int``, not when the program starts. So even if two threads start almost simultaneously, they will get different seeds (as long as ``std::random_device``'s own implementation is non-deterministic, which is true on most platforms).

### Thread-Local Memory Pool

In high-performance scenarios, frequently calling ``new`` and ``delete`` can cause severe lock contention—because the standard library's memory allocator (usually ``ptmalloc2`` or ``tcmalloc``) needs to lock internally to protect the free list. A common optimization is to give each thread a small memory pool, where small object allocations are taken directly from the thread-local pool without competing with other threads:

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
            // 从大块中切出一块
            if (current_offset_ + size > kChunkSize) {
                chunks_.emplace_back(new char[kChunkSize]);
                current_offset_ = 0;
            }
            void* ptr = chunks_.back().get() + current_offset_;
            current_offset_ += size;
            return ptr;
        }
        // 超过块大小的分配，回退到全局分配器
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
    std::size_t current_offset_{kChunkSize};  // 初始值触发首次分配
};
```

This simplified memory pool demonstrates the typical usage of ``thread_local`` in performance optimization: ``thread_local ThreadLocalPool pool`` ensures each thread has its own independent memory pool, and the allocation and deallocation of small objects are completed entirely locally without any synchronization operations. Of course, this is just a teaching example—in production environments, you should use mature memory allocators (such as ``jemalloc``, ``tcmalloc``), which already implement thread-local caching internally using similar ideas. But understanding the role ``thread_local`` plays here is very helpful for writing high-performance concurrent code.

## std::call_once and std::once_flag

Having covered the "one copy per thread" scenario, let's now look at the "all threads share one copy but initialize only once" scenario.

``std::call_once`` is a one-time initialization mechanism provided by C++11. You give it a ``std::once_flag`` and a callable object, and it guarantees that no matter how many threads call ``call_once`` simultaneously, the callable object is executed only once—the first thread to arrive executes the initialization, and the remaining threads wait for it to finish. This mechanism is very useful in scenarios like implementing the singleton pattern, global configuration initialization, and lazy loading.

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

In the output, you'll find that "Initializing shared resource..." appears only once—no matter the scheduling order of the three threads, the initialization code executes only once. ``std::once_flag`` records whether initialization has completed, and ``call_once`` checks this flag on each call. If initialization hasn't started, the first thread executes it; if it's in progress, other threads block and wait; if it's already complete, all threads skip it directly.

### call_once and Exception Retry

``std::call_once`` has a very critical behavior: if the initialization function (callable object) throws an exception, ``call_once`` will not mark the ``once_flag`` as "completed." This means that the next time a thread calls ``call_once``, the initialization will be attempted again. This design is very reasonable—if initialization fails (for example, failing to open a file, network connection timeout), you don't want all subsequent threads to think "it's already initialized" and then use an invalid state.

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
        // 模拟前两次失败
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

In this example, the first two times ``call_once`` is called, ``load_config`` will throw an exception, and ``once_flag`` won't be marked as completed, so the next call will retry the initialization. Only after the third attempt succeeds will all subsequent calls skip initialization directly. This "retry after exception" behavior is an important advantage of ``call_once`` over the Meyers singleton—we'll compare them in detail later.

## Meyers Singleton: static Local in Function Scope

Starting from C++11, ``static`` local variables in function scope have a very important guarantee: **their initialization is thread-safe**. If multiple threads simultaneously reach the declaration of a ``static`` variable for the first time, only one thread will execute the initialization, and the other threads will wait. This is the so-called "Meyers singleton" (named after Scott Meyers, who popularized this pattern in *Effective C++):

```cpp
#include <iostream>
#include <thread>

class Singleton {
public:
    static Singleton& instance()
    {
        static Singleton inst;  // 线程安全的初始化
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

    // 禁止复制和移动
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

"Singleton constructed" will only be output once, no matter how many threads simultaneously call ``instance()``. The C++11 standard ([stmt.dcl] paragraph 4) explicitly states: if control flow enters the declaration of a ``static`` local variable while multiple threads are executing, one of them will execute the initialization and the other threads will block and wait. This guarantee is implemented jointly by the compiler and the runtime library—on GCC and Clang, it is typically implemented through the ``__cxa_guard_acquire``/``__cxa_guard_release`` ABI functions, using a mechanism similar to ``call_once`` underneath.

The Meyers singleton is the most concise and safe way to implement the singleton pattern. No manual locking, no ``std::call_once``, no ``std::atomic``—the compiler handles everything for you. If your singleton initialization won't fail (won't throw exceptions), the Meyers singleton is the best choice.

## When call_once Is Better Than Meyers Singleton

Since the Meyers singleton is so easy to use, why do we still need ``std::call_once``? The key differences lie in **control granularity** and **exception handling**.

The Meyers singleton's initialization is bound to the variable's declaration—you can't do preparation work before initialization, nor can you choose a different strategy after initialization fails. ``call_once``, on the other hand, gives you complete control: the initialization function can be a regular function or a lambda, and you can freely decide its contents; initialization can access external state (such as reading a configuration file path, connecting to a database); if initialization fails (throws an exception), subsequent calls can retry.

A more subtle difference is the "location" of initialization. The Meyers singleton's initialization happens when the ``instance()`` function is first called—this timing might not be what you want. Perhaps you want to explicitly initialize all global resources after program startup, rather than suddenly triggering a time-consuming initialization in the middle of some request processing. ``call_once`` lets you place this initialization logic anywhere—you can proactively call it at the beginning of ``main()``, or lazy-load it when truly needed, entirely under your control.

There's also a practical scenario: if your "singleton" isn't a single object but a set of initialization steps (such as initializing a logging system, configuration manager, database connection pool, etc.), ``call_once`` can package all these steps into one function. The Meyers singleton can only initialize one object—to initialize multiple things, you'd need to write a ``static`` local variable for each one, which isn't flexible enough.

To summarize the selection strategy: if your initialization logic is simple, won't fail, and only needs to initialize one object, the Meyers singleton is the best choice—concise, safe, and zero-overhead. If you need more flexible control—initialization might fail, needs retrying, needs to access external state, or needs to initialize a group of resources rather than a single object—``call_once`` is the more appropriate tool.

## thread_local and Dynamically Loaded Libraries

``thread_local`` is very reliable in normal use, but there are some issues to be aware of in scenarios involving dynamically linked libraries (shared library / DLL).

The root of the problem lies in the lifetime management of ``thread_local`` variables. Each thread's ``thread_local`` variables need to be destroyed when the thread exits, which requires registering a destruction callback. In the main program, this registration is handled by the C++ runtime when the ``thread_local`` variable is first accessed. But in dynamically loaded libraries, the situation becomes more complex—the library might be loaded or unloaded at any time, and the destruction callbacks for ``thread_local`` variables need to be cleaned up before the library is unloaded.

On Linux (glibc + GCC/Clang), support for ``thread_local`` variables in dynamic libraries usually works correctly—the ``__cxa_thread_atexit`` function is responsible for registering destruction callbacks on thread exit, and it correctly handles library unloading. But in Windows's DLL model, the behavior of ``thread_local`` in DLLs has long been problematic—when a DLL is unloaded, the destruction callbacks for ``thread_local`` variables of already-exited threads point to invalid code segments, causing crashes. It wasn't until more recent MSVC versions (VS 2017 and later) that support for ``thread_local`` in DLLs became reasonably complete.

If you need to write cross-platform library code that might be dynamically loaded, you should note the following points when using ``thread_local``. First, ensure that your target platform's compiler support for ``thread_local`` in dynamic libraries is complete. Second, if the destructors of ``thread_local`` variables have side effects (such as releasing locks, writing files, notifying other threads), be especially careful—these destructions might not execute in the order you expect when the library is unloaded. Finally, in some embedded or special environments (such as WebAssembly, certain RTOSes), support for ``thread_local`` might be incomplete or entirely absent—if your code needs to run on these platforms, it's best to implement thread-local storage in other ways.

## Summary

In this article, we discussed two mechanisms for handling "initialization" problems in concurrent environments. ``thread_local`` provides each thread with an independent copy of a variable, fundamentally eliminating data sharing—suitable for scenarios like random number generators, memory pools, and log buffers where "each thread has its own copy." Its initialization is lazy (on first use), thread-safe, and destruction occurs when the corresponding thread exits.

``std::call_once`` paired with ``std::once_flag`` provides the guarantee of "all threads share one copy, but initialize only once." It is more flexible than the Meyers singleton—it supports exception retry, can initialize non-object resources (such as a group of function calls), and can trigger initialization at any location. If your initialization logic is simple and won't fail, the Meyers singleton is still the first choice—it's more concise and doesn't need an extra ``once_flag`` variable. The two are not replacements but complementary tools; which one to choose depends on your specific needs.

With this, the four articles of ch01 are fully concluded. We started from the basic usage of ``std::thread``, went through parameter passing, lifetime management, RAII wrappers, thread ownership, and thread-local storage along with one-time initialization. These are all foundations for the content that follows—when we discuss mutexes, atomic operations, and lock-free programming later, we will frequently use the concepts and tools established in this chapter.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit ``code/volumn_codes/vol5/ch01-thread-lifecycle-raii/``.

## Exercises

### Exercise 1: Thread-Safe Configuration Initializer

Implement a ``ConfigManager`` class that reads configuration from a file (you can simulate this with ``std::getline``), using ``std::call_once`` to guarantee initialization only once. Requirements: (1) If the file read fails, it should throw an exception and allow retrying; (2) Provide a ``get(key)`` method that returns the configuration value; (3) Multiple threads can simultaneously call ``get()``, but only the first call will trigger the file read.

```cpp
// 骨架代码
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

### Exercise 2: thread_local Logger

Implement a simple thread-local logger where each thread has its own log buffer (``std::stringstream``), and log writing doesn't require locks. Provide two methods: ``log(message)`` to write logs, and ``flush()`` to output the buffer contents to ``std::cout`` and clear it. In ``main()``, start four threads, have each thread write 10 log entries and then flush, and observe whether the output is thread-safe.

### Exercise 3: Comparing call_once and Meyers Singleton

Implement the same singleton in two ways—one using ``std::call_once``, and one using the Meyers singleton. Then simulate a time-consuming initialization in the singleton's constructor (``std::this_thread::sleep_for(std::chrono::milliseconds(100))``), use eight threads to access the singleton simultaneously, and measure the performance difference between the two implementations. Think about: why might the performance differ? Hint: the Meyers singleton's initialization lock is on the ``static`` variable, while ``call_once``'s lock is on the ``once_flag``—if multiple threads access simultaneously, the waiting mechanism is the same, but the implementation details may differ.

## References

- [thread_local storage — cppreference](https://en.cppreference.com/w/cpp/language/storage_duration#thread_local_storage)
- [std::call_once — cppreference](https://en.cppreference.com/w/cpp/thread/call_once)
- [Magic Statics (C++11 thread-safe statics) — cppreference](https://en.cppreference.com/w/cpp/language/static#Static_local_variables)
- [Effective C++, Item 4: Make sure that objects are initialized before they're used — Scott Meyers](https://www.oreilly.com/library/view/effective-c/0321334876/)
- [Thread-local storage — Wikipedia](https://en.wikipedia.org/wiki/Thread-local_storage)
- [Dynamic Initialization and Destruction in C++ (Itanium C++ ABI)](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#once-ctor)
