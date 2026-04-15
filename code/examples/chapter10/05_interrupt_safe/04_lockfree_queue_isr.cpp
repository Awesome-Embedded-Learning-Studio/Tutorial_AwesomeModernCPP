// ISR-主线程无锁队列 - 演示单生产者单消费者队列的实现
// 本示例展示了完整的 SPSC 队列，适用于 ISR 与主线程通信

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <array>
#include <string>

// ============================================================================
// 基础 SPSC 队列实现
// ============================================================================

template<typename T, size_t Size>
class SPSCQueue {
    static_assert(Size > 0 && (Size & (Size - 1)) == 0,
        "Size must be a power of 2 for efficient modulo");

public:
    SPSCQueue() : read_idx_(0), write_idx_(0) {}

    // 生产者调用：推送数据（ISR 或主线程）
    bool push(const T& item) noexcept {
        const size_t current_write = write_idx_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & (Size - 1);

        // 检查队列是否满
        if (next_write == read_idx_.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }

        // 写入数据
        buffer_[current_write] = item;

        // release 确保数据写入完成后，再更新写索引
        write_idx_.store(next_write, std::memory_order_release);
        return true;
    }

    // 消费者调用：弹出数据（主线程或 ISR）
    bool pop(T& item) noexcept {
        const size_t current_read = read_idx_.load(std::memory_order_relaxed);

        // 检查队列是否空
        if (current_read == write_idx_.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }

        // 读取数据
        item = buffer_[current_read];

        // release 确保数据读取完成后，再更新读索引
        const size_t next_read = (current_read + 1) & (Size - 1);
        read_idx_.store(next_read, std::memory_order_release);
        return true;
    }

    // 查询状态（仅供调试/监控）
    size_t size() const noexcept {
        const size_t w = write_idx_.load(std::memory_order_acquire);
        const size_t r = read_idx_.load(std::memory_order_acquire);
        return (w - r) & (Size - 1);
    }

    bool empty() const noexcept {
        return write_idx_.load(std::memory_order_acquire) ==
               read_idx_.load(std::memory_order_acquire);
    }

    bool full() const noexcept {
        const size_t w = write_idx_.load(std::memory_order_relaxed);
        const size_t next_write = (w + 1) & (Size - 1);
        return next_write == read_idx_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Size> buffer_;
    std::atomic<size_t> read_idx_;
    std::atomic<size_t> write_idx_;
};

// ============================================================================
// 带溢出处理的 SPSC 队列
// ============================================================================

template<typename T, size_t Size>
class SPSCQueueWithStats {
public:
    struct Stats {
        size_t push_count = 0;
        size_t pop_count = 0;
        size_t overflow_count = 0;
        size_t underflow_count = 0;
    };

    SPSCQueueWithStats() : read_idx_(0), write_idx_(0) {}

    bool push(const T& item) noexcept {
        const size_t current_write = write_idx_.load(std::memory_order_relaxed);
        const size_t next_write = (current_write + 1) & (Size - 1);

        if (next_write == read_idx_.load(std::memory_order_acquire)) {
            stats_.overflow_count++;
            return false;
        }

        buffer_[current_write] = item;
        write_idx_.store(next_write, std::memory_order_release);
        stats_.push_count++;
        return true;
    }

    bool pop(T& item) noexcept {
        const size_t current_read = read_idx_.load(std::memory_order_relaxed);

        if (current_read == write_idx_.load(std::memory_order_acquire)) {
            stats_.underflow_count++;
            return false;
        }

        item = buffer_[current_read];
        const size_t next_read = (current_read + 1) & (Size - 1);
        read_idx_.store(next_read, std::memory_order_release);
        stats_.pop_count++;
        return true;
    }

    Stats get_stats() const noexcept {
        return stats_;
    }

