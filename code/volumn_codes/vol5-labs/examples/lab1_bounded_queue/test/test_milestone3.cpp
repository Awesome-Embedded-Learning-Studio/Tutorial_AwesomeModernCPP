// MS3: 超时 try_push_for / try_pop_for。
// 阻塞版 push/pop 可能永远等下去(死锁的温床),超时版给一个"放弃"的出口。
#include <catch2/catch_test_macros.hpp>
#include <lab1/bounded_blocking_queue.h>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("MS3: try_pop_for times out on empty queue", "[lab1][milestone3]") {
    lab1::BoundedBlockingQueue<int> q(4);
    auto start = std::chrono::steady_clock::now();
    auto v = q.try_pop_for(50ms);
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE_FALSE(v.has_value());
    REQUIRE(elapsed >= 40ms); // 确实等了(留 10ms 容差,别因为调度抖动误判)
}

TEST_CASE("MS3: try_push_for times out when queue is full", "[lab1][milestone3]") {
    lab1::BoundedBlockingQueue<int> q(2);
    q.push(1);
    q.push(2);                              // 满
    REQUIRE_FALSE(q.try_push_for(3, 50ms)); // 满了且没人取, 超时返 false
}

TEST_CASE("MS3: try_push_for / try_pop_for happy path", "[lab1][milestone3]") {
    lab1::BoundedBlockingQueue<int> q(4);
    REQUIRE(q.try_push_for(99, 50ms));
    REQUIRE(q.try_pop_for(50ms).value() == 99);
}

TEST_CASE("MS3: try_push_for returns false on closed queue (no throw)", "[lab1][milestone3]") {
    lab1::BoundedBlockingQueue<int> q(4);
    q.close();
    // 超时版不抛(和阻塞版 push 区分),返 false 即可
    REQUIRE_FALSE(q.try_push_for(1, 50ms));
}
