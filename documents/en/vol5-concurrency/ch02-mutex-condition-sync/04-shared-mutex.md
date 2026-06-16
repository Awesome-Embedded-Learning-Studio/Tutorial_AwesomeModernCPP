---
chapter: 2
cpp_standard:
- 17
- 20
description: 'C++17 shared_mutex for read-heavy workloads: analyzing write starvation
  and performance boundaries'
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
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/04-shared-mutex.md
  source_hash: dc967b71909e81673517aa23da4c887f1ed01f761ea616517c31c3b20cbd2e21
  translated_at: '2026-06-16T04:03:42.342665+00:00'
  engine: anthropic
  token_count: 2324
---
# Reader-Writer Locks and shared_mutex

So far, the synchronization primitives we have discussed are "exclusive"—one thread acquires the lock, and all other threads must wait. However, a large category of real-world scenarios doesn't fit this model: **read-heavy workloads**. Configuration data, caches, routing tables, and dictionaries are read most of the time and updated only occasionally. If every read requires exclusive access to a mutex, multiple reader threads are unnecessarily serialized—they could perfectly well read the same data structure concurrently, as read operations do not modify any state.

Reader-writer locks exist to solve this problem. They distinguish between two locking modes: **shared mode (shared / read lock)** and **exclusive mode (exclusive / write lock)**. Multiple threads can hold a read lock simultaneously for reading, but a write lock requires exclusive access—no other thread (whether reading or writing) may hold the lock at the same time. `std::shared_mutex`, introduced in C++17, is the standard library's implementation of a reader-writer lock.

## std::shared_mutex: Two Locking Modes

`std::shared_mutex` is defined in the `<shared_mutex>` header (available since C++17). It provides two sets of locking interfaces: the `lock()` / `unlock()` / `try_lock()` for write locks (just like a regular `std::mutex`), and `lock_shared()` / `unlock_shared()` / `try_lock_shared()` for shared locks. While calling these raw interfaces directly is possible, we won't do that—RAII wrappers are the correct approach, and we must learn from the lessons of the previous chapter.

Let's look at a basic usage scenario. Suppose we have a configuration dictionary that is updated occasionally but queried frequently:

```cpp
#include <map>
#include <string>
#include <mutex>
#include <shared_mutex>

class ConfigDict {
    std::map<std::string, std::string> data;
    mutable std::shared_mutex mutex; // mutable allows locking in const methods

public:
    std::string get(const std::string& key) const {
        std::shared_lock lock(mutex); // Shared lock for reading
        auto it = data.find(key);
        return (it != data.end()) ? it->second : "";
    }

    void set(const std::string& key, const std::string& value) {
        std::unique_lock lock(mutex); // Exclusive lock for writing
        data[key] = value;
    }
};
```

The `get` method uses `std::shared_lock` to acquire a shared lock. Multiple threads can hold a `shared_lock` simultaneously—they do not block each other. The `set` method uses `std::unique_lock` to acquire an exclusive lock. When any thread holds the exclusive lock, other threads (whether requesting shared or exclusive locks) must wait; conversely, if threads hold shared locks, a thread attempting to acquire an exclusive lock must wait until all shared locks are released.

Note that `mutex` is declared `mutable`—because `lock` is a `const` member function, but it needs to modify the mutex's state (locking/unlocking). This is a proper use of `mutable`: the mutex is not part of the object's logical state; it is part of the synchronization mechanism.

## std::shared_lock: RAII Wrapper for Shared Mode

`std::shared_lock` is the "shared version" of `std::unique_lock`, defined in the `<shared_mutex>` header. Its interface is highly symmetric to `std::unique_lock`—it acquires a shared lock upon construction and releases it upon destruction, supporting deferred locking (`std::defer_lock`), manual locking/unlocking, and so on. However, it calls `lock_shared()` / `unlock_shared()` instead of `lock()` / `unlock()`.

Why do we need a separate `std::shared_lock` instead of adding a parameter to `std::unique_lock` to control the mode? The reason is type safety. If you have a function accepting a `std::unique_lock` parameter, you are guaranteed it holds an exclusive lock—the compiler enforces this. Conversely, `std::shared_lock` guarantees a shared lock is held. The semantics of the two lock modes are completely different, and using distinct types to express them is the safest approach.

A usage worth noting is `std::shared_lock`配合 `std::condition_variable_any` (the generic condition variable mentioned in the previous chapter) to implement "shared waiting". A regular `std::condition_variable` only accepts `std::unique_lock`, but `std::condition_variable_any` accepts any lock type—including `std::shared_lock`. This allows you to wait on a condition variable while holding a shared lock, a capability used in certain advanced patterns (such as lock upgrade protocols for reader-writer locks).

## The Complete Pattern: Read with shared_lock, Write with unique_lock

The standard usage of reader-writer locks can be summarized in one sentence: **shared_lock for reading, unique_lock for writing**. Let's look at a more complete example—a simple thread-safe cache:

