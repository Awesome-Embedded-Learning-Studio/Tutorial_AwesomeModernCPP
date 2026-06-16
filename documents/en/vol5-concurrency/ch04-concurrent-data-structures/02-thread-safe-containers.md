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
reading_time_minutes: 23
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
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/02-thread-safe-containers.md
  source_hash: 527a71f22cb11114d3ab8d7b7b6826f98a29ea507d1df2eab6e2051211a93e34
  translated_at: '2026-06-16T04:04:35.538726+00:00'
  engine: anthropic
  token_count: 4001
---
# Thread-Safe Container Design

Honestly, the first time I needed to write a "multithreaded map," my initial reaction was—how hard can this be? Just wrap a `lock_guard` around every operation, right? But when I actually started writing it, I realized it was far from simple. Locking itself isn't hard; what's hard is locking correctly, locking enough, and locking just right. Lock too coarse and performance explodes; lock too fine and correctness explodes; lock in the wrong place and a data race explodes.

In the last post, we transformed a thread-safe queue from an educational toy into a production-grade component—adding a shutdown mechanism, timed operations, `stop_token` cancellation, and backpressure strategies. That queue used a single `mutex` to protect its entire internal state, which is the simplest and crudest form of synchronization. For a data structure with simple operational logic like a queue, one lock is sufficient. But when we face more complex containers—like `map`, `set`, or hash tables—a single lock becomes a performance bottleneck: all threads, regardless of which element they operate on, must queue for the same lock.

In this post, we will discuss four design strategies for thread-safe containers of varying sophistication—from coarse-grained locking to fine-grained locking, from striped locks to copy-on-write. They don't replace each other; rather, they are tools for different scenarios. Our goal is to understand the applicable conditions, implementation complexity, and performance characteristics of each strategy so that we can make reasonable choices when facing specific requirements.

## Why STL Containers Are Not Thread-Safe

Before diving into design strategies, let's answer a common question: why aren't C++ standard library containers (like `std::map`, `std::vector`, `std::string`) thread-safe?

The C++ standard provides very limited guarantees for concurrent container access: multiple read operations (calling `const` member functions) on the same container are safe without external synchronization; however, as long as there is one write operation (calling non-`const` member functions), all other concurrent accesses (read or write) must be synchronized. In other words, "multiple reads, no writes" is safe; "write operations present" requires locking.

The reason the standard library doesn't enforce thread safety isn't oversight, but a deliberate trade-off. Different scenarios have vastly different requirements for "thread safety." A read-only query cache and a high-frequency write counter table require completely different synchronization strategies. If standard library containers built in a specific thread-safety mechanism (like an internal lock for every operation), scenarios that don't need thread safety would pay a performance penalty for nothing, while scenarios needing finer-grained control would find the built-in lock granularity too coarse—pleasing no one. The standard chose the most conservative strategy: no synchronization, leaving the decision to the user.

This leads to a practical consequence: when writing multithreaded code with STL containers, you must add locks outside the container. But "external locking" is simple to say but full of pitfalls to implement—atomicity of composite operations, iterator invalidation, lock granularity selection—these are the things this post really discusses.

## Coarse-Grained Locking: One `mutex` to Protect Everything

Let's start with the most naive approach—using one `mutex` to protect the entire container, where all operations acquire the lock before execution and release it after. The `ThreadSafeQueue` from the last post follows this pattern. While simple and crude, it guarantees correctness best.

Let's look at a coarse-grained locked concurrent map:

```cpp
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ThreadSafeMap {
public:
    bool find(const Key& key, Value& value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    void set(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[key] = value;
    }

    void erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.erase(key);
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<Key, Value, Hash> map_;
};
```

The advantage of coarse-grained locking is that correctness is easy to guarantee—all operations execute under the protection of the lock, so there are no concurrent access issues. The disadvantage is also obvious: all operations are serialized. Even if two operations access different keys, they must queue for the same lock. In low contention scenarios (few threads, low operation frequency), this is perfectly fine, but in high concurrency scenarios, this lock becomes the ceiling for throughput.

