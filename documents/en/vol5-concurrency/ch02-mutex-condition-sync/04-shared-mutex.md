---
chapter: 2
cpp_standard:
- 17
- 20
description: C++17 `shared_mutex` applications in read-heavy, write-light scenarios,
  analyzing write starvation issues and performance boundaries
difficulty: intermediate
order: 4
platform: host
prerequisites:
- condition_variable 与等待语义
reading_time_minutes: 14
related:
- mutex 与 RAII 锁
- 线程安全容器设计
tags:
- host
- cpp-modern
- intermediate
- mutex
title: Read-Write Locks and shared_mutex
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/04-shared-mutex.md
  source_hash: 9b89015a3c8b2083856030f60c54f61b1695479e3d09b86dae1ec870e0149aa7
  token_count: 2330
  translated_at: '2026-05-20T04:36:39.482406+00:00'
---
# Reader-Writer Locks and shared_mutex

So far, the synchronization primitives we have discussed are all "exclusive"—one thread acquires the lock, and all other threads must wait outside. But in reality, a large class of scenarios does not fit this pattern: **read-heavy, write-light** workloads. Configuration data, caches, routing tables, and dictionaries are read most of the time and only occasionally updated. If every read requires exclusive access to a mutex, multiple reader threads are unnecessarily serialized—they could perfectly well read the same data structure concurrently, since read operations do not modify any state.

Reader-writer locks exist to solve this problem. They distinguish between two locking modes: **shared mode (shared / read lock)** and **exclusive mode (exclusive / write lock)**. Multiple threads can hold a read lock simultaneously for reading, but a write lock requires exclusive access—no other thread (whether reading or writing) can hold the lock at the same time. The `std::shared_mutex` introduced in C++17 is the standard library's implementation of a reader-writer lock.

## std::shared_mutex: Two Locking Modes

`std::shared_mutex` is defined in the `<shared_mutex>` header (available since C++17). It provides two sets of locking interfaces: the write lock's `lock()` / `unlock()` / `try_lock()` (just like a regular `std::mutex`), and the shared lock's `lock_shared()` / `unlock_shared()` / `try_lock_shared()`. Calling these raw interfaces directly is of course possible, but we will not do that—RAII wrappers are the right approach, and we should not let the lessons from the previous chapter go to waste.

Let us first look at a basic use case. Suppose we have a configuration dictionary that is occasionally updated and frequently queried:

```cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <vector>

class ThreadSafeConfig {
public:
    std::string get(const std::string& key) const
    {
        // 读操作：获取共享锁
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : "";
    }

    void set(const std::string& key, const std::string& value)
    {
        // 写操作：获取独占锁
        std::unique_lock<std::shared_mutex> lock(mutex_);
        data_[key] = value;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};
```

The `get` method uses `std::shared_lock<std::shared_mutex>` to acquire a shared lock. Multiple threads can hold a `shared_lock` simultaneously—they do not block each other. The `set` method uses `std::unique_lock<std::shared_mutex>` to acquire an exclusive lock. When any thread holds an exclusive lock, all other threads (whether requesting a shared or exclusive lock) must wait; conversely, if threads hold shared locks, a thread wanting an exclusive lock must wait until all shared locks are released.

Note that `mutex_` is declared `mutable`—because `get` is a `const` member function, but it needs to modify the mutex's state (locking/unlocking). This is a legitimate use of `mutable`: the mutex is not part of the object's logical state; it is part of the synchronization mechanism.

## std::shared_lock: The RAII Wrapper for Shared Mode

`std::shared_lock` is the "shared version" of `std::unique_lock`, defined in the `<shared_mutex>` header. Its interface is highly symmetric with `unique_lock`—it acquires a shared lock on construction, releases it on destruction, and supports deferred locking (`defer_lock`), manual locking/unlocking, and so on. However, it calls `lock_shared()` / `unlock_shared()` instead of `lock()` / `unlock()`.

Why do we need a separate `shared_lock` instead of adding a parameter to `unique_lock` to control the mode? The reason is type safety. If you have a function that accepts a `std::unique_lock<SharedMutex>` parameter, you can be certain it holds an exclusive lock—the compiler guarantees this for you. Conversely, `std::shared_lock<SharedMutex>` guarantees that a shared lock is held. The semantics of the two locking modes are completely different, and expressing them with different types is the safest approach.

A usage worth knowing about is `shared_lock` combined with `condition_variable_any` (the generic condition variable mentioned in the previous chapter) to implement "shared waiting." A regular `condition_variable` only accepts a `unique_lock`, but `condition_variable_any` accepts any lock type—including `shared_lock`. This allows you to wait on a condition variable while holding a shared lock, a capability used by certain advanced patterns (such as reader-writer lock upgrade protocols).

## The Complete Pattern: shared_lock for Reading, unique_lock for Writing

The standard usage of reader-writer locks can be summarized in one sentence: **shared_lock for reading, unique_lock for writing**. Let us look at a more complete example—a simple thread-safe cache:

