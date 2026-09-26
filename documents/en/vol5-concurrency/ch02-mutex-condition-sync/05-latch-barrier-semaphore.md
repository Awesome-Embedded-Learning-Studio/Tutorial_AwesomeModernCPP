---
chapter: 2
cpp_standard:
- 20
description: 'C++20 synchronization primitives: single-use and reusable barriers plus
  counting semaphores—scenario selection and engineering patterns'
difficulty: advanced
order: 5
platform: host
prerequisites:
- condition_variable and Wait Semantics
reading_time_minutes: 19
related:
- Atomic Operations
- Thread Pool Design
tags:
- host
- cpp-modern
- advanced
- mutex
title: latch, barrier, and semaphore
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/05-latch-barrier-semaphore.md
  source_hash: 38d2d56a4511e46d56c4fc5e7ea62990b6d857e05d540fa67159069b40481b5c
  translated_at: '2026-09-26T07:15:55+00:00'
  engine: anthropic
  token_count: 12500
---
# latch, barrier, and semaphore

In the previous article we took apart the wait-notify machinery of `condition_variable`—spurious wakeups, lost wakeups, and the predicate-taking `wait`. With that foundation in place, we can now face a more practical question: very often we don't need the general-purpose wait semantics of "continue only once some condition holds"; we just need "wait until everyone has arrived, then continue" or "limit how many threads can access a resource at the same time". These two needs correspond to the **barrier** and the **semaphore** synchronization patterns, and C++20 finally brought both concepts into the standard library as `std::latch`, `std::barrier`, and `std::counting_semaphore`.

To be honest, before this our only option was to emulate these patterns with a mutex plus a condition_variable plus a hand-rolled counter—verbose code, easy to get wrong, and something you had to rewrite from scratch every time. The introduction of these three primitives in C++20 is, in essence, the standardization of these high-frequency patterns. But to use them well, we need to be clear about each primitive's semantic boundaries and the scenarios it fits, rather than grabbing a hammer and pounding every nail with it.

## std::latch: A Single-Use Countdown Barrier

`std::latch` is defined in the `<latch>` header; it is a **one-way decrementing counter**. You can picture it as a door with a latch on it, and the strength of that latch is set by the initial count. Every time a thread calls `count_down()`, the latch loosens by one notch; when the count reaches zero, the door opens and every thread blocked on `wait()` may pass through. The key property: **a latch is single-use**—once the count hits zero, it stays "open" forever and cannot be reset.

The `std::latch` API is remarkably lean: the constructor takes the initial count `expected` (of type `std::ptrdiff_t`); `count_down(n = 1)` subtracts n from the count (without blocking); `wait()` blocks the calling thread until the count reaches zero; `arrive_and_wait(n = 1)` is the atomic combination of `count_down(n)` and `wait()`—the calling thread both contributes a decrement and waits for the count to hit zero; and `try_wait()` is the non-blocking check—it returns `true` when the counter has reached zero (note: it is permitted to spuriously return `false` with very low probability). Let's work through a concrete scenario to understand how it is used.

### Pattern: One-Time Initialization

Suppose our program needs to initialize three subsystems at startup—logging, configuration, and network connectivity—each handled by a dedicated thread, while the main thread must wait until all subsystems are ready before starting the business logic. This is a classic one-shot synchronization scenario:

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

Here each initialization thread calls `init_done.count_down()` after finishing its own task, and the main thread calls `init_done.wait()` to block. Once all three `count_down`s have executed, the main thread wakes up and continues. Note that the worker threads call `count_down()`, not `arrive_and_wait()`—the workers have no one to wait for; once they finish their own work they can exit, and only the main thread needs to wait.

If a worker thread also wants to "finish my part, then have everyone continue together", that's what `arrive_and_wait()` is for:

```cpp
void worker(int id, std::latch& sync)
{
    std::cout << "Worker " << id << " phase 1 done\n";
    sync.arrive_and_wait();  // contribute one decrement and wait for the count to reach zero
    std::cout << "Worker " << id << " phase 2 starts\n";
}
```

The semantics of `arrive_and_wait()` are an atomic "decrement + wait"—the thread that calls it gets blocked too, until the count reaches zero. Internally it is equivalent to `count_down(); wait();`, but the standard guarantees the atomicity of these two steps. That means no other thread can sneak in between the "decrement" and the "wait", drive the count to zero, and cause the waiter to miss a wakeup.

There is an easily overlooked detail: the argument to `count_down` can be greater than 1. For instance, a thread in charge of three tasks can call `count_down(3)` once. If the value passed would drive the count negative, the behavior is undefined—so the caller must guarantee the count is never over-decremented.

## std::barrier: Reusable Phase Synchronization

