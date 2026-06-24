---
chapter: 2
cpp_standard:
- 20
description: 'C++20 Synchronization Primitives: Single/Multi-use Barriers and Counting
  Semaphores, Scenario Selection, and Engineering Patterns'
difficulty: advanced
order: 5
platform: host
prerequisites:
- condition_variable 与等待语义
reading_time_minutes: 19
related:
- atomic 操作
- 线程池设计
tags:
- host
- cpp-modern
- advanced
- mutex
title: latch, barrier, and semaphore
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/05-latch-barrier-semaphore.md
  source_hash: 38d2d56a4511e46d56c4fc5e7ea62990b6d857e05d540fa67159069b40481b5c
  translated_at: '2026-06-24T01:07:31.745667+00:00'
  engine: anthropic
  token_count: 3929
---
# Latches, Barriers, and Semaphores

In the previous post, we dissected the wait-notify mechanism of `condition_variable`—spurious wakeups, lost wakeups, and predicate-based `wait`. With this foundation, we can now tackle a more practical problem: often, we don't need the generic "wait until a condition is met" semantics. Instead, we just need "wait until everyone arrives" or "limit the number of threads accessing a resource simultaneously." These two requirements correspond to **barrier** and **semaphore** synchronization patterns, respectively. C++20 finally brought these concepts into the standard library as `std::latch`, `std::barrier`, and `std::counting_semaphore`.

Honestly, before this, we could only simulate these patterns using mutex + condition_variable + a manual counter—code that was verbose, error-prone, and required rewriting every time. The introduction of these three primitives in C++20 essentially standardizes these high-frequency patterns. However, to use them effectively, we need to understand the semantic boundaries and applicable scenarios of each primitive, rather than using a hammer to hit every nail.

## `std::latch`: One-Use Countdown Barrier

`std::latch` is defined in the `<latch>` header. It is a **single-directional decrementing counter**. You can imagine it as a door with a latch bolt; the bolt's strength is determined by the initial count. Each time a thread executes `count_down()`, the bolt loosens by one notch; when the count reaches zero, the door opens, and all threads blocked on `wait()` may proceed. The key characteristic is: **a latch is single-use**—once the count reaches zero, it remains "open" forever and cannot be reset.

The API for `std::latch` is very concise: the constructor takes an initial count value `expected` (of type `std::ptrdiff_t`); `count_down(n = 1)` decrements the count by n (non-blocking); `wait()` blocks the current thread until the count reaches zero; `arrive_and_wait(n = 1)` is an atomic combination of `count_down(n)` and `wait()`—the current thread contributes a decrement and then waits for the count to reach zero; `try_wait()` is a non-blocking check that returns `true` when the counter reaches zero (note: it allows for a very low probability of spuriously returning `false`). Let's understand its usage through a concrete scenario.

### Pattern: One-Time Initialization

Suppose our program needs to initialize three subsystems at startup—logging, configuration, and network connection. Each subsystem is handled by an independent thread, and the main thread must wait until all subsystems are ready before starting the business logic. This is a typical one-time synchronization scenario:

```cpp
#include <latch>
#include <thread>
#include <vector>
#include <iostream>

int main() {
    // We have 3 subsystems to initialize, plus the main thread needs to wait.
    // So the initial count is 3.
    std::latch init_done{3};

    // Lambda to simulate subsystem initialization
    auto init_task = [&](const std::string& name) {
        std::cout << "[" << name << "] Initializing..." << std::endl;
        // ... Perform initialization work ...
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[" << name << "] Ready." << std::endl;

        // Signal completion
        init_done.count_down();
    };

    std::vector<std::jthread> threads;
    threads.emplace_back(init_task, "Logger");
    threads.emplace_back(init_task, "Config");
    threads.emplace_back(init_task, "Network");

    // Main thread waits here.
    // Even if main finishes "instantly", it will block until count reaches 0.
    init_done.wait();

    std::cout << "All subsystems initialized. Starting main logic..." << std::endl;

    // Threads are joined automatically by std::jthread destructor
    return 0;
}
```

