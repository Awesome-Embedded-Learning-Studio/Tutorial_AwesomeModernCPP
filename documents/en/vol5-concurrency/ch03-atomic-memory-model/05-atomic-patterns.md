---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Correct implementation of classic atomic patterns such as SeqLock, Double-Checked
  Locking, reference counting, and publish-subscribe.
difficulty: advanced
order: 5
platform: host
prerequisites:
- fence 与编译器屏障
- atomic_wait 与 atomic_ref
reading_time_minutes: 22
related:
- 无锁编程基础
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
title: Atomic Operation Memory Order
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch03-atomic-memory-model/05-atomic-patterns.md
  source_hash: 6a16ee5ae8b32d406353bc2afbd7dc091077f2bcf1b3ac8dbdc6599198b87cc4
  token_count: 5394
  translated_at: '2026-06-13T11:51:22.489438+00:00'
---
# Atomic Operation Patterns

> 📖 **Application Scenario**: The atomic patterns in this chapter have a high-frequency application in embedded systems—sharing variables between an ISR and the main loop without locks. If you are writing MCU firmware, reading this alongside [Volume 8: Interrupt-Safe Programming](../../vol8-domains/embedded/05-interrupt-safe-coding.md) will provide even greater clarity.

By this point, we have fully decomposed the `std::atomic` operation set, the six memory orders, fences and barriers, `std::atomic_thread_fence`, and `std::atomic_signal_fence`. However, taking these tools in isolation only answers the question of "how"—how to perform an atomic addition, how to issue a release store, or how to wait for a value to change. Real-world engineering practice requires patterns: when facing a specific concurrency problem, which atomic operations should we choose, and how should we combine their memory orders to solve the problem correctly and efficiently?

In this chapter, we focus on several classic atomic operation patterns. These patterns were not invented in a vacuum—they come from proven solutions repeatedly verified in real-world systems like the Linux kernel, database engines, and high-performance network frameworks. We will break down the "why" for each pattern: why it is designed this way, why the memory order cannot be weaker, and why a seemingly harmless change might introduce a bug.

The patterns we cover include: SeqLock, Double-Checked Locking, reference counting, publish-subscribe flags, lock-free min/max tracking, stop flags, and spinlocks. Each pattern is accompanied by complete code and step-by-step semantic analysis.

## SeqLock: Sequence Locking Where Readers Are Never Blocked

### Pattern Motivation

A classic solution to the reader-writer problem is the reader-writer lock, but its cost is high—even if there are only read operations, it requires the full overhead of a lock/unlock flow, involving atomic operations or even system calls. In many scenarios, the read frequency is far higher than the write frequency (e.g., sensor data collection and reading, or system time retrieval). We want read operations to be as lightweight as possible—ideally, completely lock-free.

SeqLock is designed for exactly this. Its core idea is: use a spinlock to protect writers (only one writer at a time), but do not block readers at all—readers determine if the data they read is consistent by checking a sequence number. If the sequence number changes during the read (indicating a writer modified the data), the reader simply retries.

### Implementation

```cpp
#include <atomic>

struct SeqLock {
    std::atomic<uint32_t> seq_{0}; // Sequence number
    // ... shared data ...

    void write(const Data& new_data) {
        // 1. Increment sequence to odd (write start)
        seq_.fetch_add(1, std::memory_order_acquire);

        // 2. Modify shared data
        data_ = new_data;

        // 3. Increment sequence to even (write complete)
        seq_.fetch_add(1, std::memory_order_release);
    }

    Data read() {
        Data copy;
        uint32_t seq0, seq1;
        do {
            seq0 = seq_.load(std::memory_order_acquire);
            // Copy data
            copy = data_;
            seq1 = seq_.load(std::memory_order_acquire);
        } while (seq0 != seq1 || (seq0 & 1)); // Retry if changed or odd
        return copy;
    }
};
```

Let's break down the core mechanism of this design.

The parity of the sequence number is key. An even number means "no writer is currently active, data is in a consistent state"; an odd number means "a writer is modifying data, state may be inconsistent." The writer changes the sequence from even to odd at the start, and back to even upon completion—each successful write increments the sequence by 2.

