// thread_safe_example.cpp - 多线程安全的循环缓冲区演示
#include "atomic_ring_buffer.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// 多生产者-多消费者测试
const int PRODUCER_COUNT = 2;
const int CONSUMER_COUNT = 2;
const int ITEMS_PER_PRODUCER = 100;

AtomicRingBuffer<int, 64> shared_buffer;

// 生产者线程
void producer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        int value = id * 1000 + i;
        while (!shared_buffer.push(value)) {
            // 缓冲区满，等待
            std::this_thread::yield();
        }
        // 模拟生产时间
        std::this_thread::sleep_for(std::chrono::microseconds(rand() % 100));
    }
    std::cout << "Producer " << id << " finished.\n";
}

// 消费者线程
void consumer(int id) {
    int consumed = 0;
    int value;
    while (consumed < (PRODUCER_COUNT * ITEMS_PER_PRODUCER) / CONSUMER_COUNT) {
        if (shared_buffer.pop(value)) {
            ++consumed;
            // std::cout << "Consumer " << id << " got: " << value << '\n';
        } else {
            // 缓冲区空，等待
            std::this_thread::yield();
        }
    }
    std::cout << "Consumer " << id << " consumed " << consumed << " items.\n";
}

int main() {
    std::cout << "=== Thread-Safe Ring Buffer Demo ===\n\n";

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    auto start = std::chrono::steady_clock::now();

    // 启动消费者
    for (int i = 0; i < CONSUMER_COUNT; ++i) {
        consumers.emplace_back(consumer, i);
    }

    // 启动生产者
    for (int i = 0; i < PRODUCER_COUNT; ++i) {
        producers.emplace_back(producer, i);
    }

    // 等待所有生产者完成
    for (auto& p : producers) {
        p.join();
    }

    // 等待所有消费者完成
    for (auto& c : consumers) {
        c.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\n=== Test Completed ===\n";
    std::cout << "Time taken: " << duration.count() << " ms\n";
    std::cout << "Final buffer size: " << shared_buffer.size() << '\n';
    std::cout << "Buffer empty: " << (shared_buffer.empty() ? "yes" : "no") << '\n';

    std::cout << "\nKey points:\n";
    std::cout << "- Uses atomic operations for lock-free synchronization\n";
    std::cout << "- Memory ordering ensures correct visibility across threads\n";
    std::cout << "- Suitable for single-producer single-consumer scenarios\n";
    std::cout << "- For multiple producers/consumers, consider external locking\n";

    return 0;
}
