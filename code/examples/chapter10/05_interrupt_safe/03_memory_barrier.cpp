// 内存屏障示例 - 演示编译器屏障和 CPU 内存屏障的使用
// 本示例展示了如何使用显式屏障来控制内存访问顺序

#include <cstdint>
#include <cstdio>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <array>

// ============================================================================
// 编译器屏障示例
// ============================================================================

// GCC/Clang 的编译器屏障
#if defined(__GNUC__) || defined(__clang__)
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define COMPILER_BARRIER() std::atomic_thread_fence(std::memory_order_relaxed)
#endif

// 演示编译器屏障的效果
namespace compiler_barrier_demo {
    // 没有屏障的情况：编译器可能重排
    void without_barrier() {
        int x = 1;
        int y = 2;
        // 编译器可能优化为先写 y 再写 x
        printf("Without barrier: x=%d, y=%d\n", x, y);
    }

    // 有屏障的情况：保证顺序
    void with_barrier() {
        int x = 1;
        COMPILER_BARRIER();
        int y = 2;
        // 确保 y 的赋值不会在 x 之前
        printf("With barrier: x=%d, y=%d\n", x, y);
    }
}

// ============================================================================
// 原子线程屏障示例
// ============================================================================

namespace atomic_fence_demo {
    // 生产者-消费者模式，使用显式屏障

    template<typename T>
    class SPSCQueueWithFence {
    public:
        static constexpr size_t Capacity = 16;

        bool push(const T& item) noexcept {
            const size_t current_write = write_idx_;

            // 计算下一个写位置
            const size_t next_write = (current_write + 1) % Capacity;

            // 检查队列是否满（acquire 确保读可见）
            std::atomic_thread_fence(std::memory_order_acquire);
            if (next_write == read_idx_) {
                return false;  // 队列满
            }

            // 写入数据
            buffer_[current_write] = item;

            // release 屏障：确保数据写入完成后再更新索引
            std::atomic_thread_fence(std::memory_order_release);
            write_idx_ = next_write;

            return true;
        }

        bool pop(T& item) noexcept {
            const size_t current_read = read_idx_;

            // acquire 屏障：确保写索引可见后再读取数据
            std::atomic_thread_fence(std::memory_order_acquire);
            if (current_read == write_idx_) {
                return false;  // 队列空
            }

            // 读取数据
            item = buffer_[current_read];

            // release 屏障：确保数据读取完成后再更新索引
            std::atomic_thread_fence(std::memory_order_release);
            read_idx_ = (current_read + 1) % Capacity;

            return true;
        }

    private:
        std::array<T, Capacity> buffer_;
        size_t write_idx_ = 0;
        size_t read_idx_ = 0;
    };
}

// ============================================================================
// 双缓冲与屏障
// ============================================================================

namespace double_buffer_fence {
    template<typename T>
    class DoubleBuffer {
    public:
        DoubleBuffer() : write_index_(0), read_index_(1), swapped_(false) {}

        // 写入者获取写入缓冲区
        T* acquire_write_buffer() noexcept {
            return &buffers_[write_index_];
        }

        // 写入完成后提交
        void commit() noexcept {
            // release 屏障：确保数据写入完成
            std::atomic_thread_fence(std::memory_order_release);

            // 交换索引
            size_t temp = write_index_;
            write_index_ = read_index_;
            read_index_ = temp;

            // 设置交换标志
            swapped_.store(true, std::memory_order_relaxed);
        }

        // 读取者尝试获取数据
        const T* try_get_read_buffer() noexcept {
            if (!swapped_.load(std::memory_order_relaxed)) {
                return nullptr;
            }

            // acquire 屏障：确保看到完整的数据
            std::atomic_thread_fence(std::memory_order_acquire);

            swapped_.store(false, std::memory_order_relaxed);
            return &buffers_[read_index_];
        }

    private:
        std::array<T, 2> buffers_;
        size_t write_index_;
        size_t read_index_;
        std::atomic<bool> swapped_;
    };
}

// ============================================================================
// 发布-订阅模式
// ============================================================================

namespace publish_subscribe {
    class DataPublisher {
    public:
        struct Data {
            int value1;
            int value2;
            int value3;
        };

        void publish(const Data& data) noexcept {
            // 准备数据
            data_ = data;

            // 发布：release 屏障确保所有数据写入完成
            std::atomic_thread_fence(std::memory_order_release);
            ready_.store(true, std::memory_order_relaxed);
        }

        bool try_subscribe(Data& out) noexcept {
            // 检查是否就绪
            if (!ready_.load(std::memory_order_relaxed)) {
                return false;
            }

            // 订阅：acquire 屏障确保看到完整数据
            std::atomic_thread_fence(std::memory_order_acquire);
            out = data_;
            ready_.store(false, std::memory_order_relaxed);
            return true;
        }

    private:
        std::atomic<bool> ready_{false};
        Data data_;
    };
}

// ============================================================================
// 内存序对比演示
// ============================================================================

namespace memory_order_comparison {
    // 使用 relaxed：只保原子性，不保顺序
    void relaxed_example() {
        std::atomic<int> x{0};
        std::atomic<int> y{0};

        // 线程1
        std::thread t1([&]() {
            x.store(1, std::memory_order_relaxed);
            y.store(1, std::memory_order_relaxed);
        });

        // 线程2
        std::thread t2([&]() {
            int y_val = y.load(std::memory_order_relaxed);
            int x_val = x.load(std::memory_order_relaxed);
            // 可能看到 y=1 但 x=0！
            printf("  relaxed: y=%d, x=%d (顺序可能不一致)\n", y_val, x_val);
        });

        t1.join();
        t2.join();
    }