```cpp
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <optional>
#include <functional>
#include <mutex>

template <typename Key, typename Value>
class ThreadSafeCache {
public:
    /// @brief 查询缓存，命中则返回值，未命中则计算并存入
    Value get_or_compute(const Key& key,
                          std::function<Value(const Key&)> compute)
    {
        // 第一步：读锁下查询
        {
            std::shared_lock<std::shared_mutex> read_lock(mutex_);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        // 第二步：锁外计算（避免持锁期间做重活）
        Value value = compute(key);

        // 第三步：写锁下 double-check 并写入
        {
            std::unique_lock<std::shared_mutex> write_lock(mutex_);
            // double-check：另一个线程可能在我们释放读锁到获取写锁之间已经插入了
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
            cache_[key] = value;
        }

        return value;
    }

    /// @brief 清空缓存
    void clear()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.clear();
    }

    /// @brief 获取缓存大小
    std::size_t size() const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<Key, Value> cache_;
};
```

This code demonstrates a very important pattern—**double-checked locking**. Why do we read again before acquiring the write lock? Because there is a time window between releasing the first read lock and acquiring the write lock, during which another thread might have already inserted the same key. Without this double-check, we might overwrite another thread's computation result, or even waste resources on duplicate computations.

Another point worth noting is that `compute(key)` executes **outside the write lock**. This is an intentional design—computation can be time-consuming, and if we compute while holding the write lock, all reader threads will be blocked. Moving the computation outside the lock and only acquiring the write lock for the final write maximizes concurrency. Of course, the trade-off is potential duplicate computation—multiple threads might simultaneously execute compute for the same key. If your compute is very expensive and you need to guarantee uniqueness, you might need to perform the computation inside the write lock, sacrificing concurrency for correctness.

## Writer Starvation: The Dark Side of Reader-Writer Locks

Reader-writer locks look great—reads do not block reads, and writes block everything. But there is a hidden problem here: **writer starvation**. Imagine this scenario: ten reader threads continuously request shared locks, coming and going, with a few always reading at any given moment. Now a writer thread wants to acquire an exclusive lock—it must wait until **all** shared locks are released. The problem is, if reader threads arrive at a high enough frequency, the shared locks might never all be released at the same time—a new read request always comes in before the old ones finish. The writer thread gets "starved" this way, forever waiting for a chance at exclusive access.

