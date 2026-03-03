// etl_queue_demo.cpp - ETL 固定容量队列示例
// 注意：此示例需要 ETL 库支持
// https://github.com/ETLCPP/etl

#include <iostream>
#include <array>

// 模拟 ETL queue 接口（用于演示）
template<typename T, size_t N>
class EtlQueueDemo {
    std::array<T, N + 1> buffer_;  // +1 for empty/full distinction
    size_t head_ = 0;
    size_t tail_ = 0;

public:
    bool push(const T& value) {
        if (full()) return false;
        buffer_[head_] = value;
        head_ = (head_ + 1) % (N + 1);
        return true;
    }

    bool pop(T& out) {
        if (empty()) return false;
        out = buffer_[tail_];
        tail_ = (tail_ + 1) % (N + 1);
        return true;
    }

    bool empty() const { return head_ == tail_; }
    bool full() const { return (head_ + 1) % (N + 1) == tail_; }

    size_t size() const {
        if (head_ >= tail_) return head_ - tail_;
        return (N + 1) - (tail_ - head_);
    }

    size_t capacity() const { return N; }
};

// 消息结构
struct Message {
    int id;
    int data;
};

int main() {
    std::cout << "=== ETL Queue Demo ===\n\n";

    // 消息队列，容量 8
    // etl::queue<Message, 8> msg_queue;  // 使用真实 ETL 时的写法
    EtlQueueDemo<Message, 8> msg_queue;

    std::cout << "Queue capacity: " << msg_queue.capacity() << "\n\n";

    // 生产者：发送消息
    std::cout << "=== Producer: Sending messages ===\n";
    for (int i = 0; i < 10; ++i) {
        Message msg{i, i * 100};
        if (msg_queue.push(msg)) {
            std::cout << "Sent: ID=" << msg.id << ", Data=" << msg.data << '\n';
        } else {
            std::cout << "Queue full! Message " << i << " dropped.\n";
            break;
        }
    }

    std::cout << "\nQueue size: " << msg_queue.size() << "\n\n";

    // 消费者：处理消息
    std::cout << "=== Consumer: Processing messages ===\n";
    while (!msg_queue.empty()) {
        Message msg;
        if (msg_queue.pop(msg)) {
            std::cout << "Processed: ID=" << msg.id << ", Data=" << msg.data << '\n';
        }
    }

    std::cout << "\nQueue empty: " << (msg_queue.empty() ? "yes" : "no") << '\n';

    std::cout << "\n=== Typical embedded use case ===\n";
    std::cout << "- ISR pushes data to queue\n";
    std::cout << "- Main loop processes at its own pace\n";
    std::cout << "- No malloc, deterministic behavior\n";
    std::cout << "- Type-safe, unlike raw byte buffers\n";

    return 0;
}