```cpp
#include <shared_mutex>
#include <map>
#include <optional>
#include <mutex>

template <typename Key, typename Value>
class ThreadSafeCache {
    std::map<Key, Value> cache;
    mutable std::shared_mutex mutex;

public:
    // Returns the value if found, or std::nullopt if not present
    std::optional<Value> get(const Key& key) const {
        std::shared_lock lock(mutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Inserts a key-value pair
    void insert(const Key& key, const Value& value) {
        std::unique_lock lock(mutex);
        cache[key] = value;
    }

    // Computes and inserts if missing (Read-Compute-Write pattern)
    template <typename Func>
    Value get_or_compute(const Key& key, Func compute) {
        // First check: read lock
        {
            std::shared_lock lock(mutex);
            auto it = cache.find(key);
            if (it != cache.end()) {
                return it->second;
            }
        } // Shared lock released here

        // Compute the value (expensive operation)
        Value value = compute(key);

        // Second check: write lock
        std::unique_lock lock(mutex);
        // Double-check: another thread might have inserted it while we were computing
        auto it = cache.find(key);
        if (it == cache.end()) {
            cache[key] = value;
            return value;
        }
        return it->second; // Use the value inserted by another thread
    }
};
```

This code demonstrates a very important pattern—**double-checked locking**. Why do we read a second time before the write lock? Because there is a time window between releasing the first read lock and acquiring the write lock during which another thread might have inserted the same key. Without this double-check, we might overwrite another thread's calculation result or even waste resources repeating the calculation.

Another noteworthy point is that `compute` executes **outside** the write lock. This is intentional—computation can be expensive, and if performed while holding the write lock, all reader threads would be blocked. Moving the computation outside the lock and acquiring the write lock only for the final write maximizes concurrency. Of course, the cost is potential duplicate computation—multiple threads might compute for the same key simultaneously. If your `compute` is very expensive and uniqueness must be guaranteed, you might need to perform the calculation inside the write lock, sacrificing concurrency for correctness.

## Writer Starvation: The Dark Side of Reader-Writer Locks

Reader-writer locks seem beautiful—reads don't block reads, writes block everything. But there is a hidden problem here: **writer starvation**. Imagine this scenario: ten reader threads continuously request shared locks. They come and go, and at any moment, a few are always reading. At this point, a writer thread wants to acquire an exclusive lock—it must wait until **all** shared locks are released. The problem is, if reader threads arrive frequently enough, shared locks might never be released simultaneously—a new read request arrives before the old ones finish. The writer thread gets "starved," never getting a chance for exclusive access.

The C++ standard makes **no guarantees** about the scheduling policy of `std::shared_mutex`—it does not guarantee fairness, writer preference, or that readers won't starve writers. Specific scheduling behavior depends on the standard library implementation and the underlying operating system. On some platforms (like Windows SRWLock), the implementation tends to prefer writers—when a writer is waiting, new readers are blocked until the writer completes. On other platforms, readers might continuously acquire shared locks, causing writers to wait for a long time.

What does this mean for you? If you use `std::shared_mutex`, you need to be aware of the possibility of writer starvation and assess whether it poses a problem for your application. If your scenario is "reads far outnumber writes, and write latency is not sensitive," the benefits of reader-writer locks far outweigh the risks. But if write timeliness is critical (e.g., parameter updates in real-time control systems), reader-writer locks might not be the best choice—you might need a custom reader-writer lock with writer preference guarantees, or simply use a regular `std::mutex` with a copy-on-write strategy.

## Performance Boundaries: When Reader-Writer Locks Are Slower

This section might surprise some: **reader-writer locks are not a silver bullet; in some scenarios, they are slower than a regular mutex**. The reason is that the internal implementation of a reader-writer lock is much more complex than a mutex—it needs to maintain a reader count, manage waiting queues, and handle priorities between reads and writes. This extra management overhead means that even in low-contention scenarios, each lock/unlock operation of a reader-writer lock is more expensive than that of a mutex.

So, where is the crossover point? According to some benchmarks (such as a 2025 comparison study on Google Benchmark), in low-thread-count (2-4 threads) scenarios, `std::mutex` is usually faster than `std::shared_mutex`—because contention is low, and the simplicity of the mutex wins. As thread counts increase and reads dominate (e.g., 8 reader threads + 1 writer thread), `std::shared_mutex` starts to show its advantage—multiple reader threads can execute concurrently, significantly increasing throughput. The more threads and the higher the read-to-write ratio, the more obvious the advantage of reader-writer locks.

Several other factors affect the performance of reader-writer locks. First is the size of the critical section—if the critical section is very short (e.g., just reading an `int`), the overhead of a mutex and a reader-writer lock is similar, and the extra management cost of the reader-writer lock becomes a drag. But if the critical section is long (e.g., traversing a large map or performing complex queries), the benefit of allowing concurrent reads with a reader-writer lock is significant. Second is the impact of hardware caches—the reader counter in a reader-writer lock is a shared atomic variable, which can cause cache line bouncing in multi-core environments (multiple cores frequently fighting for ownership of the same cache line), potentially offsetting the gains of concurrent reading under high-frequency access.