There is an easily overlooked trap: the interface atomicity problem. The `find` and `set` above are individually atomic, but a composite operation like "get first, then decide whether to set based on the result" is not atomic—the lock is released between the two operations, allowing other threads to step in and change the map's state. For example, if you need a "insert only if not exists" semantic, you can't call `find` then `set`; you must provide an atomic operation that wraps both steps:

```cpp
    bool insert_if_absent(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto [it, inserted] = map_.insert({key, value});
        return inserted;
    }
```

This method puts "lookup" and "insert" under the protection of a single lock, ensuring atomicity. When designing the interface for a concurrent container, you need to provide atomic versions of all composite operations—otherwise, callers must either lock themselves (violating encapsulation) or write code with race conditions.

Another trap is iterator invalidation. `std::unordered_map` invalidates all iterators during a rehash. `std::map`'s insert operation doesn't invalidate iterators, but `erase` invalidates the iterator of the deleted element. However, in concurrent scenarios, the critical issue isn't the container's own invalidation rules—it's that after the lock is released during traversal, other threads may modify the container, causing iterator invalidation, crashes, or reading inconsistent data. The solution is to hold the lock continuously during traversal—but this means other threads are completely blocked during the traversal. If the traversal takes a long time, this blocking may be unacceptable.

## Fine-Grained Locking: Locking by Bucket/Node

Okay, the problem with coarse-grained locking is clear—the lock granularity is too coarse, all operations share one lock, even if they operate on completely unrelated data. So the idea is natural: split the container into multiple independent parts, each with its own lock, and operations only contend for the part they need.

Hash tables are naturally suited for this split because they are already bucketed—each key maps to a bucket via a hash function, and elements in different buckets are unrelated. We can give each bucket a lock, so threads operating on different buckets don't contend.

```cpp
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class StripedMap {
public:
    StripedMap(size_t num_buckets = 16) : buckets_(num_buckets) {}

    bool find(const Key& key, Value& value) const {
        size_t bucket_idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets_[bucket_idx].mutex);
        for (const auto& [k, v] : buckets_[bucket_idx].list) {
            if (k == key) {
                value = v;
                return true;
            }
        }
        return false;
    }

    void set(const Key& key, const Value& value) {
        size_t bucket_idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets_[bucket_idx].mutex);
        for (auto& [k, v] : buckets_[bucket_idx].list) {
            if (k == key) {
                v = value;
                return;
            }
        }
        buckets_[bucket_idx].list.push_front({key, value});
    }

    void erase(const Key& key) {
        size_t bucket_idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets_[bucket_idx].mutex);
        buckets_[bucket_idx].list.remove_if([&key](const auto& item) {
            return item.first == key;
        });
    }

private:
    struct Bucket {
        mutable std::mutex mutex;
        std::list<std::pair<Key, Value>> list;
    };

    size_t get_bucket_index(const Key& key) const {
        return hasher_(key) % buckets_.size();
    }

    std::vector<Bucket> buckets_;
    Hash hasher_;
};
```

Here each `Bucket` has its own `mutex` and `list` (implemented with `std::list` to avoid the reallocation problem of `std::vector`). `find`, `set`, and `erase` only lock the specific bucket corresponding to the key. Threads operating on different buckets run completely in parallel; contention only occurs when operating on the same bucket.

The throughput of fine-grained locking depends on the number of buckets and the quality of the hash function. More buckets mean less contention; a more uniform hash function means better load balancing. But the number of buckets can't be increased indefinitely—every extra bucket adds one `mutex` (a `pthread_mutex_t` on Linux takes at least 40 bytes), and if there are too many buckets but too few elements, most buckets are empty, wasting memory.

