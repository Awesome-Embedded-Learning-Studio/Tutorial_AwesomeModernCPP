---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: Correct implementations of classic atomic patterns such as SeqLock, double-checked
  locking, reference counting, and publish-subscribe flags
difficulty: advanced
order: 5
platform: host
prerequisites:
- Fences and Compiler Barriers
- atomic_wait and atomic_ref
reading_time_minutes: 26
related:
- Lock-Free Programming Fundamentals
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
title: Atomic Operation Patterns
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/05-atomic-patterns.md
  source_hash: 84e9aa39ea8c122cb2fd4f6a58e9e8efea2845bab29517162e057cc35d640ae9
  translated_at: '2026-09-26T07:30:01+00:00'
  engine: anthropic
  token_count: 15000
---
# Atomic Operation Patterns

As of this article, we have fully taken apart the `std::atomic` operation set, the six memory orders, fences and barriers, `wait/notify`, and `atomic_ref`. But taken one at a time, these tools only answer the "how" question—how to do an atomic addition, how to issue a release store, how to wait for a value to change. What real engineering practice needs is patterns: facing a concrete concurrency problem, which atomic operations should you pick, and in what combination of memory orders, to solve the problem both correctly and efficiently.

In this article we concentrate on several of the most classic atomic operation patterns. These patterns were not invented out of thin air—they come from solutions verified over and over in real systems such as the Linux kernel, database engines, and high-performance network frameworks. We will break down the "why" of each pattern: why it is designed this way, why the memory order cannot be any weaker, and why a seemingly harmless change can introduce a bug.

The patterns we will cover include: SeqLock (sequence locking), Double-Checked Locking, reference counting, publish-subscribe flags, lock-free max/min tracking, stop flags, and spinlocks. Each pattern comes with complete code and a step-by-step semantic analysis.

## SeqLock: Sequence Locking That Never Blocks Readers

### Pattern Motivation

A classic solution to the readers-writer problem is the reader-writer lock, but it is expensive—even when there is nothing but reading going on, every read pays the full `lock_shared()` / `unlock_shared()` round trip, involving atomic operations and possibly even system calls. In many scenarios the read frequency is far higher than the write frequency (sensor data being collected and read, retrieving the system time, and so on), and we want reads to be as lightweight as possible—ideally completely lock-free.

SeqLock is designed exactly for this. Its core idea is: a spinlock protects the writer side (only one writer at a time), but readers are never blocked at all—a reader checks a sequence number to decide whether the data it read is consistent. If the sequence number changed during the read (meaning some writer modified the data), the reader simply retries.

### Implementation

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SeqLock {
public:
    SeqLock() : sequence_(0) {}

    /// Writer: acquire write permission
    void lock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // If the sequence number is odd, a writer is already at work
        if ((seq & 1u) != 0) {
            // A multi-writer scenario needs spin-waiting or an extra mutex
            // Here we assume a single writer
            return;
        }
        // Add 1 to the sequence number, making it odd — marks "write in progress"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// Writer: release write permission
    void unlock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // Add 1 to the sequence number again, flipping it back to even — marks "write complete"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// Reader: read the data while it is in a stable state
    /// Returns the sequence number at the start of the read; the caller must verify afterwards that it has not changed
    unsigned read_begin() const
    {
        unsigned seq;
        for (;;) {
            seq = sequence_.load(std::memory_order_acquire);
            if ((seq & 1u) == 0) {
                // Even: no writer is at work
                break;
            }
            // Odd: a writer is at work, spin and wait
            // Real implementations can use pause/yield to reduce power draw
        }
        return seq;
    }

    /// Reader: verify whether a write happened during the read
    /// If it returns true, the read is valid
    bool read_validate(unsigned seq_before) const
    {
        unsigned seq_after = sequence_.load(std::memory_order_acquire);
        return (seq_after == seq_before) && ((seq_after & 1u) == 0);
    }

private:
    std::atomic<unsigned> sequence_;
};
```

Let's dissect the core mechanism of this design.

The parity of the sequence number is the key. Even means "no writer is at work right now, the data is in a consistent state"; odd means "a writer is modifying the data, it may currently be inconsistent". A writer flips the sequence number from even to odd at the start, and back to even when done—each successful write advances the sequence number by 2.

The reader's strategy is "check before reading + validate after reading": first load the sequence number and confirm it is even (no writer), then read the actual data, and finally load the sequence number one more time. If the two sequence numbers are identical and both even, no writer interfered during the read and the data is consistent. If they differ (or the number turned odd), a write happened during the read and the data may be inconsistent—the reader simply discards the result and retries.

The `memory_order_release` in `unlock_write()` and the `memory_order_acquire` in `read_begin()` / `read_validate()` establish a happens-before relationship: all of the writer's modifications to the real data complete before `sequence_` flips back to even (release guarantees that earlier writes are not reordered after the store), and the reader does not see the data until `sequence_` has become even (acquire guarantees that later reads are not reordered before the load). This way, the data the reader sees is necessarily the fully-written version left behind by the writer.

### Usage Example

```cpp
struct SensorData {
    double temperature;
    double humidity;
    double pressure;
};

