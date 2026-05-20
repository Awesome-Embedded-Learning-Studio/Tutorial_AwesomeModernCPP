/*
 * 线程安全的有界阻塞队列 —— BoundedQueue<T>
 *
 * 本文件演示如何用 mutex + condition_variable 实现一个完整的
 * 有界阻塞队列，支持：
 *   - 阻塞式 push / pop
 *   - 非阻塞式 try_push / try_pop
 *   - 带超时的 push_for / pop_for
 *   - close() 优雅关闭
 *   - 关闭后所有阻塞操作立即返回 kClosed
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/01_bounded_queue
 *
 * 编译器：GCC 12+ | Clang 15+（需要 C++20）
 * 平台：x86-64 Linux / macOS
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// QueueResult：所有队列操作的统一返回值枚举
// ---------------------------------------------------------------------------
enum class QueueResult {
    kSuccess, // 操作成功
    kClosed,  // 队列已关闭，操作被拒绝
    kTimeout, // 超时，操作未完成
    kFull,    // 队列已满（仅非阻塞 try_push）
    kEmpty,   // 队列已空（仅非阻塞 try_pop）
};

/// @brief 将 QueueResult 转为可读字符串
static std::string to_string(QueueResult r) {
    switch (r) {
        case QueueResult::kSuccess:
            return "Success";
        case QueueResult::kClosed:
            return "Closed";
        case QueueResult::kTimeout:
            return "Timeout";
        case QueueResult::kFull:
            return "Full";
        case QueueResult::kEmpty:
            return "Empty";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// BoundedQueue<T> —— 有界阻塞队列
// ---------------------------------------------------------------------------
/// @brief 线程安全的有界阻塞队列，支持优雅关闭与超时
/// @tparam T 元素类型，必须可移动
template <typename T> class BoundedQueue {
  public:
    /// @brief 构造一个容量为 capacity 的有界队列
    /// @param capacity 队列最大元素数，必须大于 0
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("BoundedQueue capacity must be > 0");
        }
    }

    // 禁止拷贝与移动——内部持有 mutex，不可移动
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // ------------------------------------------------------------------
    // 阻塞式操作
    // ------------------------------------------------------------------

    /// @brief 阻塞地将元素压入队列
    /// @param value 要压入的元素（右值）
    /// @return kSuccess / kClosed
    QueueResult push(T value) {
        std::unique_lock lock(mutex_);
        // 等待直到队列有空间，或者队列被关闭
        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    /// @brief 阻塞地从队列弹出一个元素
    /// @param out 输出参数，接收弹出的元素
    /// @return kSuccess / kClosed
    QueueResult pop(T& out) {
        std::unique_lock lock(mutex_);
        // 等待直到队列非空，或者队列被关闭
        not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });

        if (closed_ && queue_.empty()) {
            // 已关闭且已排空，不再有新元素
            return QueueResult::kClosed;
        }

        // closed_ 但 queue_ 非空：允许消费者把残余元素取走
        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // ------------------------------------------------------------------
    // 非阻塞式操作
    // ------------------------------------------------------------------

    /// @brief 尝试压入元素，不阻塞
    /// @param value 要压入的元素
    /// @return kSuccess / kFull / kClosed
    QueueResult try_push(T value) {
        std::lock_guard lock(mutex_);

        if (closed_) {
            return QueueResult::kClosed;
        }
        if (queue_.size() >= capacity_) {
            return QueueResult::kFull;
        }

        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    /// @brief 尝试弹出一个元素，不阻塞
    /// @param out 输出参数
    /// @return kSuccess / kEmpty / kClosed
    QueueResult try_pop(T& out) {
        std::lock_guard lock(mutex_);

        if (queue_.empty()) {
            return closed_ ? QueueResult::kClosed : QueueResult::kEmpty;
        }

        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // ------------------------------------------------------------------
    // 超时操作
    // ------------------------------------------------------------------

    /// @brief 带超时的压入操作
    /// @param value 要压入的元素
    /// @param duration 超时时长（任意 std::chrono::duration）
    /// @return kSuccess / kClosed / kTimeout
    template <typename Rep, typename Period>
    QueueResult push_for(T value, const std::chrono::duration<Rep, Period>& duration) {
        std::unique_lock lock(mutex_);

        bool has_space = not_full_.wait_for(
            lock, duration, [this] { return queue_.size() < capacity_ || closed_; });

        if (!has_space) {
            return QueueResult::kTimeout;
        }
        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    /// @brief 带超时的弹出操作
    /// @param out 输出参数
    /// @param duration 超时时长
    /// @return kSuccess / kClosed / kTimeout
    template <typename Rep, typename Period>
    QueueResult pop_for(T& out, const std::chrono::duration<Rep, Period>& duration) {
        std::unique_lock lock(mutex_);

        bool has_item =
            not_empty_.wait_for(lock, duration, [this] { return !queue_.empty() || closed_; });

        if (!has_item) {
            return QueueResult::kTimeout;
        }

        if (closed_ && queue_.empty()) {
            return QueueResult::kClosed;
        }

        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // ------------------------------------------------------------------
    // 关闭与查询
    // ------------------------------------------------------------------

    /// @brief 关闭队列，唤醒所有阻塞中的生产者/消费者
    /// 关闭后：push 返回 kClosed；pop 允许排空残余元素后返回 kClosed
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        // 唤醒所有等待者，让它们检查 closed_ 标志
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    /// @brief 队列是否已关闭
    bool is_closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    /// @brief 当前队列中的元素数
    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    /// @brief 队列是否为空
    bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

  private:
    std::deque<T> queue_;               // 底层容器
    mutable std::mutex mutex_;          // 保护 queue_ 与 closed_
    std::condition_variable not_full_;  // 队列不满时通知生产者
    std::condition_variable not_empty_; // 队列不空时通知消费者
    std::size_t capacity_;              // 队列容量上限
    bool closed_ = false;               // 关闭标志
};

// ===========================================================================
// 演示 1：多生产者 - 多消费者 + close() 优雅关闭
// ===========================================================================
static void demo_producer_consumer() {
    std::cout << "\n=== Demo 1: 多生产者-消费者 + close() 优雅关闭 ===\n";

    constexpr std::size_t kQueueCapacity = 10;
    constexpr int kItemsPerProducer = 20;
    constexpr int kProducerCount = 3;

    BoundedQueue<int> queue(kQueueCapacity);

    // 统计每个消费者消费的数量
    std::atomic<int> total_consumed{0};
    std::atomic<int> total_produced{0};

    // --- 启动消费者 ---
    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c) {
        consumers.emplace_back([&queue, &total_consumed, c]() {
            int value;
            while (true) {
                auto result = queue.pop(value);
                if (result == QueueResult::kClosed) {
                    // 队列已关闭且已排空，消费者退出
                    std::cout << "  [消费者 " << c << "] 收到 kClosed，退出\n";
                    break;
                }
                ++total_consumed;
            }
        });
    }

    // --- 启动生产者 ---
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducerCount; ++p) {
        producers.emplace_back([&queue, &total_produced, p]() {
            for (int i = 0; i < kItemsPerProducer; ++i) {
                int value = p * 1000 + i;
                auto result = queue.push(value);
                if (result == QueueResult::kSuccess) {
                    ++total_produced;
                }
            }
            std::cout << "  [生产者 " << p << "] 完成\n";
        });
    }

    // 等待所有生产者完成
    for (auto& t : producers) {
        t.join();
    }

    std::cout << "  所有生产者已完成，关闭队列...\n";
    queue.close();

    // 等待消费者排空并退出
    for (auto& t : consumers) {
        t.join();
    }

    std::cout << "  生产总量: " << total_produced.load() << "  消费总量: " << total_consumed.load()
              << "\n";
    std::cout << "  队列残余: " << queue.size() << "\n";
}

// ===========================================================================
// 演示 2：超时行为
// ===========================================================================
static void demo_timeout() {
    std::cout << "\n=== Demo 2: 超时行为 ===\n";

    BoundedQueue<int> queue(2);

    // --- push_for 超时：队列满时等待 200ms ---
    queue.push(1);
    queue.push(2); // 队列已满

    auto start = std::chrono::steady_clock::now();
    auto result = queue.push_for(3, std::chrono::milliseconds(200));
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    std::cout << "  push_for(3, 200ms) => " << to_string(result) << " (实际等待 " << ms.count()
              << " ms)\n";

    // --- pop_for 超时：队列空时等待 200ms ---
    int value = 0;
    int dummy;
    // 先把队列排空
    queue.pop(dummy);
    queue.pop(dummy);

    start = std::chrono::steady_clock::now();
    result = queue.pop_for(value, std::chrono::milliseconds(200));
    elapsed = std::chrono::steady_clock::now() - start;
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    std::cout << "  pop_for(val, 200ms) => " << to_string(result) << " (实际等待 " << ms.count()
              << " ms)\n";

    // --- close 后 push_for 立即返回 kClosed ---
    queue.close();
    result = queue.push_for(42, std::chrono::milliseconds(1000));
    std::cout << "  close 后 push_for(42, 1000ms) => " << to_string(result)
              << " (应该立即返回 kClosed)\n";
}

// ===========================================================================
// 演示 3：try_push / try_pop 非阻塞操作
// ===========================================================================
static void demo_nonblocking() {
    std::cout << "\n=== Demo 3: 非阻塞 try_push / try_pop ===\n";

    BoundedQueue<int> queue(3);

    // 填满队列
    for (int i = 0; i < 3; ++i) {
        auto r = queue.try_push(i);
        std::cout << "  try_push(" << i << ") => " << to_string(r) << "\n";
    }

    // 队列满，try_push 应返回 kFull
    auto r = queue.try_push(99);
    std::cout << "  try_push(99) [满] => " << to_string(r) << "\n";

    // 弹出所有元素
    int value;
    while (true) {
        r = queue.try_pop(value);
        std::cout << "  try_pop() => " << to_string(r);
        if (r == QueueResult::kSuccess) {
            std::cout << " (value=" << value << ")";
        }
        std::cout << "\n";
        if (r != QueueResult::kSuccess) {
            break;
        }
    }

    // 关闭后 try_pop 应返回 kClosed
    queue.close();
    r = queue.try_pop(value);
    std::cout << "  close 后 try_pop() => " << to_string(r) << "\n";
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    demo_producer_consumer();
    demo_timeout();
    demo_nonblocking();

    std::cout << "\n所有演示完成。\n";
    return 0;
}