The reader's strategy is "check-before-read + verify-after-read": first read the sequence number to confirm it is even (no writer), then read the actual data, and finally read the sequence number again. If the sequence numbers are identical and even, it means no writer intervened during the read, and the data is consistent. If they differ (or became odd), it means a write occurred during the read, and the data may be inconsistent—the reader discards this result and retries.

The `fetch_add` with `memory_order_acquire` in `write` and the `load` with `memory_order_acquire` in `read` establish a happens-before relationship: all modifications by the writer to the actual data complete before the sequence number changes back to even (release ensures previous writes are not reordered after the store); the reader sees the data only after the sequence number becomes even (acquire ensures subsequent reads are not reordered before the load). This ensures the reader sees a version of the data that is fully written by the writer.

### Usage Example

```cpp
// Reader
auto snapshot = lock.read(); // Returns a copy
process(snapshot);

// Writer
lock.write(new_data);
```

Note that the reader copies the data to a local variable before verifying. This is a critical detail—if we use the data directly without copying, and the verification fails, the data is already "dirty" and cannot be used or retried. SeqLock readers must be prepared to discard results at any time, so read data must either be read-only (use and discard) or copied before use.

### Applicability Boundaries of SeqLock

There are a few limitations of SeqLock to be aware of. First, it assumes at most one writer—if you need multiple writers, you must wrap it in an outer mutex. Second, the data type read must be trivially copyable—if the data contains pointers or complex objects, encountering a partially modified state during copying could lead to undefined behavior. Third, if writes are very frequent, readers may retry repeatedly, potentially performing worse than a reader-writer lock—SeqLock is suitable for "write-rarely, read-frequently" scenarios. The `seqlock_t` in the Linux kernel is a classic implementation of this pattern, used for time retrieval (`gettimeofday`) and similar scenarios.

## Double-Checked Locking: Finally Correct Since C++11

### Pattern Motivation and Historical Baggage