SensorData g_sensor_data;
SeqLock g_seq_lock;

// Writer thread (typically the sensor acquisition thread)
void writer_thread()
{
    for (int i = 0; i < 100; ++i) {
        g_seq_lock.lock_write();

        g_sensor_data.temperature = 20.0 + i * 0.1;
        g_sensor_data.humidity = 50.0 + i * 0.2;
        g_sensor_data.pressure = 1013.25 + i * 0.01;

        g_seq_lock.unlock_write();
    }
}

// Reader threads (there can be multiple)
void reader_thread(int id)
{
    for (int i = 0; i < 100; ++i) {
        SensorData local;
        unsigned seq;

        do {
            seq = g_seq_lock.read_begin();
            local = g_sensor_data;  // copy the data
        } while (!g_seq_lock.read_validate(seq));

        // local can now be used safely — it is a consistent snapshot
        std::cout << "Reader " << id << ": temp=" << local.temperature
                  << " humidity=" << local.humidity
                  << " pressure=" << local.pressure << "\n";
    }
}
```

Note that the reader copies the data into a `local` variable before validating. This is a key detail—if you use the data in place without copying, then by the time validation fails, the data is already "dirty": unusable, and there is no way to start over. A SeqLock reader must be prepared to throw away the result of a read at any moment, so the data being read is either read-only (used and discarded) or copied out before use.

### The Applicability Boundaries of SeqLock

SeqLock has a few limitations that must be clearly understood. First, it assumes at most one writer—if you need multiple writers, you must wrap a mutex around the outside. Second, the data type being read must be trivially copyable—if the data contains pointers or complex objects, encountering a partially-modified state during the copy can lead to undefined behavior. Third, if writes are very frequent, readers may retry over and over, and performance can end up worse than a reader-writer lock—SeqLock suits "few writes, many reads" scenarios. The Linux kernel's `seqlock_t` is the classic implementation of this pattern, used for time retrieval (`do_gettimeofday`) and similar cases.

## Double-Checked Locking: Finally Correct as of C++11

### Pattern Motivation and Historical Baggage

The Double-Checked Locking Pattern (DCLP) is probably one of the most-discussed patterns in multithreaded programming—not because it is the best pattern, but because before C++11 it was flat-out impossible to implement correctly. Scott Meyers and Andrei Alexandrescu analyzed in detail why it fails under the old standard in their 2004 paper "C++ and the Perils of Double-Checked Locking". There are two core reasons: the compiler may reorder memory operations (writes to the object's fields may be reordered after the publication of the pointer), and the CPU itself may also reorder (relatively constrained on x86, very aggressive on ARM/PowerPC).

The formal memory model introduced by C++11 and `std::atomic` finally gave DCLP a portable, correct implementation.

### A Correct DCLP Implementation

```cpp
#include <atomic>
#include <mutex>
#include <iostream>

class Singleton {
public:
    static Singleton& instance()
    {
        Singleton* ptr = instance_.load(std::memory_order_acquire);
        if (ptr == nullptr) {
            std::lock_guard<std::mutex> lock(mutex_);
            ptr = instance_.load(std::memory_order_relaxed);
            if (ptr == nullptr) {
                ptr = new Singleton();
                instance_.store(ptr, std::memory_order_release);
            }
        }
        return *ptr;
    }

