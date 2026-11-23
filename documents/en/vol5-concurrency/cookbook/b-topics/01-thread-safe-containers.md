---
title: "Thread-Safe Container Strategies: Coarse-Grained Locks, Sharded Locks, Copy-on-Write"
description: "The mechanics and trade-offs of four thread-safe container design strategies: coarse-grained locking, per-bucket fine-grained locking, sharded locks, and Copy-on-Write, plus the usage boundaries of shared_mutex and writer starvation."
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
  - 容器
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/01-thread-safe-containers.md
  source_hash: 1d763157833318a9e3098f4e6804a9f917b13394d97522650c4d35f88a59ac72
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 17214
---
# Thread-Safe Container Strategies: Coarse-Grained Locks, Sharded Locks, Copy-on-Write

This piece lives in the cookbook's topical readings section. The Volume 5 required line finishes mutexes and lock management in Unit 2 ([Recovering the Lost Numbers — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks)) and bounded queues in Unit 3 ([A Queue with Capacity: First Make It Stand](../../chapter03-wait-and-coordinate/03-bounded-queue)); what they answer is "how to use one lock correctly". The questions at the container layer sit at another level: is one lock enough, when should it split into several, and with many readers and few writers, can reads be made lock-free? These design strategies go beyond the required scope, so we moved the whole articles into the readings section — everything the main text does not cover is here.

Before reading it, you need to be comfortable using mutex and lock_guard (Unit 2) and know what a data race and lock granularity are. The former's home chapter is [One Counter, Two Threads — Why Does the Math Come Out Wrong?](../../chapter02-sharing-data/01-from-wrong-programs); the latter belongs to [How to Adjust the Design](../../chapter06-validating-designs/05-adjusting-design). `std::shared_mutex` is already collected in Section A (the API recipe section) as the [shared_mutex Read-Write Locks](../a-API/04-shared-mutex) recipe — jump over if you want the quick reference; skipping it costs nothing, because this piece's "Strategies for Using std::shared_mutex" section carries its own explanation.

Let me also make the migration trail clear. This piece moved here in full from old-volume material; the mechanism explanations, code, and exercises are preserved as they were. What I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a pass over the material's old wording with my banned-words list. The measured figures claimed in the text (for example the memory footprint of `pthread_mutex_t`) carry over from the material's original records; I did not re-measure them this round.

The first time I needed to write a "map usable from multiple threads", my gut reaction was: what is so hard about this — isn't it just wrapping every operation in a `lock_guard`? Only when I actually sat down to write it did I realize things were nowhere near that simple. Locking itself is not hard; what is hard is locking correctly, sufficiently, and precisely. Lock too coarsely and performance blows up; lock too finely and correctness blows up; lock in the wrong place and a data race blows up outright.

The `BoundedQueue` we wrote in [A Queue with Capacity: First Make It Stand](../../chapter03-wait-and-coordinate/03-bounded-queue) protects all its internal state with one mutex — the most direct form of synchronization. For a structure with as simple an operation logic as a queue, one lock is enough. Swap in a more complex container such as a map, a set, or a hash table, and that one lock becomes a performance bottleneck: every thread, no matter which element it touches, has to queue for the same lock.

This piece discusses four grades of thread-safe container design strategies, from coarse-grained locks to fine-grained locks, from sharded locks to Copy-on-Write. These four tools each mind their own patch, with different applicable surfaces, and none replaces another. We want a firm grip on each strategy's applicable conditions, implementation complexity, and performance character — only then can you make the call when a concrete requirement lands.

## Why STL Containers Are Not Thread-Safe

Before unfolding the design strategies, let us answer a common question: why are the C++ standard library containers, such as `std::vector`, `std::map`, and `std::unordered_map`, not thread-safe?

The C++ standard's guarantees about concurrent container access are extremely limited. Translated into plain language: multiple read operations on the same container (calls to `const` member functions) are safe and need no external synchronization. But as soon as there is one write operation (a call to a non-`const` member function), all other concurrent access (reads or writes) must be synchronized. In other words, "many reads, no writes" is safe; any write means locking.

