---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Design and trade-offs of four strategies: coarse-grained locks, fine-grained
  locks, sharded locks, and copy-on-write'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 线程安全队列
- 读写锁与 shared_mutex
reading_time_minutes: 24
related:
- 无锁编程基础
tags:
- host
- cpp-modern
- intermediate
- mutex
- 容器
title: Thread-Safe Container Design
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/02-thread-safe-containers.md
  source_hash: 9aa5dfb9e73a85705d8a82e3f06b2d70afa586cf371d68b5a9a6ee9b349428e2
  token_count: 4007
  translated_at: '2026-05-20T04:40:43.578777+00:00'
---
# Thread-Safe Container Design

To be honest, the first time I needed to write a "thread-safe map," my first reaction was—how hard could this be? Just wrap every operation in a `lock_guard`, right? But once I actually started writing, I realized things were far from simple. Adding a lock isn't hard; adding the *right* lock, with the *right* granularity, in the *right* place—that's the real challenge. Lock too coarsely, and performance tanks. Lock too finely, and correctness breaks. Put the lock in the wrong place, and you get a data race.

In the previous article, we transformed a thread-safe queue from a teaching toy into a production-grade component—adding a shutdown mechanism, timeout operations, `stop_token` cancellation, and backpressure strategies. That queue used a single mutex to protect its entire internal state, which is the simplest and most brute-force synchronization approach. For a data structure with straightforward operation logic like a queue, one lock is enough. But when we face more complex containers—such as maps, sets, or hash tables—a single lock becomes a performance bottleneck: all threads must wait for the same lock, regardless of which element they are operating on.

In this article, we discuss four thread-safe container design strategies at varying levels of sophistication—from coarse-grained locking to fine-grained locking, from striped locking to copy-on-write. These are not mutually exclusive replacements, but rather tools suited for different scenarios. Our goal is to understand the applicable conditions, implementation complexity, and performance characteristics of each strategy, enabling us to make informed choices when facing specific requirements.

## Why STL Containers Are Not Thread-Safe

Before diving into design strategies, let's answer a common question: why aren't C++ standard library containers (`std::vector`, `std::map`, `std::unordered_map`, etc.) thread-safe?

The C++ standard provides very limited guarantees for concurrent container access: multiple read operations (calling `const` member functions) on the same container are safe without external synchronization; however, as long as there is one write operation (calling non-`const` member functions), all other concurrent accesses (reads or writes) must be synchronized. In other words, "multiple reads without writes" is safe, but "any write operation" requires locking.

The reason the standard library doesn't enforce thread safety isn't an oversight, but a carefully considered trade-off. Different scenarios have vastly different requirements for "thread safety." A read-only query cache and a high-frequency write counter table require completely different synchronization strategies. If standard library containers built in a certain thread-safety mechanism (such as an internal lock for every operation), scenarios that don't need thread safety would pay an unnecessary performance penalty, while scenarios requiring finer-grained control would find the built-in lock granularity too coarse—a lose-lose situation. The standard chose the most conservative strategy: no synchronization, leaving the decision to the user.

This leads to a practical consequence: when writing multithreaded code with STL containers, we must lock externally. But "external locking" is easier said than done—it has many pitfalls, such as the atomicity of compound operations, iterator invalidation, and lock granularity selection. These are the real topics we will discuss in this article.

## Coarse-Grained Locking: One Mutex to Rule Them All

Let's start with the most naive approach—using a single mutex to protect the entire container, where all operations acquire the lock before execution and release it afterward. The ``BoundedQueue`` from the previous article follows this pattern. Although simple and brute-force, it is the easiest to guarantee correctness.

Let's look at a coarse-grained locked concurrent map:

```cpp
#include <map>
#include <mutex>
#include <optional>

template <typename Key, typename Value>
class CoarseLockedMap {
public:
    std::optional<Value> get(const Key& key) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set(const Key& key, const Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[key] = value;
    }

    void erase(const Key& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.erase(key);
    }

    bool contains(const Key& key) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.count(key) > 0;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

private:
    mutable std::mutex mutex_;
    std::map<Key, Value> map_;
};
```

The advantage of coarse-grained locking is that correctness is easy to guarantee—all operations execute under the protection of the lock, so there are no concurrent access issues. The disadvantage is also obvious: all operations are serialized. Even if two operations access completely different keys, they must still queue up for the same lock. In low-contention scenarios (few threads, low operation frequency), this is perfectly fine, but in high-concurrency scenarios, this lock becomes the throughput ceiling.

