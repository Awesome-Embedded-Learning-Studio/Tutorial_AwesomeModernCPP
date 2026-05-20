/*
 * 条件变量：wait/notify 模式与有界阻塞队列
 *
 * 本文件对应教程 vol5-ch02，全面演示 std::condition_variable 的用法：
 *   1. 基本 wait/notify_one 模式
 *   2. wait with predicate（正确的写法）
 *   3. 丢失唤醒（lost wakeup）演示
 *   4. wait_for 带超时的等待
 *   5. BoundedQueue<T> 有界阻塞队列（两个条件变量）
 *   6. 生产者-消费者模型
 *
 * 编译命令（需要 C++17）：
 *   g++ -std=c++17 -pthread -O2 -Wall -Wextra \
 *       03_condition_variable.cpp -o 03_condition_variable
 *
 * 运行：
 *   ./03_condition_variable
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

// ============================================================================
// 辅助：线程安全打印
// ============================================================================
std::mutex g_print_mutex;

template <typename... Args> void safe_print(Args&&... args) {
    std::lock_guard<std::mutex> lock(g_print_mutex);
    (std::cout << ... << std::forward<Args>(args)) << '\n';
}

// ============================================================================
// 1. 基本 wait/notify_one 模式
// ============================================================================
void demo_basic_wait_notify() {
    safe_print("\n=== 1. 基本 wait/notify_one ===");

    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    // 消费者线程：等待 ready 变为 true
    std::thread consumer([&]() {
        std::unique_lock<std::mutex> lock(mtx);
        safe_print("  [消费者] 开始等待...");
        cv.wait(lock); // 释放锁并阻塞，被唤醒时重新获取锁
        safe_print("  [消费者] 被唤醒！ready = ", ready);
    });

    // 让消费者先进入等待
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 生产者线程：设置条件并唤醒
    std::thread producer([&]() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            ready = true;
            safe_print("  [生产者] 设置 ready = true");
        }
        cv.notify_one(); // 唤醒一个等待的线程
        safe_print("  [生产者] 已发送 notify_one");
    });

    consumer.join();
    producer.join();

    safe_print("  这是最基本的 wait/notify 模式，但有丢失唤醒的风险。");
}

// ============================================================================
// 2. wait with predicate —— 正确的写法
// ============================================================================
void demo_wait_with_predicate() {
    safe_print("\n=== 2. wait with predicate（推荐写法）===");

    std::mutex mtx;
    std::condition_variable cv;
    int data = 0;
    bool done = false;

    // 消费者：等待 data > 0
    std::thread consumer([&]() {
        std::unique_lock<std::mutex> lock(mtx);
        // wait 的第二个参数是 predicate（lambda）
        // 等价于：while (!pred()) { cv.wait(lock); }
        // 这能处理虚假唤醒和丢失唤醒
        cv.wait(lock, [&]() { return data > 0; });
        safe_print("  [消费者] 收到 data = ", data);
        done = true;
        cv.notify_one(); // 通知生产者
    });

    // 生产者：先 notify 再设置条件（测试 predicate 的必要性）
    std::thread producer([&]() {
        // 先拿到锁设置数据
        {
            std::lock_guard<std::mutex> lock(mtx);
            data = 42;
            safe_print("  [生产者] 设置 data = 42");
        }
        cv.notify_one();

        // 等待消费者确认
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return done; });
        safe_print("  [生产者] 消费者已确认接收");
    });

    consumer.join();
    producer.join();

    safe_print("  wait with predicate 是使用条件变量的正确姿势。");
    safe_print("  它自动处理虚假唤醒（spurious wakeup）和丢失唤醒（lost wakeup）。");
}

// ============================================================================
// 3. 丢失唤醒（lost wakeup）演示
// ============================================================================
void demo_lost_wakeup() {
    safe_print("\n=== 3. 丢失唤醒（lost wakeup）===");

    std::mutex mtx;
    std::condition_variable cv;
    bool signal_sent = false;

    // 方式 A：没有 predicate，如果 notify 先于 wait 执行，就丢失了
    // 这里用 try_lock_for 模拟超时来演示问题
    std::thread waiter([&]() {
        std::unique_lock<std::mutex> lock(mtx);
        // 模拟：notify 可能在 wait 之前就发生了
        // 没有 predicate 的情况下，wait 会永远阻塞
        // 我们用 wait_for 加超时来避免永久阻塞
        if (cv.wait_for(lock, std::chrono::milliseconds(500)) == std::cv_status::timeout) {
            safe_print("  [waiter] 超时！如果 notify 在 wait 之前发生就会这样。");
            safe_print("  [waiter] signal_sent = ", signal_sent,
                       "（条件已满足但 wait 没有被唤醒）");
        } else {
            safe_print("  [waiter] 正常唤醒");
        }
    });

    // 先发信号
    {
        std::lock_guard<std::mutex> lock(mtx);
        signal_sent = true;
        safe_print("  [signaler] 发送信号（signal_sent = true）");
    }
    cv.notify_one();

    // 此时如果 waiter 还没开始 wait，这个 notify 就丢失了
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // waiter 开始 wait，但信号已经发过了 —— 没有 predicate 就收不到

    waiter.join();

    safe_print("  解决方案：始终使用 wait with predicate（检查条件变量）。");
    safe_print("  predicate 使得即使 notify 先于 wait，条件已满足也会立即返回。");
}

// ============================================================================
// 4. wait_for 带超时的等待
// ============================================================================
void demo_wait_for_timeout() {
    safe_print("\n=== 4. wait_for 带超时 ===");

    std::mutex mtx;
    std::condition_variable cv;
    bool event_happened = false;

    std::thread waiter([&]() {
        std::unique_lock<std::mutex> lock(mtx);
        safe_print("  [等待者] 等待事件，最多 300ms...");

        auto start = std::chrono::steady_clock::now();
        bool success =
            cv.wait_for(lock, std::chrono::milliseconds(300), [&]() { return event_happened; });
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

        if (success) {
            safe_print("  [等待者] 事件发生！耗时 ", ms.count(), "ms");
        } else {
            safe_print("  [等待者] 等待超时（300ms），事件未发生");
        }
    });

    // 不触发事件，让 waiter 超时
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    safe_print("  [主线程] 这次不触发事件，让等待者超时。");

    waiter.join();

    // 再来一次，这次在 100ms 后触发
    event_happened = false;
    std::thread waiter2([&]() {
        std::unique_lock<std::mutex> lock(mtx);
        safe_print("  [等待者2] 等待事件，最多 500ms...");

        bool success =
            cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return event_happened; });
        if (success) {
            safe_print("  [等待者2] 事件发生！");
        } else {
            safe_print("  [等待者2] 等待超时");
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(mtx);
        event_happened = true;
    }
    cv.notify_one();
    safe_print("  [主线程] 100ms 后触发事件");

    waiter2.join();

    safe_print("  wait_for 适合'带截止时间的等待'，避免无限阻塞。");
}

// ============================================================================
// 5. BoundedQueue<T> —— 有界阻塞队列
//
// 经典的并发数据结构，使用两个条件变量：
//   - not_full_：队列满时，生产者等待
//   - not_empty_：队列空时，消费者等待
// ============================================================================
template <typename T> class BoundedQueue {
  public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    // 阻塞式入队：队列满时等待
    void push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this]() { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    // 带超时的入队：队列满时等待，超时返回 false
    bool push_for(T value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool success =
            not_full_.wait_for(lock, timeout, [this]() { return queue_.size() < capacity_; });
        if (!success) {
            return false;
        }
        queue_.push(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    // 阻塞式出队：队列空时等待
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this]() { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return value;
    }

    // 非阻塞式出队：队列空时返回 nullopt
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return value;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::queue<T> queue_;
    std::size_t capacity_;
};

// ============================================================================
// 6. 生产者-消费者模型
// ============================================================================
void demo_producer_consumer() {
    safe_print("\n=== 5. BoundedQueue + 生产者-消费者 ===");

    constexpr std::size_t kQueueCapacity = 10;
    constexpr int kItemsPerProducer = 50;
    constexpr int kProducerCount = 3;
    constexpr int kConsumerCount = 3;

    BoundedQueue<int> queue(kQueueCapacity);

    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};
    std::atomic<int> expected_sum{0};
    std::atomic<int> actual_sum{0};

    // 生产者
    auto producer = [&](int producer_id) {
        for (int i = 0; i < kItemsPerProducer; ++i) {
            int value = producer_id * kItemsPerProducer + i;
            expected_sum += value;
            queue.push(value);
            ++total_produced;
        }
        safe_print("  [生产者 ", producer_id, "] 完成，生产 ", kItemsPerProducer, " 个元素");
    };

    // 消费者
    std::atomic<bool> production_done{false};
    auto consumer = [&](int consumer_id) {
        int local_sum = 0;
        int local_count = 0;
        while (true) {
            // 尝试非阻塞出队
            auto value = queue.try_pop();
            if (value.has_value()) {
                local_sum += *value;
                ++local_count;
                ++total_consumed;
            } else if (production_done.load()) {
                // 生产结束且队列为空
                break;
            } else {
                // 队列暂时为空，短暂等待
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        actual_sum += local_sum;
        safe_print("  [消费者 ", consumer_id, "] 完成，消费 ", local_count,
                   " 个元素，局部和 = ", local_sum);
    };

    // 启动消费者
    std::vector<std::thread> consumers;
    for (int i = 0; i < kConsumerCount; ++i) {
        consumers.emplace_back(consumer, i);
    }

    // 启动生产者
    std::vector<std::thread> producers;
    for (int i = 0; i < kProducerCount; ++i) {
        producers.emplace_back(producer, i);
    }

    // 等待生产者完成
    for (auto& th : producers) {
        th.join();
    }
    production_done = true;
    safe_print("  所有生产者完成。总生产：", total_produced.load());

    // 等待消费者完成
    for (auto& th : consumers) {
        th.join();
    }
    safe_print("  所有消费者完成。总消费：", total_consumed.load());

    safe_print("  期望总和：", expected_sum.load());
    safe_print("  实际总和：", actual_sum.load());
    safe_print("  数据一致性：", expected_sum.load() == actual_sum.load() ? "通过" : "失败");
    safe_print("  BoundedQueue 是线程间安全传递数据的核心工具。");
}

// ============================================================================
// main
// ============================================================================
int main() {
    safe_print("条件变量：wait/notify 模式与有界阻塞队列");

    demo_basic_wait_notify();
    demo_wait_with_predicate();
    demo_lost_wakeup();
    demo_wait_for_timeout();
    demo_producer_consumer();

    safe_print("\n全部演示完成。");
    return 0;
}