    void do_something()
    {
        std::cout << "Singleton::do_something()\n";
    }

private:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static std::atomic<Singleton*> instance_;
    static std::mutex mutex_;
};

std::atomic<Singleton*> Singleton::instance_{nullptr};
std::mutex Singleton::mutex_;
```

Let's break down what each layer of checking in this implementation does.

The first check, `instance_.load(acquire)`, happens outside the lock—if the instance has already been created (the path taken by the vast majority of calls), the pointer is returned directly, no locking needed. `memory_order_acquire` guarantees that subsequent accesses through this pointer to the members of the `Singleton` object are guaranteed to see the values initialized in the constructor. This is why this load cannot be `relaxed`—`relaxed` establishes no happens-before relationship, and we might see an object whose memory has been allocated but whose construction is not yet finished.

The second check, `instance_.load(relaxed)`, happens inside the lock—at this point we already hold the mutex, and no other thread can possibly be creating the instance at the same time, so `relaxed` is enough. If `relaxed` makes you uneasy, switching it to `acquire` causes no correctness problem either; it just adds one theoretically unnecessary barrier.

The `release` semantics in `instance_.store(ptr, release)` are the key: they guarantee that `new Singleton()` (including every initialization in the constructor) completes before the store. Combined with the `acquire` load in the first check, this builds a complete release-acquire synchronization pair: every write in the constructor happens-before the store, the store happens-before another thread's acquire load, and that acquire load happens-before the thread's access to the Singleton's members. The chain is complete, with no gaps.

### Why Not Just Use Meyers' Singleton

C++11 guarantees that initialization of a `static` local variable inside a function is thread-safe. So the simplest singleton is actually:

```cpp
class Singleton {
public:
    static Singleton& instance()
    {
        static Singleton inst;
        return inst;
    }
private:
    Singleton() = default;
};
```

This code is completely correct, and the compiler usually implements it internally with `std::call_once` or an equivalent atomic-operation-based scheme. So what is DCLP still good for?

First, the idea behind DCLP is not limited to singletons—any "check, lock, check again, initialize" pattern can use the same line of thinking. For example: lazily initializing a large object, allocating thread-local storage on demand, or deferring the loading of a configuration file. Second, in some extreme performance scenarios, DCLP's first check is lighter than the code generated for a `static` local variable—the latter usually has to check a hidden `std::once_flag`, and that flag's implementation can be heavier than a single atomic load.

## Reference Counting: The Atomic Foundation of shared_ptr

### The Atomic Requirements of Reference Counting

Reference counting is another atomic pattern you can find everywhere. The control block of a `std::shared_ptr` contains a reference count and a weak count, and both of them are atomic variables. Let's look at a simplified reference-counted pointer and understand which atomic operations it needs:

```cpp
#include <atomic>
#include <iostream>

template<typename T>
class IntrusivePtr {
public:
    IntrusivePtr() : ptr_(nullptr) {}

    explicit IntrusivePtr(T* ptr) : ptr_(ptr)
    {
        if (ptr_) {
            ptr_->add_ref();
        }
    }

    IntrusivePtr(const IntrusivePtr& other) : ptr_(other.ptr_)
    {
        if (ptr_) {
            ptr_->add_ref();
        }
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_)
    {
        other.ptr_ = nullptr;
    }

    IntrusivePtr& operator=(const IntrusivePtr& other)
    {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            if (ptr_) {
                ptr_->add_ref();
            }
        }
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept
    {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ~IntrusivePtr()
    {
        release();
    }

    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    T* get() const { return ptr_; }

private:
    void release()
    {
        if (ptr_ && ptr_->release_ref()) {
            delete ptr_;
        }
        ptr_ = nullptr;
    }

    T* ptr_;
};

/// Base class: provides intrusive reference counting
class RefCounted {
public:
    RefCounted() : ref_count_(1) {}
    virtual ~RefCounted() = default;

