// MS6: C++20 同步原语实践 —— 不是造轮子,而是用 std::latch / barrier / counting_semaphore
// 实现三种经典并发模式, 体会"为什么是这个原语"。
#include <catch2/catch_test_macros.hpp>
#include <lab1/sync_practice.h>

TEST_CASE("MS6: fork_join_sum aggregates all worker results", "[lab1][milestone6]") {
    // 每个 worker 返回 id+1, 总和 = 1+2+...+n
    auto task = [](std::size_t id) { return id + 1; };
    std::size_t n = 10;
    std::size_t expected = (n * (n + 1)) / 2;
    REQUIRE(lab1::fork_join_sum(n, task) == expected);
}

TEST_CASE("MS6: two_phase_sum equals n_workers * per_worker_value", "[lab1][milestone6]") {
    REQUIRE(lab1::two_phase_sum(8, 100) == 800);
    REQUIRE(lab1::two_phase_sum(1, 42) == 42);
}

TEST_CASE("MS6: measure_max_concurrency respects the semaphore limit", "[lab1][milestone6]") {
    // 50 个线程抢着进, 但同一时刻最多 4 个
    std::size_t observed = lab1::measure_max_concurrency(50, 4);
    REQUIRE(observed <= 4); // 峰值不能超过信号量上限(这是 semaphore 工作的核心证据)
    REQUIRE(observed >= 1); // 至少能进一个(没死锁)
}