The standard library's lack of thread safety is a deliberate trade-off, not an oversight. Different scenarios differ enormously in what they need from "thread safety": a read-only query cache and a high-frequency counter table call for completely different synchronization strategies. If standard library containers carried some built-in thread-safety mechanism (say, an internal lock per operation), scenarios that do not need thread safety would pay a performance price for nothing, while scenarios needing finer-grained control would complain the built-in lock is too coarse — pleasing neither side. So the standard chose the most conservative strategy: no synchronization, leaving the decision to us users.

A practical consequence follows: when you write multithreaded code with STL containers, you must lock outside the container. "External locking" sounds simple, but doing it calls for care in several places: the atomicity of compound operations, iterator invalidation, and the choice of lock granularity. These are exactly what this piece really discusses.

## Coarse-Grained Locks: One Mutex to Guard Everything

We start from the plainest scheme — one mutex guarding the whole container; every operation acquires the lock before running and releases it after. The `BoundedQueue` in [A Queue with Capacity: First Make It Stand](../../chapter03-wait-and-coordinate/03-bounded-queue) follows the same pattern. Crude as it is, correctness is the easiest to guarantee.

Take a look at a coarse-locked concurrent map:

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

The advantage of coarse-grained locking is that correctness comes easily: every operation runs under the lock's protection, so concurrent access problems do not exist. The disadvantage is just as obvious: everything serializes. Even two operations touching different keys must queue for the same lock. In low-contention scenarios (few threads, low operation frequency) this is perfectly fine, but when high concurrency arrives, this lock becomes the ceiling on throughput.

One easily overlooked trap is interface atomicity. In the code above, `get` and `set` are each atomic on their own, but the compound operation "get, then decide based on the result whether to set" is not — the lock is released between the two calls, and another thread can slip in and change the map's state. For example, if you need "insert only if absent" semantics, you cannot call `contains` and then `set`; you must provide one atomic operation wrapping both steps:

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

`get_or_insert` puts "lookup" and "insert" under the protection of the same lock acquisition, guaranteeing atomicity. When designing a concurrent container's interface, you need to provide atomic versions of every compound operation; otherwise callers either lock on their own (breaking encapsulation) or write code with a race condition.

The other trap is iterator invalidation. `std::unordered_map` invalidates all iterators on rehash; `std::map`'s insert does not invalidate iterators, but `erase` invalidates iterators to the erased element. In concurrent settings, though, the pressing problem is not the container's own invalidation rules. What really matters is that once the lock is released during a traversal, another thread may have modified the container — invalidating iterators, crashing, or handing you inconsistent data. The fix is to hold the lock for the whole traversal, but you must also accept a price: other threads are fully blocked while you traverse. If the traversal takes long, that blocking may be unacceptable.

## Fine-Grained Locks: Locking per Bucket/Node

Right — the problem with coarse-grained locking is now clear: the granularity is too coarse; every operation shares one lock even when the operations touch completely unrelated data. The natural next thought: split the container into independent parts, give each part its own lock, and let each operation contend only for the part it needs.

Hash tables take to this splitting naturally, because a hash table is already bucketed — each key maps through the hash function to one bucket, and elements in different buckets are unrelated. Give each bucket a lock, and threads operating on different buckets never contend.

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

