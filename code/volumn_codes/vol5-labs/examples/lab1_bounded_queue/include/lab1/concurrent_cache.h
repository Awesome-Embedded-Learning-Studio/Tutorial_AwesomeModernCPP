#pragma once

#include <cstddef>
#include <functional>
#include <optional>

namespace lab1 {

/// 分片锁并发缓存(MS5)。
///
/// 思路:把 key 按哈希分到 shard_count 个 shard,每个 shard 配一把独立的 mutex。
/// 不同 shard 的读写可以真正并行,吞吐量远高于"全局一把锁"的朴素实现。
/// 这是"细粒度锁 vs 粗粒度锁"权衡的经典练手——MS5 的测试会做并发压力对比。
template <typename K, typename V, typename Hash = std::hash<K>> class ConcurrentCache {
  public:
    /// shard_count 通常取 2 的幂(便于用位运算取 shard index);默认 16。
    explicit ConcurrentCache(std::size_t shard_count = 16);

    /// 查 key,不存在返 nullopt。const 接口但内部要对 shard 加锁,所以 mutable 成员是预期的。
    std::optional<V> get(const K& key) const;

    /// 插入或覆盖 key。
    void put(K key, V value);

    /// 删除 key,返回是否真的删掉了。
    bool erase(const K& key);

    /// 所有 shard 大小之和(近似,并发下不必精确)。
    std::size_t size() const noexcept;
};

} // namespace lab1
