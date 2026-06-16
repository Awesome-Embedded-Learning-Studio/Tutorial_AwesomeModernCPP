---
chapter: 2
cpp_standard:
- 20
description: 'C++20 Synchronization Primitives: Single/Multi-use Synchronization Barriers
  and Counting Semaphores, Scenario Selection, and Engineering Patterns'
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
  source_hash: e7253da4419fd31836111903b31e6bcf0adcb57e291950e4971072fd4de9a607
  translated_at: '2026-06-16T04:04:15.874631+00:00'
  engine: anthropic
  token_count: 3929
---
# Latches, Barriers, and Semaphores

In the previous post, we deconstructed the wait-notify mechanism of `std::condition_variable`—spurious wakeups, lost wakeups, and waiting with predicates. With these foundations in place, we can now tackle a more practical problem: often, we don't need the generic "wait until a condition is met" semantics. Instead, we only need "wait until everyone arrives before proceeding" or "limit the number of threads accessing a resource simultaneously." These two needs correspond to two synchronization patterns: **barrier** and **semaphore**. C++20 finally brought these concepts into the standard library as `std::latch`, `std::barrier`, and `std::counting_semaphore`.

To be honest, before this, we could only simulate these patterns using mutex + condition_variable + a manual counter—the code was verbose, error-prone, and had to be rewritten every time. The introduction of these three primitives in C++20 essentially standardizes these high-frequency patterns. But to use them well, we need to understand the semantic boundaries and applicable scenarios of each primitive, rather than treating everything like a nail just because we have a hammer.

## `std::latch`: One-shot Count-down Barrier

`std::latch` is defined in the `<latch>` header file. It is a **single-direction decrementing counter**. You can imagine it as a door with a latch (bolt) on it. The strength of the latch is determined by the initial count. Every time a thread executes `count_down`, the latch loosens by one notch; when the count reaches zero, the door opens, and all threads blocked on `wait` can pass. The key characteristic is: **a latch is one-shot**—once the count reaches zero, it remains "open" forever and cannot be reset.

`std::latch`'s API is very minimal: pass the initial count value `max` (of type `std::ptrdiff_t`) at construction; `count_down(n)` decrements the count by `n` (non-blocking); `wait` blocks the current thread until the count reaches zero; `arrive_and_wait` is an atomic combination of `count_down` + `wait`—the current thread contributes a decrement and then waits for the count to reach zero; `try_wait` is a non-blocking check—it returns `true` when the counter is zero (note: it allows for a very low probability of spurious returns `true`). Let's understand its usage through a specific scenario.

### Pattern: One-time Initialization

Suppose our program needs to initialize three subsystems at startup—logging, configuration, and network connection. Each subsystem is handled by an independent thread, and the main thread must wait until all subsystems are ready before starting business logic. This is a typical one-time synchronization scenario:

```cpp
#include <latch>
#include <thread>
#include <vector>

int main() {
    // Initialize latch with count 3
    std::latch done(3);

    auto startup_task = [&](const char* name) {
        // Initialize subsystem...
        done.count_down(); // Signal completion
        printf("%s initialized.\n", name);
    };

    std::thread t1(startup_task, "Logger");
    std::thread t2(startup_task, "Config");
    std::thread t3(startup_task, "Network");

    // Main thread waits for all subsystems
    done.wait();
    printf("All systems go. Starting main logic...\n");

    t1.join(); t2.join(); t3.join();
}
```

Here, each initialization thread calls `count_down` after completing its task, and the main thread calls `wait` to block. When all three `count_down`s are executed, the main thread wakes up and continues. Note that the worker threads call `count_down` instead of `arrive_and_wait`—because the workers don't need to wait for the others; they can exit once they finish their work. Only the main thread needs to wait.

If a worker thread also wants to "finish its part and then wait for everyone to continue together," we use `arrive_and_wait`:

```cpp
auto worker_task = [&](int id) {
    do_work(id);
    // "I'm done, and I'll wait for you guys"
    done.arrive_and_wait();
    do_phase_two(id);
};
```

