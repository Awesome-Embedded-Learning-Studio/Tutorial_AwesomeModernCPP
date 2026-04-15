// fixed_pool_demo.cpp - 固定大小内存池演示
#include "fixed_pool.h"
#include <iostream>
#include <cstring>

// 消息结构
struct Message {
    int id;
    int priority;
    char data[32];

    Message(int i, int p, const char* d) : id(i), priority(p) {
        std::strncpy(data, d, sizeof(data) - 1);
        data[sizeof(data) - 1] = '\0';
    }

    void display() const {
        std::cout << "  Message " << id << " (pri=" << priority
                  << "): " << data << '\n';
    }
};

int main() {
    std::cout << "=== Fixed Size Pool Demo ===\n\n";

    // 创建内存池
    constexpr std::size_t NUM_MESSAGES = 8;
    constexpr std::size_t MESSAGE_SIZE = sizeof(Message);
    alignas(Message) static char buffer[NUM_MESSAGES * MESSAGE_SIZE];

    SimpleFixedPool pool(buffer, MESSAGE_SIZE, NUM_MESSAGES);

    std::cout << "Pool configuration:\n";
    std::cout << "  Slot size: " << pool.slot_size() << " bytes\n";
    std::cout << "  Slot count: " << pool.slot_count() << '\n';
    std::cout << "  Total capacity: " << pool.capacity() << " bytes\n\n";

    // 分配消息
    std::cout << "=== Allocating messages ===\n";
    Message* msg1 = static_cast<Message*>(pool.allocate());
    if (msg1) {
        new (msg1) Message(1, 10, "Hello");
        msg1->display();
    }

    Message* msg2 = static_cast<Message*>(pool.allocate());
    if (msg2) {
        new (msg2) Message(2, 5, "World");
        msg2->display();
    }

    Message* msg3 = static_cast<Message*>(pool.allocate());
    if (msg3) {
        new (msg3) Message(3, 8, "Embedded");
        msg3->display();
    }

    std::cout << "\n=== Deallocating msg2 ===\n";
    msg2->~Message();
    pool.deallocate(msg2);

    // 重新分配
    std::cout << "\n=== Allocating new message ===\n";
    Message* msg4 = static_cast<Message*>(pool.allocate());
    if (msg4) {
        new (msg4) Message(4, 3, "Systems");
        msg4->display();
    }

    // 填满池子
    std::cout << "\n=== Filling the pool ===\n";
    Message* msgs[NUM_MESSAGES];
    msgs[0] = msg1;
    msgs[1] = msg3;
    msgs[2] = msg4;

    for (int i = 3; i < NUM_MESSAGES; ++i) {
        msgs[i] = static_cast<Message*>(pool.allocate());
        if (msgs[i]) {
            new (msgs[i]) Message(i + 1, i, "Filler");
            std::cout << "  Allocated message " << (i + 1) << '\n';
        }
    }

    // 尝试过度分配
    std::cout << "\n=== Attempting over-allocation ===\n";
    Message* fail = static_cast<Message*>(pool.allocate());
    if (!fail) {
        std::cout << "  Correctly failed (pool exhausted)\n";
    }

    // 清理
    std::cout << "\n=== Cleanup ===\n";
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        if (msgs[i]) {
            msgs[i]->~Message();
            pool.deallocate(msgs[i]);
        }
    }

    std::cout << "All messages deallocated\n";

    std::cout << "\n=== Key benefits ===\n";
    std::cout << "- O(1) allocation and deallocation\n";
    std::cout << "- Zero fragmentation\n";
    std::cout << "- High memory utilization\n";
    std::cout << "- Perfect for same-sized objects\n";
    std::cout << "- Cache-friendly (local allocation)\n";

    return 0;
}