The biggest implementation challenge for fine-grained locking is **rehash**. When the number of elements grows to a certain point, the hash table needs to expand—increase the number of buckets and redistribute all elements. Rehash needs to access all buckets, not just one—meaning it needs to lock all bucket mutexes. If other threads are still operating on the container during a rehash, deadlock or data inconsistency will occur. The solution is to use a global write lock during rehash to stop all other operations—but this essentially degrades into coarse-grained locking, only happening during rehash. A more sophisticated approach is incremental rehash: instead of moving all elements at once, move a small portion each operation, spreading the rehash cost over multiple operations. Java's `ConcurrentHashMap` uses this strategy. However, this greatly increases implementation complexity, so we won't expand on it here.

Also, a detail that might confuse you: `find` in `StripedMap` is declared as `const`. This is because `find` is a `const` method but it needs to acquire a mutex—`const` methods cannot modify member variables, but mutex's `lock` essentially modifies the mutex's internal state. If you miss `mutable`, the compiler will error directly. The `mutable` keyword is designed for this scenario—"logically doesn't change object state, but physically needs to modify internal data"—this usage is very common in concurrent containers.

## Striped Locking: N Shards, Each with a `mutex`

At this point, you might find a contradiction: the number of locks in fine-grained locking equals the number of buckets—if there are many buckets, the lock overhead is huge. Each mutex takes at least a few dozen bytes, and the operating system has extra costs to manage many locks. Striped locking (also called sharded lock) is a compromise born to solve this contradiction: split the container into N shards, each shard with one lock, but the number of shards is much smaller than the number of buckets. A key's shard is decided by the key's hash value modulo the number of shards.

The difference between striped locking and fine-grained locking is granularity: fine-grained locking is one lock per bucket, striped locking is one lock shared by every K buckets. Striped locking has slightly more contention than fine-grained locking (operations on different buckets but the same shard contend), but the number of locks is drastically reduced—usually 16 to 64 shards are enough, no need to grow linearly with bucket count.

Let's implement a striped locked concurrent cache. The typical scenario for this cache is a routing cache in an HTTP server or a database query cache—read-many, write-few, reads need to be fast, writes can tolerate some delay.

```cpp
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ShardedCache {
public:
    explicit ShardedCache(size_t num_shards = 16) : shards_(num_shards) {}

    bool get(const Key& key, Value& value) const {
        size_t shard_idx = get_shard_index(key);
        std::shared_lock<std::shared_mutex> lock(shards_[shard_idx].mutex);
        auto it = shards_[shard_idx].map.find(key);
        if (it != shards_[shard_idx].map.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    void put(const Key& key, const Value& value) {
        size_t shard_idx = get_shard_index(key);
        std::unique_lock<std::shared_mutex> lock(shards_[shard_idx].mutex);
        shards_[shard_idx].map[key] = value;
    }

    void erase(const Key& key) {
        size_t shard_idx = get_shard_index(key);
        std::unique_lock<std::shared_mutex> lock(shards_[shard_idx].mutex);
        shards_[shard_idx].map.erase(key);
    }

    size_t size() const {
        size_t total = 0;
        for (auto& shard : shards_) {
            std::shared_lock<std::shared_mutex> lock(shard.mutex);
            total += shard.map.size();
        }
        return total;
    }

private:
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<Key, Value, Hash> map;
    };

    size_t get_shard_index(const Key& key) const {
        return hasher_(key) % shards_.size();
    }

    std::vector<Shard> shards_;
    Hash hasher_;
};
```

This implementation has several noteworthy design decisions. First, we used `std::shared_mutex` (C++17) instead of `std::mutex`—read operations acquire `shared_lock` (shared lock, multiple readers can parallelize), write operations acquire `unique_lock` (exclusive lock, exclusive access). In a "read-many, write-few" cache scenario, this distinction is critical: if 90% of operations are `get`, the shared lock allows these 90% of operations to execute almost contention-free in parallel; only `put` and `erase` need exclusivity. If you used `std::mutex`, both reads and writes need exclusive locks, and read parallelism is completely lost.