    void add_ref()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Returns true when the reference count reaches zero, meaning the object should be destroyed
    bool release_ref()
    {
        // acquire guarantees that once the count reaches zero, we can see every modification
        // made to the object by threads that previously add_ref'd it — ensuring consistent state at destruction
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

private:
    std::atomic<int> ref_count_;
};
```

There are two key points about the atomic operations in reference counting. `add_ref()` uses `memory_order_relaxed`—incrementing the reference count needs no synchronization with anything else; we only care about the atomicity of the count itself. Even when thread A's `add_ref` races with thread B's `release_ref`, `fetch_add` and `fetch_sub` are individually atomic, so the count cannot go wrong.

`release_ref()` using `memory_order_acq_rel` is a more finely-tuned choice. The `acquire` semantics guarantee that when the reference count reaches zero, the current thread sees every modification other threads made to the object beforehand (because every object access after an `add_ref` implicitly carries a "holds a reference" relationship). The `release` semantics guarantee that before the object is destroyed, all of the current thread's accesses to the object have completed. Together, these two directions make destruction safe—the destructor sees a fully consistent object state, with no other thread still accessing the object.

## Publish-Subscribe Flags: a relaxed Counter + an acquire-release Flag

### Pattern Description

This is a very practical composite pattern: a `relaxed` atomic counter for statistics (no precise synchronization needed), plus an `acquire-release` atomic flag for notification. The typical scenario is a task queue—worker threads pull tasks from the queue and execute them, bump the counter by 1 for every completed task, and once everything is done, set the flag to notify the main thread.

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

std::atomic<int> tasks_completed{0};
std::atomic<bool> all_done{false};

void worker(int num_tasks)
{
    for (int i = 0; i < num_tasks; ++i) {
        // Simulate task processing
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        tasks_completed.fetch_add(1, std::memory_order_relaxed);
    }
}

int main()
{
    constexpr int kNumWorkers = 4;
    constexpr int kTasksPerWorker = 25;
    constexpr int kTotalTasks = kNumWorkers * kTasksPerWorker;

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumWorkers; ++i) {
        threads.emplace_back(worker, kTasksPerWorker);
    }

    // The main thread waits for all tasks to complete
    while (!all_done.load(std::memory_order_acquire)) {
        std::cout << "Progress: " << tasks_completed.load(std::memory_order_relaxed)
                  << "/" << kTotalTasks << "\n";
        if (tasks_completed.load(std::memory_order_relaxed) >= kTotalTasks) {
            all_done.store(true, std::memory_order_release);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (auto& t : threads) {
        t.join();
    }
    std::cout << "All " << kTotalTasks << " tasks completed!\n";
    return 0;
}
```

The key to this pattern is the separation of concerns. `tasks_completed` is only for displaying progress—it needs no precise synchronization, so `memory_order_relaxed` is enough. Even if the main thread occasionally reads a "stale" count (off by 1 or 2), the user experience is unaffected. `all_done` is the real synchronization point—with `acquire-release` it guarantees that when the main thread sees `all_done == true`, all modifications the worker threads made to shared data are already visible.

This "loose statistics + strict synchronization" combination is extremely common in engineering. One more example: a network server uses a relaxed counter to record the number of processed requests (losing an update now and then doesn't matter), and an acquire-release flag to signal shutdown (which must guarantee that every request has finished processing before shutting down).

## Lock-Free Max/Min Tracking: the CAS Loop

### Pattern Description

Maintaining a global maximum or minimum and updating it lock-free in a multithreaded environment—this is a classic CAS (compare-and-swap) usage pattern. For instance, a network server that wants to track the slowest request latency, or a sensor system that wants to record extreme temperatures.

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <random>
#include <iostream>
#include <cmath>

class MaxTracker {
public:
    explicit MaxTracker(double initial)
        : max_value_(initial)
    {}

    /// Update the maximum if the new value is greater than the current one
    void update(double candidate)
    {
        double current = max_value_.load(std::memory_order_relaxed);
        while (candidate > current) {
            if (max_value_.compare_exchange_weak(
                    current, candidate,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;  // CAS succeeded, update complete
            }
            // CAS failed; current was automatically updated to the latest value, keep looping
        }
    }

    double get() const
    {
        return max_value_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<double> max_value_;
};

int main()
{
    MaxTracker tracker(0.0);
    constexpr int kNumThreads = 4;
    constexpr int kUpdatesPerThread = 100000;

    auto worker = [&](int seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(0.0, 100.0);
        for (int i = 0; i < kUpdatesPerThread; ++i) {
            tracker.update(dist(rng));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back(worker, i + 42);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Max value tracked: " << tracker.get() << "\n";
    return 0;
}
```

The CAS loop is the heart of this pattern. We first load the current maximum; if the candidate is not greater than it, we do nothing and return. If the candidate is larger, we try to replace the current value with the candidate via CAS. The CAS may fail—because another thread may have updated the maximum between our load and our CAS. On failure, `compare_exchange_weak` writes the latest value into `current`, and we compare again to decide whether another attempt is needed.

Using `compare_exchange_weak` rather than `strong` here is a common optimization—inside a loop, the `weak` version's occasional spurious failure just costs one extra iteration, but on certain platforms (ARM and PowerPC in particular, the LL/SC architectures) it is more efficient than `strong`.

The memory orders are all `relaxed`—we only care about the correctness of this single variable (the maximum) itself, and need no synchronization relationship with other variables. If max tracking only feeds statistics or monitoring, no strict happens-before guarantee is needed.

One caveat, though: CAS on `std::atomic<double>` is not lock-free on most platforms—`double` is 64 bits, and CAS on some 32-bit platforms can only handle 32 bits. If your target is a 32-bit embedded platform, this pattern may not be as efficient as you expect. On 64-bit platforms, a 64-bit CAS is usually lock-free.

## The Stop Flag: Using atomic<bool> Correctly

### The Basic Pattern

The stop flag is probably the simplest atomic pattern there is—a background thread periodically checks the flag, and the main thread sets the flag and then waits for the thread to exit. It looks simple, but the details are still worth discussing:

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<bool> should_stop{false};

void background_task()
{
    int count = 0;
    while (!should_stop.load(std::memory_order_acquire)) {
        // Do some work
        ++count;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Task stopped after " << count << " iterations\n";
}

int main()
{
    std::thread t(background_task);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    should_stop.store(true, std::memory_order_release);
    t.join();
    std::cout << "Main: thread joined\n";
    return 0;
}
```

Why `memory_order_acquire` and `memory_order_release` here rather than `relaxed` deserves an explanation. If the background thread also reads some shared data after checking the stop flag (say it reads the latest configuration after its `sleep_for`), then `acquire` guarantees it sees every modification the flag-setting thread made to shared data beforehand. Symmetrically, `release` guarantees that all of the main thread's writes before setting the flag (updating the configuration, for instance) are visible to the background thread.

If your stop flag is purely a Boolean signal—the background thread reads no other shared data—then `relaxed` is safe too. But getting into the habit of using `acquire/release` does no harm, and the performance difference is negligible (on x86 a load is an ordinary read regardless of the memory order, and an acquire load on ARM is just a single `ldar` instruction).

### Low-Latency Stops with atomic_wait

In the previous article we introduced `std::atomic::wait/notify`; here we can upgrade the stop flag to a "wait-style stop"—instead of polling the flag, the background thread blocks waiting on it:

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

std::atomic<bool> should_stop{false};

void waiting_task()
{
    int count = 0;
    while (!should_stop.load(std::memory_order_acquire)) {
        ++count;
        std::cout << "Working... iteration " << count << "\n";

        // Wait 100ms, or until woken by a notify
        should_stop.wait(false, std::memory_order_acquire);
    }
    std::cout << "Task stopped after " << count << " iterations\n";
}

int main()
{
    std::thread t(waiting_task);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    should_stop.store(true, std::memory_order_release);
    should_stop.notify_one();

    t.join();
    std::cout << "Main: thread joined\n";
    return 0;
}
```

In this version, `wait(false)` blocks while `should_stop` is still `false`, consuming no CPU at all. Once the main thread does `store(true) + notify_one()`, the background thread wakes immediately and exits. But there is one problem: `wait` has no timeout—if the background thread needs to do periodic work between two `wait`s (checking a sensor every 100 ms, say), a bare `wait` no longer fits. In that case, a hybrid of `sleep_for` + `notify` is more practical: most of the time, `sleep_for` drives the periodic work, and `notify` wakes the thread when an immediate stop is needed.

## Spinlocks: A Teaching Implementation and When to Use One

### The Basic Implementation

The spinlock is the simplest mutual-exclusion primitive—a thread that fails to acquire it does not block, but retries over and over in a tight loop. It is usually unsuitable for production (we will explain why below), but it is excellent as a teaching tool—because with the least code possible, it demonstrates how `atomic_flag` is used and the basic principles of lock-free synchronization.

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SpinLock {
public:
    SpinLock() : locked_(false) {}

    void lock()
    {
        while (locked_.exchange(true, std::memory_order_acquire)) {
            // exchange returns the old value: if it is true, the lock is already taken, keep spinning
            // If it is false, we have successfully acquired the lock
        }
    }

    void unlock()
    {
        locked_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> locked_;
};

int main()
{
    SpinLock spinlock;
    int counter = 0;

    auto work = [&](int times) {
        for (int i = 0; i < times; ++i) {
            spinlock.lock();
            ++counter;
            spinlock.unlock();
        }
    };

    std::thread t1(work, 1000000);
    std::thread t2(work, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter << "\n";  // 2000000
    return 0;
}
```

The `exchange(true, acquire)` in `lock()` is a clever operation: it atomically sets `locked_` to `true` while returning the value it had before. If the old value is `false`, the lock was not held and we have acquired it. If the old value is `true`, someone else already holds the lock, so we keep looping. The `acquire` semantics guarantee that operations after acquiring the lock are not reordered before the `exchange`—the modifications the other thread made before releasing the lock are visible to this thread.

The `release` semantics in `unlock()` guarantee that all writes inside the critical section complete before the lock is released—the next thread to acquire the lock will see those modifications.

### Why Spinlocks Are Usually Unsuitable for Production

The biggest problem with spinlocks is that they burn CPU while waiting. If the critical section is very short (a few instructions), the cost of spinning may be lower than the context-switch overhead of a mutex. But if the critical section is a bit longer, or several threads are contending for the same lock, a spinlock wastes CPU time on pure "idle spinning". Worse still, on a single-core system a spinlock is completely pointless—the spinning thread hogs the CPU, so the thread holding the lock never gets a chance to run and release it: deadlock.

In real projects, prefer `std::mutex` or `std::shared_mutex`. Only consider a spinlock when all of the following conditions hold at once: the critical section is extremely short (no more than a few dozen instructions), contention is light, and you are running on a multicore system. The Linux kernel uses spinlocks heavily under preemptible-kernel configurations—but the kernel has special scheduling guarantees (disabling preemption) that userspace does not have.

### A Better Version with atomic_flag

The `SpinLock` above is built on `std::atomic<bool>`, but the more canonical approach is `std::atomic_flag`—it is the only atomic type the standard guarantees to be lock-free (`std::atomic<bool>` is theoretically allowed not to be):

```cpp
class SpinLockFlag {
public:
    SpinLockFlag() { flag_.clear(); }

    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // test_and_set atomically sets the flag to true and returns the old value
        }
    }

    void unlock()
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
```

`test_and_set` and `clear` are the two core operations of `atomic_flag`—the former atomically sets the flag to `true` and returns the old value, the latter atomically sets the flag to `false`. This version is semantically identical to the `atomic<bool>` version, but it guarantees lock-free operation.

## A Decision Guide for Choosing a Pattern

With this many patterns in hand, how do you choose one while actually coding? We can decide based on the characteristics of the critical section.

If the critical section is just a simple variable read or update—a counter, a flag, a maximum—`std::atomic`'s RMW operations (`fetch_add`, CAS, and friends) are enough. No mutex, and no spinlock. This is the lightest option, with the best performance. The choice of memory order depends on whether you need synchronization with other variables: if not, `relaxed` will do; if you do, use `acquire/release`.

If the critical section involves coordinated modification of several variables—inserting into a map while updating a counter, say—then `std::atomic` is no longer enough (unless you can pack the variables into a single struct updated via CAS), and you should honestly just use `std::mutex`. A mutex does carry the overhead of context switches, but it guarantees correctness, and under light contention its cost is very low (Linux's `futex` completes entirely in userspace when uncontended).

If reads far outnumber writes, and the data is trivially copyable—SeqLock is a good choice. It keeps readers entirely lock-free, at the cost of an occasional retry. The Linux kernel uses it in many high-frequency-read scenarios.

If you need lazy initialization or a "check, lock, check again" pattern—DCLP has been correct since C++11. But if it is just a singleton, prefer Meyers' Singleton (a `static` local variable): it is simpler and harder to get wrong.

If you need to wait for some condition to hold—use `std::atomic::wait/notify` instead of busy-waiting or a condition_variable. On Linux it is built on futexes, with latency an order of magnitude lower than condition_variable, and it needs no extra mutex.

## Summary

In this article we put every tool from ch03—the `std::atomic` operation set, memory orders, fences, `wait/notify`, `atomic_ref`—to work together across seven classic concurrency patterns.

SeqLock uses the parity of a sequence number to let readers detect write interference lock-free—fitting for "many reads, few writes, trivially copyable data" scenarios. Under the C++11 memory model, Double-Checked Locking finally has a correct, portable implementation—the core is the `acquire` load and `release` store on `std::atomic<T*>`. The reference-counting pattern shows the combination of a `relaxed` `fetch_add` with an `acq_rel` `fetch_sub`—the former cares only about atomicity, the latter must also guarantee visibility at destruction time. The publish-subscribe flag separates loose counting statistics from strict synchronized notification—each takes what it needs, without dragging the other down. Lock-free max/min tracking implements a lock-free "compare and update" via a CAS loop. The stop flag is the simplest atomic pattern, yet combined with `wait/notify` it can also deliver a low-latency stop signal. The spinlock is a classic teaching example; use it with caution in production.

These patterns are not isolated—they are frequently combined. A SeqLock may use a spinlock internally to protect its writer; a DCLP uses an acquire-release synchronization pair inside; the destruction of a reference-counted pointer may trigger a publish-subscribe notification. The real goal is to understand the core idea of each pattern, and then combine them flexibly in concrete scenarios.

In the next article we leave the atomic world of ch03 and move on to a new topic. Before that, though, we suggest working through this article's exercises—in particular the SeqLock and DCLP implementations. They are frequent interview topics, and a touchstone for whether you truly understand memory ordering.

## Exercises

### Exercise 1: Implement a SeqLock

Based on the `SeqLock` class above, write a complete program: one writer thread updates a struct with three `double` fields at 10 ms intervals, while four reader threads each read and print the data at 1 ms intervals. After running for a while, observe whether the readers always obtain consistent data (the three fields all coming from the same write). If inconsistency shows up (say the temperature is the value from write 5 but the humidity is from write 6), check whether you are using `read_begin` / `read_validate` correctly.

### Exercise 2: Implement a DCLP Singleton

Use the DCLP pattern to implement a thread-safe configuration manager. Requirements:

1. Use the classic DCLP structure of `std::atomic<ConfigManager*>` + `std::mutex`
2. Use `memory_order_acquire` and `memory_order_release` correctly inside `instance()`
3. Write a multithreaded test: 8 threads call `ConfigManager::instance()` simultaneously, and verify that they all obtain the same instance

Extra challenge: compare the performance of your DCLP implementation against Meyers' Singleton (a `static` local variable). Use `std::chrono` to measure how long each takes over 1 million calls to `instance()`.

### Exercise 3: A Lock-Free Min Tracker

Implement a `MinTracker` class that tracks the minimum of a `double` with a CAS loop. Then have 4 threads each generate random numbers and call `update()`, and finally verify that `get()` really returns the minimum among all the numbers generated by all threads.

Hint: pay attention to whether atomic operations on floating-point types are lock-free on your current platform. Check with `std::atomic<double>::is_lock_free()`. If it is not lock-free, performance may fall short of expectations.

> 💡 Complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); browse to `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [Double-Checked Locking is Fixed In C++11 — Jeff Preshing](https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11)
- [C++ and the Perils of Double-Checked Locking — Scott Meyers, Andrei Alexandrescu](https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf)
- [Seqlock — Wikipedia](https://en.wikipedia.org/wiki/Seqlock)
- [Linux Kernel seqlock.h — source code](https://github.com/torvalds/linux/blob/master/include/linux/seqlock.h)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [std::shared_ptr thread safety — cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [Preshing on Programming: Atomic vs. Non-Atomic Operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations/)
