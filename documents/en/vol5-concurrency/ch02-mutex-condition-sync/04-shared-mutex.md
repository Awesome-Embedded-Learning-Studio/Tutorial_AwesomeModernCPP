---
chapter: 2
cpp_standard:
- 17
- 20
description: Applying C++17 shared_mutex to read-heavy workloads, with an analysis
  of writer starvation and performance boundaries
difficulty: intermediate
order: 4
platform: host
prerequisites:
- condition_variable and Wait Semantics
reading_time_minutes: 14
related:
- mutex and RAII Locks
- Thread-Safe Container Design
tags:
- host
- cpp-modern
- intermediate
- mutex
title: Reader-Writer Locks and shared_mutex
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/04-shared-mutex.md
  source_hash: dc967b71909e81673517aa23da4c887f1ed01f761ea616517c31c3b20cbd2e21
  translated_at: '2026-09-26T07:02:09+00:00'
  engine: anthropic
  token_count: 5500
---
# Reader-Writer Locks and shared_mutex

So far, every synchronization primitive we have discussed has been "exclusive"—one thread grabs the lock, and everyone else has to wait outside. But a huge class of real-world scenarios doesn't work that way: **read-heavy, write-rare**. Configuration data, caches, routing tables, dictionaries—these things spend the vast majority of their time being read and only occasionally get updated. If every read has to acquire a mutex exclusively, reader threads get serialized for no good reason—they could perfectly well read the same data structure concurrently, because reads don't modify any state.

The reader-writer lock exists to solve exactly this problem. It distinguishes two locking modes: **shared mode (shared / read lock)** and **exclusive mode (exclusive / write lock)**. Multiple threads can hold the read lock at the same time, but the write lock demands exclusive access—no other thread (reader or writer) may hold the lock concurrently. `std::shared_mutex`, introduced in C++17, is the standard library's implementation of a reader-writer lock.

## std::shared_mutex: Two Locking Modes

`std::shared_mutex` is defined in the `<shared_mutex>` header (available since C++17). It provides two sets of locking interfaces: `lock()` / `unlock()` / `try_lock()` for the write lock (exactly like a plain `std::mutex`), and `lock_shared()` / `unlock_shared()` / `try_lock_shared()` for the shared lock. Calling these raw interfaces directly is of course possible, but we won't do that—RAII wrappers are the correct posture, and the lesson from the previous article must not go to waste.

Let's start with the most basic usage scenario. Suppose we have a configuration dictionary that is updated occasionally and queried frequently:

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
        // Read operation: acquire the shared lock
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        return (it != data_.end()) ? it->second : "";
    }

    void set(const std::string& key, const std::string& value)
    {
        // Write operation: acquire the exclusive lock
        std::unique_lock<std::shared_mutex> lock(mutex_);
        data_[key] = value;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};