Each `Bucket` owns its `mutex` and its `entries` (a linked list built on `std::list`, sidestepping `std::vector`'s reallocation problem). `get`, `set`, and `erase` each lock only the bucket the key maps to. As you can see, threads operating on different buckets run fully in parallel; contention happens only when operations share a bucket.

Fine-grained locking throughput depends on the bucket count and the hash function's quality. More buckets, less contention. A more uniform hash function spreads the load more evenly. But the bucket count cannot grow without bound either: every extra bucket adds a mutex (on Linux, one `pthread_mutex_t` takes at least 40 bytes). With many buckets but few elements, most buckets sit empty and the memory is wasted.

The biggest implementation headache for fine-grained locking is **rehash**. When the element count grows past a point, the hash table must grow — adding buckets and redistributing every element. A rehash touches all buckets, not one, which means locking every bucket's mutex. If other threads keep operating on the container during the rehash, deadlocks or data inconsistency follow. One fix is a global write lock during rehash that blocks all other operations — but that is essentially a regression to coarse-grained locking, happening only at rehash time. A more refined approach is incremental rehash: instead of moving all elements at once, move a small portion on each operation, spreading the rehash cost across many operations. Java's `ConcurrentHashMap` uses exactly this strategy. It inflates implementation complexity considerably, so we will not unfold it here.

One more detail that might puzzle you: the `mutex` inside `Bucket` is declared `mutable`. That is because `get` is a `const` method yet needs to acquire the mutex — a `const` method may not modify members, while the mutex's `lock()` is in essence modifying the mutex's internal state. Leave out `mutable` and the compiler rejects the code outright. The `mutable` keyword was designed for exactly the "logically does not change object state, but physically must touch internal data" situations, and this usage is extremely common in concurrent containers.

## Sharded Locks: N Shards, One Mutex Each

At this point you will notice a tension: with fine-grained locking the lock count equals the bucket count — many buckets mean heavy lock overhead, with each mutex taking at least a few dozen bytes and the OS paying extra to manage a large number of locks. Sharded locks (also called striped locks) are the compromise born to resolve this tension: split the container into N shards, one lock per shard, with the shard count far below the bucket count. Which shard a key belongs to is decided by the key's hash value modulo the shard count.

The difference between sharded and fine-grained locking is granularity: fine-grained gives every bucket its own lock; sharded lets every K buckets share one. Contention rises slightly over fine-grained locking (operations on different buckets in the same shard contend), but the lock count drops sharply. A rough scale to keep in mind: 16 to 64 shards suffice — no need to grow linearly with the bucket count.

Let us implement a sharded concurrent cache. Its typical scenarios are route caches in HTTP servers or database query caches — many reads, few writes; reads must be fast, writes can tolerate a little latency.

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

Several design decisions in this implementation deserve attention. First, we used `std::shared_mutex` (C++17) in place of `std::mutex` — reads acquire a `shared_lock` (a shared lock; multiple readers run in parallel), writes acquire a `unique_lock` (an exclusive lock; sole access). In a "many reads, few writes" cache this distinction matters greatly: if 90% of operations are `get`, the shared lock lets those 90% run nearly contention-free in parallel, and only `set` and `erase` need exclusivity. With a `std::mutex`, both reads and writes require the exclusive lock, and the parallelism of reads vanishes entirely.

Second, the `for_each` method walks the shards in order, taking a shared lock on each. That means shards unlock one by one during the walk: finish one shard, release its lock, lock the next. The benefit of this policy is never holding all locks at once (avoiding deadlock risk); the cost is that the traversal result may not reflect a global snapshot at any single point in time (between finishing one shard and locking the next, a write may have changed the data). If you need a true global snapshot, you must lock all shards simultaneously — and accept the added deadlock risk (if other code also acquires shard locks in some order).

Third, the shard count is fixed (decided at construction, never changed afterward). This sidesteps rehash complexity: the `unordered_map` inside each shard may rehash freely (guarded by the shard-level lock), but the number of shards and the key-to-shard mapping never change. This is an important simplification. If your cache needs to adjust the shard count dynamically (say, auto-scaling with load), you would have to handle synchronization during shard migration — far more complex than static sharding.

## Copy-on-Write: Pushing Lock-Free Reads to the Limit

Sharded locks perform well under many-reads-few-writes workloads, but reads still have to acquire a shared lock — much lighter than an exclusive lock, yet under extremely high read frequencies (say, millions of reads per second) the lock's overhead still cannot be ignored. You might ask: is there a way to make reads completely lock-free? There is.

Copy-on-Write (CoW) is exactly such a strategy. Its core idea: a write does not modify the shared data directly; it creates a complete copy, modifies the copy, and then atomically switches the pointer from the old data to the new. Reads simply follow the pointer. Notice that because writes never modify the old data (they only create new data), reads need no synchronization at all.

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

Let us walk through the implementation step by step. `data_` is a `shared_ptr<Data>` pointing at the current map data. The read operation `get` uses `std::atomic_load` to take a copy of the current `data_` (the `shared_ptr` reference count increments atomically), then searches the map it obtained. Because a write never modifies the old data (it only creates new data and atomically switches the pointer), the data behind the `shared_ptr` a read obtains stays valid and consistent for the entire read — no lock needed.

The write operation `set` proceeds in three steps. We acquire `write_mutex_`; this mutex does not protect the data itself (the data lives in the `shared_ptr`, protected by atomic operations) — it protects the mutual exclusion between writes: it ensures only one write at a time creates a copy; otherwise two writes would each copy the old data, each apply its own modification, and each store its own pointer, the later store overwriting the earlier one's change. Then the modification happens on the copy. Finally `std::atomic_store` switches the pointer to the new data; this operation is atomic, guaranteeing that a read sees either the old data or the new data, never an intermediate state.

CoW's price is plain: every write copies the whole map. With 10000 elements in the map, one `set` copies 10000 elements. So CoW fits only "reads far outnumber writes" scenarios — configuration tables, routing tables, dictionary data — where writes happen occasionally and reads are frequent and latency-sensitive. If writes are frequent too, CoW's copying cost eats everything the lock-free reads earned, and we toiled for nothing.

About `std::atomic_load` and `std::atomic_store`: they are the `shared_ptr` atomic operation functions C++11 provided (defined in the `<memory>` header). C++20 introduced `std::atomic<std::shared_ptr<T>>` as the replacement, with a cleaner interface; the underlying implementation is similar — both use a CAS (compare-and-swap) loop, or a global spinlock, to keep the `shared_ptr`'s control-block pointer updated atomically. Note that C++20 has deprecated the `shared_ptr` atomic free functions such as `std::atomic_load` and `std::atomic_store`, with removal planned for C++26. If your project uses C++20 or later, prefer `std::atomic<std::shared_ptr<T>>` directly. In our scenario, the `shared_ptr` atomic operations involve only pointer reads and writes (not copies of map data), so the overhead is small.

One more detail to hand over: the `snapshot()` method returns a `shared_ptr<const Data>` — an immutable snapshot. The caller may hold this snapshot for as long as it likes without worrying about data changes, because the underlying CoW mechanism guarantees the old data is not destroyed until the last reference is released. This property is extremely useful whenever a "consistent read" is needed — for example, when we traverse the whole map for an aggregate computation.

## Strategies for Using std::shared_mutex

We already used `std::shared_mutex` in the sharded-lock implementation above, but have not yet talked carefully about where its usage boundaries lie in concurrent containers. The topic deserves its own unfolding, because it is subtler than most people think.

`std::shared_mutex` (C++17, defined in the `<shared_mutex>` header) offers two locking modes: shared (`shared_lock`) and exclusive (`unique_lock`). Multiple threads may hold the shared lock simultaneously, but an exclusive lock blocks every other lock request (shared or exclusive). That makes it especially effective in "many reads, few writes" scenarios — we already saw it at work in the `ShardedCache` above.

But `shared_mutex` is no cure-all; let us put the costs on the table: on Linux, `shared_mutex` is usually built on `pthread_rwlock_t`, internally maintaining a reader count and a waiter queue, so acquiring and releasing it is heavier than `pthread_mutex_t`. In "reads and writes split evenly" or "writes outnumber reads" scenarios, `shared_mutex` may perform worse than a plain `mutex`.

Let me also relay a loss I took — writer starvation. If new readers keep acquiring the shared lock, a writer may never get its chance at the exclusive lock — because as long as any reader holds the shared lock, the writer cannot take the exclusive one. Linux glibc's `pthread_rwlock_t` defaults to a reader-preference policy (a steady stream of readers keeps deferring the writer's chance at the lock — the classic cause of writer starvation), but the C++ standard does not guarantee this. If your application is sensitive to write latency, be sure to test the scheduling policy of `shared_mutex` on your platform.

