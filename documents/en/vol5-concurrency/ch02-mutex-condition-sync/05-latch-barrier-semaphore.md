---
title: latch, barrier, and semaphore
description: 'C++20 Synchronization Primitives: One-shot and reusable barriers and
  counting semaphores, use-case selection, and engineering patterns'
chapter: 2
order: 5
tags:
- host
- cpp-modern
- advanced
- mutex
difficulty: advanced
platform: host
reading_time_minutes: 20
cpp_standard:
- 20
prerequisites:
- condition_variable 与等待语义
related:
- atomic 操作
- 线程池设计
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/05-latch-barrier-semaphore.md
  source_hash: 7314f7ecd132f093e0cb961108ba0b73423f1e207d003e3cd3e60f6c585c14e8
  translated_at: '2026-05-20T04:37:54.332340+00:00'
  engine: anthropic
  token_count: 3935
---
# Latch, Barrier, and Semaphore

In the previous article, we deeply analyzed the wait-notify mechanism of `condition_variable`—spurious wakeups, lost wakeups, and `wait` with a predicate. With these foundations in place, we can now tackle a more practical problem: often, we don't need the general "wait until a condition is met" semantics. Instead, we just need "wait until everyone arrives before continuing" or "limit the number of threads accessing a resource concurrently." These two requirements correspond to the **barrier** and **semaphore** synchronization patterns, respectively, and C++20 finally brought these concepts into the standard library as `std::latch`, `std::barrier`, and `std::counting_semaphore`.

Honestly, before this, we could only simulate these patterns using a mutex + condition_variable + a manual counter—the code was verbose, error-prone, and had to be rewritten every time. The introduction of these three primitives in C++20 essentially standardizes these high-frequency patterns. But to use them well, we need to understand the semantic boundaries and applicable scenarios of each primitive, rather than treating every problem like a nail just because we have a hammer.

## std::latch: A One-Shot Countdown Barrier

`std::latch` is defined in the `<latch>` header, and it is a **single-directional decrementing counter**. You can think of it as a door with a latch; the latch's strength is determined by the initial count. Each time a thread executes `count_down()`, the latch loosens by one notch. When the count reaches zero, the door opens, and all threads blocked on `wait()` can pass through. The key characteristic is: **a latch is one-shot**—once the count reaches zero, it permanently remains "open" and cannot be reset.

The API of `std::latch` is very concise: pass the initial count `expected` (of type `std::ptrdiff_t`) at construction; `count_down(n = 1)` decrements the count by n (non-blocking); `wait()` blocks the current thread until the count reaches zero; `arrive_and_wait(n = 1)` is an atomic combination of `count_down(n)` and `wait()`—the current thread both contributes a decrement and waits for the count to reach zero; `try_wait()` is a non-blocking check—it returns `true` when the counter reaches zero (note: it allows for a very low probability of a spurious return of `false`). Let's understand its usage through a concrete scenario.

### Pattern: One-Shot Initialization

Suppose our program needs to initialize three subsystems at startup—logging, configuration, and network connections—each managed by an independent thread. The main thread must wait until all subsystems are ready before starting the business logic. This is a typical one-shot synchronization scenario:

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

Here, each initialization thread calls `init_done.count_down()` after completing its own task, and the main thread calls `init_done.wait()` to block and wait. When all three `count_down` calls have finished, the main thread is woken up and continues execution. Note that the worker threads call `count_down()` instead of `arrive_and_wait()`—because the worker threads don't need to wait for others; they can exit once they finish their own work. Only the main thread needs to wait.

If the worker threads also want to "finish their part and then wait for everyone to continue together," we use `arrive_and_wait()`:

```cpp
void worker(int id, std::latch& sync)
{
    std::cout << "Worker " << id << " phase 1 done\n";
    sync.arrive_and_wait();  // 贡献一个递减，同时等待计数归零
    std::cout << "Worker " << id << " phase 2 starts\n";
}
```

The semantics of `arrive_and_wait()` are an atomic "decrement + wait"—the thread calling it will also be blocked until the count reaches zero. Internally, it is equivalent to `count_down(); wait();`, but the standard guarantees the atomicity of these two steps. This means no other thread can reduce the count to zero between the "decrement" and "wait," which would cause the waiter to miss the wakeup.