```

The `get` method uses `std::shared_lock<std::shared_mutex>` to acquire the shared lock. Multiple threads can hold a `shared_lock` at the same time—they don't block each other. The `set` method uses `std::unique_lock<std::shared_mutex>` to acquire the exclusive lock. While any thread holds the exclusive lock, every other thread (whether it wants a shared lock or the exclusive lock) must wait; conversely, if some thread holds a shared lock, a thread that wants the exclusive lock must also wait until all shared locks are released.

Note that `mutex_` is declared `mutable`—because `get` is a `const` member function, yet it needs to modify the mutex's state (locking/unlocking). This is a legitimate use of `mutable`: the mutex is not part of the object's logical state; it is part of the synchronization machinery.

## std::shared_lock: The RAII Wrapper for Shared Mode

`std::shared_lock` is the "shared version" of `std::unique_lock`, also defined in the `<shared_mutex>` header. Its interface mirrors `unique_lock` closely—it acquires the shared lock on construction and releases it on destruction, and it supports deferred locking (`defer_lock`), manual locking/unlocking, and so on. The difference is that it calls `lock_shared()` / `unlock_shared()` instead of `lock()` / `unlock()`.

Why do we need a separate `shared_lock` instead of a parameter on `unique_lock` to select the mode? Type safety. If you have a function that takes a `std::unique_lock<SharedMutex>` parameter, you know for certain it holds an exclusive lock—the compiler makes that guarantee for you. Conversely, `std::shared_lock<SharedMutex>` guarantees a shared lock. The semantics of the two lock modes are completely different, and expressing them with distinct types is the safest approach.

A usage worth knowing is pairing `shared_lock` with `condition_variable_any` (the generic condition variable mentioned in the previous article) to implement "shared waiting". A plain `condition_variable` only accepts `unique_lock`, but `condition_variable_any` accepts any lock type—including `shared_lock`. This lets you wait on a condition variable while holding a shared lock, a capability that certain advanced patterns (such as upgrade protocols on reader-writer locks) rely on.

## The Complete Pattern: Read with shared_lock, Write with unique_lock

The canonical use of reader-writer locks can be summarized in one sentence: **shared_lock when reading, unique_lock when writing**. Let's look at a more complete example—a simple thread-safe cache:

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
    /// @brief Look up the cache; return the value on a hit, otherwise compute and store
    Value get_or_compute(const Key& key,
                          std::function<Value(const Key&)> compute)
    {
        // Step 1: look up under the read lock
        {
            std::shared_lock<std::shared_mutex> read_lock(mutex_);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        // Step 2: compute outside the lock (avoid doing heavy work while holding it)
        Value value = compute(key);

        // Step 3: double-check under the write lock, then store
        {
            std::unique_lock<std::shared_mutex> write_lock(mutex_);
            // double-check: another thread may have inserted the key between our
            // releasing the read lock and acquiring the write lock
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
            cache_[key] = value;
        }

        return value;
    }

    /// @brief Clear the cache
    void clear()
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.clear();
    }

    /// @brief Return the cache size
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

This code demonstrates a genuinely important pattern—**double-checked locking**. Why read one more time before taking the write lock? Because there is a time window between releasing the first read lock and acquiring the write lock, and another thread may well have inserted the same key during it. Without that double-check, we could overwrite another thread's computation result, or even waste resources computing it twice.

Another point worth noticing is that `compute(key)` executes **outside the write lock**. That is deliberate—computation can be time-consuming, and doing it while holding the write lock would block every reader. Moving the computation outside the lock and acquiring the write lock only for the final store maximizes concurrency. Of course, the price is possible duplicate computation—multiple threads may run compute on the same key at the same time. If your compute is very expensive and must happen exactly once, you may want to execute it inside the write lock, trading concurrency for correctness.

## Writer Starvation: The Dark Side of Reader-Writer Locks

Reader-writer locks look lovely—reads don't block reads, and writes block everything. But a sneaky problem hides in there: **writer starvation**. Picture this scenario: ten reader threads request the shared lock in a steady stream; they come and go, and at any given moment a few of them are reading. Now a writer thread wants to acquire the exclusive lock—it must wait until **all** shared locks are released. The trouble is, if readers arrive frequently enough, the shared locks will never all be free at the same moment—some new read request always slips in before the old ones finish. The writer just sits there "starving", forever denied its chance at exclusive access.

The C++ standard makes **no guarantees whatsoever** about the scheduling policy of `std::shared_mutex`—it does not guarantee fairness, does not guarantee writer preference, and does not promise that readers won't starve writers. The actual scheduling behavior depends on the standard library implementation and the underlying operating system. On some platforms (Windows SRWLock, for example), the implementation leans toward writer preference—when a writer is waiting, new readers are blocked until the writer completes. On other platforms, readers may keep acquiring the shared lock indefinitely, leaving writers waiting for a long time.

What does this mean in practice? If you use `std::shared_mutex`, you need to be aware that writer starvation is possible and assess whether it poses a problem for your application. If your workload is "reads far outnumber writes, and write latency doesn't matter much", the benefits of reader-writer locks far outweigh the risks. But if write timeliness is critical (parameter updates in a real-time control system, say), a reader-writer lock may not be the best choice—you would want a custom reader-writer lock with a writer-preference guarantee, or simply a plain `std::mutex` combined with a copy-on-write strategy.

## Performance Boundaries: When Reader-Writer Locks Are Actually Slower

This section may surprise some readers: **reader-writer locks are not a universal win—in some scenarios they are slower than a plain mutex**. The reason is that a reader-writer lock's internal implementation is far more complex than a mutex's—it has to maintain a reader count, manage waiting queues, and juggle priorities between reads and writes. That extra management overhead means every lock/unlock on a reader-writer lock costs more than on a mutex, even in low-contention scenarios.

So where is the crossover point? According to some benchmarks (a 2025 comparison study on Google Benchmark, for instance), at low thread counts (2-4 threads) `std::mutex` is usually faster than `std::shared_mutex`—contention is mild there, and the mutex's simplicity wins. As the thread count grows and reads dominate (8 reader threads + 1 writer thread, say), `shared_mutex` starts to show its strength—multiple reader threads execute concurrently and throughput climbs noticeably. The more threads and the higher the read/write ratio, the more pronounced the reader-writer lock's advantage.

A few more factors shape how reader-writer locks perform. First, the size of the critical section—if it is very short (reading a single `int`, say), a mutex costs about the same, and the reader-writer lock's extra management overhead becomes dead weight. But if the critical section is long (traversing a large map or running a complex query), the payoff from allowing concurrent reads becomes substantial. Second, the influence of hardware caches—the reader counter inside a reader-writer lock is a shared atomic variable, which in a multi-core environment can cause cache line bouncing (cores repeatedly fighting over ownership of the same cache line), and at high read frequencies that can cancel out the gains from concurrent reading.

In real projects, our advice is: start with `std::mutex`, and only consider switching to `std::shared_mutex` when you have a demonstrated "read-heavy + high-concurrency reads" performance bottleneck. Before switching, it is best to run a benchmark with your real workload, because the crossover point depends on the specific data structure, access pattern, and hardware environment. Premature optimization is the root of all evil, and that applies equally to choosing synchronization primitives.

## std::shared_timed_mutex: The Version with Timeouts

C++14 introduced `std::shared_timed_mutex`, the timed sibling of `std::shared_mutex`—on top of basic shared/exclusive locking, it supports timeout operations such as `try_lock_for`, `try_lock_until`, `try_lock_shared_for`, and `try_lock_shared_until`. C++17's `std::shared_mutex` drops the timeout functionality, making it a leaner version.

If your project is still on C++14, `shared_timed_mutex` is the only choice. If you are on C++17 or later and don't need timeouts, prefer `std::shared_mutex`—simpler implementation, lower overhead. The scenarios that call for timeout support are the same kind we discussed in the previous article around `wait_for` / `wait_until`—for example, "try to acquire the write lock within 100ms, and abandon this update if it times out".

## Lock Upgrade and Downgrade: Advanced Operations the Standard Does Not Directly Support

A lock upgrade means "upgrading" a shared lock to an exclusive one—for example, I read the data first, discover it needs modifying, and upgrade to the write lock without releasing the lock. A lock downgrade is the reverse—turning an exclusive lock into a shared one. Both operations are very common in certain database systems (transaction lock management, for instance), but the C++ standard library **does not support them directly**.

Why? Because lock upgrades can cause deadlocks in a multi-threaded environment. Consider this scenario: thread A holds a shared lock and tries to upgrade to an exclusive lock, and thread B also holds a shared lock and tries to upgrade to an exclusive lock—both are waiting for the other to release its shared lock, and neither ever will. Deadlock. This is the so-called "upgrade deadlock".

The standard library's approach is to require you to **release the shared lock first, then acquire the exclusive lock**. This guarantees a "lock-free" gap between shared and exclusive, during which other threads are free to acquire the lock. The cost is that you have to cope with state changes during that gap—which is exactly where the double-checked locking pattern from earlier earns its keep.

```cpp
// Manual implementation of a lock upgrade: release the shared lock -> acquire the exclusive lock
void upgrade_example()
{
    // Read phase
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    auto data = read_something();
    read_lock.unlock();  // The shared lock must be released first

    // Write phase
    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    // Careful: data may already be stale here!
    // Re-read or double-check first
    write_something(data);
}
```

Lock downgrade (exclusive -> shared) is safe—dropping from exclusive to shared cannot cause a deadlock, because downgrading only releases permissions and requests nothing extra. But the standard library does not support it directly either; you have to manually release the exclusive lock and then acquire the shared lock. Some platform-specific APIs (Windows SRWLock, for instance) provide an atomic downgrade operation, but POSIX `pthread_rwlock` and the C++ standard library do not have that capability—under POSIX the only way is to `unlock` first and then `rdlock`, leaving a lock-free window in between. If your scenario requires frequent lock downgrades, you may need to consider platform-specific APIs or a custom reader-writer lock implementation.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); see `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Thread-Safe Cache