The semantics of `arrive_and_wait` are atomic "decrement + wait"—the thread calling it will also be blocked until the count reaches zero. Internally, it is equivalent to `count_down(); wait();`, but the standard guarantees the atomicity of these two steps. This means no other thread can reduce the count to zero and cause the waiter to miss the wakeup between the "decrement" and "wait".

There is a detail that is easily overlooked: the parameter to `count_down` can be greater than 1. For example, if a thread is responsible for completing three tasks, it can `count_down(3)` at once. If the value passed causes the count to become negative, the behavior is undefined—so the caller must ensure the count is not decremented too far.

## `std::barrier`: Reusable Phase Synchronization

`std::latch` solves the "wait for everyone to arrive once" problem, but many parallel algorithms require **repeated synchronization**—for example, in iterative computations, every round of iteration requires all threads to complete the current step before entering the next. If we used a latch, we would have to create a new latch object for every iteration, which is wasteful and inelegant. `std::barrier` is designed for this: it is a **reusable** synchronization barrier. After all participating threads arrive at the barrier point, the barrier automatically resets and can be used for the next round of synchronization.

`std::barrier` is defined in the `<barrier>` header. It is a class template `std::barrier<CompletionFunction>`, where `CompletionFunction` defaults to an empty function. The constructor takes the number of participating threads (and an optional completion function). The core API consists of three methods: `arrive` notifies the barrier "I'm here" but does not block; `arrive_and_wait` notifies and blocks until all threads have arrived; `arrive_and_drop` notifies and permanently reduces the number of participating threads (used for scenarios where participants dynamically shrink).

### Basic Usage: Multi-phase Parallel Computation

Let's look at a simple multi-phase parallel computation scenario. Suppose we have 4 worker threads, and each thread needs to execute three phases sequentially. Synchronization is required between all threads after each phase:

```cpp
#include <barrier>
#include <thread>
#include <vector>
#include <iostream>

int main() {
    const int num_threads = 4;
    std::barrier sync_point(num_threads);

    auto worker = [&](int id) {
        // Phase 1
        std::cout << "Thread " << id << " Phase 1\n";
        sync_point.arrive_and_wait();

        // Phase 2
        std::cout << "Thread " << id << " Phase 2\n";
        sync_point.arrive_and_wait();

        // Phase 3
        std::cout << "Thread " << id << " Phase 3\n";
        sync_point.arrive_and_wait();
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) t.join();
}
```

The key to this code is that each thread calls `arrive_and_wait` after completing a phase. When all 4 threads have called `arrive_and_wait`, the barrier "opens"—all threads are released simultaneously and enter the next phase. The barrier automatically resets to the initial count, waiting for the next round. You will notice that the whole process requires no additional mutex or condition_variable; the barrier handles all waiting and wakeup logic internally.

### Completion Function: Centralized Processing Between Phases

`std::barrier` has a powerful but little-known feature—the **completion function**. When all participating threads have arrived at the barrier, the barrier executes this completion function in the context of one of the arriving threads before releasing the threads. This mechanism is perfect for "reduction" operations: each thread calculates a partial result independently, and when all threads arrive at the barrier, the completion function is responsible for aggregating these partial results.

```cpp
#include <barrier>
#include <atomic>
#include <thread>
#include <vector>

int main() {
    std::atomic<int> total_sum{0};
    std::vector<int> partial_sums(4, 0);

    // Completion function: aggregate partial sums
    auto on_completion = [&]() noexcept {
        int sum = 0;
        for (auto& val : partial_sums) sum += val;
        total_sum.store(sum);
    };

    std::barrier sync_point(4, on_completion);

    auto worker = [&](int id) {
        // Calculate partial sum
        partial_sums[id] = (id + 1) * 10;

        // Wait for everyone, then completion function runs
        sync_point.arrive_and_wait();

        // Now total_sum is valid
        printf("Thread %d sees total: %d\n", id, total_sum.load());
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) t.join();
}
```

Here we define a lambda `on_completion` as the barrier's completion function. When all threads arrive at the barrier, the barrier calls this function to accumulate the partial sums in `partial_sums` into `total_sum`. Only after the completion function finishes are all threads released—this means threads can safely read `total_sum` after returning from `arrive_and_wait`, because the completion function has already executed.