`std::latch` solves the "one-shot wait for everyone to arrive" problem, but many parallel algorithms need to **synchronize repeatedly**—in an iterative computation, for example, every round requires all threads to finish the current step before moving on to the next. With a latch, you would have to create a fresh latch object for every round, which is both wasteful and inelegant. `std::barrier` was designed for exactly this: it is a **reusable** synchronization barrier—every time all participating threads reach the barrier point, the barrier resets itself automatically and is ready for the next round.

`std::barrier` is defined in the `<barrier>` header. It is a class template, `std::barrier<CompletionFunction>`, where `CompletionFunction` defaults to an empty function. The constructor takes the number of participating threads (plus an optional completion function). There are three core API calls: `arrive()` tells the barrier "I'm here" without blocking; `arrive_and_wait()` notifies and blocks until all threads have arrived; `arrive_and_drop()` notifies and permanently reduces the participant count (for scenarios where participants shrink dynamically).

### Basic Usage: Multi-Phase Parallel Computation

Let's start with a simple multi-phase parallel computation. Suppose we have 4 worker threads, each of which must execute three phases in sequence, with every phase boundary requiring all threads to synchronize:

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
            // each thread independently finishes the current phase's work
            std::osyncstream(std::cout)
                << "Thread " << id << " phase " << phase << " working\n";

            // arrive at the barrier and wait for the other threads
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

The key to this code: each thread calls `arrive_and_wait()` after finishing a phase. When all 4 threads have called `arrive_and_wait()`, the barrier "opens"—all threads are released simultaneously and move on to the next phase. The barrier automatically resets to the initial count and waits for the next round. Notice that the whole process needs no extra mutex or condition_variable—the barrier internally handles all the waiting and wakeup logic.

### The Completion Function: Centralized Processing Between Phases

`std::barrier` has a powerful but little-known feature—the **completion function**. When all participating threads have arrived at the barrier, the barrier runs this completion function, in the context of one of the arriving threads, before releasing them. This mechanism is a perfect fit for reductions: each thread computes a partial result independently, and when all threads arrive at the barrier, the completion function aggregates those partial results.

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

    // each thread's partial sum
    std::array<long long, kNumThreads> partial_sums{};
    long long total_sum = 0;

    // completion function: after all threads arrive, aggregate the partial sums
    auto on_completion = [&]() noexcept {
        total_sum = std::accumulate(partial_sums.begin(),
                                     partial_sums.end(), 0LL);
    };

    std::barrier sync_point(kNumThreads, on_completion);

    auto worker = [&](int id) {
        int start = id * kChunkSize;
        int end = start + kChunkSize;

        // phase 1: each thread computes the sum of its own chunk
        long long local_sum = 0;
        for (int i = start; i < end; ++i) {
            local_sum += data[i];
        }
        partial_sums[id] = local_sum;

        // synchronize and trigger the completion function's aggregation
        sync_point.arrive_and_wait();

        // phase 2: every thread can see total_sum
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

Here we defined an `on_completion` lambda as the barrier's completion function. Once all threads have arrived at the barrier, the barrier calls this function, accumulating the partial sums from `partial_sums` into `total_sum`. Only after the completion function finishes are all threads released—which means a thread can safely read `total_sum` after `arrive_and_wait()` returns, because the completion function has already run.

A few constraints on the completion function deserve attention. First, it must be `noexcept`—the barrier runs it before releasing the threads, and if it throws, the whole program calls `std::terminate()`. Second, the completion function runs in the context of "one of the arriving threads" (exactly which one is implementation-defined), so it should not perform blocking or time-consuming operations. Finally, accesses to shared state inside the completion function need no extra locking—while the completion function runs, every other thread is still blocked on the barrier, so there is no concurrent access.

### arrive() and arrive_and_drop()

`arrive()` is the "check in without waiting" variant—the thread tells the barrier "I'm here" and immediately returns, without blocking. This fits scenarios shaped like "producers just arrive, consumers do the waiting". Note, though, that `arrive()` returns an `arrival_token`, and this token currently has no practical use in the standard (it is reserved for future extensions), but you still need to make sure every `arrive()` call corresponds to one participating thread.

`arrive_and_drop()` is an even more special operation—it tells the barrier "I've arrived, but I won't participate anymore". Every call to `arrive_and_drop()` permanently decrements the barrier's participant count by 1. This suits the "worker threads leaving dynamically" scenario in a thread pool: after a thread finishes its last round of work, it calls `arrive_and_drop()`, and subsequent synchronization rounds simply stop waiting for it.

## std::counting_semaphore: A General-Purpose Counting Semaphore

`std::latch` and `std::barrier` solve the problem of synchronization between threads—everyone arrives, everyone moves on together. `std::counting_semaphore`, by contrast, solves the problem of resource counting—limiting how many threads may access some resource simultaneously. It is defined in the `<semaphore>` header as the class template `std::counting_semaphore<LeastMaxValue>`, where `LeastMaxValue` is the semaphore's maximum value (the default is an implementation-defined value, at least as large as the maximum of `ptrdiff_t`).

The core idea of a semaphore is simple: it maintains an internal counter. `acquire()` tries to decrement the counter by 1, and blocks if the counter is already 0; `release(n = 1)` increments the counter by n and wakes up waiting threads. This "acquire-release" semantics can model a great many real problems.

`std::counting_semaphore<1>` has the type alias `std::binary_semaphore`—when the maximum is 1, the semaphore degenerates into a simple binary semaphore whose counter has only the two states 0 and 1.

### Pattern: A Resource Pool

Suppose we have a database connection pool that allows at most 3 threads to hold a connection at the same time. Using `counting_semaphore` to control this is very natural:

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
        semaphore_.acquire();  // acquire one connection slot
        std::osyncstream(std::cout)
            << "Thread " << thread_id << " acquired connection\n";

        // simulate using the connection
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::osyncstream(std::cout)
            << "Thread " << thread_id << " releasing connection\n";
        semaphore_.release();  // release the connection slot
    }

