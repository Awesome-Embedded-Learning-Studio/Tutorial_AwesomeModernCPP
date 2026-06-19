// MS1: 阻塞 push / pop —— 把"多线程同时工作"跑通,不追求完美。
// 这个 milestone 只验证基本语义:能放能取、FIFO、阻塞行为、多生产者不丢数据。
// close / 超时是 MS2/MS3 的事,这里不碰。
#include <catch2/catch_test_macros.hpp>
#include <lab1/bounded_blocking_queue.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST_CASE("MS1: single push/pop roundtrip", "[lab1][milestone1]") {
    lab1::BoundedBlockingQueue<int> q(4);
    q.push(42);
    REQUIRE(q.pop() == 42);
}

TEST_CASE("MS1: FIFO order preserved", "[lab1][milestone1]") {
    lab1::BoundedBlockingQueue<int> q(8);
    for (int i = 0; i < 6; ++i)
        q.push(i);
    for (int i = 0; i < 6; ++i)
        REQUIRE(q.pop() == i);
}

TEST_CASE("MS1: pop blocks until a producer pushes", "[lab1][milestone1]") {
    lab1::BoundedBlockingQueue<int> q(4);
    std::atomic<bool> got_it{false};
    std::thread consumer([&]() {
        auto v = q.pop(); // 空队列, 应该阻塞在这里
        got_it = (*v == 7);
    });
    std::this_thread::sleep_for(50ms);
    REQUIRE_FALSE(got_it.load()); // 还没 push, consumer 必须还在阻塞
    q.push(7);
    consumer.join();
    REQUIRE(got_it.load());
}

// 多生产者并发 push,主线程消费(用单消费者避免"无 close 时多消费者卡在 pop"的死锁——那是 MS2 的事)。
TEST_CASE("MS1: multiple producers, no loss no duplicate", "[lab1][milestone1]") {
    lab1::BoundedBlockingQueue<int> q(64);
    constexpr int N_PRODUCERS = 4;
    constexpr int PER_PRODUCER = 500;
    constexpr int TOTAL = N_PRODUCERS * PER_PRODUCER;

    std::vector<std::thread> producers;
    for (int p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&q, p]() {
            int base = p * PER_PRODUCER;
            for (int i = 0; i < PER_PRODUCER; ++i)
                q.push(base + i);
        });
    }
    for (auto& t : producers)
        t.join();

    std::vector<int> seen(TOTAL, 0);
    for (int i = 0; i < TOTAL; ++i) {
        auto v = q.pop();
        REQUIRE(v.has_value());
        REQUIRE(*v >= 0);
        REQUIRE(*v < TOTAL);
        seen[*v]++;
    }
    for (int c : seen)
        REQUIRE(c == 1); // 每个值恰好被消费一次:不丢、不重
}
