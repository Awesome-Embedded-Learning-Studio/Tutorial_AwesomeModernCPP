// 背压演示：容量 10 的双 cv 有界队列，生产者连塞 20 个数、消费者慢取，总数不丢
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

template <typename T> class BoundedQueue {
  public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one(); // 叫的是消费者那一边
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one(); // 叫的是生产者那一边
        return value;
    }

  private:
    std::queue<T> queue_;
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_full_;  // 生产者在这把上等"不满"
    std::condition_variable not_empty_; // 消费者在这把上等"不空"
};

int main() {
    BoundedQueue<int> q(10);
    std::thread producer([&q] {
        for (int i = 1; i <= 20; ++i) {
            q.push(i);
        }
    });
    long sum = 0;
    std::thread consumer([&q, &sum] {
        for (int i = 1; i <= 20; ++i) {
            sum += q.pop();
        }
    });
    producer.join();
    consumer.join();
    std::cout << "sum = " << sum << '\n'; // 期望 210
}