The Double-Checked Locking Pattern (DCLP) is perhaps one of the most discussed patterns in multithreaded programming—not because it is the best pattern, but because it could not be implemented correctly prior to C++11. In their 2004 paper "C++ and the Perils of Double-Checked Locking," Scott Meyers and Andrei Alexandrescu analyzed in detail why it failed under the old standard. The core reasons were twofold: compilers could reorder memory operations (writes to an object's fields could be reordered after publishing the pointer), and the CPU itself could also reorder (relatively constrained on x86, but very aggressive on ARM/PowerPC).

The formal memory model and `std::atomic` introduced in C++11 finally provided a portable, correct implementation for DCLP.

### Correct DCLP Implementation

```cpp
class Singleton {
    static std::atomic<Singleton*> inst_;
    static std::mutex mtx_;

    Singleton() = default;
public:
    static Singleton* get_instance() {
        Singleton* ptr = inst_.load(std::memory_order_acquire);
        if (ptr == nullptr) { // 1st check
            std::lock_guard<std::mutex> lock(mtx_);
            if (ptr == nullptr) { // 2nd check
                ptr = new Singleton;
                // Publish with release
                inst_.store(ptr, std::memory_order_release);
            }
        }
        return ptr;
    }
};
```

Let's break down the role of each check in this implementation.

The first check `ptr == nullptr` is performed outside the lock—if the instance is already created (the vast majority of calls take this path), it returns the pointer directly without locking. `memory_order_acquire` ensures that subsequent access to the `Singleton` object's members via this pointer will definitely see the values initialized in the constructor. This is why this load cannot use `memory_order_relaxed`—`relaxed` does not establish a happens-before relationship, and we might see an object for which memory has been allocated but construction has not finished.

The second check `ptr == nullptr` is performed inside the lock—at this point we hold the mutex, so no other thread can be creating the instance simultaneously, so `relaxed` is sufficient. If you feel `relaxed` looks unsafe, switching to `acquire` would not be a correctness issue, just theoretically adding an unnecessary barrier.

The `memory_order_release` semantics in `inst_.store` are key: it guarantees that the initialization of `*ptr` (including all initialization operations in the constructor) completes before the store. Combined with the `acquire` load in the first check, a complete release-acquire synchronization pair is established: all writes from the constructor happen-before the store, the store happens-before the acquire load of another thread, and the acquire load happens-before that thread's access to the Singleton members. The chain is complete with no gaps.

### Not Just Using Meyers' Singleton

C++11 guarantees that the initialization of `static` local variables within a function is thread-safe. Therefore, the simplest Singleton pattern is actually:

```cpp
class Singleton {
public:
    static Singleton& get_instance() {
        static Singleton inst;
        return inst;
    }
};
```

This code is entirely correct, and compilers usually implement it internally using `std::atomic` or equivalent atomic operations. So, is DCLP still useful?

First, the idea of DCLP is not limited to Singletons—any "check-lock-check-initialize" pattern can use this logic. Examples include lazy initialization of a large object, on-demand allocation of thread-local storage, or lazy loading of configuration files. Second, in some extreme performance scenarios, the first check of DCLP generates lighter code than the `static` local variable—the latter usually requires checking a hidden guard flag, and the implementation of that flag might be heavier than a single atomic load.

## Reference Counting: The Atomic Foundation of shared_ptr

### Atomic Requirements for Reference Counting

Reference counting is another ubiquitous atomic pattern. The control block of `std::shared_ptr` contains a reference count and a weak reference count, both of which are atomic variables. Let's look at a simplified reference-counted pointer to understand which atomic operations it needs:

```cpp
template<typename T>
class RefCountedPtr {
    struct ControlBlock {
        std::atomic<size_t> ref_count{1};
        T* ptr;
        // ... weak_count, etc.
    };
    ControlBlock* ctrl;

    void add_ref() {
        // Just atomic increment, no synchronization needed
        ctrl->ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        // fetch_add returns the old value
        if (ctrl->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Acquire ensures we see all writes to the object
            delete ctrl->ptr;
            delete ctrl;
        }
    }
};
```

There are two key points regarding atomic operations in reference counting. `add_ref` uses `memory_order_relaxed`—incrementing the reference count does not need to synchronize with other operations; we only care about the atomicity of the count itself. Even if thread A's `add_ref` races with thread B's `release`, the `fetch_add` and `fetch_sub` themselves are atomic and will not cause counting errors.

`release` using `memory_order_acq_rel` is a more nuanced choice. The `acquire` semantics guarantee that when the reference count reaches zero, the current thread sees all modifications to the object by other threads prior to that point (because every object access after a `add_ref` implies a "holding a reference" relationship). The `release` semantics guarantee that all accesses by the current thread to the object complete before destruction. Together, these two directions ensure the safety of destruction—the destructor sees a fully consistent object state, and no other thread is still accessing the object.

## Publish-Subscribe Flag: Relaxed Counter + Acquire-Release Flag

### Pattern Description

This is a very practical combined pattern: a `relaxed` atomic counter for statistics (no precise synchronization needed) plus an `acquire-release` atomic flag for notification. A typical scenario is a task queue—worker threads take tasks from a queue to execute, increment a counter after completing each task, and set a flag to notify the main thread when all are done.

```cpp
struct ProgressTracker {
    std::atomic<int> completed{0}; // Relaxed
    std::atomic<bool> done{false}; // Acquire/Release

    void worker_complete() {
        completed.fetch_add(1, std::memory_order_relaxed);
        if (is_all_done()) {
            done.store(true, std::memory_order_release);
        }
    }

    void main_wait() {
        while (!done.load(std::memory_order_acquire)) {
            // spin or wait
        }
        // Now safe to read 'completed'
        print_stats(completed.load(std::memory_order_relaxed));
    }
};
```

The key to this pattern is the separation of concerns. `completed` is only for displaying progress—it doesn't need precise synchronization, so `relaxed` is enough. Even if the main thread occasionally reads an "old" count (off by 1 or 2), it has no impact on user experience. `done` is the true synchronization point—it uses `acquire-release` to guarantee that when the main thread sees `done == true`, all modifications to shared data by worker threads are visible.

This combination of "relaxed statistics + strict synchronization" is very common in engineering. Another example: a network server uses a relaxed counter to record processed requests (losing an occasional update is fine), and an acquire-release flag to signal a shutdown (must guarantee all requests are processed before closing).

## Lock-Free Min/ax Tracking: CAS Loop

### Pattern Description

Maintaining a global maximum or minimum value and updating it in a lock-free manner in a multithreaded environment is a classic CAS (compare-and-swap) usage pattern. For example, a network server might want to track the slowest request latency, or a sensor system might record extreme temperatures.

```cpp
class MaxTracker {
    std::atomic<uint64_t> max_val_{0};
public:
    void update(uint64_t candidate) {
        uint64_t current = max_val_.load(std::memory_order_relaxed);
        while (candidate > current) {
            // Try to update if current hasn't changed
            if (max_val_.compare_exchange_weak(current, candidate,
                std::memory_order_relaxed)) {
                break; // Success
            }
            // Failure: current updated by CAS, loop continues
        }
    }
};
```

The CAS loop is the core of this pattern. We first load the current maximum. If the candidate value is not greater than the current value, we do nothing and return. If the candidate is larger, we attempt to replace the current value with the candidate using CAS. CAS might fail—because another thread may have updated the maximum between our load and CAS. Upon failure, `compare_exchange_weak` updates `current` to the latest value, and we re-compare to decide if we need to try again.

Using `compare_exchange_weak` instead of `compare_exchange_strong` here is a common optimization—in a loop, the occasional spurious failure of the `weak` version just means one extra iteration, but it is more efficient on certain platforms (especially ARM, PowerPC, and other LL/SC architectures) than the `strong` version.

All memory orders use `relaxed`—because we only care about the correctness of the single variable (the maximum value) itself, and do not need to establish synchronization relationships with other variables. If max tracking is purely for statistics or monitoring, strict happens-before guarantees are not needed.

However, note that the CAS operation for `uint64_t` is not lock-free on most platforms—because `uint64_t` is 64-bit, and CAS on some 32-bit platforms can only handle 32-bit. If your target is a 32-bit embedded platform, this pattern might not be as efficient as expected. On 64-bit platforms, 64-bit CAS is usually lock-free.

## Stop Flag: Correct Usage of atomic<bool>

### Basic Pattern

The stop flag is likely the simplest atomic pattern—a background thread periodically checks the flag, and the main thread sets the flag and waits for the thread to exit. It looks simple, but there are details worth discussing:

```cpp
std::atomic<bool> stop_{false};

void worker() {
    while (!stop_.load(std::memory_order_acquire)) {
        // Do work
    }
}

void shutdown() {
    // Update shared data...
    stop_.store(true, std::memory_order_release);
}
```

Using `acquire` and `release` here instead of `relaxed` is worth explaining. If the background thread reads some shared data after checking the stop flag (e.g., reading the latest config after the loop), `acquire` ensures it sees all modifications to the shared data made by the thread setting the flag prior to that point. Similarly, `release` ensures that all writes by the main thread before setting the flag (like updating config) are visible to the background thread.

If your stop flag is purely a boolean signal—the background thread doesn't need to read any other shared data—then `relaxed` is also safe. But forming the habit of using `acquire-release` does no harm, and the performance difference is negligible (on x86, loads are normal reads regardless of memory order; on ARM, an acquire load is just one extra instruction).

### Low-Latency Stopping with atomic_wait

In the previous chapter, we introduced `atomic_wait`. Here, we can upgrade the stop flag to a "wait-style stop"—the background thread blocks waiting on the flag instead of polling it:

```cpp
void worker() {
    while (!stop_.load(std::memory_order_acquire)) {
        // Do periodic work
        if (need_stop) break;

        // Wait for signal or timeout
        stop_.wait(false);
    }
}

void shutdown() {
    stop_.store(true, std::memory_order_release);
    stop_.notify_one();
}
```

In this version, `stop_.wait` blocks while `stop_` is still `false`, consuming no CPU. When the main thread calls `store`, the background thread wakes immediately and exits. However, there is an issue: `wait` has no timeout—if the background thread needs to do work periodically between two `wait` calls (e.g., checking a sensor every 100ms), pure `wait` is not suitable. In this case, a hybrid solution combining `sleep_for` + `wait` is more practical: use `sleep_for` for periodic work most of the time, and use `wait` to wake the thread when immediate stopping is needed.

## Spinlock: Educational Implementation and Applicable Scenarios

### Basic Implementation

A spinlock is the simplest mutual exclusion primitive—a thread that fails to acquire it doesn't block, but retries in a tight loop. It is generally not suitable for production environments (explained later), but it serves as an excellent educational tool because it demonstrates the usage of `std::atomic` and the basic principles of lock-free synchronization with minimal code.

```cpp
class SpinLock {
    std::atomic<bool> locked_{false};
public:
    void lock() {
        // Keep trying until we successfully swap false to true
        while (locked_.exchange(true, std::memory_order_acquire)) {
            // Spin
        }
    }

    void unlock() {
        locked_.store(false, std::memory_order_release);
    }
};
```

The `exchange` in `lock` is a clever operation: it atomically sets `locked_` to `true` while returning the previous value. If the old value is `false`, the lock was free and we successfully acquired it. If the old value is `true`, the lock is already held by someone else, so we continue looping. The `acquire` semantics guarantee that operations after acquiring the lock are not reordered before the `exchange`—modifications by other threads before releasing the lock are visible to the current thread.

The `release` semantics in `unlock` guarantee that all writes in the critical section complete before releasing the lock—the next thread to acquire the lock will see these modifications.

### Why Spinlocks Are Usually Not Suitable for Production

The biggest problem with spinlocks is that they consume CPU while waiting. If the critical section is very short (a few instructions), the overhead of spinning might be lower than the context switch overhead of a mutex. But if the critical section is slightly longer, or if multiple threads are competing for the same lock, spinlocks lead to a massive waste of CPU time on "spinning." Worse, on single-core systems, spinlocks are completely meaningless—the thread holds the CPU while spinning, so the thread holding the lock never gets a chance to run to release it, resulting in deadlock.

In actual projects, prioritize `std::mutex` or `std::shared_mutex`. Only consider a spinlock when all the following conditions are met: the critical section is extremely short (no more than a few dozen instructions), contention is low, and it runs on a multi-core system. The Linux kernel uses spinlocks extensively in preemptible kernels—but the kernel has special scheduling guarantees (preemption is disabled), which user-space lacks.

### Better Version Using atomic_flag

The `SpinLock` above uses `std::atomic<bool>`, but a more canonical approach uses `std::atomic_flag`—it is the only atomic type guaranteed by the standard to be lock-free (`std::atomic<bool>` is theoretically not required to be lock-free):

```cpp
class SpinLock {
    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (locked_.test_and_set(std::memory_order_acquire)) {
            // Spin
        }
    }

    void unlock() {
        locked_.clear(std::memory_order_release);
    }
};
```

`test_and_set` and `clear` are the two core operations of `std::atomic_flag`—the former atomically sets the flag to `true` and returns the old value, the latter atomically sets the flag to `false`. This version is semantically equivalent to the `std::atomic<bool>` version but guarantees lock-free behavior.

## Decision Guide for Pattern Selection

With so many patterns understood, how do we choose when coding? We can decide based on the characteristics of the critical section.

If the critical section is just a simple variable read or update—like a counter, a flag, or a maximum value—direct RMW operations on `std::atomic` (`fetch_add`, CAS, etc.) are sufficient. No mutex, no spinlock. This is the lightest choice with the best performance. The choice of memory order depends on whether synchronization with other variables is needed: if not, `relaxed` is fine; if yes, use `acquire-release`.

If the critical section involves coordinated modification of multiple variables—like inserting an element into a map while updating a counter—`std::atomic` is not enough (unless you can pack multiple variables into a struct updated via CAS), so honestly use a `std::mutex`. Although a mutex has context switch overhead, it guarantees correctness, and overhead is low when contention is low (Linux's `std::mutex` is entirely in user-space when uncontested).

If the read frequency is far higher than the write frequency, and the data is trivially copyable—SeqLock is a good choice. It keeps readers completely lock-free, at the cost of occasional retries. The Linux kernel uses this in many high-frequency read scenarios.

If you need lazy initialization or a "check-lock-check" pattern—DCLP has been correct since C++11. But if it's just a Singleton, prioritize Meyers' Singleton (`static` local variable); it is simpler and less error-prone.

If you need to wait for a condition to be met—use `atomic_wait` instead of busy-waiting or `condition_variable`. On Linux, it uses futex, with latency an order of magnitude lower than `condition_variable`, and no extra mutex is needed.

## Summary

In this chapter, we applied all the tools learned in ch03—`std::atomic` operation sets, memory orders, fences, `std::atomic_thread_fence`, and `std::atomic_signal_fence`—to seven classic concurrency patterns.

SeqLock allows readers to detect writer interference lock-free via sequence parity, suitable for "read-many-write-few, trivially copyable data" scenarios. Double-Checked Locking finally has a correct, portable implementation under the C++11 memory model—core is the `acquire` load and `release` store. The reference counting pattern demonstrates the combination of `relaxed` for increment and `acq_rel` for decrement—the former cares only about atomicity, the latter ensures visibility at destruction. The publish-subscribe flag separates relaxed count statistics from strict synchronization notifications—each gets what it needs without dragging the other down. Lock-free min/max tracking uses a CAS loop to implement lock-free "compare-and-update." The stop flag is the simplest atomic pattern, but combined with `atomic_wait`, it can also achieve low-latency stop signals. The spinlock is a classic teaching tool but should be used cautiously in production.

These patterns are not isolated—they are often combined. A SeqLock might use a spinlock internally to protect writers; a DCLP uses an acquire-release synchronization pair internally; the destruction of a reference-counted pointer might trigger a publish-subscribe notification. Understanding the core idea of each pattern and flexibly combining them in specific scenarios is the real goal.

The next chapter leaves the atomic world of ch03 and enters a new topic. But before that, I suggest doing the exercises in this chapter—especially the implementations of SeqLock and DCLP, as they are high-frequency topics in interviews and the touchstone for testing whether you truly understand memory ordering.

## Exercises

### Exercise 1: Implement SeqLock

Based on the `SeqLock` class above, write a complete program: one writer thread updates a struct containing three `uint32_t` fields at 10ms intervals, and four reader threads read and print the data at 1ms intervals. Run for a while and observe if readers always obtain consistent data (values for all three fields come from the same write). If data is inconsistent (e.g., temperature is from the 5th write, but humidity is from the 6th), check if your `acquire`/`release` usage is correct.

### Exercise 2: Implement DCLP Singleton

Implement a thread-safe configuration manager using the DCLP pattern. Requirements:

1. Use the classic DCLP structure of `std::atomic` + `std::mutex`
2. Correctly use `memory_order_acquire` and `memory_order_release` in `get_instance`
3. Write a multi-threaded test: 8 threads call `get_instance` simultaneously, verifying that all threads get the same instance

**Extra Challenge**: Compare the performance of your DCLP implementation with Meyers' Singleton (`static` local variable). Use `std::chrono` to measure the time taken for 1 million `get_instance` calls under both implementations.

### Exercise 3: Lock-Free Minimum Tracker

Implement a `MinTracker` class that uses a CAS loop to track a minimum value of type `double`. Then, have 4 threads generate random numbers and call `update`, finally verifying that the value returned by `get_min` is indeed the minimum of all numbers generated by the threads.

**Hint**: You need to check if atomic operations for floating-point numbers are lock-free on your current platform. Use `std::atomic<double>::is_always_lock_free`. If not lock-free, performance may not be as expected.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `exercises`.

## References

- [Double-Checked Locking is Fixed In C++11 — Jeff Preshing](https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11)
- [C++ and the Perils of Double-Checked Locking — Scott Meyers, Andrei Alexandrescu](https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf)
- [Seqlock — Wikipedia](https://en.wikipedia.org/wiki/Seqlock)
- [Linux Kernel seqlock.h — source code](https://github.com/torvalds/linux/blob/master/include/linux/seqlock.h)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [std::shared_ptr thread safety — cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [Preshing on Programming: Atomic vs. Non-Atomic Operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations/)
