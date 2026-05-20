---
title: Atomic Operation Patterns
description: Correct implementation of classic atomic patterns such as SeqLock, double-checked
  locking, reference counting, and publish-subscribe.
chapter: 3
order: 5
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- fence 与编译器屏障
- atomic_wait 与 atomic_ref
related:
- 无锁编程基础
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/05-atomic-patterns.md
  source_hash: a24002aa66dbcff3a2a2245c22295dbf3dad35b01877a7a1b01489a21a372f16
  translated_at: '2026-05-20T04:39:53.283588+00:00'
  engine: anthropic
  token_count: 5359
---
# Atomic Operation Patterns

By this point, we have thoroughly broken down the `std::atomic` operation set, the six memory orders, fences and barriers, `std::atomic_ref`, and `std::atomic_wait`. But taken in isolation, these tools only answer the question of "how"—how to perform an atomic addition, how to issue a release store, or how to wait for a value to change. Real-world engineering practice demands patterns: given a specific concurrency problem, which atomic operations should we choose, and what memory order combinations should we use, to solve the problem both correctly and efficiently?

In this chapter, we focus on several of the most classic atomic operation patterns. These patterns were not invented in a vacuum—they come from battle-tested solutions in real systems like the Linux kernel, database engines, and high-performance networking frameworks. We will break down the "why" behind each pattern: why it is designed this way, why the memory order cannot be weaker, and why a seemingly harmless change can introduce a bug.

The patterns we cover include: SeqLock, Double-Checked Locking, reference counting, publish-subscribe flags, lock-free max/min tracking, stop flags, and spinlocks. Each pattern comes with complete code and a step-by-step semantic analysis.

## SeqLock: Sequence Locking Without Blocking Readers

### Pattern Motivation

A classic solution to the reader-writer problem is the reader-writer lock, but its cost is high—even when there are only read operations, it requires the full flow of `lock()` / `unlock()`, involving atomic operations or even system calls. In many scenarios, reads vastly outnumber writes (such as sensor data collection and reading, or fetching system time), and we want read operations to be as lightweight as possible—ideally, completely lock-free.

SeqLock is designed for exactly this. Its core idea is to use a spinlock to protect the writer (only one writer at a time), but it does not block readers at all—readers check a sequence number to determine whether the data they read is consistent. If the sequence number changes during a read (indicating a writer modified the data), the reader simply retries.

### Implementation

```cpp
#include <atomic>
#include <thread>
#include <iostream>

class SeqLock {
public:
    SeqLock() : sequence_(0) {}

    /// 写入者：获取写入权限
    void lock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // 如果序列号是奇数，说明已经有写入者在工作
        if ((seq & 1u) != 0) {
            // 多写入者场景需要自旋等待或用额外的 mutex
            // 这里假设只有一个写入者
            return;
        }
        // 序列号加 1，变成奇数——标记"正在写入"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// 写入者：释放写入权限
    void unlock_write()
    {
        unsigned seq = sequence_.load(std::memory_order_relaxed);
        // 序列号再加 1，变回偶数——标记"写入完成"
        sequence_.store(seq + 1, std::memory_order_release);
    }

    /// 读取者：在稳定状态下读取数据
    /// 返回读取开始时的序列号；调用者需要在读取后验证序列号是否变化
    unsigned read_begin() const
    {
        unsigned seq;
        for (;;) {
            seq = sequence_.load(std::memory_order_acquire);
            if ((seq & 1u) == 0) {
                // 偶数：没有写入者正在工作
                break;
            }
            // 奇数：有写入者正在工作，自旋等待
            // 实际实现中可以用 pause/yield 减少功耗
        }
        return seq;
    }

    /// 读取者：验证读取期间是否有写入发生
    /// 如果返回 true，说明读取是有效的
    bool read_validate(unsigned seq_before) const
    {
        unsigned seq_after = sequence_.load(std::memory_order_acquire);
        return (seq_after == seq_before) && ((seq_after & 1u) == 0);
    }

private:
    std::atomic<unsigned> sequence_;
};
```

Let's break down the core mechanism of this design.

