// Milestone 2: RAII 包装
// 验证 JoiningThread 在正常路径和异常路径都能自动 join，且 move 语义正确。
// 通过本测试：补全 include/lab0/joining_thread.h 的 TODO(MS2) 成员。
// 注意：本测试只依赖 JoiningThread，与 FileScanner 解耦。
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <vector>

#include "lab0/joining_thread.h"

TEST_CASE("MS2: auto-joins on scope exit", "[lab0][milestone2]") {
    std::atomic<bool> ran{false};
    {
        lab0::JoiningThread t([&]() { ran.store(true, std::memory_order_relaxed); });
        // 离开作用域，t 析构应自动 join
    }
    REQUIRE(ran.load());
}

TEST_CASE("MS2: exception path still joins all workers", "[lab0][milestone2]") {
    std::atomic<int> counter{0};
    auto make_workers = [&]() {
        std::vector<lab0::JoiningThread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        throw std::runtime_error("simulated failure");
        // workers 在栈展开时析构，应自动 join
    };

    REQUIRE_THROWS_AS(make_workers(), std::runtime_error);
    // 即使抛了异常，4 个 worker 都应已完成
    REQUIRE(counter.load() == 4);
}

TEST_CASE("MS2: move transfers ownership", "[lab0][milestone2]") {
    lab0::JoiningThread t1([] {});
    REQUIRE(t1.joinable());

    lab0::JoiningThread t2 = std::move(t1);
    REQUIRE(!t1.joinable());
    REQUIRE(t2.joinable());
    // t2 析构时 join
}

TEST_CASE("MS2: vector of JoiningThread joins all on destruction", "[lab0][milestone2]") {
    std::atomic<int> counter{0};
    {
        std::vector<lab0::JoiningThread> workers;
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        // vector 析构 → 每个 JoiningThread 析构 → 自动 join
    }
    REQUIRE(counter.load() == 8);
}