The C++ standard makes **no guarantees** about the scheduling policy of `std::shared_mutex`—it does not guarantee fairness, it does not guarantee writer priority, and it does not guarantee that readers will not starve writers. The specific scheduling behavior depends on the standard library implementation and the underlying operating system. On some platforms (such as Windows' SRWLock), the implementation tends to favor writers—when a writer is waiting, new readers are blocked until the writer finishes. But on other platforms, readers might continuously acquire shared locks, causing the writer to wait for a long time.

What does this mean for you? If you use `std::shared_mutex`, you need to be aware of the possibility of writer starvation and evaluate whether it poses a problem for your application. If your scenario is "reads far outnumber writes, and write latency is not sensitive," then the benefits of a reader-writer lock far outweigh the risks. But if write timeliness is important (such as parameter updates in a real-time control system), a reader-writer lock might not be the best choice—you may need a custom reader-writer lock with writer-priority guarantees, or simply use a regular `std::mutex` with a copy-on-write strategy.

## Performance Boundaries: When Reader-Writer Locks Are Actually Slower

This section might surprise some people: **reader-writer locks are not a silver bullet; in certain scenarios, they are slower than a regular mutex**. The reason is that the internal implementation of a reader-writer lock is much more complex than a mutex—it needs to maintain a reader count, manage wait queues, and handle priorities between reads and writes. This extra management overhead means that even in low-contention scenarios, each lock/unlock operation of a reader-writer lock is more expensive than that of a mutex.

So where is the crossover point? According to some benchmarks (such as a 2025 comparison study using Google Benchmark), in low-thread-count scenarios (two to four threads), `std::mutex` is usually faster than `std::shared_mutex`—because contention is low at this point, and the simplicity of the mutex wins out. When the thread count increases and read operations dominate (for example, eight reader threads and one writer thread), `shared_mutex` starts to show its advantage—multiple reader threads can execute concurrently, significantly improving throughput. The more threads there are, and the higher the read-to-write ratio, the more pronounced the advantage of reader-writer locks becomes.

Several other factors affect the performance of reader-writer locks. First is the size of the critical section—if the critical section is very short (such as reading a single `int`), the overhead of a mutex is about the same as that of a reader-writer lock, and the reader-writer lock's extra management cost actually drags performance down. But if the critical section is long (such as traversing a large map or performing a complex query), the benefit of allowing concurrent reads with a reader-writer lock becomes substantial. Second is the impact of hardware caching—the reader-writer lock's reader counter is a shared atomic variable, which in a multi-core environment can cause cache line bouncing (multiple cores frequently competing for ownership of the same cache line), potentially offsetting the gains of concurrent reads during high-frequency reads.

In real-world projects, my recommendation is: start with `std::mutex`, and only consider switching to `std::shared_mutex` if you have a clear performance bottleneck characterized by "read-heavy, write-light + high-concurrency reads." Before switching, it is best to run a benchmark using your actual workload for comparison, because the crossover point depends on the specific data structures, access patterns, and hardware environment. Premature optimization is the root of all evil, and this applies equally to the choice of synchronization primitives.

## std::shared_timed_mutex: The Version with Timeouts

C++14 introduced `std::shared_timed_mutex`, which is the timeout-capable version of `std::shared_mutex`—in addition to basic shared/exclusive locking, it supports timeout operations such as `try_lock_for`, `try_lock_until`, `try_lock_shared_for`, and `try_lock_shared_until`. C++17's `std::shared_mutex` removes the timeout functionality, becoming a lighter-weight version.

If your project is still on C++14, `shared_timed_mutex` is your only option. If you are on C++17 or later and do not need timeout functionality, prefer using `std::shared_mutex`—its implementation is simpler and its overhead is lower. Scenarios that require timeout functionality are similar to the `wait_for` / `wait_until` discussed in the previous chapter—such as "try to acquire the write lock within 100 ms, and give up on this update if it times out."

## Lock Upgrades and Downgrades: Advanced Operations Not Directly Supported by the Standard

A lock upgrade means converting a shared lock to an exclusive lock—for example, you read the data first, find that it needs modification, and then upgrade to a write lock without releasing the lock. A lock downgrade is the reverse—converting an exclusive lock to a shared lock. These two operations are very common in some database systems (such as transaction lock management), but the C++ standard library **does not directly support** them.

Why? Because lock upgrades can cause deadlocks in a multi-threaded environment. Consider this scenario: Thread A holds a shared lock and tries to upgrade to an exclusive lock, while Thread B also holds a shared lock and tries to upgrade to an exclusive lock—both are waiting for the other to release its shared lock, but neither will release first. Deadlock. This is the so-called "upgrade deadlock."

The standard library's approach requires you to **release the shared lock first, then acquire the exclusive lock**. This guarantees a "lock-free" gap between shared and exclusive modes, during which other threads are free to acquire locks. The trade-off is that you need to handle state changes during this gap—this is exactly where the double-checked locking pattern comes into play.

```cpp
// 锁升级的手动实现：释放共享锁 -> 获取独占锁
void upgrade_example()
{
    // 读取阶段
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    auto data = read_something();
    read_lock.unlock();  // 必须先释放共享锁

    // 写入阶段
    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    // 注意：这里的 data 可能已经过期了！
    // 需要重新读取或做 double-check
    write_something(data);
}
```

Lock downgrades (exclusive -> shared) are safe—downgrading from exclusive to shared does not cause deadlocks, because downgrading only releases permissions and does not request additional ones. However, the standard library does not directly support this either; you need to manually release the exclusive lock and then acquire the shared lock. Some platform-specific APIs (such as Windows' SRWLock) provide atomic downgrade operations, but POSIX `pthread_rwlock` and the C++ standard library lack this capability—under POSIX, the only way is to unlock first and then rdlock, with a lock-free window in between. If your scenario requires frequent lock downgrades, you may need to consider using platform-specific APIs or a custom reader-writer lock implementation.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Thread-Safe Cache

Implement a template class `ThreadSafeCache<Key, Value>` that supports the following operations:

- `get(key)`: Query the cache, returning `std::optional<Value>`
- `put(key, value)`: Insert or update
- `remove(key)`: Delete
- `size()`: Return the current cache size

Requirements: Use `std::shared_mutex`. Read operations (`get`, `size`) must use `shared_lock`, and write operations (`put`, `remove`) must use `unique_lock`.

Then write a test program: four reader threads continuously query random keys, and one writer thread inserts new data at regular intervals. Observe whether reads and writes can proceed concurrently (you can add a tiny delay in read operations to amplify the concurrency effect).

### Exercise 2: Comparing the Performance of mutex and shared_mutex

Write a benchmark: protect the same `std::unordered_map<int, std::string>` with `std::mutex` and `std::shared_mutex` respectively, and then execute 90% read operations + 10% write operations under multiple threads. Increase the thread count from one to 16, and record the total time for each configuration.

Consider the following questions:

- On your platform, where is the crossover point in terms of thread count?
- What happens if you change the read-to-write ratio from 90:10 to 50:50?
- What happens if the critical section is very short (reading only a single int)?

### Exercise 3: Reproducing Writer Starvation

Construct a scenario to observe writer starvation: launch N reader threads, where each thread loops acquiring a shared lock, reading data, and releasing the lock (you can add a tiny delay to control the reading frequency). Then launch one writer thread that tries to acquire an exclusive lock to update the data. Measure the writer thread's wait time from requesting the lock to acquiring it. Gradually increase the number of reader threads and the reading frequency, and observe how the writer thread's wait time changes.

Hint: You might find that under extreme read-to-write ratios (such as 20 reader threads reading frantically), the writer thread's wait time increases dramatically. This is the intuitive manifestation of writer starvation.

## References

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::shared_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_lock)
- [std::shared_timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)
- [When std::shared_mutex Outperforms std::mutex -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [Understanding std::shared_mutex from C++17 -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