There are a few constraints on the completion function to note. First, it must be `noexcept`—because the barrier executes it before releasing threads; if it throws an exception, the entire program calls `std::terminate`. Second, the completion function executes in the context of "one of the arriving threads" (specifically which one is implementation-defined), so it should not perform blocking or time-consuming operations. Finally, access to shared state within the completion function does not need additional locking—because while the completion function is executing, other threads are still blocked at the barrier, so there is no concurrent access.

### `arrive()` and `arrive_and_drop()`

`arrive()` is the "check-in without waiting" version—the thread notifies the barrier "I'm here" and returns immediately without blocking. This suits scenarios where "producers just arrive, consumers wait." However, note that `arrive()` returns a `token`. This token currently has no practical use in the standard (it is reserved for future extensions), but you still need to ensure each `arrive` call corresponds to a participating thread.

`arrive_and_drop()` is a more special operation—it notifies the barrier "I'm here, but I won't participate in the future." Every time `arrive_and_drop()` is called, the barrier's participation count permanently decreases by 1. This fits scenarios in thread pools where "worker threads exit dynamically": after a thread finishes its last round of work, it calls `arrive_and_drop()`, and subsequent synchronization rounds will no longer wait for it.

## `std::counting_semaphore`: General-purpose Counting Semaphore

`std::latch` and `std::barrier` solve "inter-thread synchronization"—everyone arrives and moves together. `std::counting_semaphore` solves the "resource counting" problem—limiting the number of threads accessing a resource simultaneously. It is defined in the `<semaphore>` header and is a class template `std::counting_semaphore<N>`, where `N` is the maximum value of the semaphore (default is an implementation-defined value, at least as large as the maximum value of `std::ptrdiff_t`).

The core concept of a semaphore is simple: it maintains an internal counter. `acquire` tries to decrement the counter by 1; if the counter is already 0, it blocks and waits. `release(n)` increments the counter by `n` and wakes up waiting threads. This "acquire-release" semantics can model many real-world problems.

`std::counting_semaphore` has a type alias `std::binary_semaphore`—when the maximum value is 1, the semaphore degenerates into a simple binary semaphore, where the counter has only two states: 0 and 1.

### Pattern: Resource Pool

Suppose we have a database connection pool that allows a maximum of 3 threads to hold connections simultaneously. Using `std::counting_semaphore<3>` to control this is very natural:

```cpp
#include <semaphore>
#include <thread>
#include <vector>
#include <iostream>

int main() {
    std::counting_semaphore<3> pool(3); // Capacity 3

    auto worker = [&](int id) {
        pool.acquire(); // Get connection
        printf("Thread %d acquired connection.\n", id);

        // Work with connection...
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        pool.release(); // Return connection
        printf("Thread %d released connection.\n", id);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) t.join();
}
```

8 threads compete for 3 connection slots. The first 3 threads immediately acquire connections, and the next 5 threads block on `acquire`. Whenever a thread calls `release`, a waiting thread is woken up to acquire the connection. The entire process is controlled entirely by the semaphore's count, without any mutex or condition_variable.

### `std::binary_semaphore`: Semaphore-shaped Mutex

`std::binary_semaphore` is an alias for `std::counting_semaphore<1>`, where the counter has only two states: 0 and 1. It can be used in scenarios requiring simple mutual exclusion, such as one-time signal notification between threads:

```cpp
std::binary_semaphore signal(0);

void receiver() {
    signal.acquire(); // Block until signaled
    printf("Received signal!\n");
}

void sender() {
    signal.release(); // Send signal
    printf("Signal sent.\n");
}
```

The semaphore's initial value is 0 (constructor parameter). `receiver` blocks on `acquire`; `sender` calls `release` to change the counter from 0 to 1, waking the waiting thread.

You might ask: what is the difference between `std::binary_semaphore` and `std::mutex`? Capability-wise they are similar—both can achieve mutual exclusion and wait-notify. But semantically there is a key difference: mutex emphasizes **ownership** (who locks must unlock), while semaphores have no concept of ownership—Thread A can `release`, and Thread B can come along to `acquire`. This decoupling is useful in some scenarios (e.g., in producer-consumer, the producer releases the semaphore to notify the consumer), but it also means semaphores cannot replace mutexes to protect critical sections—because you cannot guarantee that only the lock-holding thread can unlock.