There is an easily overlooked detail: the parameter of `count_down` can be greater than 1. For example, if one thread is responsible for completing three tasks, it can do `count_down(3)` all at once. If the value passed in would cause the count to become negative, the behavior is undefined behavior (UB)—so the caller must guarantee that the count will not be over-decremented.

## std::barrier: Reusable Phase Synchronization

`std::latch` solves the "wait for everyone to arrive once" problem, but many parallel algorithms require **repeated synchronization**—for example, in iterative computations, each iteration requires all threads to finish the current step before entering the next. If we used a latch, we would have to create a new latch object for each iteration, which is both wasteful and inelegant. `std::barrier` is designed for this: it is a **reusable** synchronization barrier. After all participating threads arrive at the barrier point, it automatically resets and can be used for the next round of synchronization.

`std::barrier` is defined in the `<barrier>` header. It is a class template `std::barrier<CompletionFunction>`, where `CompletionFunction` defaults to an empty function. You pass the number of participating threads (and an optional completion function) at construction. The core API consists of three methods: `arrive()` notifies the barrier "I'm here" but does not block; `arrive_and_wait()` notifies and blocks until all threads have arrived; `arrive_and_drop()` notifies and permanently reduces the number of participating threads (used for scenarios where participants are dynamically reduced).

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

The key to this code is that each thread calls `arrive_and_wait()` after completing a phase. When all four threads have called `arrive_and_wait()`, the barrier "opens"—all threads are released simultaneously and enter the next phase. The barrier automatically resets to the initial count, waiting for the next round. You'll notice that the entire process requires no additional mutex or condition_variable; the barrier handles all the waiting and wakeup logic internally.

### Completion Function: Centralized Processing Between Phases

`std::barrier` has a powerful but somewhat lesser-known feature—the **completion function**. When all participating threads arrive at the barrier, the barrier executes this completion function in the context of one of the arriving threads before releasing the threads. This mechanism is perfect for "reduction" operations: each thread independently computes a partial result, and when all threads arrive at the barrier, the completion function is responsible for aggregating these partial results.

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

Here we define a `on_completion` lambda as the barrier's completion function. After all threads arrive at the barrier, the barrier calls this function to accumulate the partial sums from `partial_sums` into `total_sum`. Only after the completion function finishes executing are all threads released—this means threads can safely read `total_sum` after `arrive_and_wait()` returns, because the completion function has already finished.

There are a few constraints regarding the completion function to note. First, it must be `noexcept`—because the barrier executes it before releasing threads, if it throws an exception, the entire program will call `std::terminate()`. Second, the completion function executes in the context of "one of the arriving threads" (specifically which thread is implementation-defined), so it should not perform blocking or time-consuming operations. Finally, accessing shared state within the completion function does not require additional locking—because while the completion function is executing, other threads are still blocked on the barrier, so there is no concurrent access.

### arrive() and arrive_and_drop()

`arrive()` is the "report only, don't wait" version—a thread notifies the barrier "I'm here" and then returns immediately without blocking. This suits scenarios where "producers only arrive, and consumers are responsible for waiting." However, note that `arrive()` returns a `arrival_token`; this token currently has no practical use in the standard (it is reserved for future extensions), but you still need to ensure that each `arrive()` call corresponds to one participating thread.

`arrive_and_drop()` is a more special operation—it notifies the barrier "I'm here, but I won't participate in the future." Each call to `arrive_and_drop()` permanently decreases the barrier's participation count by one. This suits scenarios like "worker threads dynamically exiting" in a thread pool: after a thread finishes its last round of work, it calls `arrive_and_drop()`, and subsequent synchronization rounds will no longer wait for it.

## std::counting_semaphore: General-Purpose Counting Semaphore

`std::latch` and `std::barrier` solve the problem of "thread synchronization"—everyone arrives and moves forward together. `std::counting_semaphore`, on the other hand, solves the problem of "resource counting"—limiting the number of threads that can access a certain resource concurrently. It is defined in the `<semaphore>` header and is a class template `std::counting_semaphore<LeastMaxValue>`, where `LeastMaxValue` is the maximum value of the semaphore (defaulting to an implementation-defined value that is at least as large as the maximum value of `ptrdiff_t`).

The core concept of a semaphore is very simple: it maintains an internal counter. `acquire()` tries to decrement the counter by one, blocking if the counter is already 0; `release(n = 1)` increments the counter by n and wakes up waiting threads. This "acquire-release" semantics can model many real-world problems.

