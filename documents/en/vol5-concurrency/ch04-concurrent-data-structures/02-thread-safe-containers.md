---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Design and trade-offs of four strategies: coarse-grained locking, fine-grained
  locking, sharded locks, and copy-on-write'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Thread-Safe Queue
- Reader-Writer Locks and shared_mutex
reading_time_minutes: 23
related:
- Lock-Free Programming Fundamentals
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
  translated_at: '2026-09-26T08:16:27+00:00'
  engine: anthropic
  token_count: 12400
---
# Thread-Safe Container Design

Honestly, the first time I needed to write a "map that multiple threads can use," my first reaction was—how hard can this be? Isn't it just wrapping every operation in a lock_guard? Then I actually sat down and wrote it, and discovered things were nowhere near that simple. Adding a lock isn't hard; what's hard is adding it correctly, adding enough of it, and adding exactly the right amount. Make the lock too coarse and performance blows up; make it too fine and correctness blows up; put it in the wrong place and you get a data race blowing up in your face.

In the previous post we turned a thread-safe queue from a teaching toy into a production-grade component—adding a close mechanism, timed operations, stop_token cancellation, and a backpressure strategy. That queue used a single mutex to protect its entire internal state, which is the simplest, most brute-force form of synchronization. For a data structure with operations as simple as a queue's, one lock is enough. But once we face more complex containers—map, set, hash tables—a single lock becomes a performance bottleneck: every thread, no matter which element it touches, has to line up for the same lock.

In this post we'll walk through four design strategies for thread-safe containers at different levels of sophistication—from coarse-grained locking to fine-grained locking, from sharded locks to copy-on-write. They don't replace one another; they are tools fitted to different scenarios. Our goal is to understand each strategy's applicable conditions, implementation complexity, and performance characteristics, so that when a concrete requirement shows up we can make a reasonable choice.

## Why STL Containers Are Not Thread-Safe

Before diving into design strategies, let's answer a common question: why aren't the C++ standard library containers (`std::vector`, `std::map`, `std::unordered_map`, and friends) thread-safe?

The C++ standard makes very limited guarantees about concurrent container access: multiple read operations (calls to `const` member functions) on the same container are safe and need no external synchronization; but the moment there is a single write (a call to a non-`const` member function), every other concurrent access—read or write—must be synchronized. In other words, "many readers, no writers" is safe; "a write exists" means you need locking.

The standard library's lack of thread safety isn't an oversight—it's a deliberate trade-off. Different scenarios have wildly different needs for "thread safety." A read-only query cache and a counter table written at high frequency want completely different synchronization strategies. If standard containers baked in some thread-safety mechanism (say, an internal lock per operation), scenarios that don't need thread safety would pay a performance price for nothing, while scenarios needing finer-grained control would find the built-in lock granularity too coarse—nobody wins. The standard picked the most conservative stance: do no synchronization, and leave the decision to the user.

This has a practical consequence: when you write multithreaded code on STL containers, you must lock outside the container. "External locking" is easy to say and full of pitfalls to do—atomicity of composite operations, iterator invalidation, choosing lock granularity—these are what this post is really about.

## Coarse-Grained Locking: One mutex to Protect Everything

Let's start with the most naive scheme—one mutex protecting the entire container; every operation acquires the lock before it runs and releases it afterwards. The `BoundedQueue` from the previous post follows exactly this pattern: crude, yes, but correctness is the easiest to guarantee.

Here's a concurrent map with coarse-grained locking:

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

The strength of coarse-grained locking is that correctness is easy to guarantee—all operations run under the protection of the lock, so no concurrent-access problem is left. The weakness is just as obvious: all operations are serialized; even two operations touching entirely different keys must queue for the same lock. In low-contention situations (few threads, low operation frequency) that's perfectly fine, but under heavy concurrency this lock becomes the ceiling on your throughput.

There's a trap that's easy to overlook: interface atomicity. The `get` and `set` above are individually atomic, but a composite operation like "first get, then decide from the result whether to set" is not—the lock is released between the two calls, and another thread can slip in and change the map's state. For instance, if you need an "insert only if absent" semantic, you can't call `contains` and then `set`; you must provide one atomic operation that wraps both steps:

