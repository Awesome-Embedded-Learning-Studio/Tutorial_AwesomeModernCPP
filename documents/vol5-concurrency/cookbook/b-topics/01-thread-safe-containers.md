---
title: "线程安全容器策略：粗粒度锁、分片锁、Copy-on-Write"
description: "四种线程安全容器设计策略的机制与取舍：粗粒度锁、按桶细粒度锁、分片锁与 Copy-on-Write，外加 shared_mutex 的使用边界与写者饥饿。"
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
---

# 线程安全容器策略：粗粒度锁、分片锁、Copy-on-Write

本篇住在 cookbook 的主题选读区。卷五的必修线在第二单元把互斥与锁管理（[《丢掉的数怎么收回来？——std::mutex 与三种作用域锁》](../../chapter02-sharing-data/02-mutex-and-locks)）讲完，在第三单元把有界队列（[《有容量的队列：先让它站得住》](../../chapter03-wait-and-coordinate/03-bounded-queue)）讲完，它们回答的是“怎么把一把锁用对”。容器这一层的问题是另一档的：一把锁够不够，什么时候拆成多把，读多写少时能不能把读做到无锁。这些设计策略超出必修范围，咱们把它们整篇平移到选读区，正文没讲的部分都在这里。

读它之前，您需要会用 mutex 与 lock_guard（第二单元），知道 data race 与锁粒度是什么。前者的正篇是[《同一个计数器，两条线程为什么算不对？》](../../chapter02-sharing-data/01-from-wrong-programs)，后者归[《怎样调整设计》](../../chapter06-validating-designs/05-adjusting-design)。`std::shared_mutex` 已收在 A 区（API 配方区）的[《shared_mutex 读写锁》](../a-API/04-shared-mutex)配方里，想查速记就跳过去，不读它也不影响，本篇的《std::shared_mutex 的使用策略》一节自带说明。

再把平移的来路交代清楚。本篇由旧卷素材整篇迁来，机制讲解、代码与练习原样保留，我改动的只有编号、frontmatter、指向正文的交叉引用，外加把素材里的旧词按判死词表过了一遍。文中声称的实测数字（比如 `pthread_mutex_t` 的内存占用）沿用素材当年的记录，本轮我没有重新测量。

我第一次需要写一个“多线程能用的 map”的时候，第一反应是：这有什么难的，不就是在每个操作外面套个 `lock_guard` 吗？真动手写了才发现，事情远没有那么简单。加锁本身不难，难的是加对了、加够了、加得恰到好处。锁加粗了性能炸，锁加细了正确性炸，锁加错位置了直接 data race 炸。

咱们在[《有容量的队列：先让它站得住》](../../chapter03-wait-and-coordinate/03-bounded-queue)里写过的 `BoundedQueue`，用一把 mutex 保护全部内部状态，属于最直接的同步方式。对队列这样操作逻辑单一的结构，一把锁就够了。换成 map、set、哈希表这类更复杂的容器，一把锁就成了性能瓶颈：所有线程不管操作哪个元素，都得排队等同一把锁。

本篇讨论四种程度的线程安全容器设计策略，从粗粒度锁到细粒度锁，从分片锁到 Copy-on-Write。这四样工具各管一摊，适用面不同，谁也不取代谁。咱们要把每种策略的适用条件、实现复杂度和性能特征都摸清，您面对具体需求时才拍得了板。

## STL 容器为何非线程安全

展开设计策略之前，咱们回答一个常见问题：为什么 C++ 标准库的容器，比如 `std::vector`、`std::map`、`std::unordered_map`，不是线程安全的？

C++ 标准对容器并发访问的保证非常有限。咱们把它翻成大白话：对同一个容器的多个读操作（调用 `const` 成员函数）是安全的，不需要外部同步。但只要有一个写操作（调用非 `const` 成员函数），所有其他并发访问（读或写）都必须被同步。换言之，“多读无写”安全，有写操作就需要加锁。

标准库不做线程安全，这背后是深思熟虑的权衡，不是疏忽。不同场景对“线程安全”的需求差异巨大，一个只读查询的缓存和一个高频写入的计数器表，需要的同步策略完全不同。假如标准库容器内置了某种线程安全机制（比如每个操作加一把内部锁），不需要线程安全的场景就白白付出性能代价，需要更细粒度控制的场景又会嫌内置的锁粒度太粗，两头不讨好。所以标准选择了最保守的策略：不做同步，把决定权交给咱们使用者。

