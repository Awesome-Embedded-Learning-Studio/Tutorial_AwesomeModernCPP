// MS5: ConcurrentCache —— 分片锁并发缓存。
// 思路: key 按哈希分到 N 个 shard, 每个 shard 一把锁。不同 shard 并行, 比全局一把锁吞吐高。
#include <catch2/catch_test_macros.hpp>
#include <lab1/concurrent_cache.h>

#include <string>
#include <thread>
#include <vector>

TEST_CASE("MS5: basic put/get/erase", "[lab1][milestone5]") {
    lab1::ConcurrentCache<int, std::string> cache(8);
    REQUIRE_FALSE(cache.get(1).has_value());
    cache.put(1, "hello");
    REQUIRE(cache.get(1).value() == "hello");
    cache.put(1, "world"); // 覆盖
    REQUIRE(cache.get(1).value() == "world");
    REQUIRE(cache.erase(1));
    REQUIRE_FALSE(cache.get(1).has_value());
    REQUIRE_FALSE(cache.erase(1)); // 不存在的 key 删返 false
}

TEST_CASE("MS5: concurrent put, no loss, size correct", "[lab1][milestone5]") {
    lab1::ConcurrentCache<int, int> cache(16);
    constexpr int N_THREADS = 8;
    constexpr int PER = 1000;
    constexpr int TOTAL = N_THREADS * PER;

    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&cache, t]() {
            int base = t * PER;
            for (int i = 0; i < PER; ++i)
                cache.put(base + i, base + i);
        });
    }
    for (auto& th : threads)
        th.join();

    REQUIRE(cache.size() == TOTAL); // 没丢
    for (int i = 0; i < TOTAL; ++i) {
        REQUIRE(cache.get(i).value() == i); // 值正确
    }
}

TEST_CASE("MS5: concurrent mixed put/get under stress", "[lab1][milestone5]") {
    // 一半线程写、一半线程读, 验证不崩溃 + 最终一致
    lab1::ConcurrentCache<int, int> cache(16);
    constexpr int N = 8;
    constexpr int PER = 2000;
    constexpr int TOTAL = (N / 2) * PER;

    std::vector<std::thread> threads;
    for (int t = 0; t < N; ++t) {
        threads.emplace_back([&cache, t, PER]() {
            int base = (t % (N / 2)) * PER;
            if (t < N / 2) {
                for (int i = 0; i < PER; ++i)
                    cache.put(base + i, base + i);
            } else {
                for (int i = 0; i < PER; ++i)
                    (void)cache.get(base + i); // 读, 不要求存在
            }
        });
    }
    for (auto& th : threads)
        th.join();

    REQUIRE(cache.size() == TOTAL); // 写入完整保留
}