```cpp
// Atomic "get or insert"
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

This method puts "lookup" and "insert" under the protection of a single lock acquisition, guaranteeing atomicity. When you design a concurrent container's interface, you need to provide atomic versions of every composite operation—otherwise callers will either lock on their own (breaking encapsulation) or write code with a race condition in it.

Another trap is iterator invalidation. A `std::unordered_map` rehash invalidates all iterators; `std::map` insertion doesn't invalidate iterators, but `erase` invalidates the iterators of the erased element. In a concurrent setting, though, the crux isn't the container's own invalidation rules—it's that once the lock is released mid-traversal, another thread may have modified the container, leaving you with invalidated iterators, a crash, or inconsistent reads. The fix is to hold the lock for the entire traversal—but that also means every other thread is fully blocked while you walk. If the traversal takes long, that blocking may be unacceptable.

## Fine-Grained Locking: Lock by Bucket/Node

Alright, the problem with coarse-grained locking is now clear—the granularity is too coarse: all operations share one lock even when they touch completely unrelated data. So the idea suggests itself naturally: split the container into several independent parts, give each part its own lock, and let an operation contend only for the part it needs.

Hash tables take to this split naturally, because a hash table is already bucketed—each key maps to a bucket through the hash function, and elements in different buckets are unrelated. We can give each bucket its own lock, so threads operating on different buckets never contend.

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

Here each `Bucket` owns its `mutex` and its `entries` (a linked list built on `std::list`, sidestepping `std::vector`'s reallocation problem). `get`, `set`, and `erase` lock only the one bucket the key maps to. Threads hitting different buckets run fully in parallel; contention happens only when operations land on the same bucket.

Throughput under fine-grained locking depends on the bucket count and the quality of the hash function. More buckets, less contention; a more uniform hash function, better load balancing. But the bucket count can't grow without bound—each extra bucket is an extra mutex (on Linux a `pthread_mutex_t` occupies at least 40 bytes), and with many buckets but few elements most of them sit empty, wasting memory.

The biggest implementation headache for fine-grained locking is **rehash**. When the element count grows past a point, the hash table must expand—adding buckets and redistributing every element. A rehash has to access all buckets, not just one—which means locking every bucket's mutex. If other threads keep operating on the container mid-rehash, you get deadlock or inconsistent data. The way out is to block all other operations with a global write lock during the rehash—but that essentially degenerates into coarse-grained locking, just only while the rehash runs. A more refined approach is incremental rehash: instead of moving all elements at once, move a small slice on each operation and spread the rehash cost across many operations. Java's `ConcurrentHashMap` adopts exactly this strategy. It does, however, add a lot of implementation complexity, so we won't go down that road here.

One more detail that may puzzle you: the `mutex` inside `Bucket` is declared `mutable`. That's because `get` is a `const` member function, yet it needs to acquire the mutex—a `const` member function must not modify member variables, but a mutex's `lock()` is, at bottom, modifying the mutex's internal state. Leave out `mutable` and the compiler rejects the code outright. The `mutable` keyword exists precisely for this kind of situation—"logically the object's state doesn't change, but physically some internal data must be modified"—and in concurrent containers this idiom is everywhere.

## Sharded Locking: N Shards, Each with Its Own mutex

By now you've probably spotted a tension: in fine-grained locking the number of locks equals the number of buckets—if buckets are many, locks get expensive; every mutex costs at least a few dozen bytes, and the OS pays extra to manage huge numbers of locks. The sharded lock (also called a striped lock) is the compromise built to resolve that tension: split the container into N shards, one lock per shard, but with far fewer shards than buckets. Which shard a key belongs to is decided by the key's hash value modulo the shard count.

The difference between sharded and fine-grained locking is the granularity: fine-grained is one lock per bucket; sharded is one lock shared by every K buckets. Contention is somewhat higher than fine-grained (operations on different buckets within the same shard still contend), but the lock count drops dramatically—16 to 64 shards usually suffice, with no need to grow linearly with the bucket count.

Let's implement a sharded concurrent cache. The typical home for such a cache is a routing cache in an HTTP server or a database query cache—read-heavy and write-light: reads must be fast, writes can tolerate a little latency.

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
        // Reads take a shared_lock, allowing multiple readers in parallel
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
        // Writes take a unique_lock for exclusive access
        std::unique_lock<std::shared_mutex> lock(shard.rw_mutex);
        shard.cache[key] = value;
    }

    void erase(const Key& key)
    {
        auto& shard = get_shard(key);
        std::unique_lock<std::shared_mutex> lock(shard.rw_mutex);
        shard.cache.erase(key);
    }

    // Walk every shard and invoke the callback for each key-value
    // Note: this operation locks all shards
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

This implementation packs several design decisions worth a closer look. First, we used `std::shared_mutex` (C++17) instead of `std::mutex`—read operations acquire a `shared_lock` (shared mode; multiple readers can proceed in parallel), write operations acquire a `unique_lock` (exclusive mode; access is exclusive). In a read-heavy, write-light cache this distinction matters enormously: if 90% of operations are `get`, the shared lock lets those 90% run in parallel with almost no contention, and only `set` and `erase` need exclusivity. With a plain `std::mutex`, both reads and writes need the exclusive lock, and read parallelism evaporates entirely.

Second, the `for_each` method walks the shards in order, taking a shared lock on each one. That means shards unlock one at a time—finish a shard, release its lock, then lock the next shard. The upside of this strategy is that you never hold all the locks at once (dodging deadlock risk); the downside is that the traversal result may not reflect a global snapshot at any single instant (a write may sneak in after one shard is traversed but before the next is locked). If you need a true global snapshot, you must lock every shard simultaneously—but that raises the deadlock risk again (if some other code also acquires shard locks in some order).

Third, the shard count is fixed (decided at construction, never changed afterwards). This sidesteps rehash complexity—the `unordered_map` inside each shard can rehash freely (protected by the shard-level lock)—but the number of shards and the key-to-shard mapping never change. That's an important simplification: if your cache must adjust its shard count dynamically (auto-scaling with load, say), you have to handle synchronization during shard migration, which is far more complex than static sharding.

## Copy-on-Write: Taking Lock-Free Reads to the Extreme

Sharded locking performs well in read-heavy, write-light scenarios, but reads still have to acquire a shared lock—a shared lock is far lighter than an exclusive one, yet under extreme read frequencies (millions of reads per second, say) the lock's cost is still non-negligible. You might ask: is there a way to make read operations completely lock-free? There is.

Copy-on-Write (CoW) is exactly that strategy. The core idea: a write doesn't modify the shared data directly; it creates a complete copy, modifies the copy, then uses an atomic operation to swing the pointer from the old data to the new. Reads just follow the pointer—because a write never modifies the old data (it only creates new data), a read needs no synchronization at all.

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
        // Atomically grab the shared_ptr to the current data
        // Reads are completely lock-free
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

        // 1. Copy the current data
        auto new_data = std::make_shared<Data>(*std::atomic_load(&data_));

        // 2. Modify the copy
        (*new_data)[key] = value;

        // 3. Atomically swap the pointer
        std::atomic_store(&data_, new_data);
    }

    void erase(const Key& key)
    {
        std::lock_guard<std::mutex> lock(write_mutex_);

        auto new_data = std::make_shared<Data>(*std::atomic_load(&data_));
        new_data->erase(key);
        std::atomic_store(&data_, new_data);
    }

    // Get a snapshot of the current data - reads are lock-free
    std::shared_ptr<const Data> snapshot() const
    {
        return std::atomic_load(&data_);
    }

private:
    using Data = std::unordered_map<Key, Value>;

    mutable std::mutex write_mutex_;  // Only serializes writers against each other
    std::shared_ptr<Data> data_;
};
```