There is an easily overlooked pitfall: the atomicity of the interface. The ``get`` and ``set`` above are individually atomic, but a compound operation like "get first, then decide whether to set based on the result" is not atomic—the lock is released between the two operations, allowing other threads to step in and change the map's state. For example, if we need a "insert if absent" semantic, we cannot call ``contains`` and then ``set``. We must provide an atomic operation that encapsulates both steps:

```cpp
// 原子的 "get or insert"
Value get_or_insert(const Key& key, const Value& default_value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        return it->second;
    }
    map_[key] = default_value;
    return default_value;
}
```

This method puts the "lookup" and "insertion" under the protection of a single lock acquisition, guaranteeing atomicity. When designing the interface of a concurrent container, we need to provide atomic versions of all compound operations—otherwise, callers either have to lock themselves (violating encapsulation) or write code with race conditions.

Another pitfall is iterator invalidation. ``std::unordered_map`` invalidates all iterators during a rehash, ``std::map`` insertions do not invalidate iterators but ``erase`` invalidates the iterator of the deleted element. However, in concurrent scenarios, the key issue isn't the container's own invalidation rules—it's that after the lock is released during traversal, other threads might modify the container, causing iterator invalidation, crashes, or reading inconsistent data. The solution is to hold the lock continuously during traversal—but this also means other threads are completely blocked while traversing. If the traversal takes a long time, this blocking may be unacceptable.

## Fine-Grained Locking: Locking by Bucket/Node

The problem with coarse-grained locking is now clear—the lock granularity is too coarse, and all operations share a single lock even when they operate on completely unrelated data. The natural next step is to split the container into multiple independent parts, each with its own lock, so that operations only contend for the specific part they need.

Hash tables are naturally suited for this kind of splitting because they are already bucketed—each key is mapped to a bucket via a hash function, and elements in different buckets are independent. We can give each bucket its own lock, so threads operating on different buckets won't contend.

```cpp
#include <vector>
#include <list>
#include <mutex>
#include <optional>
#include <functional>

template <typename Key, typename Value,
          typename Hash = std::hash<Key>>
class FineLockedHashMap {
public:
    explicit FineLockedHashMap(std::size_t bucket_count = 16)
        : buckets_(bucket_count)
    {}

    std::optional<Value> get(const Key& key) const
    {
        std::size_t idx = hash_fn_(key) % buckets_.size();
        std::lock_guard<std::mutex> lock(buckets_[idx].mutex);
        for (const auto& [k, v] : buckets_[idx].entries) {
            if (k == key) {
                return v;
            }
        }
        return std::nullopt;
    }

    void set(const Key& key, const Value& value)
    {
        std::size_t idx = hash_fn_(key) % buckets_.size();
        std::lock_guard<std::mutex> lock(buckets_[idx].mutex);
        for (auto& [k, v] : buckets_[idx].entries) {
            if (k == key) {
                v = value;
                return;
            }
        }
        buckets_[idx].entries.emplace_back(key, value);
    }

    void erase(const Key& key)
    {
        std::size_t idx = hash_fn_(key) % buckets_.size();
        std::lock_guard<std::mutex> lock(buckets_[idx].mutex);
        auto& entries = buckets_[idx].entries;
        entries.remove_if([&key](const auto& pair) {
            return pair.first == key;
        });
    }

private:
    struct Bucket {
        mutable std::mutex mutex;
        std::list<std::pair<Key, Value>> entries;
    };

    std::vector<Bucket> buckets_;
    Hash hash_fn_;
};
```

Here, each ``Bucket`` has its own ``mutex`` and ``entries`` (a linked list implemented with ``std::list`` to avoid the reallocation issues of ``std::vector``). ``get``, ``set``, and ``erase`` only lock the single bucket corresponding to the key. Threads operating on different buckets run completely in parallel, and contention only occurs when operating on the same bucket.

The throughput of fine-grained locking depends on the number of buckets and the quality of the hash function. More buckets mean less contention; a more uniform hash function means a more balanced load. But the number of buckets can't be increased indefinitely—each additional bucket means one more mutex (a ``pthread_mutex_t`` takes at least 40 bytes on Linux), and if there are too many buckets but too few elements, most buckets will be empty, wasting memory.