### Comparing Semaphores and Condition Variables

Since semaphores can also do wait-notify, why do we still need `condition_variable`? Conversely, since `condition_variable` is more general, why did C++20 introduce semaphores? The core of this question lies in the **semantic complexity** and **performance characteristics** of the two.

The advantage of semaphores lies in their lightweight nature. They don't need to be used with a mutex (they maintain state internally), don't need to handle spurious wakeups, and the API has only two core operations: `acquire`/`release`. For simple resource counting or one-shot notification scenarios, semaphore code is much more concise than `condition_variable`. Performance-wise, semaphores are usually based on platform-native semaphores (sem_t on Linux, HANDLE objects on Windows), which may be faster than `condition_variable` in simple wait-notify scenarios—because `condition_variable` needs to work with a mutex, and every wait/notify involves mutex acquisition and release.

The advantage of condition variables lies in **expressiveness**. When the wait condition is not simply "is the counter 0", but a composite condition like "is the queue empty AND is the shutdown flag not set", `condition_variable` combined with a mutex and a predicate can express this logic precisely. Condition variables also support timed waits (`wait_for`/`wait_until`). Semaphores' `acquire` doesn't support timeouts natively, but C++20 provides `try_acquire_for` and `try_acquire_until` for timed acquisition. If you need more fine-grained timeout control or complex condition judgment, `condition_variable` is still the better choice.

To summarize the selection strategy in one sentence: if your synchronization logic can be expressed with "counting", prefer semaphores; if your synchronization logic involves complex condition checking or needs timeouts, use `condition_variable`.

## What if I don't have C++20? Simulating with mutex + CV

If your project is still using C++17 or earlier, don't despair—the semantics of all three primitives can be simulated using mutex + condition_variable + a counter. Although the code is more verbose, understanding these simulations helps you deeply understand the underlying mechanisms of C++20 primitives.

### Simulating `latch`

```cpp
class SimpleLatch {
    std::mutex m;
    std::condition_variable cv;
    std::ptrdiff_t count;

public:
    explicit SimpleLatch(std::ptrdiff_t n) : count(n) {}

    void count_down(std::ptrdiff_t n = 1) {
        std::lock_guard lock(m);
        count -= n;
        if (count == 0) cv.notify_all();
    }

    void wait() {
        std::unique_lock lock(m);
        cv.wait(lock, [this] { return count == 0; });
    }

    void arrive_and_wait() {
        std::unique_lock lock(m);
        if (--count == 0) {
            cv.notify_all();
        } else {
            cv.wait(lock, [this] { return count == 0; });
        }
    }
};
```

We see that this simulation implementation is exactly the standard application of the "wait with predicate + notify_all" pattern learned in the previous post. `count_down` decrements the counter while holding the lock and calls `notify_all` to wake all waiters when the count reaches zero. `wait` uses `wait` with a predicate to prevent spurious wakeups and lost wakeups. `arrive_and_wait` combines `count_down` and `wait`—note that there is no atomicity guarantee here (after `count_down` releases the lock and before `wait` acquires it, another thread might reduce the count to zero), but because `wait` has a predicate, even if the notification happened first, it won't be missed.

### Simulating `barrier`

```cpp
class SimpleBarrier {
    std::mutex m;
    std::condition_variable cv;
    std::ptrdiff_t n;
    std::ptrdiff_t count;
    std::ptrdiff_t generation{0};

public:
    explicit SimpleBarrier(std::ptrdiff_t n) : n(n), count(n) {}

    void arrive_and_wait() {
        std::unique_lock lock(m);
        auto my_gen = generation;

        if (--count == 0) {
            generation++; // Reset for next round
            count = n;
            cv.notify_all();
        } else {
            cv.wait(lock, [this, my_gen] { return my_gen != generation; });
        }
    }
};
```

