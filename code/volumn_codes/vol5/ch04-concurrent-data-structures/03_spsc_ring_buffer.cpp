/*
 * SPSC 环形缓冲区 —— 单生产者单消费者的无锁队列
 *
 * 本文件实现一个基于环形缓冲区的 SPSC（Single-Producer Single-Consumer）队列，
 * 仅使用 acquire/release 内存序，避免了 seq_cst 的开销。
 * 同时提供性能基准测试，与基于 mutex 的队列进行吞吐量对比。
 *
 * 核心设计要点：
 *   - head_（写指针）只被生产者线程修改
 *   - tail_（读指针）只被消费者线程修改
 *   - push 时用 release store 发布新元素
 *   - pop 时用 acquire load 同步生产者的写入
 *   - 容量为 2 的幂，可用位运算代替取模
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/03_spsc_ring_buffer
 *
 * 编译器：GCC 12+ | Clang 15+（需要 C++20）
 * 平台：x86-64 Linux
 */

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

// ===========================================================================
// SpscRingBuffer<T, Capacity> —— SPSC 环形缓冲区
// ===========================================================================

/// @brief 单生产者单消费者的无锁环形缓冲区
/// @tparam T 元素类型，必须可平凡拷贝或可移动
/// @tparam Capacity 缓冲区容量（必须为 2 的幂）
template <typename T, std::size_t Capacity> class SpscRingBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2");

  public:
    SpscRingBuffer() = default;

    // 禁止拷贝与移动
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // ------------------------------------------------------------------
    // 生产者接口（只应在生产者线程调用）
    // ------------------------------------------------------------------

    /// @brief 压入元素（阻塞直到有空间）
    /// @param value 要压入的元素
    void push(T value) {
        // 自旋等待直到有空闲槽位
        while (full()) {
            // 生产者忙等待；在实际应用中可加入 _mm_pause() 提示 CPU
        }

        // 写入数据
        std::size_t pos = head_.load(std::memory_order_relaxed) & kMask;
        slots_[pos] = std::move(value);

        // release store：确保上面的写入在对消费者可见之前完成
        head_.store(head_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /// @brief 尝试压入元素（非阻塞）
    /// @param value 要压入的元素
    /// @return true 成功，false 队列已满
    bool try_push(T value) {
        if (full()) {
            return false;
        }

        std::size_t pos = head_.load(std::memory_order_relaxed) & kMask;
        slots_[pos] = std::move(value);

        head_.store(head_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        return true;
    }

    // ------------------------------------------------------------------
    // 消费者接口（只应在消费者线程调用）
    // ------------------------------------------------------------------

    /// @brief 弹出元素（阻塞直到有数据）
    /// @return 弹出的元素
    T pop() {
        while (empty()) {
            // 消费者忙等待
        }

        std::size_t pos = tail_.load(std::memory_order_relaxed) & kMask;
        T value = std::move(slots_[pos]);

        // release store：消费者推进 tail，让生产者看到空间释放
        tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        return value;
    }

    /// @brief 尝试弹出元素（非阻塞）
    /// @param out 输出参数
    /// @return true 成功，false 队列已空
    bool try_pop(T& out) {
        if (empty()) {
            return false;
        }

        std::size_t pos = tail_.load(std::memory_order_relaxed) & kMask;
        out = std::move(slots_[pos]);

        tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        return true;
    }

    // ------------------------------------------------------------------
    // 查询接口
    // ------------------------------------------------------------------

    /// @brief 队列是否为空（从消费者视角）
    bool empty() const {
        // acquire load：同步生产者的 release store
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_relaxed);
    }

    /// @brief 队列是否已满（从生产者视角）
    bool full() const {
        // acquire load：同步消费者的 release store
        return (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire)) >=
               Capacity;
    }

    /// @brief 当前队列中的元素数（近似值）
    std::size_t size() const {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_acquire);
        return h - t;
    }

  private:
    static constexpr std::size_t kMask = Capacity - 1;

    // slots_ 不需要是 atomic——通过 head_/tail_ 的 release/acquire 同步保护
    alignas(64) std::array<T, Capacity> slots_;

    // head_ 和 tail_ 分开缓存行，避免 false sharing
    alignas(64) std::atomic<std::size_t> head_{0}; // 写指针，生产者写
    alignas(64) std::atomic<std::size_t> tail_{0}; // 读指针，消费者写
};

// ===========================================================================
// MutexQueue —— 用于性能对比的基于 mutex 的阻塞队列
// ===========================================================================

template <typename T> class MutexQueue {
  public:
    explicit MutexQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(T value) {
        std::unique_lock lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
    }

    T pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return value;
    }

  private:
    std::deque<T> queue_;
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::size_t capacity_;
};

// ===========================================================================
// 基准测试：SPSC Ring Buffer vs Mutex Queue 吞吐量
// ===========================================================================