`std::counting_semaphore<1>` has a type alias `std::binary_semaphore`—when the maximum value is 1, the semaphore degenerates into a simple binary semaphore, where the counter only has two states: 0 and 1.

### Pattern: Resource Pool

Suppose we have a database connection pool that allows a maximum of three threads to hold connections concurrently. Using `counting_semaphore` to control this is very natural:

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

Eight threads compete for three connection slots. The first three threads immediately acquire connections, and the next five threads block on `acquire()`. Whenever a thread calls `release()`, one of the waiting threads is woken up to acquire a connection. The entire process is controlled entirely by the semaphore's count, without needing any mutex or condition_variable.

### std::binary_semaphore: Semaphore-Form Mutex

`std::binary_semaphore` is an alias for `std::counting_semaphore<1>`, where the counter only has two states: 0 and 1. It can be used in scenarios requiring simple mutual exclusion, such as one-shot signal notification between threads:

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

The semaphore's initial value is 0 (the constructor parameter). `waiting_thread` blocks on `acquire()`; `signaling_thread` calls `release()` to change the counter from 0 to 1, waking up the waiting thread.

You might ask: what is the difference between `binary_semaphore` and `mutex`? In terms of capability, they are very similar—both can implement mutual exclusion and wait-notify. But semantically, there is a key difference: a mutex emphasizes **ownership** (whoever locks it must unlock it), whereas a semaphore has no concept of ownership—thread A can `acquire()`, and thread B can come along to `release()`. This decoupling is very useful in certain scenarios (such as in producer-consumer patterns, where the producer releases the semaphore to notify the consumer), but it also means a semaphore cannot replace a mutex to protect a critical section—because you cannot guarantee that only the lock-holding thread can unlock it.

### Comparing Semaphores and Condition Variables

Since a semaphore can also do wait-notify, why do we still need condition_variable? Conversely, since condition_variable is more general, why did C++20 introduce semaphores? The core of this question lies in their **semantic complexity** and **performance characteristics**.

The advantage of a semaphore is its lightweight nature. It doesn't need to be paired with a mutex (it maintains its state internally), doesn't need to handle spurious wakeups, and its API consists of only two core operations: `acquire`/`release`. For simple resource counting or one-shot notification scenarios, semaphore code is much more concise than condition_variable code. Performance-wise, semaphores are usually implemented based on platform-native semaphores (``sem_t`` on Linux, ``Semaphore`` objects on Windows), which might be faster than condition_variable in simple wait-notify scenarios—because condition_variable needs to work with a mutex, and every wait/notify involves acquiring and releasing the mutex.

The advantage of a condition variable lies in its **expressiveness**. When the wait condition is not simply "is the counter 0," but a compound condition like "is the queue empty AND is the shutdown flag not set," a condition_variable paired with a mutex and a predicate can express this logic precisely. Condition variables also support timed waits (`wait_for`/`wait_until`). The `acquire()` of a semaphore doesn't natively support timeouts, but C++20 simultaneously provides `try_acquire_for()` and `try_acquire_until()` for timed acquires—if you need more fine-grained timeout control or compound condition checking, condition_variable remains the better choice.

To summarize the selection strategy in one sentence: if your synchronization logic can be expressed with "counting," prefer a semaphore; if your synchronization logic involves complex condition checking or requires timeouts, use a condition_variable.

## What If You Don't Have C++20: Simulating with Mutex + CV

If your project is still using C++17 or earlier standards, don't despair—the semantics of all three primitives can be simulated using a mutex + condition_variable + a counter. Although the code is more verbose, understanding these simulated implementations will help you deeply understand the underlying mechanisms of the C++20 primitives.

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

We can see that this simulated implementation is a standard application of the "wait with predicate + notify_all" pattern we learned in the previous article. `count_down` decrements the counter while holding the lock, and when the count reaches zero, it calls `notify_all` to wake all waiters. `wait` uses `wait` with a predicate to prevent spurious wakeups and lost wakeups. `arrive_and_wait` combines `count_down` and `wait` together—note that there is no atomicity guarantee here (after `count_down` releases the lock but before `wait` acquires it, another thread might reduce the count to zero), but because `wait` uses a predicate, even if the notification happens first, it won't be lost.