In this example, the `std::latch` acts as a synchronization point. The main thread calls `wait()` to enter a waiting state. The three worker threads call `count_down()` after completing their tasks. Once the counter hits zero, the main thread wakes up and proceeds. Note that `std::latch` is not a "gate" that closes again; if we tried to wait on it a second time, we would return immediately because the counter is already zero.

## `std::barrier`: Reusable Synchronization Point

If `std::latch` is a disposable gate, then `std::barrier` is a turnstile that can be used repeatedly. It is defined in `<barrier>`. Its core use case is **iterative parallel computation**: multiple threads perform a task in stages, and they must all finish stage N before any thread can start stage N+1.

Unlike a latch, a barrier has a **phase completion phase**. When the expected number of threads arrive (i.e., the counter reaches zero), the "completion phase" is executed. By default, this does nothing, but we can customize it. Crucially, **the counter is automatically reset** after completion, ready for the next round.

The API is slightly more complex: the constructor takes `expected` (count) and an optional `CompletionFunction` object. `arrive_and_wait()` decrements the count and blocks until the phase completes (resetting the counter for the next phase).

Let's look at a classic example: iterative data processing.

### Pattern: Iterative Parallel Processing

Imagine we have a large dataset. We split it into chunks, each processed by a thread. However, each iteration depends on the results of the previous iteration (for example, a specific smoothing algorithm). We need to ensure all threads finish the current iteration before anyone starts the next one.

```cpp
#include <barrier>
#include <vector>
#include <thread>
#include <iostream>
#include <numeric>

// Data to be processed
std::vector<int> data(1000, 0);

int main() {
    const int num_threads = 4;
    const int iterations = 5;

    // Define a completion function: runs when all threads arrive for each iteration
    auto on_barrier_completion = []() noexcept {
        // This runs exactly once per iteration, by one of the threads
        std::cout << "Iteration complete. Swapping buffers..." << std::endl;
        // In a real app, we might swap double buffers here or update global stats
    };

    // The barrier expects 4 threads
    std::barrier sync_point(num_threads, on_barrier_completion);

    std::vector<std::jthread> workers;

    auto worker = [&](int id) {
        for (int i = 0; i < iterations; ++i) {
            // 1. Do the work for this iteration
            // (Simulated: just increment some values)
            std::cout << "Thread " << id << " working on iteration " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10 * id));

            // 2. Wait for everyone to finish this iteration
            // This decrements the counter. If we are the last to arrive,
            // on_barrier_completion runs, counter resets, and everyone wakes up.
            sync_point.arrive_and_wait();
        }
    };

    for (int i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker, i);
    }
}
```

**Key Difference:** `std::latch` is for "one-shot" events (like startup or shutdown). `std::barrier` is for "looping" or "phased" collaboration. If you try to reuse a `std::latch`, it won't work because it stays open. `std::barrier` automatically resets.

## `std::counting_semaphore`: Resource Limiter

`std::counting_semaphore` (defined in `<semaphore>`) is the most flexible of the three, but also the most prone to misuse if you don't understand its model. It maintains an internal counter (initially non-negative). Unlike `latch` and `barrier`, its counter can go up and down.

- **`acquire()` (or `wait()` in older terms):** Decrements the counter. If the counter is zero, it blocks until it becomes positive.
- **`release(n = 1)`:** Increments the counter by n. If threads are waiting, this wakes them up.

This is essentially the classic **Dijkstra semaphore**. In C++, we use it primarily to **limit concurrency** (e.g., "Only 3 threads can access this database connection at a time").

### Pattern: Bounded Concurrency (Pool)

Let's say we have a task queue with 100 tasks, but we only want 5 threads executing them simultaneously to avoid exhausting memory or CPU.

```cpp
#include <semaphore>
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

// Limit concurrency to 3 threads
std::counting_semaphore<3> signal(3); // Max count is 3 (template parameter), initial value 3

void worker(int id) {
    // Attempt to acquire a "slot". If 3 threads are already here, we block.
    signal.acquire();

    std::cout << "Thread " << id << " is running." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Thread " << id << " is done." << std::endl;

    // Release the slot for the next thread
    signal.release();
}

int main() {
    std::vector<std::jthread> threads;
    // Launch 10 threads, but only 3 will run at a time
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(worker, i);
    }
}
```

### Why not just use `std::mutex`?