Let's take this implementation apart step by step. `data_` is a `shared_ptr<Data>` pointing at the current map data. The read operation `get` obtains a copy of the current `data_` via `std::atomic_load` (the `shared_ptr`'s reference count is incremented atomically), then searches the map it obtained. Because a write never modifies the old data—it only creates new data and then atomically swings the pointer—the data behind the `shared_ptr` a read holds stays valid and consistent for the entire read, with no lock required.

The write operation `set` runs in three steps. First it acquires `write_mutex_`—this mutex doesn't protect the data itself (the data lives in a `shared_ptr`, protected by atomic operations) but the mutual exclusion between writes: it guarantees only one write is building a copy at a time, because otherwise two writes would each copy the old data, each apply its own modification, and each store its own pointer—the later store would clobber the earlier one's modification. Then it modifies the copy. Finally `std::atomic_store` swings the pointer to the new data—that operation is atomic, guaranteeing that a read sees either the old data or the new data, never an intermediate state.

CoW's price is plain to see: every write copies the entire map. With 10,000 elements in the map, a single `set` copies 10,000 elements. Hence CoW only fits "reads far outnumber writes" scenarios—configuration tables, routing tables, dictionary data—where writes happen occasionally and reads are frequent and latency-sensitive. If writes are frequent too, CoW's copying overhead will eat up whatever the lock-free reads gained you.