private:
    std::counting_semaphore<> semaphore_;
};

int main()
{
    DatabaseConnectionPool pool(3);  // at most 3 concurrent connections

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

Eight threads compete for 3 connection slots. The first 3 threads get connections immediately; the next 5 block on `acquire()`. Each time a thread calls `release()`, one waiting thread is woken and gets a connection. The whole process is governed purely by the semaphore's count—no mutex or condition_variable needed anywhere.

### std::binary_semaphore: A Mutex in Semaphore Shape

`std::binary_semaphore` is an alias for `std::counting_semaphore<1>`; its counter has only the two states 0 and 1. It can be used wherever simple mutual exclusion is needed—for example, a one-shot signal between threads:

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

The semaphore's initial value is 0 (the constructor argument); `waiting_thread` blocks on `acquire()`; `signaling_thread` calls `release()` to take the counter from 0 to 1, waking the waiting thread.

You might ask: what's the difference between `binary_semaphore` and a `mutex`? In terms of raw capability they are very similar—both can do mutual exclusion and wait-notify. But semantically there is a key difference: a mutex emphasizes **ownership** (whoever locks it unlocks it), while a semaphore has no notion of ownership—thread A can `acquire()` and thread B can be the one to `release()`. This decoupling is extremely useful in some scenarios (in producer-consumer setups, for instance, the producer releases the semaphore to notify the consumer), but it also means a semaphore cannot replace a mutex for protecting a critical section—because you cannot guarantee that only the lock-holding thread can unlock it.

### Semaphores vs. Condition Variables

Since a semaphore can do wait-notify too, why do we still need condition_variable? And conversely, since condition_variable is more general, why did C++20 introduce semaphores at all? The heart of the answer lies in the two mechanisms' **semantic complexity** and **performance characteristics**.

A semaphore's advantage is lightness. It doesn't need to be paired with a mutex (it maintains its own state internally), it doesn't have to deal with spurious wakeups, and its API has just two core operations, `acquire` and `release`. For simple resource counting or one-shot notification, semaphore code is far tidier than the condition_variable equivalent. Performance-wise, semaphores are typically built on the platform's native semaphore (the `sem_t` on Linux, the `Semaphore` object on Windows), and in simple wait-notify scenarios they may be faster than condition_variable—because condition_variable has to work with a mutex, and every wait/notify involves acquiring and releasing that mutex.

A condition variable's advantage is **expressive power**. When the wait condition isn't a simple "is the counter zero" but a compound condition like "the queue is non-empty AND the shutdown flag is not set", condition_variable together with a mutex and a predicate can express the logic precisely. Condition variables also support timed waits (`wait_for`/`wait_until`). Semaphore's `acquire()` itself has no timeout support, but C++20 also provides `try_acquire_for()` and `try_acquire_until()` for acquiring with a timeout—if you need finer-grained timeout control or compound condition checks, condition_variable is still the better choice.

A one-sentence selection strategy: if your synchronization logic can be expressed as counting, prefer a semaphore; if it involves complex condition checks or needs timeouts, use condition_variable.

## Without C++20: Simulating with mutex + CV

If your project is still on C++17 or an earlier standard, don't lose heart—the semantics of all three primitives can be emulated with a mutex plus a condition_variable plus a counter. The code is more verbose, but understanding these emulations helps you understand what lies underneath the C++20 primitives.

### Emulating a latch

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

As we can see, this emulation is precisely the standard application of the "predicate wait + notify_all" pattern we learned in the previous article. `count_down` decrements the counter while holding the lock, and calls `notify_all` to wake every waiter when the count hits zero. `wait` uses a predicate-taking `wait` to guard against spurious wakeups and lost wakeups. `arrive_and_wait` combines `count_down` and `wait`—note that there is no atomicity guarantee here (after `count_down` releases the lock and before `wait` acquires it, another thread could drive the count to zero), but because `wait` carries a predicate, even a notification that has already happened cannot be lost.

### Emulating a barrier

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
            // all threads have arrived; reset the barrier
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

What makes emulating a barrier harder than a latch is reusability. We cannot simply reset when the count hits zero—threads from the previous round may not yet have returned from `wait`, while threads of the new round have already started `arrive_and_wait`. The solution is to introduce a **generation** counter: every time the barrier resets, the generation is incremented, and what waiting threads check is "has my generation changed yet"—if it has, the barrier has opened and they may continue.

This generation trick is the core technique for implementing reusable barriers, and it is also the mechanism used inside C++20's `std::barrier`. Once you understand it, a generation counter will never look alien to you when reading standard library implementations or third-party concurrency libraries.

## A Scenario Selection Guide

We now have five major synchronization primitives (mutex, condition_variable, latch, barrier, counting_semaphore). Facing a concrete synchronization need, how do we choose? Based on my own experience, here is a simple decision path.

If your need is "protect a critical section so only one thread enters at a time", use a mutex (paired with `lock_guard` or `unique_lock`). If your need is "wait until some condition holds", use condition_variable paired with a mutex and a predicate. If your need is "wait for N threads to each finish something and then continue together, synchronizing only once", use a latch. If your need is "synchronize repeatedly—every round, every phase, everyone must arrive", use a barrier. If your need is "limit how many threads access some resource simultaneously" or "simple signal notification between threads", use counting_semaphore.

Sometimes a single scenario fits several of these at once—a barrier can be emulated internally with a condition_variable, and a counting_semaphore can do one-shot notification too (degenerating into a binary_semaphore). The key to choosing is which primitive's semantics best match your problem—the closer the semantic match, the less error-prone the code.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); see `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Multi-Phase Parallel Matrix Computation

Given an N x N integer matrix, use 4 threads to compute the matrix's transpose and the sum of all its elements in parallel. Split the computation into three phases: in phase one, each thread computes the sum of a slice of the elements; in phase two, all partial sums are aggregated into the total; in phase three, each thread transposes a slice of the matrix. You need one synchronization point between phases one and three, and another one after phase three.

Hint: use `std::barrier` together with a completion function. The phase-one completion function aggregates the partial sums, and after phase three the main thread must wait for all worker threads to finish. Think about this: phase two is a single aggregation step—should it run in a worker thread, or as the completion function?

### Exercise 2: A Bounded Blocking Queue with counting_semaphore

Re-implement the `BoundedQueue` from the previous article using `std::counting_semaphore` (instead of a condition_variable). Hint: you will need two semaphores—`items_available`, initialized to 0 (tracking the number of elements in the queue), and `spaces_available`, initialized to the queue's capacity (tracking the free slots left). On `push`, first `spaces_available.acquire()`, then lock, insert the element, and `items_available.release()`; on `pop`, first `items_available.acquire()`, then lock, remove the element, and `spaces_available.release()`. Note: you still need a mutex to protect the queue container itself—the semaphore only decides "may I operate", it does not protect the consistency of the data structure.

### Exercise 3: Emulating counting_semaphore with mutex + condition_variable

Implement a simple counting semaphore class using `std::mutex`, `std::condition_variable`, and an internal counter, providing `acquire()`, `release()`, and `try_acquire()` methods. `try_acquire()` attempts to take one resource, returning `true` on success and `false` when the counter is zero (without blocking). Write a simple test program to verify your implementation: create 5 threads competing for a semaphore with an initial count of 2, and observe whether the number of threads holding the resource at the same time ever exceeds 2.

## References

- [std::latch -- cppreference](https://en.cppreference.com/w/cpp/thread/latch)
- [std::barrier -- cppreference](https://en.cppreference.com/w/cpp/thread/barrier)
- [std::counting_semaphore -- cppreference](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
- [Synchronization Primitives in C++20 -- KDAB](https://www.kdab.com/synchronization-primitives-in-c20/)
- [Latches and Barriers -- Modernes C++](https://www.modernescpp.com/index.php/latches-and-barriers/)
- [Semaphores in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/semaphores-in-c-20/)
- [P0666R2: Revised Latches and Barriers for C++20 (the proposal)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0666r2.pdf)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