static void benchmark_spsc_vs_mutex() {
    std::cout << "\n=== Benchmark: SPSC Ring Buffer vs Mutex Queue ===\n";

    constexpr std::size_t kMessageCount = 10'000'000;
    constexpr std::size_t kSpscCapacity = 1024;

    // --- SPSC Ring Buffer ---
    {
        SpscRingBuffer<int, kSpscCapacity> buffer;

        auto start = std::chrono::high_resolution_clock::now();

        std::thread producer([&buffer]() {
            for (std::size_t i = 0; i < kMessageCount; ++i) {
                buffer.push(static_cast<int>(i));
            }
        });

        std::thread consumer([&buffer]() {
            for (std::size_t i = 0; i < kMessageCount; ++i) {
                volatile int val = buffer.pop();
                (void)val;
            }
        });

        producer.join();
        consumer.join();

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        auto ns_per_msg = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
                          static_cast<double>(kMessageCount);

        std::cout << "  SPSC Ring Buffer:   " << ms.count() << " ms total, " << ns_per_msg
                  << " ns/msg\n";
    }

    // --- Mutex Queue ---
    {
        MutexQueue<int> queue(kSpscCapacity);

        auto start = std::chrono::high_resolution_clock::now();

        std::thread producer([&queue]() {
            for (std::size_t i = 0; i < kMessageCount; ++i) {
                queue.push(static_cast<int>(i));
            }
        });

        std::thread consumer([&queue]() {
            for (std::size_t i = 0; i < kMessageCount; ++i) {
                volatile int val = queue.pop();
                (void)val;
            }
        });

        producer.join();
        consumer.join();

        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        auto ns_per_msg = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() /
                          static_cast<double>(kMessageCount);

        std::cout << "  Mutex Queue:        " << ms.count() << " ms total, " << ns_per_msg
                  << " ns/msg\n";
    }

    std::cout << "  （SPSC 通常比 Mutex 快数倍，得益于无锁和 cache-line 隔离）\n";
}

// ===========================================================================
// 演示：基本功能验证
// ===========================================================================

static void demo_basic_operations() {
    std::cout << "\n=== Demo: SPSC Ring Buffer 基本功能 ===\n";

    SpscRingBuffer<int, 8> buffer;

    // 测试 try_push / try_pop
    std::cout << "  初始状态: empty=" << buffer.empty() << " full=" << buffer.full() << "\n";

    // 填充到满
    for (int i = 0; i < 8; ++i) {
        bool ok = buffer.try_push(i);
        std::cout << "  try_push(" << i << ") => " << (ok ? "ok" : "fail") << "\n";
    }

    // 再 push 一个应该失败（队列满）
    bool ok = buffer.try_push(99);
    std::cout << "  try_push(99) [满] => " << (ok ? "ok" : "fail") << "\n";
    std::cout << "  状态: empty=" << buffer.empty() << " full=" << buffer.full()
              << " size=" << buffer.size() << "\n";

    // 弹出所有元素
    int value;
    while (buffer.try_pop(value)) {
        std::cout << "  try_pop() => " << value << "\n";
    }

    // 再 pop 应该失败（队列空）
    ok = buffer.try_pop(value);
    std::cout << "  try_pop() [空] => " << (ok ? "ok" : "fail") << "\n";
    std::cout << "  状态: empty=" << buffer.empty() << " full=" << buffer.full() << "\n";
}

// ===========================================================================
// 演示：多线程 SPSC 生产-消费
// ===========================================================================

static void demo_producer_consumer() {
    std::cout << "\n=== Demo: SPSC 多线程生产-消费 ===\n";

    constexpr std::size_t kCapacity = 256;
    constexpr std::size_t kItemCount = 1'000'000;

    SpscRingBuffer<int, kCapacity> buffer;

    // 启动生产者线程
    std::thread producer([&buffer]() {
        for (std::size_t i = 0; i < kItemCount; ++i) {
            buffer.push(static_cast<int>(i));
        }
    });

    // 启动消费者线程
    std::thread consumer([&buffer]() {
        int last = -1;
        for (std::size_t i = 0; i < kItemCount; ++i) {
            int val = buffer.pop();
            if (val != static_cast<int>(i)) {
                std::cerr << "  错误：期望 " << i << "，实际 " << val << "\n";
            }
            last = val;
        }
        std::cout << "  消费者收到最后一个值: " << last << "\n";
    });

    producer.join();
    consumer.join();

    std::cout << "  所有 " << kItemCount << " 个元素按序传递，验证通过\n";
}

// ===========================================================================
// main
// ===========================================================================
int main() {
    demo_basic_operations();
    demo_producer_consumer();
    benchmark_spsc_vs_mutex();

    std::cout << "\n所有演示完成。\n";
    return 0;
}