于是有了一个实际后果：您用 STL 容器写多线程代码时，必须在容器外部加锁。“外部加锁”说起来简单，做起来要小心不少地方：复合操作的原子性，迭代器失效，锁的粒度选择。本篇真正要讨论的，正是这些。

## 粗粒度锁：一把 mutex 保护一切

咱们从最朴素的方案开始——用一把 mutex 保护整个容器，所有操作在执行前获取锁，执行后释放锁。[《有容量的队列：先让它站得住》](../../chapter03-wait-and-coordinate/03-bounded-queue)里的 `BoundedQueue` 就是同一套模式。它虽然简单粗暴，但正确性最好保证。

咱们来看一个粗粒度锁的并发 map：

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

粗粒度锁的优点是正确性容易保证：所有操作在锁的保护下执行，不存在并发访问的问题。缺点也很明显，所有操作串行化，即使两个操作访问的是不同的 key，也必须排队等同一把锁。在低争用场景（线程少、操作频率低）下这完全没问题，但您遇到高并发场景时，这把锁会成为吞吐量的天花板。

有一个容易被忽视的陷阱：接口原子性问题。上面的 `get` 和 `set` 各自是原子的，但“get 之后再根据结果决定是否 set”的复合操作不是原子的——两次操作之间锁被释放了，其他线程可以插进来改变 map 的状态。举个例子，您需要一个“不存在才插入”的语义，就不能调完 `contains` 再调 `set`，必须提供一个封装了两步的原子操作：

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

`get_or_insert` 把“查找”和“插入”放在同一次锁的保护下，保证了原子性。设计并发容器的接口时，您需要把所有复合操作的原子版本都提供出来，不然调用者要么自己加锁（违反封装），要么写出有 race condition 的代码。

另一个陷阱是迭代器失效。`std::unordered_map` 在 rehash 时会使所有迭代器失效，`std::map` 的插入操作不会使迭代器失效，但 `erase` 会使被删除元素的迭代器失效。不过在并发场景中，要紧的问题不在容器本身的失效规则。真正要紧的是遍历期间锁被释放后，其他线程可能修改了容器，导致迭代器失效，崩溃，或者读到不一致的数据。解决办法是遍历期间持续持有锁，但您也要接受一个代价：遍历期间其他线程完全被阻塞。如果遍历耗时很长，这个阻塞可能不可接受。

## 细粒度锁：按桶/节点加锁

好，粗粒度锁的问题咱们已经很清楚了：锁的粒度太粗，所有操作共享一把锁，即使它们操作的是完全不相关的数据。那思路就很自然了，把容器分成多个独立的部分，每个部分有自己的锁，操作只争它需要的那部分。

哈希表天然适合这么拆，因为哈希表本身就是分桶的——每个 key 通过哈希函数映射到一个桶（bucket），不同桶的元素互不相关。咱们给每个桶一把锁，操作不同桶的线程就不会争用。

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

每个 `Bucket` 有自己的 `mutex` 和 `entries`（用 `std::list` 实现的链表，避开 `std::vector` 的重分配问题）。`get`、`set`、`erase` 都只锁住 key 对应的那个桶。您会看到，操作不同桶的线程完全并行，争用只在操作同一个桶时发生。

细粒度锁的吞吐量取决于桶的数量和哈希函数的质量。桶越多，争用越少。哈希函数越均匀，负载越均衡。但桶的数量也不能无限增加，每多一个桶就多一把 mutex（在 Linux 上，一个 `pthread_mutex_t` 至少占 40 字节）。桶太多但元素太少的话，大部分桶都是空的，咱们白花内存。

细粒度锁最大的实现难题是 **rehash**。元素数量增长到一定程度时，哈希表需要扩容，也就是增加桶的数量并重新分配所有元素。rehash 需要访问所有桶，而不是某一个桶，这意味着需要锁住所有桶的 mutex。如果 rehash 期间其他线程还在操作容器，就会出现死锁或者数据不一致。解决方式是在 rehash 时用一个全局的写锁阻止所有其他操作，但它本质上退化成了粗粒度锁，只不过只在 rehash 时发生。一个更精巧的方式是渐进式 rehash（incremental rehash）：不一次性搬迁所有元素，每次操作时搬迁一小部分，把 rehash 的开销分摊到多次操作中。Java 的 `ConcurrentHashMap` 用的就是这套策略。不过它大大增加了实现的复杂度，咱们这里就不展开了。