The complexity of simulating `barrier` compared to `latch` lies in "reusability". We can't simply reset when the count reaches zero—because threads from the previous round might not have returned from `wait` yet, and threads from the new round might have started calling `arrive_and_wait`. The solution is to introduce a **generation** counter: increment the generation every time the barrier resets. Waiting threads check "has my generation changed"—if it has, it means the barrier has opened, and they can proceed.

This generation trick is the core technique for implementing reusable barriers and is also the mechanism used internally in C++20's `std::barrier`. Understanding this trick, you won't be unfamiliar with generation counters when reading standard library implementations or third-party concurrency libraries.

## Scenario Selection Guide

We now have five main synchronization primitives (mutex, condition_variable, latch, barrier, counting_semaphore). How do we choose when facing a specific synchronization requirement? Based on my experience, I have summarized a simple decision path.

If your requirement is "protect a critical section, only one thread can enter at a time", use mutex (with `std::unique_lock` or `std::lock_guard`). If your requirement is "wait for a condition to be true", use condition_variable with mutex and a predicate. If your requirement is "wait for N threads to finish something before continuing together, and synchronization is only needed once", use latch. If your requirement is "repeated synchronization—every round of iteration, every phase needs everyone to arrive", use barrier. If your requirement is "limit the number of threads accessing a resource simultaneously" or "simple signal notification between threads", use counting_semaphore.

Sometimes a scenario might satisfy multiple conditions—for example, a barrier can be simulated internally with condition_variable, and counting_semaphore can also be used for one-time notification (degenerating to binary_semaphore). The key to selection is seeing which primitive's semantics best matches your problem—the higher the semantic match, the less error-prone the code.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `ch04`.

## Exercises

### Exercise 1: Multi-phase Parallel Matrix Computation

Given an N x N integer matrix, use 4 threads to compute the transpose of the matrix and the sum of all elements in parallel. Divide the computation into three phases: Phase 1, each thread computes the sum of a part of the matrix elements; Phase 2, aggregate all partial sums to get the total sum; Phase 3, each thread is responsible for transposing a part of the matrix. A synchronization point is needed between Phase 1 and Phase 3, and after Phase 3.

**Hint:** Use `std::barrier` with a completion function. The completion function for Phase 1 is responsible for aggregating partial sums. After Phase 3, the main thread needs to wait for all worker threads to finish. Think about this: Phase 2 is just a single aggregation operation; should it be executed in a worker thread or as a completion function?

### Exercise 2: Implement a Bounded Blocking Queue with `counting_semaphore`

Re-implement the `BoundedBlockingQueue` from the previous post using `std::counting_semaphore` (instead of `condition_variable`). **Hint:** You need two semaphores—`items` initialized to 0 (tracking the number of elements in the queue), `spaces` initialized to the queue capacity (tracking remaining empty slots). When `push`ing, first `spaces.acquire()`, lock to put the element in, then `items.release()`. When `pop`ing, first `items.acquire()`, lock to take the element out, then `spaces.release()`. Note: You still need a mutex to protect the queue container itself—the semaphore only controls "can I operate", not the consistency of the data structure.

### Exercise 3: Simulate `counting_semaphore` with mutex + condition_variable

Implement a simple counting semaphore class using `std::mutex`, `std::condition_variable`, and an internal counter, providing `acquire`, `release`, and `try_acquire` methods. `try_acquire` attempts to acquire a resource, returning `true` on success, or `false` if the counter is zero (non-blocking). Write a simple test program to verify your implementation: create 5 threads competing for a semaphore with an initial count of 2, and observe that the number of threads holding the resource simultaneously does not exceed 2.

## Reference Resources

- [std::latch -- cppreference](https://en.cppreference.com/w/cpp/thread/latch)
- [std::barrier -- cppreference](https://en.cppreference.com/w/cpp/thread/barrier)
- [std::counting_semaphore -- cppreference](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
- [Synchronization Primitives in C++20 -- KDAB](https://www.kdab.com/synchronization-primitives-in-c20/)
- [Latches and Barriers -- Modernes C++](https://www.modernescpp.com/index.php/latches-and-barriers/)
- [Semaphores in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/semaphores-in-c-20/)
- [P0666R2: Revised Latches and Barriers for C++20 (Proposal Paper)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0666r2.pdf)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