About `std::atomic_load` and `std::atomic_store`: they are the `shared_ptr` atomic operation functions C++11 provided (defined in `<memory>`). C++20 introduced `std::atomic<std::shared_ptr<T>>` as the replacement—cleaner interface, similar implementation underneath—both use a CAS (compare-and-swap) loop or a global spinlock to keep the `shared_ptr` control-block pointer updated atomically. Note that C++20 has deprecated `std::atomic_load`, `std::atomic_store`, and the other `shared_ptr` atomic free functions, with removal planned for C++26. If your project is on C++20 or a later standard, prefer `std::atomic<std::shared_ptr<T>>` outright. In our scenario, the atomic operations on `shared_ptr` involve only reading and writing the pointer (not copying the map data), so the overhead is tiny.

One more detail worth flagging: `snapshot()` returns a `shared_ptr<const Data>`—an immutable snapshot. The caller can hold that snapshot for as long as it likes without worrying about the data changing, because the underlying CoW mechanism guarantees the old data won't be destroyed until the last reference is released. This property is extremely useful whenever you need a "consistent read"—traversing the entire map to compute an aggregate, for example.

## Usage Strategy for std::shared_mutex

We already used `std::shared_mutex` in the sharded-lock implementation above, but we haven't talked carefully about where its boundaries lie inside a concurrent container. The topic deserves a section of its own, because it's subtler than most people think.

`std::shared_mutex` (C++17, defined in the `<shared_mutex>` header) offers two lock modes: shared mode (`shared_lock`) and exclusive mode (`unique_lock`). Multiple threads can hold the shared lock simultaneously, but the exclusive lock blocks every other lock request (shared or exclusive). That's what makes it especially effective in "read-heavy, write-light" scenarios—we already saw it pay off in `ShardedCache` above.

But `shared_mutex` is no cure-all. Start with performance: it costs more than a plain `mutex`—on Linux, `shared_mutex` is typically built on `pthread_rwlock_t`, which internally maintains a reader count and a waiter queue, so acquiring and releasing it is heavier than `pthread_mutex_t`. In "reads and writes half and half" or "writes outnumber reads" scenarios, `shared_mutex` may actually perform worse than a plain `mutex`.

Then a pit I stepped in personally—writer starvation. If new readers keep arriving and taking the shared lock, the writer may never get a shot at the exclusive lock—because the writer cannot acquire exclusivity while any reader at all holds the shared lock. Linux glibc's `pthread_rwlock_t` defaults to a reader-preference policy (a steady stream of readers keeps postponing the writer's chance to acquire the lock—the textbook cause of writer starvation), but the C++ standard doesn't guarantee this. If your application is sensitive to write latency, be sure to test the scheduling policy of `shared_mutex` on your platform.

A practical rule of thumb: `shared_mutex` pays off clearly only when read operations make up more than 80% of all operations. If the read/write ratio is close to 1:1 or writes dominate, a plain `mutex` is simpler and more efficient.

## Trade-offs of the Four Strategies

At this point we've been through all four strategies, and looking back, their trade-off relationships are actually quite clear. Let's put them side by side in a table:

| Strategy | Read performance | Write performance | Implementation complexity | Applicable scenarios |
|------|--------|--------|------------|----------|
| Coarse-grained lock | Low (exclusive lock) | Low (exclusive lock) | Low | Low contention, prototype validation |
| Fine-grained lock | Medium (bucket-level lock) | Medium (bucket-level lock) | High (rehash is hard) | High-contention hash tables |
| Sharded lock | High (shard-level shared lock) | Medium (shard-level exclusive lock) | Medium | Read-heavy, write-light caches |
| Copy-on-Write | Extreme (lock-free reads) | Low (full copy) | Medium | Config tables, routing tables |