The parity of the sequence number is key. An even number means "no writer is currently active, and the data is in a consistent state"; an odd number means "a writer is modifying data, and the current state may be inconsistent." The writer changes the sequence number from even to odd at the start, and back to even when finished—each successful write increments the sequence number by two.

The reader's strategy is "pre-read check + post-read verification": first read the sequence number and confirm it is even (no writer active), then read the actual data, and finally read the sequence number again. If the sequence numbers before and after are the same and both are even, it means no writer intervened during the read, and the data is consistent. If they differ (or if it became odd), it means a write occurred during the read, and the data may be inconsistent—the reader simply discards the result and retries.

The `release` in ``memory_order_release`` and the `acquire` in ``memory_order_acquire`` / ``read_validate()`` establish a happens-before relationship: all of the writer's modifications to the actual data complete before ``sequence_`` changes back to even (release guarantees prior writes are not reordered after the store); the reader sees the data only after ``sequence_`` becomes even (acquire guarantees subsequent reads are not reordered before the load). This ensures that the data read by the reader is strictly the version fully written by the writer.

### Usage Example

```cpp
struct SensorData {
    double temperature;
    double humidity;
    double pressure;
};

SensorData g_sensor_data;
SeqLock g_seq_lock;

// 写入者线程（通常是传感器采集线程）
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

// 读取者线程（可以有多个）
void reader_thread(int id)
{
    for (int i = 0; i < 100; ++i) {
        SensorData local;
        unsigned seq;

        do {
            seq = g_seq_lock.read_begin();
            local = g_sensor_data;  // 拷贝数据
        } while (!g_seq_lock.read_validate(seq));

        // 现在可以安全地使用 local——它是一个一致的快照
        std::cout << "Reader " << id << ": temp=" << local.temperature
                  << " humidity=" << local.humidity
                  << " pressure=" << local.pressure << "\n";
    }
}
```

Note that the reader copies the data into a ``local`` variable before verifying. This is a crucial detail—if we use the data directly without copying, it is already "dirty" when verification fails, and we can neither use it nor retry. SeqLock readers must be prepared to discard read results at any time, so the read data must either be read-only (use and discard) or copied out before use.

### Applicability Boundaries of SeqLock

There are a few limitations of SeqLock we need to clearly understand. First, it assumes at most one writer—if multiple writers are needed, a mutex must be wrapped around the outside. Second, the data type being read must be trivially copyable—if the data contains pointers or complex objects, encountering a partially modified state during the copy could lead to undefined behavior. Third, if writes are very frequent, readers may retry repeatedly, and performance could actually be worse than a reader-writer lock—SeqLock is suited for "few writes, many reads" scenarios. The ``seqlock_t`` in the Linux kernel is a classic implementation of this pattern, used for scenarios like time fetching (``do_gettimeofday``).

## Double-Checked Locking: Finally Correct Since C++11

### Pattern Motivation and Historical Baggage