A mutex allows **only one** thread at a time (binary semaphore). A `counting_semaphore` allows **N** threads. If you have a pool of 5 database connections, you initialize a semaphore with 5. `acquire()` takes a connection, `release()` puts it back.

## Summary: Which One to Use?

| Primitive | Counter Behavior | Reusable? | Primary Use Case |
| :--- | :--- | :--- | :--- |
| **`std::latch`** | Decrements only | No | One-time coordination (e.g., startup, shutdown). |
| **`std::barrier`** | Decrements, then auto-resets | Yes | Repeated phased synchronization (e.g., time-step simulations). |
| **`std::counting_semaphore`** | Increments & Decrements | Yes | Managing limited resources (e.g., thread pools, connection pools). |

### Common Pitfalls

1. **Don't forget to `release()`:** If you use a semaphore and an exception occurs before `release()`, you have a "leaked" semaphore slot, effectively reducing your pool size (similar to a mutex leak). Use RAII wrappers or `try/finally` logic (or C++ code flow control) to ensure this doesn't happen.
2. **Barrier Completion Function:** The completion function in `std::barrier` runs in the context of the thread that caused the counter to reach zero. Make sure this function is thread-safe and fast, as it blocks all other threads waiting on the barrier.
3. **Spurious Wakeups:** While `latch` and `barrier` are less prone to the classic spurious wakeups of condition variables (in terms of logic), `try_wait()` on a latch can still return false spuriously, and semaphores deal with OS-level scheduling which can have its own quirks. Always use the blocking `wait()`/`acquire()` methods for logic correctness unless you have a specific polling loop.

By using these C++20 primitives, we avoid the "reinvent the wheel" syndrome of manual counters and condition variables, leading to cleaner, more declarative, and less error-prone multithreaded code.

```cpp
#include <latch>
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

void init_logger()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Logger initialized\n";
}

void init_config()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Config loaded\n";
}

void init_network()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << "Network connected\n";
}

int main()
{
    constexpr int kInitCount = 3;
    std::latch init_done(kInitCount);

    std::vector<std::thread> threads;
    threads.emplace_back([&init_done]() {
        init_logger();
        init_done.count_down();
    });
    threads.emplace_back([&init_done]() {
        init_config();
        init_done.count_down();
    });
    threads.emplace_back([&init_done]() {
        init_network();
        init_done.count_down();
    });

    init_done.wait();
    std::cout << "All subsystems ready, starting application\n";

    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

Here, each initialization thread calls `init_done.count_down()` after completing its task, while the main thread calls `init_done.wait()` to block and wait. Once all three `count_down` calls have finished, the main thread wakes up and continues execution. Note that the worker threads call `count_down()` instead of `arrive_and_wait()`—because they don't need to wait for the others; they can exit once their work is done. Only the main thread needs to wait.

If the worker threads also want to "finish their part and then wait for everyone to continue together," we use `arrive_and_wait()`:

```cpp
void worker(int id, std::latch& sync)
{
    std::cout << "Worker " << id << " phase 1 done\n";
    sync.arrive_and_wait();  // 贡献一个递减，同时等待计数归零
    std::cout << "Worker " << id << " phase 2 starts\n";
}
```

The semantics of `arrive_and_wait()` constitute an atomic "decrement + wait"—the calling thread blocks itself until the count reaches zero. Internally, this is equivalent to `count_down(); wait();`, but the standard guarantees the atomicity of these two steps. This means that no other thread can reduce the count to zero between the "decrement" and "wait" operations, causing a waiter to miss the wake-up signal.

There is an easily overlooked detail: the parameter for `count_down` can be greater than one. For example, if a thread is responsible for completing three tasks, it can call `count_down(3)` in one go. If the passed value causes the count to become negative, the behavior is undefined—so the caller must ensure the count is not decremented too far.

## std::barrier: Reusable Phase Synchronization

`std::latch` solves the problem of "waiting for everyone to arrive" exactly once, but many parallel algorithms require **repeated synchronization**—for example, in iterative computations, every round of iteration requires all threads to complete the current step before proceeding to the next. Using a latch would mean creating a new latch object for every iteration, which is both wasteful and inelegant. `std::barrier` is designed for this: it is a **reusable** synchronization barrier. Once all participating threads arrive at the barrier point, the barrier automatically resets and can be used for the next round of synchronization.

`std::barrier` is defined in the `<barrier>` header file. It is a class template `std::barrier<CompletionFunction>`, where `CompletionFunction` defaults to an empty function. The constructor takes the number of participating threads (and an optional completion function). The core API consists of three functions: `arrive()` notifies the barrier "I'm here" without blocking; `arrive_and_wait()` notifies and blocks until all threads have arrived; `arrive_and_drop()` notifies and permanently reduces the number of participating threads (useful for scenarios where participants are dynamically removed).

### Basic Usage: Multi-Phase Parallel Computation

Let's look at a simple multi-phase parallel computation scenario. Suppose we have four worker threads, and each thread needs to execute three phases sequentially, requiring synchronization among all threads between each phase:

```cpp
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <syncstream>