A practical rule of thumb: the payoff of `shared_mutex` shows only when reads exceed 80% of total operations. If the read-write ratio approaches 1:1 or tips toward writes, a plain `mutex` is simpler and more efficient.

## Trade-offs Among the Four Strategies

By now we have been through all four strategies; looking back, their trade-off relationships are actually quite clear. Let us lay them side by side in a table:

| Strategy | Read Performance | Write Performance | Implementation Complexity | Applicable Scenarios |
|------|--------|--------|------------|----------|
| Coarse-grained lock | Low (exclusive lock) | Low (exclusive lock) | Low | Low contention, prototyping |
| Fine-grained lock | Medium (bucket-level lock) | Medium (bucket-level lock) | High (rehash is hard) | High-contention hash tables |
| Sharded lock | High (shard-level shared lock) | Medium (shard-level exclusive lock) | Medium | Read-heavy caches |
| Copy-on-Write | Extremely high (lock-free reads) | Low (full copy) | Medium | Config tables, routing tables |

The key to choosing a strategy is looking at your concrete scenario, not at which one is "fastest". You need to answer a few questions: what is the read-write ratio? How much data? How frequent and how long are the writes? Do you need strongly consistent snapshots? Can you tolerate data loss? The answers decide which strategy fits best.

Most projects need nothing more complex than coarse-grained locking early on — it is correct, simple, and easy to debug. Only after performance tests confirm lock contention is the bottleneck should you consider upgrading to sharded or fine-grained locks. Premature optimization is the root of all evil, and doubly so in concurrent container design: finer-grained locks mean more subtle bugs and deadlocks that are harder to reproduce.