Implement a template class `ThreadSafeCache<Key, Value>` that supports the following operations:

- `get(key)`: look up the cache, returning `std::optional<Value>`
- `put(key, value)`: insert or update
- `remove(key)`: remove
- `size()`: return the current cache size

The requirement is to use `std::shared_mutex`, with read operations (`get`, `size`) taking a `shared_lock` and write operations (`put`, `remove`) taking a `unique_lock`.

Then write a test program: 4 reader threads continuously query random keys, while 1 writer thread inserts new data at intervals. Observe whether reads and writes actually proceed concurrently (adding a tiny delay inside the read operations amplifies the concurrency effect).

### Exercise 2: Comparing the Performance of mutex and shared_mutex

Write a benchmark: protect the same `std::unordered_map<int, std::string>` with `std::mutex` and with `std::shared_mutex` respectively, then run 90% read operations + 10% write operations under multiple threads. Step the thread count from 1 up to 16, recording the total time for each configuration.

Think about the following questions:

- On your platform, at which thread count does the crossover point land?
- What happens if you change the read/write ratio from 90:10 to 50:50?
- And what if the critical section is very short (just reading an int)?

### Exercise 3: Reproducing Writer Starvation

Construct a scenario to observe writer starvation: start N reader threads, each looping through acquiring the shared lock, reading the data, and releasing the lock (a tiny delay lets you control the read frequency). Then start 1 writer thread that tries to acquire the exclusive lock to update the data. Measure how long the writer waits from requesting the lock to acquiring it. Gradually increase the number of reader threads and the read frequency, and watch how the writer's wait time changes.

Hint: you will likely find that under an extreme read/write ratio (20 reader threads reading frantically, say), the writer's wait time increases drastically. That is writer starvation made visible.

## References

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::shared_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_lock)
- [std::shared_timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_timed_mutex)
- [When std::shared_mutex Outperforms std::mutex -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [Understanding std::shared_mutex from C++17 -- C++ Stories](https://www.cppstories.com/2026/shared_mutex/)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