另外提一个可能让您困惑的细节：`Bucket` 里的 `mutex` 被声明为 `mutable`。这是因为 `get` 是 `const` 方法但它需要获取 mutex——`const` 方法不能修改成员变量，而 mutex 的 `lock()` 本质上是在修改 mutex 的内部状态。如果您漏掉 `mutable`，编译器会直接报错。`mutable` 关键字就是为了“逻辑上不改变对象状态，但物理上需要修改内部数据”的场景设计的，在并发容器中这个用法非常普遍。

## 分片锁：N 个分片各有 mutex

到这里您会发现一个矛盾：细粒度锁的锁数量等于桶的数量——如果桶很多，锁的开销就很大，每把 mutex 至少占几十个字节，而且操作系统管理大量锁也有额外成本。分片锁（也叫 striped lock）就是为了化解这个矛盾而生的折衷方案：把容器分成 N 个分片（shard），每个分片一把锁，但分片数量远小于桶数量。一个 key 属于哪个分片，由 key 的哈希值对分片数量取模决定。

分片锁和细粒度锁的区别在于粒度：细粒度锁是每个桶一把锁，分片锁是每 K 个桶共享一把锁。分片锁的争用比细粒度锁稍多（不同桶但同一个分片的操作会争用），但锁的数量大幅减少。咱们心里有个量级就行：16 到 64 个分片就够，不需要随桶数量线性增长。

咱们来实现一个分片锁的并发缓存。它的典型场景是 HTTP 服务器中的路由缓存或者数据库查询缓存——读多写少，读操作要求快，写操作可以容忍一点延迟。

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

这个实现有几个值得关注的设计决策。第一，咱们用了 `std::shared_mutex`（C++17）代替 `std::mutex`——读操作获取 `shared_lock`（共享锁，多个读者可以并行），写操作获取 `unique_lock`（独占锁，排他访问）。在“读多写少”的缓存场景中，这个区分非常要紧：如果 90% 的操作都是 `get`，共享锁让这 90% 的操作几乎无争用地并行执行，只有 `set` 和 `erase` 才需要独占。假如您用 `std::mutex`，读和写都要独占锁，读操作的并行性就完全丧失了。

第二，`for_each` 方法按顺序遍历每个分片，在每个分片上获取共享锁。这意味着遍历期间分片是逐一解锁的，遍历完一个分片后释放它的锁，再锁下一个分片。这样策略的好处是不同时占用所有锁（避免死锁风险），代价是遍历结果可能不反映任何单一时间点的全局快照（某个分片遍历完后、下一个分片还没锁上时，可能有写操作修改了数据）。如果您需要真正的全局快照，就必须同时锁住所有分片，但您要接受增加的死锁风险（如果其他代码也在按某种顺序获取分片锁）。

第三，分片数量是固定的（构造时确定，之后不变）。这避开了 rehash 的复杂性：每个分片内部的 `unordered_map` 可以自由 rehash（因为有分片级别的锁保护），但分片的数量和 key 到分片的映射关系不会改变。这是一个重要的简化。假如您的缓存需要动态调整分片数量（比如根据负载自动扩容），您就需要处理分片迁移时的同步问题，比静态分片复杂得多。

## Copy-on-Write：读无锁的极致优化

分片锁在读多写少的场景下表现很好，但读操作仍然需要获取共享锁——虽然共享锁比独占锁轻量得多，但在极端高频读取的场景下（比如每秒百万级读取），锁的开销仍然不可忽视。您可能会问：有没有一种方式让读操作完全无锁？答案是有的。

Copy-on-Write（CoW）就是这样一种策略。它的核心思想是：写操作不直接修改共享数据，而是创建一个完整的副本，在副本上修改，然后用原子操作把指针从旧数据切换到新数据。读操作直接读取指针指向的数据。您看，因为写操作不会修改旧数据（只创建新数据），所以读操作不需要任何同步。

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

咱们逐步看这个实现。`data_` 是一个 `shared_ptr<Data>`，指向当前的 map 数据。读操作 `get` 通过 `std::atomic_load` 获取当前 `data_` 的拷贝（`shared_ptr` 的引用计数原子递增），然后在获取到的 map 上做查找。因为写操作永远不会修改旧数据（它只创建新数据，然后原子切换指针），所以读操作拿到的 `shared_ptr` 指向的数据，在整个读取过程中都是有效的、一致的，不需要任何锁。

写操作 `set` 的流程分三步。咱们获取 `write_mutex_`，这个 mutex 保护的不是数据本身（数据在 `shared_ptr` 里，通过原子操作保护），保护的是写操作之间的互斥：确保同一时间只有一个写操作在创建副本，否则两个写操作各自拷贝了旧数据、各自修改、然后各自写入指针，晚写入的会覆盖早写入的修改。接着在副本上做修改。最后用 `std::atomic_store` 把指针切换到新数据，这个操作是原子的，保证了读操作看到的要么是旧数据要么是新数据，不会看到中间状态。