int main()
{
    constexpr int kNumThreads = 4;
    std::barrier sync_point(kNumThreads);

    auto worker = [&sync_point](int id) {
        for (int phase = 1; phase <= 3; ++phase) {
            // 每个线程独立完成当前阶段的工作
            std::osyncstream(std::cout)
                << "Thread " << id << " phase " << phase << " working\n";

            // 到达屏障，等待其他线程
            sync_point.arrive_and_wait();

            std::osyncstream(std::cout)
                << "Thread " << id << " phase " << phase << " done\n";
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

The key to this code lies in each thread calling `arrive_and_wait()` after completing a phase. When all four threads have invoked `arrive_and_wait()`, the barrier "opens"—releasing all threads simultaneously to proceed to the next phase. The barrier automatically resets to its initial count, ready for the next round. We can see that the entire process requires no extra `mutex` or `condition_variable`; the barrier handles all waiting and wake-up logic internally.

### Completion Function: Centralized Processing Between Phases

`std::barrier` possesses a powerful but lesser-known feature—the **completion function**. When all participating threads arrive at the barrier, it executes this completion function within the context of one of the arriving threads before releasing them. This mechanism is ideal for "reduction" operations: each thread calculates a partial result independently, and when all threads arrive, the completion function aggregates these partial results.

```cpp
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <array>
#include <numeric>

int main()
{
    constexpr int kNumThreads = 4;
    constexpr int kDataSize = 1000;
    constexpr int kChunkSize = kDataSize / kNumThreads;

    std::array<int, kDataSize> data;
    for (int i = 0; i < kDataSize; ++i) {
        data[i] = i + 1;
    }

    // 每个线程的部分和
    std::array<long long, kNumThreads> partial_sums{};
    long long total_sum = 0;

    // 完成函数：在所有线程到达后，汇总部分和
    auto on_completion = [&]() noexcept {
        total_sum = std::accumulate(partial_sums.begin(),
                                     partial_sums.end(), 0LL);
    };

    std::barrier sync_point(kNumThreads, on_completion);

    auto worker = [&](int id) {
        int start = id * kChunkSize;
        int end = start + kChunkSize;

        // 阶段 1：每个线程计算自己那部分的和
        long long local_sum = 0;
        for (int i = start; i < end; ++i) {
            local_sum += data[i];
        }
        partial_sums[id] = local_sum;

        // 同步并触发完成函数汇总
        sync_point.arrive_and_wait();

        // 阶段 2：所有线程都能看到 total_sum
        std::osyncstream(std::cout)
            << "Thread " << id << ": total_sum = " << total_sum << "\n";
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

Here, we define an `on_completion` lambda as the completion function for the barrier. Once all threads arrive at the barrier, the barrier invokes this function to accumulate the partial sums from `partial_sums` into `total_sum`. The threads are only released after the completion function finishes executing. This means that threads can safely read `total_sum` after `arrive_and_wait()` returns, as the completion function has already completed.

There are several constraints to keep in mind for the completion function. First, it must be `noexcept`. Since the barrier executes it before releasing threads, if it throws an exception, the entire program will call `std::terminate()`. Second, the completion function executes in the context of "one of the arriving threads" (specifically which thread is implementation-defined), so it should not perform blocking or time-consuming operations. Finally, accessing shared state within the completion function does not require additional locking, because other threads remain blocked on the barrier while the completion function runs, preventing concurrent access.

### arrive() and arrive_and_drop()

`arrive()` is the "check-in without waiting" version—a thread notifies the barrier "I'm here" and returns immediately without blocking. This fits scenarios where "producers simply arrive, and consumers handle the waiting." Note that `arrive()` returns an `arrival_token`. Currently, this token has no practical use in the standard (it is reserved for future extensions), but you must still ensure that every `arrive()` call corresponds to a participating thread.

`arrive_and_drop()` is a more specialized operation—it notifies the barrier "I'm here, but I won't participate in the future." Each call to `arrive_and_drop()` permanently decrements the barrier's participation count. This is useful for scenarios like "dynamic worker thread exit" in a thread pool: a thread calls `arrive_and_drop()` after finishing its last round of work, so subsequent synchronization rounds won't wait for it.

## std::counting_semaphore: General Counting Semaphore

`std::latch` and `std::barrier` solve "inter-thread synchronization" problems—everyone arrives and proceeds together. `std::counting_semaphore`, on the other hand, solves "resource counting" problems—limiting the number of threads accessing a specific resource simultaneously. It is defined in the `<semaphore>` header file as a class template `std::counting_semaphore<LeastMaxValue>`, where `LeastMaxValue` is the maximum value of the semaphore (defaulting to an implementation-defined value, at least as large as the maximum value of `ptrdiff_t`).

The core concept of a semaphore is simple: it maintains an internal counter. `acquire()` attempts to decrement the counter by one; if the counter is already zero, it blocks and waits. `release(n = 1)` increments the counter by *n* and wakes up waiting threads. This "acquire-release" semantics can model many real-world problems.

`std::counting_semaphore<1>` has a type alias `std::binary_semaphore`. When the maximum value is 1, the semaphore degenerates into a simple binary semaphore, where the counter has only two states: 0 and 1.

### Pattern: Resource Pool

Suppose we have a database connection pool that allows a maximum of three threads to hold connections simultaneously. Using `counting_semaphore` to control this is very natural:

```cpp
#include <semaphore>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <syncstream>

class DatabaseConnectionPool {
public:
    explicit DatabaseConnectionPool(int max_connections)
        : semaphore_(max_connections)
    {}

    void use_connection(int thread_id)
    {
        semaphore_.acquire();  // 获取一个连接名额
        std::osyncstream(std::cout)
            << "Thread " << thread_id << " acquired connection\n";

        // 模拟使用连接
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::osyncstream(std::cout)
            << "Thread " << thread_id << " releasing connection\n";
        semaphore_.release();  // 释放连接名额
    }

private:
    std::counting_semaphore<> semaphore_;
};

int main()
{
    DatabaseConnectionPool pool(3);  // 最多 3 个并发连接

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(&DatabaseConnectionPool::use_connection,
                             &pool, i);
    }
    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

Eight threads compete for three connection slots. The first three threads acquire connections immediately, while the next five threads block on `acquire()`. Whenever a thread calls `release()`, one waiting thread wakes up and obtains a connection. The entire process is controlled entirely by the semaphore's counter, requiring no mutex or condition_variable.

### std::binary_semaphore: A Mutex in Semaphore's Clothing

`std::binary_semaphore` is an alias for `std::counting_semaphore<1>`, where the counter has only two states: 0 and 1. It is useful in scenarios requiring simple mutual exclusion, such as one-time signal notification between threads:

```cpp
#include <semaphore>
#include <iostream>
#include <thread>

std::binary_semaphore signal{0};

void waiting_thread()
{
    std::cout << "Waiting for signal...\n";
    signal.acquire();
    std::cout << "Signal received, proceeding\n";
}

void signaling_thread()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Sending signal\n";
    signal.release();
}

int main()
{
    std::thread t1(waiting_thread);
    std::thread t2(signaling_thread);
    t1.join();
    t2.join();
    return 0;
}
```

The semaphore's initial value is 0 (the constructor argument), so `waiting_thread` blocks on `acquire()`; `signaling_thread` calls `release()` to increment the counter from 0 to 1, waking the waiting thread.

You might ask: what is the difference between a `binary_semaphore` and a `mutex`? Functionally, they are quite similar—both can achieve mutual exclusion and wait-notify mechanisms. However, there is a key semantic difference: a `mutex` emphasizes **ownership** (who locks must unlock), whereas a semaphore has no concept of ownership—Thread A can `acquire()`, and Thread B can `release()`. This decoupling is useful in certain scenarios (for example, in producer-consumer patterns where the producer releases the semaphore to signal the consumer), but it also implies that a semaphore cannot replace a `mutex` to protect a critical section—because you cannot guarantee that only the locking thread holds the right to unlock.

### Comparing Semaphores and Condition Variables

Since semaphores can handle wait-notify logic, why do we still need `condition_variable`? Conversely, since `condition_variable` is more general, why did C++20 introduce semaphores? The core of this issue lies in their **semantic complexity** and **performance characteristics**.

The advantage of semaphores is their lightweight nature. They do not need to be paired with a `mutex` (they maintain state internally), do not require handling spurious wakeups, and have an API consisting of only two core operations: `acquire` and `release`. For simple resource counting or one-shot notification scenarios, code using semaphores is much more concise than `condition_variable`. Performance-wise, semaphores are usually implemented based on platform-native primitives (like `sem_t` on Linux or `Semaphore` objects on Windows), which may be faster than `condition_variable` in simple wait-notify scenarios—because `condition_variable` requires working with a `mutex`, involving lock acquisition and release on every `wait` or `notify`.

The advantage of condition variables lies in **expressiveness**. When the wait condition is not simply "is the counter zero," but a complex condition like "is the queue empty AND is the shutdown flag not set," `condition_variable` combined with a `mutex` and a predicate can express this logic precisely. Condition variables also support timed waits (`wait_for`/`wait_until`). While semaphore `acquire()` does not natively support timeouts, C++20 provides `try_acquire_for()` and `try_acquire_until()` for timed acquisition. However, if you need more fine-grained timeout control or complex condition evaluation, `condition_variable` remains the better choice.

To summarize the selection strategy in one sentence: if your synchronization logic can be expressed as "counting," prefer semaphores; if your logic involves complex condition checking or requires timeouts, use `condition_variable`.

## What If I Don't Have C++20: Simulating with Mutex + CV

If your project is still using C++17 or earlier, don't worry—the semantics of these three primitives can be simulated using a `mutex`, `condition_variable`, and a counter. Although the code is more verbose, understanding these simulations helps you deeply understand the underlying mechanisms of C++20 primitives.

### Simulating a Latch

```cpp
#include <mutex>
#include <condition_variable>

class Latch {
public:
    explicit Latch(std::ptrdiff_t count)
        : count_(count)
    {}

    void count_down(std::ptrdiff_t n = 1)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ -= n;
        if (count_ <= 0) {
            cv_.notify_all();
        }
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return count_ <= 0; });
    }

    void arrive_and_wait(std::ptrdiff_t n = 1)
    {
        count_down(n);
        wait();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::ptrdiff_t count_;
};
```

We can see that this simulated implementation is a standard application of the "wait with predicate + notify_all" pattern covered in the previous chapter. `count_down` decrements the counter while holding the lock, and calls `notify_all` to wake all waiting threads when the counter reaches zero. `wait` uses the predicate version of `wait` to guard against spurious wakeups and missed wakeups. `arrive_and_wait` combines `count_down` and `wait`—note that there is no atomicity guarantee here (another thread might decrement the count to zero after `count_down` releases the lock but before `wait` acquires it), but because `wait` uses a predicate, the notification will not be missed even if it occurs early.

### Simulating a barrier

```cpp
#include <mutex>
#include <condition_variable>

class Barrier {
public:
    explicit Barrier(std::ptrdiff_t count)
        : initial_count_(count), count_(count), generation_(0)
    {}

    void arrive_and_wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::ptrdiff_t gen = generation_;
        if (--count_ == 0) {
            // 所有线程到齐，重置屏障
            generation_++;
            count_ = initial_count_;
            cv_.notify_all();
        } else {
            cv_.wait(lock, [this, gen] { return gen != generation_; });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::ptrdiff_t initial_count_;
    std::ptrdiff_t count_;
    std::ptrdiff_t generation_;
};
```

The simulation of a `barrier` is more complex than a `latch` due to "reusability." We cannot simply reset when the count reaches zero—because threads from the previous round might not have returned from `wait` yet, while threads from the new round have already started `arrive_and_wait`. The solution is to introduce a **generation** counter: we increment the generation each time the barrier resets. Waiting threads check "has the generation for my round changed?"—if it has, it means the barrier has opened, and they can proceed.

This generation trick is the core technique for implementing reusable barriers and is the mechanism used internally by C++20's `std::barrier`. Once you understand this technique, you won't be surprised by generation counters when reading standard library implementations or third-party concurrency libraries.

## Scenario Selection Guide

We now have five main synchronization primitives (mutex, condition_variable, latch, barrier, counting_semaphore). How do we choose when facing a specific synchronization requirement? I have summarized a simple decision path based on my experience.

If your requirement is "protect a critical section, allowing only one thread to enter at a time," use a mutex (along with `lock_guard` or `unique_lock`). If your requirement is "wait for a condition to become true," use a condition_variable with a mutex and a predicate. If your requirement is "wait for N threads to complete something once before proceeding together," use a latch. If your requirement is "repeated synchronization—waiting for everyone to arrive at every iteration or stage," use a barrier. If your requirement is "limit the number of threads accessing a resource simultaneously" or "simple inter-thread signaling," use a counting_semaphore.

Sometimes a scenario might fit multiple conditions—for example, a barrier can be simulated internally using a condition_variable, and a counting_semaphore can also be used for one-shot notification (degrading to a binary_semaphore). The key to selection is which primitive's semantics best match your problem—the higher the semantic match, the less error-prone the code.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Multi-Stage Parallel Matrix Computation

Given an N x N integer matrix, use four threads to compute the transpose of the matrix and the sum of all elements in parallel. Divide the computation into three stages: Stage one, each thread computes the sum of a portion of the matrix elements; Stage two, aggregate all partial sums to get the total sum; Stage three, each thread is responsible for transposing a portion of the matrix. Synchronization points are needed between Stage one and Stage three, and after Stage three.

Hint: Use `std::barrier` to complete the function. The completion function for Stage one is responsible for aggregating the partial sums. After Stage three, the main thread needs to wait for all worker threads to finish. Think about this: Stage two involves only one aggregation operation; should it be executed in the worker threads or as the completion function?

### Exercise 2: Implement a Bounded Blocking Queue with counting_semaphore

Re-implement the `BoundedQueue` from the previous article using `std::counting_semaphore` (instead of condition_variable). Hint: You need two semaphores—`items_available` initialized to 0 (tracking the number of elements in the queue), and `spaces_available` initialized to the queue capacity (tracking the remaining empty slots). For `push`, first `spaces_available.acquire()`, then lock and insert the element, then `items_available.release()`. For `pop`, first `items_available.acquire()`, then lock and remove the element, then `spaces_available.release()`. Note: You still need a mutex to protect the queue container itself—the semaphore only controls "permission to operate," not the consistency of the data structure.

### Exercise 3: Simulate counting_semaphore with mutex + condition_variable

Implement a simple counting semaphore class using `std::mutex`, `std::condition_variable`, and an internal counter. Provide `acquire()`, `release()`, and `try_acquire()` methods. `try_acquire()` attempts to acquire a resource, returning `true` on success, or `false` if the counter is zero (without blocking). Write a simple test program to verify your implementation: create five threads competing for a semaphore with an initial count of two, and observe that the number of threads holding the resource simultaneously never exceeds two.

## References

- [std::latch -- cppreference](https://en.cppreference.com/w/cpp/thread/latch)
- [std::barrier -- cppreference](https://en.cppreference.com/w/cpp/thread/barrier)
- [std::counting_semaphore -- cppreference](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
- [Synchronization Primitives in C++20 -- KDAB](https://www.kdab.com/synchronization-primitives-in-c20/)
- [Latches and Barriers -- Modernes C++](https://www.modernescpp.com/index.php/latches-and-barriers/)
- [Semaphores in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/semaphores-in-c-20/)
- [P0666R2: Revised Latches and Barriers for C++20 (Proposal Paper)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0666r2.pdf)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