### Simulating a Barrier

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

The part where simulating a barrier is more complex than a latch lies in "reusability." We can't simply reset when the count reaches zero—because threads from the previous round might not have returned from `wait` yet, while threads from the new round have already started calling `arrive_and_wait`. The solution is to introduce a **generation** counter: increment the generation each time the barrier resets, and waiting threads check "has the generation for my round changed"—if it has, it means the barrier has opened, and they can proceed.

This generation trick is the core technique for implementing reusable barriers, and it is also the mechanism used internally by the C++20 `std::barrier`. Once you understand this trick, you won't be unfamiliar with generation counters when reading standard library implementations or third-party concurrency libraries.

## Scenario Selection Guide

We now have five main synchronization primitives (mutex, condition_variable, latch, barrier, counting_semaphore). How do we choose when facing a specific synchronization requirement? Based on my experience, I've summarized a simple decision path.

If your requirement is "protect a critical section, allowing only one thread to enter at a time," use a mutex (paired with `lock_guard` or `unique_lock`). If your requirement is "wait until a certain condition is true," use a condition_variable paired with a mutex and a predicate. If your requirement is "wait for N threads to all finish something before continuing together, and you only need to synchronize once," use a latch. If your requirement is "repeated synchronization—waiting for everyone to arrive at every iteration or phase," use a barrier. If your requirement is "limit the number of threads accessing a certain resource concurrently" or "simple signal notification between threads," use a counting_semaphore.

Sometimes a scenario might satisfy multiple conditions at once—for example, a barrier can be simulated internally with a condition_variable, and a counting_semaphore can also be used for one-shot notification (degenerating into a binary_semaphore). The key to selection is seeing which primitive's semantics best match your problem—the higher the semantic match, the less prone the code is to errors.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Multi-Phase Parallel Matrix Computation

Given an N x N integer matrix, use four threads to compute the matrix transpose and the sum of all elements in parallel. The computation must be divided into three phases: Phase 1, each thread computes the sum of a portion of the matrix elements; Phase 2, aggregate all partial sums to get the total sum; Phase 3, each thread is responsible for transposing a portion of the matrix. A synchronization point is needed between Phase 1 and Phase 3, and after Phase 3.

Hint: Use `std::barrier` with a completion function. The completion function for Phase 1 is responsible for aggregating the partial sums, and after Phase 3, the main thread needs to wait for all worker threads to finish. Think about this: Phase 2 has only one aggregation operation—should it be executed in the worker threads or as a completion function?

### Exercise 2: Implementing a Bounded Blocking Queue with counting_semaphore

Reimplement the `BoundedQueue` from the previous article using `std::counting_semaphore` (instead of condition_variable). Hint: You need two semaphores—`items_available` initialized to 0 (tracking the number of elements in the queue), and `spaces_available` initialized to the queue capacity (tracking the remaining empty slots). When `push`, first `spaces_available.acquire()`, lock to insert the element, then `items_available.release()`; when `pop`, first `items_available.acquire()`, lock to extract the element, then `spaces_available.release()`. Note: You still need a mutex to protect the queue container itself—a semaphore only controls "whether you can operate," it does not protect the consistency of the data structure.

### Exercise 3: Simulating counting_semaphore with mutex + condition_variable

Implement a simple counting semaphore class using `std::mutex`, `std::condition_variable`, and an internal counter, providing `acquire()`, `release()`, and `try_acquire()` methods. `try_acquire()` attempts to acquire one resource, returning `true` on success, and returning `false` when the counter is zero (without blocking). Write a simple test program to verify your implementation: create five threads competing for a semaphore with an initial count of two, and observe whether the number of threads holding the resource concurrently never exceeds two.

## References

- [std::latch -- cppreference](https://en.cppreference.com/w/cpp/thread/latch)
- [std::barrier -- cppreference](https://en.cppreference.com/w/cpp/thread/barrier)
- [std::counting_semaphore -- cppreference](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
- [Synchronization Primitives in C++20 -- KDAB](https://www.kdab.com/synchronization-primitives-in-c20/)
- [Latches and Barriers -- Modernes C++](https://www.modernescpp.com/index.php/latches-and-barriers/)
- [Semaphores in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/semaphores-in-c-20/)
- [P0666R2: Revised Latches and Barriers for C++20 (Proposal Document)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0666r2.pdf)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