CoW 的代价很明显：每次写操作都要拷贝整个 map。如果 map 里有 10000 个元素，一个 `set` 就要拷贝 10000 个元素。所以 CoW 只适用于“读远多于写”的场景，比如配置表、路由表、字典数据，写操作偶尔发生，读操作频繁且要求低延迟。如果写操作也很频繁，CoW 的拷贝开销会吃掉读无锁带来的收益，咱们就白忙了。

关于 `std::atomic_load` 和 `std::atomic_store`，它们是 C++11 提供的 `shared_ptr` 原子操作函数（定义在 `<memory>` 头文件里）。C++20 引入了 `std::atomic<std::shared_ptr<T>>` 作为替代，接口更清晰，底层实现类似，都是用 CAS（compare-and-swap）循环，或者全局 spinlock，来保证 `shared_ptr` 的控制块指针原子更新。您需要注意，C++20 已将 `std::atomic_load`/`std::atomic_store` 等 `shared_ptr` 原子自由函数标记为 deprecated，计划在 C++26 中移除。如果您的项目使用 C++20 或更高标准，建议直接使用 `std::atomic<std::shared_ptr<T>>`。在咱们的场景中，`shared_ptr` 的原子操作只涉及指针的读写（不是 map 数据的拷贝），开销很小。

还有一个细节要向您交代：`snapshot()` 方法返回的是 `shared_ptr<const Data>`，这是一个不可变的快照。调用者可以持有这个快照任意长时间，不必担心数据变化，因为底层的 CoW 机制保证了旧数据在最后一个引用释放之前不会被销毁。这个特性在需要“一致性读取”的场景中非常有用，比如咱们遍历整个 map 做聚合计算的时候。

## std::shared_mutex 的使用策略

咱们在上面的分片锁实现中已经用到了 `std::shared_mutex`，但还没有仔细聊过它在并发容器里的使用边界。这个话题值得专门展开，因为它比大多数人想的要微妙。

`std::shared_mutex`（C++17，定义在 `<shared_mutex>` 头文件中）提供两种锁模式：共享模式（`shared_lock`）和独占模式（`unique_lock`）。多个线程可以同时持有共享锁，但独占锁会阻塞所有其他锁请求（共享的和独占的）。这让它在“读多写少”的场景中特别有效，咱们已经在上面的 `ShardedCache` 里看到了效果。

但 `shared_mutex` 不是万能药，咱们把开销摆在明处：在 Linux 上，`shared_mutex` 通常基于 `pthread_rwlock_t` 实现，内部需要维护读者计数和等待者队列，锁的获取和释放比 `pthread_mutex_t` 更重。在“读写各半”或者“写多于读”的场景中，`shared_mutex` 的性能可能还不如普通 `mutex`。

再说一个我踩过的亏——写者饥饿。如果不断有新的读者获取共享锁，写者可能永远等不到独占锁的机会——因为只要有任何一个读者持有共享锁，写者就不能获取独占锁。Linux glibc 的 `pthread_rwlock_t` 默认是读者优先策略（持续到来的读者会不断推迟写者获取锁的机会，这是写者饥饿的典型成因），但 C++ 标准不保证这一点。如果您的应用对写延迟敏感，一定要测试您所在平台上 `shared_mutex` 的调度策略。

给您一个实用的经验法则：当读操作占总操作的 80% 以上时，`shared_mutex` 的收益才明显。如果读写比例接近 1:1 或者写更多，用普通 `mutex` 更简单也更高效。

## 四种策略的权衡

到这里咱们已经把四种策略都过了一遍，回头看，它们的取舍关系其实很清晰。咱们把它们放在一个表格里对比一下：

| 策略 | 读性能 | 写性能 | 实现复杂度 | 适用场景 |
|------|--------|--------|------------|----------|
| 粗粒度锁 | 低（独占锁） | 低（独占锁） | 低 | 低争用、原型验证 |
| 细粒度锁 | 中（桶级锁） | 中（桶级锁） | 高（rehash 困难） | 高争用哈希表 |
| 分片锁 | 高（分片级共享锁） | 中（分片级独占锁） | 中 | 读多写少的缓存 |
| Copy-on-Write | 极高（无锁读） | 低（全量拷贝） | 中 | 配置表、路由表 |