The Double-Checked Locking Pattern (DCLP) is arguably one of the most discussed patterns in multithreaded programming—not because it is the best pattern, but because it was simply impossible to implement correctly before C++11. In their 2004 paper "C++ and the Perils of Double-Checked Locking," Scott Meyers and Andrei Alexandrescu analyzed in detail why it fails under the old standard. There are two core reasons: compilers can reorder memory operations (writing an object's fields might be reordered after publishing the pointer), and the CPU itself might also reorder (relatively restricted on x86, but very aggressive on ARM/PowerPC).

The formal memory model and ``std::atomic`` introduced in C++11 finally gave DCLP a portable, correct implementation.

### Correct DCLP Implementation

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

Let's break down the role of each check in this implementation.

The first check, ``instance_.load(acquire)``, happens outside the lock—if the instance is already created (the vast majority of calls take this path), it returns the pointer directly without needing to lock. ``memory_order_acquire`` guarantees that subsequent accesses to the ``Singleton`` object's members through this pointer will definitely see the values initialized in the constructor. This is why this load cannot use ``relaxed``—``relaxed`` does not establish a happens-before relationship, and we might see an object that has been allocated but not yet fully constructed.

The second check, ``instance_.load(relaxed)``, happens inside the lock—at this point we hold the mutex, so no other thread can be creating the instance simultaneously, making ``relaxed`` sufficient. If you feel uneasy about ``relaxed``, switching to ``acquire`` would not cause correctness issues; it just theoretically adds an unnecessary barrier.

The ``release`` semantics in ``instance_.store(ptr, release)`` are key: it guarantees that ``new Singleton()`` (including all initialization operations in the constructor) completes before the store. Combined with the ``acquire`` load in the first check, this establishes a complete release-acquire synchronization pair: all writes from the constructor happen-before the store, the store happens-before another thread's acquire load, and the acquire load happens-before that thread's access to the Singleton's members. The chain is complete, with no gaps.

### Why Not Just Use Meyers' Singleton

C++11 guarantees that the initialization of ``static`` local variables inside a function is thread-safe. So the simplest singleton pattern is actually:

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

This code is completely correct, and compilers typically implement it internally using ``std::call_once`` or equivalent atomic operations. So what is the point of DCLP?

First, the idea behind DCLP is not limited to singletons—any "check-lock-recheck-initialize" pattern can use this approach. Examples include lazily initializing a large object, allocating thread-local storage on demand, or lazily loading a configuration file. Second, in some extreme performance scenarios, the first check of DCLP generates lighter code than the ``static`` local variable—the latter usually requires checking a hidden ``std::once_flag``, and the implementation of this flag might be heavier than a single ``atomic load``.

## Reference Counting: The Atomic Foundation of shared_ptr

### Atomic Requirements of Reference Counting

Reference counting is another ubiquitous atomic pattern. The control block inside ``std::shared_ptr`` contains a reference count and a weak reference count, both of which are atomic variables. Let's look at a simplified reference-counted pointer to understand what atomic operations it needs:

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

/// 基类：提供侵入式引用计数
class RefCounted {
public:
    RefCounted() : ref_count_(1) {}
    virtual ~RefCounted() = default;

    void add_ref()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// 返回 true 表示引用计数归零，应该销毁对象
    bool release_ref()
    {
        // acquire 保证在引用计数归零后，能看到所有之前 add_ref 的线程
        // 对对象的全部修改——确保析构时对象状态一致
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

private:
    std::atomic<int> ref_count_;
};
```

There are two key points regarding the atomic operations in reference counting. ``add_ref()`` uses ``memory_order_relaxed``—incrementing the reference count does not need to synchronize with other operations; we only care about the atomicity of the count itself. Even if thread A's ``add_ref`` and thread B's ``release_ref`` race, ``fetch_add`` and ``fetch_sub`` are themselves atomic and will not cause count errors.

Using ``memory_order_acq_rel`` for ``release_ref()`` is a more nuanced choice. ``acquire`` semantics guarantee that when the reference count reaches zero, the current thread can see all modifications made to the object by other threads prior to that point (because every object access after a ``add_ref`` implicitly carries a "holding a reference" relationship). ``release`` semantics guarantee that before destructing the object, all accesses to the object by the current thread have completed. Together, these two directions ensure the safety of destruction—the destructor sees a fully consistent object state, with no other threads still accessing the object.

## Publish-Subscribe Flag: Relaxed Counter + Acquire-Release Flag

### Pattern Description

This is a highly practical combination pattern: a ``relaxed`` atomic counter for statistics (not requiring precise synchronization), plus an ``acquire-release`` atomic flag for notification. A typical scenario is a task queue—worker threads fetch and execute tasks from the queue, increment the counter after completing each task, and set the flag to notify the main thread when all are done.

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
        // 模拟任务处理
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

    // 主线程等待所有任务完成
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

The key to this pattern is the separation of concerns. ``tasks_completed`` is only used for displaying progress—it does not need precise synchronization, so ``memory_order_relaxed`` is sufficient. Even if the main thread occasionally reads a "stale" count (off by one or two), it has no impact on user experience. ``all_done`` is the true synchronization point—it uses ``acquire-release`` to guarantee that when the main thread sees ``all_done == true``, all modifications to shared data by worker threads are already visible.

This combination of "relaxed statistics + strict synchronization" is very common in engineering. As another example: a network server uses a relaxed counter to record the number of processed requests (losing an update or two occasionally does not matter), and uses an acquire-release flag to notify a shutdown signal (which must guarantee all request processing is complete before shutting down).

## Lock-Free Max/Min Tracking: CAS Loop

### Pattern Description

Maintaining a global maximum or minimum value and updating it lock-free in a multithreaded environment is a classic CAS (compare-and-swap) usage pattern. For example, a network server might want to track the slowest request latency, or a sensor system might want to record extreme temperatures.

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

    /// 如果新值大于当前最大值，更新最大值
    void update(double candidate)
    {
        double current = max_value_.load(std::memory_order_relaxed);
        while (candidate > current) {
            if (max_value_.compare_exchange_weak(
                    current, candidate,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;  // CAS 成功，更新完成
            }
            // CAS 失败，current 被自动更新为当前值，继续循环
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

The CAS loop is the core of this pattern. We first load the current maximum value; if the candidate value is not greater than the current value, we do nothing and return directly. If the candidate value is larger, we try to use CAS to replace the current value with the candidate. CAS might fail—because another thread might have already updated the maximum between our load and CAS. On failure, ``compare_exchange_weak`` updates ``current`` to the latest value, and we recompare to decide whether to try again.

Using ``compare_exchange_weak`` instead of ``strong`` here is a common optimization—in a loop, the occasional spurious failure of the ``weak`` version just means one extra iteration, but it is more efficient than ``strong`` on certain platforms (especially LL/SC architectures like ARM and PowerPC).

All memory orders use ``relaxed``—because we only care about the correctness of the single variable (the maximum value) itself, and do not need to establish synchronization relationships with other variables. This holds if the max tracking is only used for statistics or monitoring and does not require strict happens-before guarantees.

However, note that CAS operations on ``std::atomic<double>`` are not lock-free on most platforms—because ``double`` is 64-bit, while CAS on some 32-bit platforms can only handle 32 bits. If your target is a 32-bit embedded platform, this pattern might not be as efficient as expected. On 64-bit platforms, 64-bit CAS is usually lock-free.

## Stop Flag: The Correct Usage of atomic<bool>

### Basic Pattern

The stop flag is perhaps the simplest atomic pattern—a background thread periodically checks the flag, and the main thread sets the flag and waits for the thread to exit. It looks simple, but there are details worth discussing:

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
        // 做一些工作
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

Using ``memory_order_acquire`` and ``memory_order_release`` instead of ``relaxed`` here has reasons worth explaining. If the background thread reads some shared data after checking the stop flag (for example, reading the latest configuration after ``sleep_for``), then ``acquire`` guarantees it can see all modifications to the shared data made by the thread setting the flag prior to that point. Similarly, ``release`` guarantees that all writes by the main thread before setting the flag (such as updating the configuration) are visible to the background thread.

If your stop flag is purely a boolean signal—the background thread does not need to read any other shared data—then ``relaxed`` is also safe. But there is no harm in making a habit of using ``acquire/release``, and the performance difference is negligible (on x86, a load is a normal read regardless of memory order; on ARM, an acquire load is just a single ``ldar`` instruction).

### Implementing Low-Latency Stopping with atomic_wait

In the previous chapter, we introduced ``std::atomic::wait/notify``. Here we can upgrade the stop flag to a "wait-based stop"—the background thread blocks and waits on the flag instead of polling it:

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

        // 等待 100ms 或被 notify 唤醒
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

In this version, ``wait(false)`` blocks while ``should_stop`` is still ``false``, consuming no CPU at all. When the main thread executes ``store(true) + notify_one()``, the background thread is woken up immediately and exits. But there is a problem: ``wait`` has no timeout—if the background thread needs to do some work periodically between ``wait`` calls (such as checking a sensor every 100 ms), a pure ``wait`` is not appropriate. In this case, a hybrid approach combining ``sleep_for`` + ``notify`` is more practical: use ``sleep_for`` for periodic work most of the time, and use ``notify`` to wake the thread when immediate stopping is needed.

## Spinlock: Educational Implementation and Applicable Scenarios

### Basic Implementation

The spinlock is the simplest mutual exclusion primitive—a thread that fails to acquire the lock does not block, but repeatedly tries in a tight loop. It is usually not suitable for production environments (we will explain why later), but it is an excellent teaching tool—because it demonstrates the usage of ``atomic_flag`` and the basic principles of lock-free synchronization with the minimum amount of code.

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
            // exchange 返回旧值：如果是 true，说明锁已经被占用，继续自旋
            // 如果是 false，说明我们成功获取了锁
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

The ``exchange(true, acquire)`` in ``lock()`` is a clever operation: it atomically sets ``locked_`` to ``true`` while returning the previous value. If the old value is ``false``, it means the lock was not previously held, and we successfully acquired it. If the old value is ``true``, it means the lock is already held by someone else, and we continue looping. ``acquire`` semantics guarantee that operations after acquiring the lock are not reordered before ``exchange``—modifications made by other threads before releasing the lock are visible to the current thread.

The ``release`` semantics in ``unlock()`` guarantee that all writes inside the critical section complete before releasing the lock—the next thread to acquire the lock will see these modifications.

### Why Spinlocks Are Usually Unsuitable for Production

The biggest problem with spinlocks is that they consume CPU while waiting. If the critical section is very short (a few instructions), the overhead of spin-waiting might be lower than the context switch overhead of a mutex. But if the critical section is slightly longer, or if multiple threads are competing for the same lock, spinlocks cause CPU time to be heavily wasted on "spinning." Even worse, on single-core systems, spinlocks are completely pointless—the thread occupies the CPU while spinning, and the thread holding the lock has no chance to run and release it, resulting in a dead lock.

In real projects, prefer using ``std::mutex`` or ``std::shared_mutex``. Only consider a spinlock when all of the following conditions are met simultaneously: the critical section is extremely short (no more than a few dozen instructions), contention is low, and it runs on a multi-core system. The Linux kernel makes extensive use of spinlocks in preemptible kernels—but the kernel has special scheduling guarantees (disabling preemption), which user space does not have.

### A Better Version Using atomic_flag

The ``SpinLock`` above is implemented using ``std::atomic<bool>``, but a more canonical approach is to use ``std::atomic_flag``—it is the only atomic type guaranteed by the standard to be lock-free (``std::atomic<bool>`` might theoretically not be lock-free):

```cpp
class SpinLockFlag {
public:
    SpinLockFlag() { flag_.clear(); }

    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // test_and_set 原子地设置 flag 为 true 并返回旧值
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

``test_and_set`` and ``clear`` are the two core operations of ``atomic_flag``—the former atomically sets the flag to ``true`` and returns the old value, while the latter atomically sets the flag to ``false``. This version is semantically completely equivalent to the ``atomic<bool>`` version, but guarantees lock-free behavior.

## Decision Guide for Pattern Selection

Having learned about so many patterns, how do we choose when actually coding? We can make decisions based on the characteristics of the critical section.

If the critical section is just a simple variable read or update—such as a counter, a flag, or a maximum value—directly using ``std::atomic`` RMW operations (``fetch_add``, CAS, etc.) is sufficient. No mutex or spinlock is needed. This is the lightest choice and offers the best performance. The choice of memory order depends on whether synchronization with other variables is needed: if not, ``relaxed`` is fine; if yes, use ``acquire/release``.

If the critical section involves coordinated modifications to multiple variables—such as inserting an element into a map while updating a counter—then ``std::atomic`` is no longer enough (unless you can pack multiple variables into a single struct updated via CAS), and you should honestly use a ``std::mutex``. Although a mutex has context switch overhead, it guarantees correctness, and the overhead is very low when contention is low (Linux's ``futex`` completes entirely in user space when uncontended).

If reads vastly outnumber writes, and the data is trivially copyable—SeqLock is a good choice. It keeps readers completely lock-free, at the cost of only occasional retries. The Linux kernel uses it in many high-frequency read scenarios.

If you need lazy initialization or a "check-lock-recheck" pattern—DCLP has been correct since C++11. But if it is just a singleton, prefer Meyers' Singleton (``static`` local variable), as it is simpler and less error-prone.

If you need to wait for a condition to be met—use ``std::atomic::wait/notify`` instead of busy-waiting or `condition_variable`. It uses futex on Linux, its latency is an order of magnitude lower than `condition_variable`, and it does not require an additional mutex.

## Summary

In this chapter, we applied all the tools learned in ch03—the ``std::atomic`` operation set, memory orders, fences, ``wait/notify``, and ``atomic_ref``—comprehensively across seven classic concurrency patterns.

SeqLock uses the parity of a sequence number to allow readers to lock-free detect write interference, suited for "many reads, few writes, trivially copyable data" scenarios. Double-Checked Locking finally has a correct, portable implementation under the C++11 memory model—the core is the ``acquire`` load and ``release`` store of ``std::atomic<T*>``. The reference counting pattern demonstrates the combination of ``fetch_add`` with ``relaxed`` and ``fetch_sub`` with ``acq_rel``—the former only cares about atomicity, while the latter also guarantees visibility at destruction time. The publish-subscribe flag separates relaxed count statistics from strict synchronization notifications—each gets what it needs without dragging the other down. Lock-free max/min tracking uses a CAS loop to implement a lock-free "compare and update." The stop flag is the simplest atomic pattern, but combined with ``wait/notify``, it can also achieve low-latency stop signals. The spinlock is a classic teaching example, but should be used cautiously in production environments.

These patterns are not isolated—they are often used in combination. A SeqLock might use a spinlock internally to protect writers; a DCLP uses an acquire-release synchronization pair internally; the destruction of a reference-counted pointer might trigger a publish-subscribe notification. Understanding the core idea of each pattern and flexibly combining them in specific scenarios is the real goal.

In the next chapter, we leave the atomic world of ch03 and move on to a new topic. But before that, we recommend completing the exercises in this chapter—especially the implementations of SeqLock and DCLP, as they are high-frequency interview topics and the litmus test for whether you truly understand memory orders.

## Exercises

### Exercise 1: Implement a SeqLock

Based on the ``SeqLock`` class above, write a complete program: one writer thread updates a struct containing three ``double`` fields at 10 ms intervals, and four reader threads each read and print the data at 1 ms intervals. After running for a while, observe whether the readers always obtain consistent data (the values of all three fields come from the same write). If the data is inconsistent (for example, the temperature is from the 5th write but the humidity is from the 6th), check whether your ``read_begin`` / ``read_validate`` are used correctly.

### Exercise 2: Implement a DCLP Singleton

Use the DCLP pattern to implement a thread-safe configuration manager. Requirements:

1. Use the classic DCLP structure of ``std::atomic<ConfigManager*>`` + ``std::mutex``
2. Correctly use ``memory_order_acquire`` and ``memory_order_release`` in ``instance()``
3. Write a multithreaded test: 8 threads simultaneously call ``ConfigManager::instance()``, verifying that all threads receive the same instance

Extra challenge: compare the performance difference between your DCLP implementation and a Meyers' Singleton (``static`` local variable) implementation. Use ``std::chrono`` to measure the time taken by both implementations under 1 million ``instance()`` calls.

### Exercise 3: Lock-Free Minimum Tracker

Implement a ``MinTracker`` class that uses a CAS loop to track a minimum value of ``double`` type. Then use 4 threads to each generate random numbers and call ``update()``, and finally verify that ``get()`` indeed returns the minimum value among all numbers generated by the threads.

Hint: you need to be aware of whether atomic operations on floating-point numbers are lock-free on your current platform. Check with ``std::atomic<double>::is_lock_free()``. If it is not lock-free, performance might not be as expected.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit ``code/volumn_codes/vol5/ch03-atomic-memory-model/``.

## References

- [Double-Checked Locking is Fixed In C++11 — Jeff Preshing](https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11)
- [C++ and the Perils of Double-Checked Locking — Scott Meyers, Andrei Alexandrescu](https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf)
- [Seqlock — Wikipedia](https://en.wikipedia.org/wiki/Seqlock)
- [Linux Kernel seqlock.h — source code](https://github.com/torvalds/linux/blob/master/include/linux/seqlock.h)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [std::shared_ptr thread safety — cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [Preshing on Programming: Atomic vs. Non-Atomic Operations](https://preshing.com/20130618/atomic-vs-non-atomic-operations/)