In actual projects, the author suggests: use `std::mutex` first. If you have a clear performance bottleneck characterized by "read-heavy, write-light + high-concurrency reads," then consider switching to `std::shared_mutex`. Before switching, it is best to run a benchmark with your real workload to compare, because the crossover point relates to specific data structures, access patterns, and hardware environments. Premature optimization is the root of all evil, and this applies equally to the choice of synchronization primitives.

## std::shared_timed_mutex: The Version with Timeouts

C++14 introduced `std::shared_timed_mutex`, a timed version of `std::shared_mutex`—in addition to basic shared/exclusive locking, it supports timeout operations like `try_lock_for()`, `try_lock_until()`, `try_lock_shared_for()`, and `try_lock_shared_until()`. C++17's `std::shared_mutex` removes timeout functionality to become a lighter-weight version.

If your project is still on C++14, `std::shared_timed_mutex` is the only choice. If you are on C++17 or later and do not need timeout functionality, prefer `std::shared_mutex`—its implementation is simpler and has lower overhead. Scenarios requiring timeout functionality are similar to the `std::timed_mutex` / `std::recursive_timed_mutex` discussed in the previous chapter—such as "try to acquire a write lock within 100ms, and abandon the update if it times out."

## Lock Upgrade and Downgrade: Advanced Operations Not Directly Supported by the Standard

Lock upgrade refers to converting a shared lock to an exclusive lock—for example, I read the data first, found I need to modify it, and then upgrade to a write lock without releasing the lock. Lock downgrade is the reverse—converting an exclusive lock to a shared lock. These two operations are very common in some database systems (e.g., transaction lock management), but the C++ standard library **does not directly support** them.

Why? Because lock upgrade can cause deadlocks in a multi-threaded environment. Consider this scenario: Thread A holds a shared lock and tries to upgrade to an exclusive lock, Thread B also holds a shared lock and tries to upgrade to an exclusive lock—both are waiting for the other to release the shared lock, but neither will release first. Deadlock. This is the so-called "upgrade deadlock."

The standard library's approach requires you to **release the shared lock first, then acquire the exclusive lock**. This guarantees a "lock-free" window between shared and exclusive states during which other threads are free to acquire the lock. The cost is that you need to handle state changes within that window—this is where the double-checked locking pattern mentioned earlier comes into play.

```cpp
// Manual lock upgrade pattern
void update_data(const Key& key) {
    std::shared_lock shared_lock(mutex);
    // ... read data ...

    // Need to modify? Release shared, acquire exclusive
    shared_lock.unlock();
    std::unique_lock exclusive_lock(mutex);

    // Double-check state
    // ... write data ...
}
```

Lock downgrade (exclusive -> shared) is safe—downgrading from exclusive to shared does not cause deadlocks because it only releases permissions and requests no additional permissions. However, the standard library does not directly support this either; you need to manually release the exclusive lock and then acquire the shared lock. Some platform-specific APIs (like Windows SRWLock) provide atomic downgrade operations, but POSIX `pthread_rwlock` and the C++ standard library do not have this capability—the only way under POSIX is to `unlock` then `rdlock`, leaving a lock-free window in between. If your scenario requires frequent lock downgrades, you might need to consider using platform-specific APIs or a custom reader-writer lock implementation.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `demo/shared_mutex_demo.cpp`.

## Exercises

### Exercise 1: Thread-Safe Cache

Implement a template class `ThreadSafeCache<Key, Value>` supporting the following operations:

- `get(key)`: Query the cache, returns `std::optional<Value>`
- `set(key, value)`: Insert or update
- `remove(key)`: Delete
- `size()`: Return current cache size

Requirement: Use `std::shared_mutex`. Read operations (`get`, `size`) use `std::shared_lock`. Write operations (`set`, `remove`) use `std::unique_lock`.

Then write a test program: 4 reader threads continuously query random keys, 1 writer thread inserts new data periodically. Observe whether reads and writes can proceed concurrently (you can add tiny delays in read operations to amplify the concurrency effect).

### Exercise 2: Compare Performance of mutex vs. shared_mutex

Write a benchmark: protect the same `std::map` with `std::mutex` and `std::shared_mutex` respectively, then execute 90% read operations + 10% write operations under multiple threads. Increase the thread count from 1 to 16 and record the total time for each configuration.

Consider the following questions:

- On your platform, where is the crossover point in terms of thread count?
- If you change the read/write ratio from 90:10 to 50:50, what happens?
- If the critical section is very short (just reading an int), what are the results?

### Exercise 3: Reproduce Writer Starvation

Construct a scenario to observe writer starvation: Start N reader threads, each thread loops acquiring a shared lock, reading data, and releasing the lock (you can add tiny delays to control read frequency). Then start 1 writer thread attempting to acquire an exclusive lock to update data. Measure the wait time of the writer thread from requesting the lock to acquiring it. Gradually increase the number of reader threads and read frequency, observing how the writer's wait time changes.

Hint: You may find that under extreme read/write ratios (e.g., 20 reader threads reading frantically), the writer thread's wait time increases drastically. This is the直观 manifestation of writer starvation.

## References

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::shared_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_lock)
- [std::shared_timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)
- [When std::shared_mutex Outperforms std::mutex -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [Understanding std::shared_mutex from C++17 -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