The biggest implementation challenge of fine-grained locking is **rehash**. When the number of elements grows to a certain point, the hash table needs to expand—increasing the number of buckets and redistributing all elements. Rehashing requires accessing all buckets, not just one—which means locking all bucket mutexes. If other threads are still operating on the container during a rehash, deadlocks or data inconsistency will occur. One solution is to use a global write lock during rehash to block all other operations—but this essentially degrades into coarse-grained locking, albeit only during rehash. A more elegant approach is incremental rehash: instead of moving all elements at once, move a small portion with each operation, amortizing the rehash overhead across multiple operations. Java's ``ConcurrentHashMap`` uses this strategy. However, this greatly increases implementation complexity, so we won't expand on it here.

Let me also mention a detail that might confuse you: the ``mutex`` in ``Bucket`` is declared as ``mutable``. This is because ``get`` is a ``const`` method, but it needs to acquire a mutex—a ``const`` method cannot modify member variables, but ``lock()`` on a mutex essentially modifies the mutex's internal state. If you omit ``mutable``, the compiler will directly report an error. The ``mutable`` keyword is designed exactly for scenarios where "the object's logical state doesn't change, but internal data physically needs to be modified"—this usage is very common in concurrent containers.

## Striped Locking: N Shards, Each with a Mutex

At this point, you might notice a contradiction: the number of locks in fine-grained locking equals the number of buckets—if there are many buckets, the lock overhead is significant. Each mutex takes at least dozens of bytes, and the operating system incurs additional costs managing a large number of locks. Striped locking was born as a compromise to solve this contradiction: we split the container into N shards, each with one lock, but the number of shards is much smaller than the number of buckets. Which shard a key belongs to is determined by taking the key's hash value modulo the number of shards.

The difference between striped locking and fine-grained locking lies in the granularity: fine-grained locking uses one lock per bucket, while striped locking has every K buckets share one lock. Striped locking has slightly more contention than fine-grained locking (operations on different buckets but the same shard will contend), but the number of locks is drastically reduced—usually 16 to 64 shards are enough, without needing to grow linearly with the number of buckets.

Let's implement a striped locked concurrent cache. A typical scenario for this cache is route caching in an HTTP server or database query caching—read-heavy and write-light, where read operations need to be fast, and write operations can tolerate a little delay.

```cpp
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <functional>

template <typename Key, typename Value,
          typename Hash = std::hash<Key>>
class ShardedCache {
public:
    explicit ShardedCache(std::size_t shard_count = kDefaultShardCount)
        : shards_(shard_count)
    {}

    std::optional<Value> get(const Key& key) const
    {
        auto& shard = get_shard(key);
        // 读操作用 shared_lock，允许多个读者并行
        std::shared_lock<std::shared_mutex> lock(shard.rw_mutex);
        auto it = shard.cache.find(key);
        if (it != shard.cache.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set(const Key& key, const Value& value)
    {
        auto& shard = get_shard(key);
        // 写操作用 unique_lock，独占访问
        std::unique_lock<std::shared_mutex> lock(shard.rw_mutex);
        shard.cache[key] = value;
    }

    void erase(const Key& key)
    {
        auto& shard = get_shard(key);
        std::unique_lock<std::shared_mutex> lock(shard.rw_mutex);
        shard.cache.erase(key);
    }

    // 遍历所有分片，对每个 key-value 执行回调
    // 注意：此操作锁住所有分片
    void for_each(std::function<void(const Key&, const Value&)> fn) const
    {
        for (const auto& shard : shards_) {
            std::shared_lock<std::shared_mutex> lock(shard.rw_mutex);
            for (const auto& [k, v] : shard.cache) {
                fn(k, v);
            }
        }
    }

    std::size_t size() const
    {
        std::size_t total = 0;
        for (const auto& shard : shards_) {
            std::shared_lock<std::shared_mutex> lock(shard.rw_mutex);
            total += shard.cache.size();
        }
        return total;
    }

private:
    static constexpr std::size_t kDefaultShardCount = 16;

    struct Shard {
        mutable std::shared_mutex rw_mutex;
        std::unordered_map<Key, Value> cache;
    };

    std::vector<Shard> shards_;
    Hash hash_fn_;

    std::size_t shard_index(const Key& key) const
    {
        return hash_fn_(key) % shards_.size();
    }

    Shard& get_shard(const Key& key)
    {
        return shards_[shard_index(key)];
    }

    const Shard& get_shard(const Key& key) const
    {
        return shards_[shard_index(key)];
    }
};
```