## Where to Go from Here

Starting from "why STL containers are not thread-safe", this piece discussed four concurrent container design strategies. Coarse-grained locking guards the whole container with one mutex — simple and correct, but throughput capped by lock contention. Fine-grained locking pushes locks down to the bucket/node level, slashing contention, while rehash handling sends implementation complexity up steeply. Sharded locking strikes a compromise between the two: a small number of shards each with a `shared_mutex`, writes locking only the relevant shard, reads running in shared parallel. Copy-on-Write pushes reads to the lock-free extreme, at the price of copying all data on every write — fitting only scenarios where reads far outnumber writes.

The four strategies do not form a progression; they are parallel tools with different applicable surfaces, and the key to choosing is understanding your read-write pattern and data characteristics. Do not rush to the most complex scheme — let measurements speak ([Before Measuring, Set the Conditions: Serial Baselines and Measurement Traps](../../chapter06-validating-designs/03-measuring-benefits)) before touching the design. One rung up are lock-free data structures, replacing every lock with atomic operations — that is the subject of [Lock-Free Introduction](02-lock-free-intro). But before you consider lock-free, get the lock-based schemes right: after all, locks suffice in most scenarios.

> Some of the sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) under `code/volumn_codes/vol5/ch04-concurrent-data-structures/`; for the rest, the code blocks in this text are authoritative — take and compare as you like.

## Exercises

### Exercise 1: A Concurrent Cache with Sharded Locks

Building on this piece's `ShardedCache`, add a `get_or_compute(key, factory)` method: if the key exists, return the value directly; if not, call `factory()` to compute the value, store it in the cache, and return it. The whole "look up; if absent, compute and insert" process must be atomic — two threads must never simultaneously compute the value for the same key.

Hint: inside `get_or_compute` you need the exclusive lock on the shard (not the shared lock, because a write may follow). If you want a shared lock on the "key already exists" fast path to boost read performance, you can look up under the shared lock and upgrade to the exclusive lock on a miss — but `shared_mutex` does not support lock upgrade directly; you must release the shared lock and then acquire the exclusive one, and that gap is a time window you need to handle.

### Exercise 2: Benchmarking Copy-on-Write

Write a benchmark program comparing `CopyOnWriteMap` and `CoarseLockedMap` under different read-write ratios. Test scenario: 10000 keys, 4 reader threads and 1 writer thread running together for 10 seconds, recording total read throughput (ops/sec). Then rerun with 1 reader and 4 writers and compare the results.

Expected results: in the read-heavy case (4 readers, 1 writer), `CopyOnWriteMap`'s read throughput lands markedly above `CoarseLockedMap` (lock-free reads versus reads that must take a mutex). In the write-heavy case (1 reader, 4 writers), `CopyOnWriteMap`'s performance falls sharply (every write copies the entire map).

### Exercise 3: The Effect of Shard Count on Performance

Modify the `ShardedCache` constructor to accept different shard counts (say 1, 4, 16, 64, 256). Run the benchmark with 8 threads (4 readers, 4 writers) and watch how throughput shifts with the shard count. Expect: throughput climbs markedly from 1 to 16 shards, then the gain flattens or even reverses past some point (lock management overhead starts to show). For a full account of measurement methodology, see [Before Measuring, Set the Conditions: Serial Baselines and Measurement Traps](../../chapter06-validating-designs/03-measuring-benefits).

## Reference Resources

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::atomic_load, std::atomic_store for shared_ptr -- cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [Concurrent Hash Table Designs -- bluuewhale.github.io](https://bluuewhale.github.io/posts/concurrent-hashmap-designs/)
- [Design Concurrent HashMap -- AlgoMaster.io](https://algomaster.io/learn/concurrency-interview/design-concurrent-hashmap)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 6 & 7](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [P1761R0: Concurrent Map Customization Options -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1761r0.pdf)