Second, the `size` method traverses each shard in order, acquiring a shared lock on each shard. This means shards are unlocked one by one during traversal—after traversing one shard, release its lock, then lock the next shard. The benefit of this strategy is not holding all locks at the same time (avoiding deadlock risk), but the cost is that the traversal result might not reflect a global snapshot of any single point in time (after one shard is traversed and before the next is locked, a write operation might modify data). If you need a true global snapshot, you must lock all shards simultaneously—but this increases deadlock risk (if other code is also acquiring shard locks in some order).

Third, the number of shards is fixed (determined at construction, unchanged afterwards). This avoids the complexity of rehash—the `std::unordered_map` inside each shard can freely rehash (protected by the shard-level lock), but the number of shards and the key-to-shard mapping won't change. This is an important simplification: if your cache needs to dynamically adjust the number of shards (like auto-expansion based on load), you need to handle synchronization during shard migration, which is much more complex than static sharding.

## Copy-on-Write: The Ultimate Optimization for Lock-Free Reads

Striped locking performs well in read-many, write-few scenarios, but read operations still need to acquire a shared lock—although a shared lock is much lighter than an exclusive lock, in extreme high-frequency read scenarios (like millions of reads per second), the lock overhead is still non-negligible. You might ask: is there a way to make read operations completely lock-free? The answer is yes.

Copy-on-Write (CoW) is exactly such a strategy. The core idea is: write operations don't directly modify shared data, but create a complete copy, modify on the copy, then use an atomic operation to switch the pointer from old data to new data. Read operations directly read the data pointed to by the pointer—because write operations don't modify the old data (only create new data), read operations don't need any synchronization.

```cpp
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class CowMap {
public:
    bool get(const Key& key, Value& value) const {
        std::shared_ptr<std::unordered_map<Key, Value, Hash>> local_map;
        // Atomic load: acquire a shared_ptr to the current map
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            local_map = std::atomic_load(&map_ptr_);
        }

        auto it = local_map->find(key);
        if (it != local_map->end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    void put(const Key& key, const Value& value) {
        // 1. Acquire write lock to protect pointer swap
        std::unique_lock<std::shared_mutex> lock(mutex_);

        // 2. Copy the entire map
        auto new_map = std::make_shared<std::unordered_map<Key, Value, Hash>>(*map_ptr_);

        // 3. Modify the copy
        (*new_map)[key] = value;

        // 4. Atomically switch the pointer
        std::atomic_store(&map_ptr_, new_map);
    }

private:
    mutable std::shared_mutex mutex_; // Protects pointer swap, not data
    std::shared_ptr<std::unordered_map<Key, Value, Hash>> map_ptr_;
    Hash hasher_;
};
```

Let's break down this implementation step by step. `map_ptr_` is a `shared_ptr`, pointing to the current map data. The read operation `get` acquires a copy of the current `shared_ptr` via `atomic_load` (the reference count of `shared_ptr` is atomically incremented), then performs the lookup on the acquired map. Because write operations never modify old data—they only create new data and then atomically switch pointers—the data pointed to by the `shared_ptr` acquired by the read operation is valid and consistent throughout the read process, requiring no locks.

The write operation `put` flows in three steps. First acquire `unique_lock`—this mutex isn't protecting the data itself (data is in `map_ptr_`, protected by atomic operations), but protecting mutual exclusion between write operations: ensuring only one write operation creates a copy at a time, otherwise two write operations might each copy the old data, modify each, then each write to the pointer, and the later write would overwrite the earlier write's modification. Then modify on the copy. Finally use `atomic_store` to switch the pointer to the new data—this operation is atomic, ensuring read operations see either the old data or the new data, never an intermediate state.

The cost of CoW is obvious: every write operation needs to copy the entire map. If the map has 10,000 elements, one `put` copies 10,000 elements. So CoW is only suitable for "reads far exceed writes" scenarios—like config tables, routing tables, dictionary data—writes happen occasionally, reads are frequent and require low latency. If writes are also frequent, CoW's copy overhead will eat up the gains from lock-free reads.