The key to choosing a strategy isn't which one is "fastest"—it's your concrete scenario. You need to answer a few questions: what's the read/write ratio? How large is the data volume? How frequent and how slow are writes? Do you need strongly consistent snapshots? Can you tolerate data loss? The answers to these questions determine which strategy fits best.

Honestly, most projects in the early stage don't need anything more complex than coarse-grained locking—coarse-grained locking is correct, simple, and easy to debug. Only after performance tests confirm that lock contention is the bottleneck should you consider upgrading to sharded or fine-grained locking. Premature optimization is the root of all evil, and especially so in concurrent container design—finer-grained locks mean more subtle bugs and harder-to-reproduce deadlocks.

## Where We Are

This post started from "why STL containers aren't thread-safe" and worked through four concurrent container design strategies. Coarse-grained locking guards the entire container with one mutex—simple and correct, but throughput is capped by lock contention. Fine-grained locking pushes locks down to the bucket/node level and slashes contention, but handling rehash sends implementation complexity up steeply. Sharded locking takes the middle ground between coarse and fine—a handful of shards each with its own `shared_mutex`; writes lock only the shard involved, reads run in parallel under the shared lock. Copy-on-Write pushes read operations to the lock-free extreme, at the cost of copying all the data on every write—suitable only for scenarios where reads far outnumber writes.

These four strategies aren't rungs on a ladder; they are parallel tools for different scenarios. The key to choosing is understanding your read/write patterns and your data's characteristics. Don't rush to the most complex option—in the next post we'll discuss the more extreme strategy, lock-free data structures, replacing every lock with atomic operations. But before you consider going lock-free, get the lock-based solutions right first; after all, for most scenarios locks are enough.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); see `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Exercises

### Exercise 1: A Concurrent Cache with Sharded Locking

Building on this post's `ShardedCache`, add the following feature: a `get_or_compute(key, factory)` method—if the key exists, return the value directly; if not, call `factory()` to compute the value, store it in the cache, and return it. The whole "look up, compute if absent, insert" sequence must be atomic (it must never happen that two threads simultaneously compute the value for the same key).

Hint: inside `get_or_compute`, you need the exclusive lock on the shard (a shared lock won't do, because you might write). If you want the shared lock on the "key already exists" fast path to boost read performance, you can first take the shared lock to search and upgrade to the exclusive lock on a miss—but `shared_mutex` doesn't support lock upgrade directly; you'd have to release the shared lock and then acquire the exclusive one, and there's a time window in between that needs handling.

### Exercise 2: Performance Testing of Copy-on-Write

Write a benchmark program comparing `CopyOnWriteMap` and `CoarseLockedMap` under different read/write ratios. Test scenario: 10,000 keys, 4 reader threads and 1 writer thread running simultaneously for 10 seconds; record the total read throughput (ops/sec). Then rerun with 1 reader thread and 4 writer threads and compare results.

Expected result: in the read-heavy, write-light scenario (4 readers, 1 writer), `CopyOnWriteMap`'s read throughput should be markedly higher than `CoarseLockedMap`'s (lock-free reads versus reads that must acquire a mutex). In the write-heavy, read-light scenario (1 reader, 4 writers), `CopyOnWriteMap`'s performance drops sharply (because every write copies the entire map).

### Exercise 3: The Effect of Shard Count on Performance

Modify `ShardedCache`'s constructor to accept different shard-count parameters (say 1, 4, 16, 64, 256). Run the benchmark with 8 threads (4 readers, 4 writers) and observe how throughput changes under different shard counts. Expectation: throughput climbs significantly as shards go from 1 to 16, but past a certain value the gains slow or even reverse (because lock-management overhead starts to show).

## References

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::atomic_load, std::atomic_store for shared_ptr -- cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [Concurrent Hash Table Designs -- bluuewhale.github.io](https://bluuewhale.github.io/posts/concurrent-hashmap-designs/)
- [Design Concurrent HashMap -- AlgoMaster.io](https://algomaster.io/learn/concurrency-interview/design-concurrent-hashmap)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 6 & 7](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [P1761R0: Concurrent Map Customization Options -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1761r0.pdf)
