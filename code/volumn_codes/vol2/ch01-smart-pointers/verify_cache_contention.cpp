/**
 * @file verify_cache_contention.cpp
 * @brief 验证 shared_ptr 在多线程生产者-消费者场景下的缓存行争用开销
 * @date 2026-04-24
 *
 * 编译环境: g++ (GCC) 15.2.0 on x86_64-linux
 * 编译命令: g++ -std=c++17 -O2 -pthread -o verify_cache_contention verify_cache_contention.cpp
 *
 * 验证内容:
 * 1. 裸指针 vs shared_ptr 在队列场景下的性能差异
 * 2. 缓存行争用 (cache line bouncing) 的实际影响
 * 3. 原子操作在高并发场景下的开销
 *
 * 注意: 此基准测试简化了原始测试，使用单生产者单消费者模型
 */

#include <memory>
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

/**
 * @brief 线程安全队列 (使用 mutex 保护)
 *
 * 这个队列的 mutex 开销会掩盖 shared_ptr 的部分开销，
 * 但在高吞吐量场景下仍能观察到差异
 */
template <typename T>
class ThreadSafeQueue {
public:
    void push(T msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(msg));
        cv_.notify_one();
    }

    bool pop(T& msg) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        msg = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool wait_pop(T& msg) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        msg = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

/// 测试消息 - 64 字节 (一个缓存行)
struct Message {
    int data[16];  // 64 bytes on x86_64
};

/**
 * @brief 基准测试: 使用裸指针的消息传递
 *
 * @param num_messages 消息数量
 * @return 耗时 (毫秒)
 */
int benchmark_with_raw_pointers(int num_messages) {
    ThreadSafeQueue<Message*> queue;
    std::atomic<bool> producer_done{false};
    std::atomic<int> consumed_count{0};

    auto start = std::chrono::high_resolution_clock::now();

    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < num_messages; ++i) {
            auto msg = new Message{};
            queue.push(msg);
        }
        producer_done = true;
    });

    // 消费者线程
    std::thread consumer([&]() {
        while (!producer_done || consumed_count < num_messages) {
            Message* msg;
            if (queue.pop(msg)) {
                ++consumed_count;
                delete msg;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

/**
 * @brief 基准测试: 使用 shared_ptr 的消息传递
 *
 * @param num_messages 消息数量
 * @return 耗时 (毫秒)
 */
int benchmark_with_shared_ptr(int num_messages) {
    ThreadSafeQueue<std::shared_ptr<Message>> queue;
    std::atomic<bool> producer_done{false};
    std::atomic<int> consumed_count{0};

    auto start = std::chrono::high_resolution_clock::now();

    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < num_messages; ++i) {
            auto msg = std::make_shared<Message>();
            queue.push(msg);
        }
        producer_done = true;
    });

    // 消费者线程
    std::thread consumer([&]() {
        while (!producer_done || consumed_count < num_messages) {
            std::shared_ptr<Message> msg;
            if (queue.pop(msg)) {
                ++consumed_count;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

/**
 * @brief 运行基准测试
 *
 * @param test_name 测试名称
 * @param num_messages 消息数量
 * @param num_rounds 测试轮数
 */
void run_benchmark(const std::string& test_name, int num_messages, int num_rounds) {
    std::cout << "\n=== " << test_name << " ===\n";
    std::cout << "消息数量: " << num_messages << "\n";
    std::cout << "测试轮数: " << num_rounds << "\n\n";

    int total_raw_time = 0;
    int total_shared_time = 0;

    for (int round = 0; round < num_rounds; ++round) {
        int raw_time = benchmark_with_raw_pointers(num_messages);
        int shared_time = benchmark_with_shared_ptr(num_messages);

        total_raw_time += raw_time;
        total_shared_time += shared_time;

        std::cout << "第 " << (round + 1) << " 轮: "
                  << "裸指针 " << raw_time << " ms, "
                  << "shared_ptr " << shared_time << " ms, "
                  << "开销 +" << ((shared_time - raw_time) * 100 / raw_time) << "%\n";
    }

    int avg_raw_time = total_raw_time / num_rounds;
    int avg_shared_time = total_shared_time / num_rounds;
    double overhead_ratio = (static_cast<double>(avg_shared_time) / avg_raw_time - 1.0) * 100.0;

    std::cout << "\n平均结果:\n";
    std::cout << "  裸指针: " << avg_raw_time << " ms\n";
    std::cout << "  shared_ptr: " << avg_shared_time << " ms\n";
    std::cout << "  开销: +" << std::fixed << std::setprecision(1) << overhead_ratio << "%\n";
}

int main() {
    std::cout << "shared_ptr 缓存行争用开销验证\n";
    std::cout << "编译时间: " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "编译器: g++ " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
    std::cout << "优化级别: -O2\n";
    std::cout << "消息大小: " << sizeof(Message) << " bytes\n";

    // 小规模快速测试
    run_benchmark("小规模测试 (10,000 条消息)", 10000, 3);

    // 中等规模测试
    run_benchmark("中等规模测试 (100,000 条消息)", 100000, 3);

    std::cout << "\n注意事项:\n";
    std::cout << "1. 此测试使用 mutex 保护的队列，mutex 开销会掩盖部分 shared_ptr 开销\n";
    std::cout << "2. 在无锁队列或更高并发场景下，shared_ptr 的开销会更加显著\n";
    std::cout << "3. 实际开销取决于硬件架构、线程数、消息大小等因素\n";

    std::cout << "\n所有验证完成!\n";
    return 0;
}

/*
 * 预期输出 (x86_64-linux, GCC 15.2, -O2):
 *
 * shared_ptr 缓存行争用开销验证
 * 编译时间: Apr 24 2026
 * 编译器: g++ 15.2.0
 * 优化级别: -O2
 * 消息大小: 64 bytes
 *
 * === 小规模测试 (10,000 条消息) ===
 * 消息数量: 10000
 * 测试轮数: 3
 *
 * 第 1 轮: 裸指针 X ms, shared_ptr Y ms, 开销 +Z%
 * 第 2 轮: ...
 * 第 3 轮: ...
 *
 * 平均结果:
 *   裸指针: X ms
 *   shared_ptr: Y ms
 *   开销: +Z%
 *
 * === 中等规模测试 (100,000 条消息) ===
 * ...
 *
 * 注意事项:
 * ...
 *
 * 所有验证完成!
 *
 * 注: 实际数值因硬件和系统负载而异
 */
