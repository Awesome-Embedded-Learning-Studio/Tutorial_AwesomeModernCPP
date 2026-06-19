// MS2: close() 语义 —— 这是生产者-消费者能"优雅退出"的关键。
// 没有它,消费者循环 pop 永远等不到结束信号(参考 MS1 为什么要用单消费者+已知数量绕开)。
#include <catch2/catch_test_macros.hpp>
#include <lab1/bounded_blocking_queue.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("MS2: push after close throws", "[lab1][milestone2]") {
    lab1::BoundedBlockingQueue<int> q(4);
    REQUIRE_FALSE(q.is_closed());
    q.close();
    REQUIRE(q.is_closed());
    // close 后再 push 是程序错误(生产者明知关闭还塞),用异常明确告知
    REQUIRE_THROWS_AS(q.push(1), std::runtime_error);
}

TEST_CASE("MS2: pop drains remaining elements then returns nullopt", "[lab1][milestone2]") {
    lab1::BoundedBlockingQueue<int> q(4);
    q.push(1);
    q.push(2);
    q.close();
    // close 后队列里已有的元素必须仍能被取走
    REQUIRE(q.pop() == 1);
    REQUIRE(q.pop() == 2);
    // 耗尽后返 nullopt —— 这就是消费者的"正常结束"信号
    REQUIRE_FALSE(q.pop().has_value());
    REQUIRE_FALSE(q.pop().has_value()); // 重复调用仍然 nullopt,不是 UB
}

TEST_CASE("MS2: close wakes blocked consumer so it can exit", "[lab1][milestone2]") {
    lab1::BoundedBlockingQueue<int> q(4);
    std::atomic<int> received{0};
    std::thread consumer([&]() {
        // 经典消费者循环:pop 返 nullopt 时退出。只有 close 能让它退出。
        while (auto v = q.pop())
            received.fetch_add(1);
    });
    std::this_thread::sleep_for(50ms);
    q.push(10);
    q.push(20);
    q.close(); // 必须唤醒正在阻塞的 consumer
    consumer.join();
    REQUIRE(received.load() == 2);
}