    void reset_stats() noexcept {
        stats_ = Stats{};
    }

private:
    std::array<T, Size> buffer_;
    std::atomic<size_t> read_idx_;
    std::atomic<size_t> write_idx_;
    Stats stats_;  // 注意：统计信息不是原子的，适用于单线程读取
};

// ============================================================================
// UART 接收缓冲区示例
// ============================================================================

class UART_RXBuffer {
public:
    static constexpr size_t BufferSize = 256;

    // ISR 中调用：接收字节
    void irq_handler(uint8_t byte) noexcept {
        if (!queue_.push(byte)) {
            overflow_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 主线程调用：获取接收到的字节
    bool get_byte(uint8_t& byte) noexcept {
        return queue_.pop(byte);
    }

    // 批量读取
    size_t read_bytes(uint8_t* buffer, size_t max_len) noexcept {
        size_t count = 0;
        while (count < max_len && queue_.pop(buffer[count])) {
            count++;
        }
        return count;
    }

    // 获取溢出计数
    uint32_t get_overflow_count() const noexcept {
        return overflow_.load(std::memory_order_relaxed);
    }

    // 获取当前缓冲区大小
    size_t available() const noexcept {
        return queue_.size();
    }

private:
    SPSCQueue<uint8_t, BufferSize> queue_;
    std::atomic<uint32_t> overflow_{0};
};

// ============================================================================
// 日志缓冲区示例（主线程到 ISR）
// ============================================================================

template<size_t Size = 64>
class LogBuffer {
public:
    struct LogEntry {
        uint32_t timestamp;
        char message[32];
    };

    // 主线程调用：添加日志
    bool log(const char* msg) noexcept {
        LogEntry entry;
        entry.timestamp = get_timestamp();
        // 安全地复制消息
        for (size_t i = 0; i < sizeof(entry.message) - 1 && msg[i] != '\0'; ++i) {
            entry.message[i] = msg[i];
        }
        entry.message[sizeof(entry.message) - 1] = '\0';

        return queue_.push(entry);
    }

    // ISR 中调用：获取日志（例如发送到调试接口）
    bool get_log(LogEntry& entry) noexcept {
        return queue_.pop(entry);
    }

private:
    static uint32_t get_timestamp() noexcept {
        static std::atomic<uint32_t> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    SPSCQueue<LogEntry, Size> queue_;
};

// ============================================================================
// 演示函数
// ============================================================================

void demonstrate_basic_queue() {
    printf("\n=== 基本 SPSC 队列 ===\n");

    SPSCQueue<int, 16> queue;
    std::atomic<bool> stop{false};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // 生产者（模拟 UART ISR）
    std::thread producer([&]() {
        int value = 0;
        while (!stop.load() && value < 100) {
            if (queue.push(value)) {
                produced.fetch_add(1);
                value++;
            } else {
                printf("  队列满，等待...\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    // 消费者（主循环）
    std::thread consumer([&]() {
        int received = 0;
        int value;
        while (received < 100) {
            if (queue.pop(value)) {
                consumed.fetch_add(1);
                received++;
                if (received % 20 == 0) {
                    printf("  已处理: %d 项, 队列深度: %zu\n",
                        received, queue.size());
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        stop.store(true);
    });

    producer.join();
    consumer.join();

    printf("  完成: 生产=%d, 消费=%d\n", produced.load(), consumed.load());
}

void demonstrate_uart_buffer() {
    printf("\n=== UART 接收缓冲区示例 ===\n");

    UART_RXBuffer uart_rx;
    std::atomic<bool> stop{false};
    std::atomic<int> total_received{0};

    // 模拟 UART ISR
    std::thread uart_isr_thread([&]() {
        uint8_t byte = 'A';
        int count = 0;
        while (!stop.load() && count < 200) {
            // 模拟字节到达
            uart_rx.irq_handler(byte);
            byte = (byte - 'A' + 1) % 26 + 'A';
            count++;

            // 模拟波特率延迟
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // 主循环处理接收数据
    std::thread main_loop([&]() {
        uint8_t buffer[32];
        int total = 0;

        while (total < 200) {
            size_t count = uart_rx.read_bytes(buffer, sizeof(buffer));
            if (count > 0) {
                total += count;
                total_received.fetch_add(count);
                printf("  收到 %zu 字节: ", count);
                for (size_t i = 0; i < count && i < 8; ++i) {
                    printf("%c", buffer[i]);
                }
                if (count > 8) printf("...");
                printf("\n");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        stop.store(true);
    });

    uart_isr_thread.join();
    main_loop.join();

    printf("  总接收: %d 字节\n", total_received.load());
    printf("  溢出次数: %u\n", uart_rx.get_overflow_count());
}

void demonstrate_queue_with_stats() {
    printf("\n=== 带统计的队列 ===\n");

    SPSCQueueWithStats<int, 8> queue;
    std::atomic<bool> stop{false};

    // 快速生产者
    std::thread producer([&]() {
        int value = 0;
        while (!stop.load() && value < 100) {
            if (!queue.push(value)) {
                // 队列满，继续尝试
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            } else {
                value++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // 慢速消费者
    std::thread consumer([&]() {
        int value;
        int received = 0;
        while (received < 100) {
            if (queue.pop(value)) {
                received++;
                if (received % 20 == 0) {
                    auto stats = queue.get_stats();
                    printf("  进度: %d, 入队: %zu, 出队: %zu, 溢出: %zu, 空读: %zu\n",
                        received, stats.push_count, stats.pop_count,
                        stats.overflow_count, stats.underflow_count);
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        stop.store(true);
    });

    producer.join();
    consumer.join();

    auto final_stats = queue.get_stats();
    printf("\n  最终统计:\n");
    printf("    入队: %zu\n", final_stats.push_count);
    printf("    出队: %zu\n", final_stats.pop_count);
    printf("    溢出: %zu (队列满时丢弃)\n", final_stats.overflow_count);
    printf("    空读: %zu (队列空时尝试)\n", final_stats.underflow_count);
}

void demonstrate_log_buffer() {
    printf("\n=== 日志缓冲区示例 ===\n");

    LogBuffer<16> log_buffer;
    std::atomic<bool> stop{false};
    std::atomic<int> logged_count{0};
    std::atomic<int> sent_count{0};

    // 主线程记录日志
    std::thread logger_thread([&]() {
        const char* messages[] = {
            "System started",
            "Init hardware",
            "Config UART",
            "Start tasks",
            "Running..."
        };

        for (int i = 0; i < 20 && !stop.load(); ++i) {
            const char* msg = messages[i % 5];
            if (log_buffer.log(msg)) {
                logged_count.fetch_add(1);
                printf("  [主线程] 日志: %s\n", msg);
            } else {
                printf("  [主线程] 日志缓冲区满!\n");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        stop.store(true);
    });

    // 模拟 ISR 发送日志到调试接口
    std::thread debug_tx_thread([&]() {
        LogBuffer<16>::LogEntry entry;
        while (!stop.load()) {
            if (log_buffer.get_log(entry)) {
                sent_count.fetch_add(1);
                printf("  [调试输出] T=%u: %s\n", entry.timestamp, entry.message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    logger_thread.join();
    debug_tx_thread.join();

    printf("  日志记录: %d, 发送: %d\n", logged_count.load(), sent_count.load());
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  ISR-主线程无锁队列演示\n");
    printf("========================================\n");

    demonstrate_basic_queue();
    demonstrate_uart_buffer();
    demonstrate_queue_with_stats();
    demonstrate_log_buffer();

    printf("\n=== 总结 ===\n");
    printf("1. SPSC 队列使用两个原子索引，无需锁\n");
    printf("2. 正确的内存序确保数据可见性\n");
    printf("3. ISR 中处理队列满的情况（丢弃或等待）\n");
    printf("4. 主循环定期从队列读取并处理\n");
    printf("5. 可以添加统计信息监控队列健康状态\n");
    printf("6. 队列大小最好是 2 的幂，取模运算更高效\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 04_lockfree_queue_isr.cpp -o 04_lockfree_queue_isr