This implementation has a few noteworthy design decisions. First, we used ``std::shared_mutex`` (C++17) instead of ``std::mutex``—read operations acquire a ``shared_lock`` (shared lock, allowing multiple readers to proceed in parallel), while write operations acquire a ``unique_lock`` (exclusive lock, for exclusive access). In a "read-heavy, write-light" cache scenario, this distinction is critical: if 90% of operations are ``get``, the shared lock allows these 90% of operations to execute in parallel with almost no contention, and only ``set`` and ``erase`` require exclusive access. If we used ``std::mutex``, both reads and writes would need exclusive locks, and the parallelism of read operations would be completely lost.

Second, the ``for_each`` method iterates through each shard in order, acquiring a shared lock on each one. This means shards are unlocked one by one during traversal—after finishing one shard, its lock is released before locking the next shard. The benefit of this strategy is that it doesn't hold all locks simultaneously (avoiding deadlock risks), but the trade-off is that the traversal result might not reflect a global snapshot at any single point in time (a write operation might modify data after one shard is traversed but before the next shard is locked). If we need a true global snapshot, we must lock all shards simultaneously—but this increases the risk of deadlocks (if other code is also acquiring shard locks in some order).

Third, the number of shards is fixed (determined at construction time and unchanged afterward). This avoids the complexity of rehashing—the internal ``unordered_map`` within each shard can freely rehash (because it's protected by the shard-level lock), but the number of shards and the key-to-shard mapping never change. This is an important simplification: if our cache needs to dynamically adjust the number of shards (such as automatically scaling based on load), we would need to handle synchronization during shard migration, which is much more complex than static sharding.

## Copy-on-Write: The Ultimate Lock-Free Read Optimization

Striped locking performs well in read-heavy, write-light scenarios, but read operations still need to acquire a shared lock—although a shared lock is much lighter than an exclusive lock, in extremely high-frequency read scenarios (such as millions of reads per second), the lock overhead is still non-negligible. You might ask: is there a way to make read operations completely lock-free? The answer is yes.

Copy-on-Write (CoW) is exactly such a strategy. The core idea is: write operations don't directly modify the shared data, but instead create a complete copy, modify the copy, and then use an atomic operation to switch the pointer from the old data to the new data. Read operations directly access the data pointed to by the pointer—because write operations never modify the old data (they only create new data), read operations don't need any synchronization.

```cpp
#include <memory>
#include <unordered_map>
#include <mutex>
#include <optional>

template <typename Key, typename Value>
class CopyOnWriteMap {
public:
    CopyOnWriteMap()
        : data_(std::make_shared<Data>())
    {}

    std::optional<Value> get(const Key& key) const
    {
        // 原子地获取当前数据的 shared_ptr
        // 读操作完全无锁
        auto current = std::atomic_load(&data_);
        auto it = current->find(key);
        if (it != current->end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void set(const Key& key, const Value& value)
    {
        std::lock_guard<std::mutex> lock(write_mutex_);

        // 1. 拷贝当前数据
        auto new_data = std::make_shared<Data>(*std::atomic_load(&data_));

        // 2. 在副本上修改
        (*new_data)[key] = value;

        // 3. 原子地切换指针
        std::atomic_store(&data_, new_data);
    }

    void erase(const Key& key)
    {
        std::lock_guard<std::mutex> lock(write_mutex_);

        auto new_data = std::make_shared<Data>(*std::atomic_load(&data_));
        new_data->erase(key);
        std::atomic_store(&data_, new_data);
    }

    // 获取当前数据的快照——读操作无锁
    std::shared_ptr<const Data> snapshot() const
    {
        return std::atomic_load(&data_);
    }

private:
    using Data = std::unordered_map<Key, Value>;

    mutable std::mutex write_mutex_;  // 只保护写操作之间的互斥
    std::shared_ptr<Data> data_;
};
```

Let's break down this implementation step by step. ``data_`` is a ``shared_ptr<Data>`` pointing to the current map data. The read operation ``get`` obtains a copy of the current ``data_`` via ``std::atomic_load`` (which atomically increments the reference count of ``shared_ptr``), and then performs the lookup on the acquired map. Because write operations never modify the old data—they only create new data and then atomically switch the pointer—the data pointed to by the ``shared_ptr`` obtained by the read operation remains valid and consistent throughout the entire read process, without needing any locks.

The write operation ``set`` follows a three-step process. First, it acquires ``write_mutex_``—this mutex doesn't protect the data itself (the data is in the ``shared_ptr``, protected via atomic operations), but rather provides mutual exclusion between write operations: it ensures that only one write operation is creating a copy at a time. Otherwise, two write operations might each copy the old data, each make their own modifications, and then each write to the pointer, with the later write overwriting the earlier write's changes. Then, it makes modifications on the copy. Finally, it uses ``std::atomic_store`` to switch the pointer to the new data—this operation is atomic, guaranteeing that read operations see either the old data or the new data, never an intermediate state.

The cost of CoW is obvious: every write operation must copy the entire map. If the map has 10,000 elements, a single ``set`` requires copying 10,000 elements. Therefore, CoW is only suitable for scenarios where "reads far outnumber writes"—such as configuration tables, routing tables, and dictionary data—where write operations occur occasionally, and read operations are frequent and require low latency. If write operations are also frequent, the copying overhead of CoW will eat up the gains from lock-free reads.

Regarding ``std::atomic_load`` and ``std::atomic_store``: they are ``shared_ptr`` atomic operation functions provided by C++11 (defined in ``<memory>``). C++20 introduced ``std::atomic<std::shared_ptr<T>>`` as a replacement with a cleaner interface and similar underlying implementation—both use CAS (compare-and-swap) loops or a global spinlock to guarantee atomic updates to the ``shared_ptr`` control block pointer. It's worth noting that C++20 has marked ``std::atomic_load``/``std::atomic_store`` and other ``shared_ptr`` atomic free functions as deprecated, with plans to remove them in C++26. If your project uses C++20 or a higher standard, we recommend using ``std::atomic<std::shared_ptr<T>>`` directly. In our scenario, the atomic operations on ``shared_ptr`` only involve reading and writing a pointer (not copying the map data), so the overhead is very small.

Another detail worth noting: the ``snapshot()`` method returns a ``shared_ptr<const Data>``—an immutable snapshot. The caller can hold this snapshot for any length of time without worrying about data changes—because the underlying CoW mechanism guarantees that old data won't be destroyed until the last reference is released. This feature is very useful in scenarios requiring "consistent reads," such as traversing the entire map to perform aggregate calculations.

## Usage Strategies for std::shared_mutex

We already used ``std::shared_mutex`` in the striped locking implementation above, but haven't discussed its usage boundaries in concurrent containers in detail. This topic deserves a dedicated section because it's more subtle than most people think.

``std::shared_mutex`` (C++17, defined in the ``<shared_mutex>`` header) provides two locking modes: shared mode (``shared_lock``) and exclusive mode (``unique_lock``). Multiple threads can hold a shared lock simultaneously, but an exclusive lock blocks all other lock requests (both shared and exclusive). This makes it particularly effective in "read-heavy, write-light" scenarios—we've already seen the effect in the ``ShardedCache`` above.

But ``shared_mutex`` isn't a silver bullet. First, regarding performance: its overhead is larger than a regular ``mutex``—on Linux, ``shared_mutex`` is typically implemented based on ``pthread_rwlock_t``, which internally needs to maintain a reader count and a waiter queue, making lock acquisition and release heavier than ``pthread_mutex_t``. In "half-read, half-write" or "write-heavy" scenarios, the performance of ``shared_mutex`` might actually be worse than a regular ``mutex``.

Let me also mention a pitfall I've personally fallen into—writer starvation. If new readers continuously acquire the shared lock, a writer might never get a chance to acquire the exclusive lock—because as long as any single reader holds the shared lock, the writer cannot acquire the exclusive lock. Linux glibc's ``pthread_rwlock_t`` defaults to a reader-preference policy (continuously arriving readers will constantly delay the writer's chance to acquire the lock, which is a typical cause of writer starvation), but the C++ standard doesn't guarantee this. If your application is sensitive to write latency, make sure to test the scheduling policy of ``shared_mutex`` on your platform.

A practical rule of thumb is: the benefits of ``shared_mutex`` only become obvious when read operations account for over 80% of total operations. If the read-to-write ratio is close to 1:1 or if there are more writes, using a regular ``mutex`` is simpler and more efficient.

## Trade-offs of the Four Strategies

Now that we've gone through all four strategies, looking back, their trade-off relationships are actually quite clear. Let's compare them in a table:

| Strategy | Read Performance | Write Performance | Implementation Complexity | Applicable Scenarios |
|----------|------------------|-------------------|---------------------------|----------------------|
| Coarse-grained locking | Low (exclusive lock) | Low (exclusive lock) | Low | Low contention, prototyping |
| Fine-grained locking | Medium (bucket-level lock) | Medium (bucket-level lock) | High (rehash is difficult) | High-contention hash tables |
| Striped locking | High (shard-level shared lock) | Medium (shard-level exclusive lock) | Medium | Read-heavy, write-light caches |
| Copy-on-Write | Extremely high (lock-free read) | Low (full copy) | Medium | Configuration tables, routing tables |

The key to choosing a strategy isn't about which one is "fastest," but about your specific scenario. We need to answer a few questions: what is the read-to-write ratio? How large is the data volume? What is the frequency and duration of write operations? Do we need strong consistency snapshots? Is data loss tolerable? The answers to these questions determine which strategy is most suitable.

To be honest, most projects don't need anything more complex than coarse-grained locking in the early stages—coarse-grained locking is correct, simple, and easy to debug. Only after performance testing confirms that lock contention is the bottleneck should we consider upgrading to striped locking or fine-grained locking. Premature optimization is the root of all evil, especially in concurrent container design—finer-grained locks mean more subtle bugs and harder-to-reproduce deadlocks.

## Where We Are

In this article, starting from "why STL containers aren't thread-safe," we discussed four concurrent container design strategies. Coarse-grained locking uses a single mutex to protect the entire container—it's simple and correct, but throughput is limited by lock contention. Fine-grained locking pushes locks down to the bucket/node level, drastically reducing contention, but handling rehash causes implementation complexity to spike. Striped locking strikes a compromise between coarse-grained and fine-grained—a small number of shards each have a ``shared_mutex``, write operations only lock the relevant shard, and read operations proceed in parallel with shared locks. Copy-on-Write pushes read operations to the extreme of being lock-free, with the cost that every write must copy all data—it's only suitable for scenarios where reads far outnumber writes.

These four strategies are not a progression, but parallel tools suited for different scenarios. The key to choosing is understanding your read-write patterns and data characteristics. Don't rush to use the most complex solution—in the next article, we will discuss a more extreme strategy—lock-free data structures—using atomic operations to replace all locks. But before considering lock-free approaches, let's get lock-based solutions right first; after all, locks are sufficient for most scenarios.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Exercises

### Exercise 1: Concurrent Cache with Striped Locking

Building on the ``ShardedCache`` in this article, add the following feature: a ``get_or_compute(key, factory)`` method—if the key exists, return the value directly; if it doesn't exist, call ``factory()`` to compute the value, store it in the cache, and return it. The entire process of "look up, compute if absent, and insert" must be atomic (two threads must not simultaneously compute the value for the same key).

Hint: In ``get_or_compute``, we need to acquire an exclusive lock on the shard (we can't use a shared lock because we might write). If we want to use a shared lock on the fast path where "the key already exists" to improve read performance, we can consider acquiring a shared lock first for the lookup, and if not found, upgrading to an exclusive lock—but ``shared_mutex`` doesn't directly support lock upgrades. We need to release the shared lock first and then acquire the exclusive lock, and there is a time window in between that needs to be handled.

### Exercise 2: Performance Testing for Copy-on-Write

Write a benchmark program comparing the performance of ``CopyOnWriteMap`` and ``CoarseLockedMap`` under different read-write ratios. Test scenario: 10,000 keys, 4 reader threads and 1 writer thread running simultaneously for 10 seconds, measuring the total read throughput (ops/sec). Then rerun with 1 reader thread and 4 writer threads, and compare the results.

Expected result: In the read-heavy, write-light scenario (4 reads, 1 write), the read throughput of ``CopyOnWriteMap`` should be significantly higher than ``CoarseLockedMap`` (because of lock-free reads vs. reads needing to acquire a mutex). In the write-heavy, read-light scenario (1 read, 4 writes), the performance of ``CopyOnWriteMap`` will drop significantly (because every write requires copying the entire map).

### Exercise 3: Impact of Shard Count on Performance

Modify the constructor of ``ShardedCache`` to accept different shard count parameters (such as 1, 4, 16, 64, 256). Run a benchmark with 8 threads (4 readers, 4 writers) and observe the throughput changes under different shard counts. Expectation: as shards increase from 1 to 16, throughput improves significantly, but beyond a certain value, the improvement slows or even decreases (because the management overhead of locks starts to become apparent).

## References

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::atomic_load, std::atomic_store for shared_ptr -- cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [Concurrent Hash Table Designs -- bluuewhale.github.io](https://bluuewhale.github.io/posts/concurrent-hashmap-designs/)
- [Design Concurrent HashMap -- AlgoMaster.io](https://algomaster.io/learn/concurrency-interview/design-concurrent-hashmap)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 6 & 7](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [P1761R0: Concurrent Map Customization Options -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1761r0.pdf)