    // 使用 acquire-release：保证同步
    void acquire_release_example() {
        std::atomic<int> x{0};
        std::atomic<int> y{0};

        // 线程1
        std::thread t1([&]() {
            x.store(1, std::memory_order_relaxed);
            y.store(1, std::memory_order_release);  // 发布
        });

        // 线程2
        std::thread t2([&]() {
            if (y.load(std::memory_order_acquire) == 1) {  // 订阅
                int x_val = x.load(std::memory_order_relaxed);
                // 一定能看到 x=1！
                printf("  acq_rel: y=1, x=%d (顺序保证一致)\n", x_val);
            }
        });

        t1.join();
        t2.join();
    }
}

// ============================================================================
// 演示函数
// ============================================================================

void demonstrate_compiler_barrier() {
    printf("\n=== 编译器屏障 ===\n");
    printf("编译器屏障阻止编译器重排内存操作\n\n");

    compiler_barrier_demo::without_barrier();
    compiler_barrier_demo::with_barrier();

    printf("\n注意：实际效果需要查看汇编代码验证\n");
}

void demonstrate_atomic_fence_queue() {
    printf("\n=== 原子屏障：SPSC队列 ===\n");

    atomic_fence_demo::SPSCQueueWithFence<int> queue;
    std::atomic<bool> stop{false};
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};

    // 生产者线程（模拟 ISR）
    std::thread producer([&]() {
        int value = 0;
        while (!stop.load() && value < 100) {
            if (queue.push(value)) {
                total_produced.fetch_add(1);
                value++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // 消费者线程（主线程）
    std::thread consumer([&]() {
        int value;
        int received = 0;
        while (received < 100) {
            if (queue.pop(value)) {
                total_consumed.fetch_add(1);
                received++;
                if (received % 20 == 0) {
                    printf("  已处理: %d 项\n", received);
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        stop.store(true);
    });

    producer.join();
    consumer.join();

    printf("  生产: %d, 消费: %d\n",
        total_produced.load(), total_consumed.load());
}

void demonstrate_double_buffer_fence() {
    printf("\n=== 双缓冲与屏障 ===\n");

    double_buffer_fence::DoubleBuffer<int> buffer;
    std::atomic<bool> stop{false};

    // 生产者
    std::thread producer([&]() {
        for (int i = 0; i < 10 && !stop.load(); ++i) {
            int* buf = buffer.acquire_write_buffer();
            *buf = i * 100;
            buffer.commit();
            printf("  生产者写入: %d\n", i * 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // 消费者
    std::thread consumer([&]() {
        int received = 0;
        while (received < 10) {
            const int* data = buffer.try_get_read_buffer();
            if (data) {
                printf("  消费者读取: %d\n", *data);
                received++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        stop.store(true);
    });

    producer.join();
    consumer.join();
}

void demonstrate_publish_subscribe() {
    printf("\n=== 发布-订阅模式 ===\n");

    publish_subscribe::DataPublisher publisher;
    std::atomic<bool> stop{false};

    // 发布者（模拟 ISR）
    std::thread publisher_thread([&]() {
        for (int i = 0; i < 5 && !stop.load(); ++i) {
            publish_subscribe::DataPublisher::Data data{i, i * 10, i * 100};
            publisher.publish(data);
            printf("  发布: {%d, %d, %d}\n", data.value1, data.value2, data.value3);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // 订阅者（主线程）
    std::thread subscriber_thread([&]() {
        int received = 0;
        while (received < 5) {
            publish_subscribe::DataPublisher::Data data;
            if (publisher.try_subscribe(data)) {
                printf("  订阅: {%d, %d, %d}\n", data.value1, data.value2, data.value3);
                received++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        stop.store(true);
    });

    publisher_thread.join();
    subscriber_thread.join();
}

void demonstrate_memory_order_comparison() {
    printf("\n=== 内存序对比 ===\n");
    printf("运行多次观察不同结果...\n\n");

    for (int i = 0; i < 3; ++i) {
        printf("第 %d 轮:\n", i + 1);
        memory_order_comparison::relaxed_example();
        memory_order_comparison::acquire_release_example();
        printf("\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  内存屏障演示\n");
    printf("========================================\n");

    demonstrate_compiler_barrier();
    demonstrate_atomic_fence_queue();
    demonstrate_double_buffer_fence();
    demonstrate_publish_subscribe();
    demonstrate_memory_order_comparison();

    printf("\n=== 总结 ===\n");
    printf("1. 编译器屏障：阻止编译器重排，不生成 CPU 指令\n");
    printf("2. 原子线程屏障：控制 CPU 和编译器的内存访问顺序\n");
    printf("3. release 屏障：确保之前的写入都完成\n");
    printf("4. acquire 屏障：确保看到屏障之前的所有写入\n");
    printf("5. 大多数情况下，原子操作自带的内存序参数足够\n");
    printf("6. 显式屏障主要用于保护非原子的数据结构\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 03_memory_barrier.cpp -o 03_memory_barrier