Regarding `atomic_load` and `atomic_store`: they are `std::shared_ptr` atomic operation functions provided by C++11 (defined in `<memory>`). C++20 introduced `std::atomic<std::shared_ptr>` as a replacement with a clearer interface and similar underlying implementation—both using CAS (compare-and-swap) loops or a global spinlock to guarantee atomic update of the `shared_ptr` control block pointer. Note that C++20 has deprecated `std::atomic_load`/`std::atomic_store` and other `std::shared_ptr` atomic free functions, planning to remove them in C++26. If your project uses C++20 or higher, it's recommended to use `std::atomic<std::shared_ptr>` directly. In our scenario, the atomic operation on `shared_ptr` only involves pointer reads and writes (not copying map data), so the overhead is very small.

Another detail worth noting: the `get` method returns a copy of the value, not a snapshot. If you need to return an immutable snapshot, you can return a `shared_ptr` to the map. Callers can hold this snapshot for any length of time without worrying about data changes—because the underlying CoW mechanism guarantees old data won't be destroyed until the last reference is released. This feature is very useful in scenarios requiring "consistent reads," like aggregate calculations over the entire map.

## Usage Strategy for `std::shared_mutex`

We already used `std::shared_mutex` in the striped lock implementation above, but haven't discussed its usage boundaries in concurrent containers in detail. This topic deserves special expansion because it's more subtle than most people think.

`std::shared_mutex` (C++17, defined in `<shared_mutex>`) provides two lock modes: shared mode (`lock_shared`) and exclusive mode (`lock`). Multiple threads can hold a shared lock simultaneously, but an exclusive lock blocks all other lock requests (shared and exclusive). This makes it particularly effective in "read-many, write-few" scenarios—we've seen the effect in `ShardedCache` above.

But `std::shared_mutex` isn't a panacea. First, performance-wise: its overhead is larger than a normal `std::mutex`—on Linux, `std::shared_mutex` is usually implemented based on `pthread_rwlock_t`, internally needing to maintain a reader count and a waiter queue, acquiring and releasing locks is heavier than `std::mutex`. In "half-read, half-write" or "write-more-than-read" scenarios, `std::shared_mutex` performance might even be worse than a normal `std::mutex`.

Another pitfall I've encountered—writer starvation. If new readers constantly acquire shared locks, the writer might never get a chance at an exclusive lock—because as long as any reader holds a shared lock, the writer can't acquire an exclusive lock. Linux glibc's `pthread_rwlock_t` defaults to reader-preference strategy (continuously arriving readers constantly delay the writer's chance to acquire the lock, a typical cause of writer starvation), but the C++ standard doesn't guarantee this. If your application is sensitive to write latency, be sure to test your platform's `std::shared_mutex` scheduling policy.

A practical rule of thumb: when read operations account for more than 80% of total operations, the benefits of `std::shared_mutex` become obvious. If the read-write ratio is close to 1:1 or writes are more, using a normal `std::mutex` is simpler and more efficient.

## Trade-offs of the Four Strategies

At this point, we've gone through all four strategies. Looking back, their trade-off relationship is actually quite clear. Let's compare them in a table:

| Strategy | Read Performance | Write Performance | Implementation Complexity | Applicable Scenarios |
|----------|------------------|-------------------|---------------------------|----------------------|
| Coarse-Grained Locking | Low (exclusive lock) | Low (exclusive lock) | Low | Low contention, prototype verification |
| Fine-Grained Locking | Medium (bucket-level lock) | Medium (bucket-level lock) | High (rehash is difficult) | High contention hash table |
| Striped Locking | High (shard-level shared lock) | Medium (shard-level exclusive lock) | Medium | Read-many, write-few cache |
| Copy-on-Write | Very High (lock-free read) | Low (full copy) | Medium | Config table, routing table |

