// close/drain 演示：3 个生产者各塞 100 个数、2 个消费者取到 kClosed 为止，
// 全部 join 之后才 close，最后核对总数正好 300
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

enum class QueueResult {
    kSuccess,
    kClosed,
};

template <typename T> class BoundedQueue {
  public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    QueueResult push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
        if (closed_) {
            return QueueResult::kClosed;
        }
        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) {
            return QueueResult::kClosed; // 空且已关：drain 完成
        }
        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

  private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_ = false;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};

int main() {
    BoundedQueue<int> q(8);
    std::atomic<int> consumed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < 3; ++p) {
        producers.emplace_back([&q] {
            for (int i = 0; i < 100; ++i) {
                q.push(i);
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c) {
        consumers.emplace_back([&q, &consumed] {
            int value = 0;
            while (q.pop(value) == QueueResult::kSuccess) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }
    q.close(); // 生产者全部 join 之后才关门
    for (auto& t : consumers) {
        t.join();
    }

    std::cout << "drain consumed = " << consumed.load() << " (expect 300)\n";
}
