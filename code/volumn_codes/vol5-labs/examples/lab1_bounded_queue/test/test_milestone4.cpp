// MS4: 背压与并发压力 —— 现在有 close(MS2), 多生产者多消费者能真正并发跑完且不丢不重。
// 小容量(16)刻意制造背压:生产者会反复阻塞等空间,消费者会反复阻塞等数据。
// 这是 BoundedBlockingQueue"能用"的硬门槛:模拟真实生产者-消费者。
//
// 注意: Catch2 的 REQUIRE 在子线程里不会传播失败, 所以消费者线程只写数据,
// 全部 join 完到主线程再断言。
#include <catch2/catch_test_macros.hpp>
#include <lab1/bounded_blocking_queue.h>

#include <thread>
#include <vector>

TEST_CASE("MS4: MPMC stress with backpressure, no loss no dup", "[lab1][milestone4]") {
    lab1::BoundedBlockingQueue<int> q(16); // 小容量强背压
    constexpr int N_PRODUCERS = 4;
    constexpr int N_CONSUMERS = 4;
    constexpr int PER = 2000;
    constexpr int TOTAL = N_PRODUCERS * PER;

    std::vector<int> seen(TOTAL, 0); // 每个 *v 唯一, 不同消费者写不同元素 -> 无竞争

    std::vector<std::thread> producers;
    for (int p = 0; p < N_PRODUCERS; ++p) {
        producers.emplace_back([&q, p]() {
            int base = p * PER;
            for (int i = 0; i < PER; ++i)
                q.push(base + i);
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < N_CONSUMERS; ++c) {
        consumers.emplace_back([&]() {
            while (auto v = q.pop())
                seen[*v]++; // *v ∈ [0,TOTAL), 每个值唯一
        });
    }

    for (auto& t : producers)
        t.join();
    q.close(); // 生产完毕, 唤醒消费者取完剩余后退出
    for (auto& t : consumers)
        t.join();

    for (int c : seen)
        REQUIRE(c == 1); // 每个值恰好一次: 不丢、不重
}

TEST_CASE("MS4: size tracks capacity", "[lab1][milestone4]") {
    lab1::BoundedBlockingQueue<int> q(4);
    REQUIRE(q.size() == 0);
    for (int i = 0; i < 4; ++i)
        q.push(i);
    REQUIRE(q.size() == 4); // 满
    q.pop();
    REQUIRE(q.size() == 3);
}