The key to choosing a strategy isn't which is "fastest," but your specific scenario. You need to answer a few questions: what is the read-write ratio? How large is the data volume? What is the frequency and duration of write operations? Do you need strong consistency snapshots? Can you tolerate data loss? The answers to these questions determine which strategy fits best.

Honestly, most projects in the early stages don't need a strategy more complex than coarse-grained locking—coarse-grained locking is correct, simple, and easy to debug. Only after you confirm through performance testing that lock contention is the bottleneck should you consider upgrading to striped or fine-grained locking. Premature optimization is the root of all evil, especially in concurrent container design—finer-grained locks mean more subtle bugs and harder-to-reproduce deadlocks.

## Where We Are

In this post, starting from "why STL containers aren't thread-safe," we discussed four concurrent container design strategies. Coarse-grained locking uses one `mutex` to protect the entire container—simple and correct, but throughput limited by lock contention. Fine-grained locking pushes locks down to the bucket/node level, greatly reducing contention, but handling rehash makes implementation complexity skyrocket. Striped locking strikes a compromise between coarse and fine—few shards each with a `std::shared_mutex`, writes only lock relevant shards, reads share parallelism. Copy-on-Write pushes reads to the lock-free extreme, at the cost of copying all data on every write—only suitable for read-far-exceeds-write scenarios.

These four strategies aren't progressive relationships, but parallel tools for different scenarios. The key to choice is understanding your read-write patterns and data characteristics. Don't rush to the most complex solution—next time we'll discuss more extreme strategies—lock-free data structures—replacing all locks with atomic operations. But before you consider lock-free, get the lock-based solutions right first; after all, locks suffice for most scenarios.

> 💡 Complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `thread_safe_containers`.

## Exercises

### Exercise 1: Concurrent Cache with Striped Locking

Based on `ShardedCache` in this post, add a `get_or_compute` method—if the key exists, return the value directly; if not, call a `compute` function to calculate the value, store it in the cache, and return it. Requirement: the entire process of "find if absent then compute and insert" must be atomic (no two threads should compute the value for the same key simultaneously).

Hint: In `get_or_compute`, you need to acquire an exclusive lock on the shard (can't use a shared lock, because you might write). If you want to use a shared lock on the fast path of "key already exists" to improve read performance, consider acquiring a shared lock to search first, then upgrading to an exclusive lock if not found—but `std::shared_mutex` doesn't directly support lock upgrade; you need to release the shared lock then acquire the exclusive lock, and there is a time window in between to handle.

### Exercise 2: Performance Test for Copy-on-Write

Write a benchmark program to compare `CowMap` and `ShardedCache` performance under different read-write ratios. Test scenario: 10,000 keys, 4 read threads and 1 write thread running simultaneously for 10 seconds, counting total read throughput (ops/sec). Then rerun with 1 read thread and 4 write threads and compare results.

Expected result: In read-many, write-few (4 read, 1 write) scenarios, `CowMap`'s read throughput should be significantly higher than `ShardedCache` (because lock-free read vs read needs to acquire mutex). In write-many, read-few (1 read, 4 write) scenarios, `CowMap`'s performance will drop significantly (because every write needs to copy the entire map).

### Exercise 3: Impact of Shard Count on Performance

Modify `ShardedCache`'s constructor to accept different shard count parameters (like 1, 4, 16, 64, 256). Run a benchmark with 8 threads (4 read, 4 write) and observe throughput changes under different shard counts. Expectation: as shards increase from 1 to 16, throughput improves significantly, but after a certain value, improvement slows or even decreases (because lock management overhead starts to show).

## References

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::atomic_load, std::atomic_store for shared_ptr -- cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [Concurrent Hash Table Designs -- bluuewhale.github.io](https://bluuewhale.github.io/posts/concurrent-hashmap-designs/)
- [Design Concurrent HashMap -- AlgoMaster.io](https://algomaster.io/learn/concurrency-interview/design-concurrent-hashmap)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 6 & 7](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [P1761R0: Concurrent Map Customization Options -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1761r0.pdf)
