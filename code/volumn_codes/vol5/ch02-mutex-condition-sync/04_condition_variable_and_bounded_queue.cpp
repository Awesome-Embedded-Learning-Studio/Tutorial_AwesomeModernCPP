// vol5 ch02-04：condition_variable 与阻塞队列（正源）
// 完整可编译版：有界阻塞队列 + close/drain + try_pop_for 三态 + push_or_drop
// 编译：g++ -std=c++20 -Wall -Wextra -pedantic 04_condition_variable_and_bounded_queue.cpp
// TSan：g++ -std=c++20 -fsanitize=thread -g 04_condition_variable_and_bounded_queue.cpp && ./a.out

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout,
};

template <typename T>
class BoundedQueue {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "T must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");

public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });
        if (closed_) {
            return QueueResult::kClosed;
        }
        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });
        if (queue_.empty()) {
            return QueueResult::kClosed;   // 空且已关：drain 完成
        }
        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    template <typename Rep, typename Period>
    QueueResult try_pop_for(T& value,
                            const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ok = not_empty_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || closed_;
        });
        if (!ok) {
            return QueueResult::kTimeout;
        }
        if (queue_.empty()) {
            return QueueResult::kClosed;
        }
        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    bool push_or_drop(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            return false;   // 满了就丢，绝不阻塞
        }
        queue_.push(std::move(value));
        not_empty_.notify_one();
        return true;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_ = false;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;   // 生产者在这把上等“不满”
    std::condition_variable not_empty_;  // 消费者在这把上等“不空”
};

// 演示 1：背压。容量 10、生产 20，两个线程被容量逼着交替推进。
static void demo_backpressure()
{
    BoundedQueue<int> q(10);
    std::thread producer([&q] {
        for (int i = 1; i <= 20; ++i) {
            q.push(i);
        }
    });
    long sum = 0;
    std::thread consumer([&q, &sum] {
        for (int i = 1; i <= 20; ++i) {
            int value = 0;
            if (q.pop(value) != QueueResult::kSuccess) {
                break;
            }
            sum += value;
        }
    });
    producer.join();
    consumer.join();
    std::printf("backpressure sum = %ld (expect 210)\n", sum);
}

// 演示 2：drain。3 生产者各 100、2 消费者取到 kClosed，总数必须正好 300。
static void demo_drain()
{
    BoundedQueue<int> q(8);
    std::atomic<long> consumed{0};
    std::vector<std::thread> producers;
    for (int p = 0; p < 3; ++p) {
        producers.emplace_back([&q] {
            for (int i = 0; i < 100; ++i) {
                if (q.push(i) != QueueResult::kSuccess) {
                    return;
                }
            }
        });
    }
    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c) {
        consumers.emplace_back([&] {
            int value = 0;
            while (q.pop(value) == QueueResult::kSuccess) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    q.close();
    for (auto& t : consumers) {
        t.join();
    }
    std::printf("drain consumed = %ld (expect 300)\n", consumed.load());
}

// 演示 3：超时三态。空队列、有货、关门各试一次。
static void demo_timeout_three_states()
{
    BoundedQueue<int> q(4);
    int value = 0;

    auto r1 = q.try_pop_for(value, std::chrono::milliseconds(100));
    std::printf("empty queue -> %d (expect 2 kTimeout)\n",
                static_cast<int>(r1));

    q.push(42);
    auto r2 = q.try_pop_for(value, std::chrono::milliseconds(100));
    std::printf("after push -> %d value = %d (expect 0 / 42)\n",
                static_cast<int>(r2), value);

    q.close();
    auto r3 = q.try_pop_for(value, std::chrono::milliseconds(100));
    std::printf("after close -> %d (expect 1 kClosed)\n",
                static_cast<int>(r3));
}

// 演示 4：丢而不等。容量 4 塞 10 个，留 4 丢 6。
static void demo_push_or_drop()
{
    BoundedQueue<int> q(4);
    int dropped = 0;
    for (int i = 0; i < 10; ++i) {
        if (!q.push_or_drop(i)) {
            ++dropped;
        }
    }
    q.close();
    int value = 0;
    int got = 0;
    while (q.pop(value) == QueueResult::kSuccess) {
        ++got;
    }
    std::printf("push_or_drop got = %d dropped = %d (expect 4 / 6)\n",
                got, dropped);
}

int main()
{
    demo_backpressure();
    demo_drain();
    demo_timeout_three_states();
    demo_push_or_drop();
    return 0;
}