选择策略的要点，是看您的具体场景，不看哪种“最快”。您需要回答几个问题：读写比例是多少？数据量有多大？写操作的频率和耗时如何？是否需要强一致性快照？是否容忍数据丢失？这些问题的答案决定了哪种策略最适合。

大多数项目在初期不需要比粗粒度锁更复杂的方案，粗粒度锁正确、简单、容易调试。当您通过性能测试确认锁争用是瓶颈之后，再考虑升级到分片锁或细粒度锁。过早优化是万恶之源，在并发容器设计中尤其如此，更细粒度的锁意味着更多微妙的 bug 和更难复现的死锁。

## 往哪里走

本篇从“为什么 STL 容器不线程安全”讲起，咱们讨论了四种并发容器设计策略。粗粒度锁用一把 mutex 保护整个容器，简单正确但吞吐量受限于锁争用。细粒度锁把锁下推到桶/节点级别，大幅减少争用，但 rehash 的处理让实现复杂度陡增。分片锁在粗粒度和细粒度之间取了折衷，少量分片各有一把 `shared_mutex`，写操作只锁相关分片，读操作共享并行。Copy-on-Write 把读操作推向无锁的极致，代价是每次写都要拷贝全部数据，只适用于读远多于写的场景。

四种策略不构成递进关系，是适用面不同的平行工具，选择的要点是摸清您的读写模式与数据特征。别急着上最复杂的方案，用测量说话（[《量之前，先把条件立住：串行基线与测量的陷阱》](../../chapter06-validating-designs/03-measuring-benefits)）之后再动设计。再往上一档是无锁数据结构，用原子操作替掉所有的锁，那是 [《无锁入门》](02-lock-free-intro)的题目。不过在您考虑无锁之前，请把基于锁的方案做好，毕竟大部分场景下锁够用了。

> 部分示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 的 `code/volumn_codes/vol5/ch04-concurrent-data-structures/`，其余以正文代码块为准，您可以对照着取用。

## 练习

### 练习 1：带分片锁的并发缓存

请您在本篇 `ShardedCache` 的基础上，增加一个 `get_or_compute(key, factory)` 方法：key 存在就直接返回值，不存在就调用 `factory()` 计算值、存入缓存并返回。要求“查找不存在则计算并插入”整个过程是原子的，不会出现两个线程同时计算同一个 key 的值的情况。

提示：在 `get_or_compute` 中，您需要对分片加独占锁（不能用共享锁，因为可能会写入）。如果您想在“key 已存在”的快速路径上用共享锁以提升读性能，可以考虑用共享锁查找，没找到再升级为独占锁——但 `shared_mutex` 不直接支持锁升级，您需要释放共享锁再获取独占锁，这中间有一个时间窗口需要处理。

### 练习 2：Copy-on-Write 的性能测试

请您编写一个基准测试程序，比较 `CopyOnWriteMap` 和 `CoarseLockedMap` 在不同读写比例下的性能。测试场景：10000 个 key，4 个读线程和 1 个写线程同时运行 10 秒，统计读操作的总吞吐量（ops/sec）。然后您再用 1 个读线程和 4 个写线程重跑，对比结果。

预期结果：在读多写少（4 读 1 写）的场景下，您会看到 `CopyOnWriteMap` 的读吞吐量显著高于 `CoarseLockedMap`（因为读无锁对比读要获取 mutex）。在写多读少（1 读 4 写）的场景下，`CopyOnWriteMap` 的性能会大幅下降（因为每次写都要拷贝整个 map）。

### 练习 3：分片数量对性能的影响

请您修改 `ShardedCache` 的构造函数，让它接受不同的分片数量参数（比如 1、4、16、64、256）。用 8 个线程（4 读 4 写）运行基准测试，观察不同分片数量下的吞吐量变化。预期：分片从 1 增加到 16 时吞吐量显著提升，但超过某个值后提升变缓甚至下降（因为锁的管理开销开始显现）。测量方法的注意事项，[《量之前，先把条件立住：串行基线与测量的陷阱》](../../chapter06-validating-designs/03-measuring-benefits)有整套说明。

## 参考资源

- [std::shared_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/shared_mutex)
- [std::atomic_load, std::atomic_store for shared_ptr -- cppreference](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic)
- [Concurrent Hash Table Designs -- bluuewhale.github.io](https://bluuewhale.github.io/posts/concurrent-hashmap-designs/)
- [Design Concurrent HashMap -- AlgoMaster.io](https://algomaster.io/learn/concurrency-interview/design-concurrent-hashmap)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 6 & 7](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [P1761R0: Concurrent Map Customization Options -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1761r0.pdf)
