/*
 * C++20 同步原语：latch、barrier、semaphore
 *
 * 本文件对应教程 vol5-ch02，演示 C++20 引入的全新同步原语：
 *   1. std::latch —— 一次性同步屏障
 *   2. std::barrier —— 多阶段同步
 *   3. std::barrier 带完成函数（并行归约求和）
 *   4. std::counting_semaphore —— 连接池模拟
 *   5. std::binary_semaphore —— 简单信号传递
 *   6. 用 mutex + condition_variable 模拟 latch（对比）
 *   7. 用 mutex + condition_variable 模拟 barrier（对比）
 *
 * 编译命令（需要 C++20）：
 *   g++ -std=c++20 -pthread -O2 -Wall -Wextra \
 *       05_sync_primitives.cpp -o 05_sync_primitives
 *
 * 运行：
 *   ./05_sync_primitives
 */

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <latch>
#include <mutex>
#include <semaphore>
#include <string>
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
// 1. std::latch —— 一次性同步屏障
//
// latch 是一次性的倒计时器：初始化一个计数，每个线程 arrive_and_wait
// 会让计数减一，当计数减到零时，所有等待的线程被释放。
// 常见用途：等待 N 个线程全部完成初始化后继续执行。
// ============================================================================
void demo_latch() {
    safe_print("\n=== 1. std::latch（一次性同步屏障）===");

    constexpr int kWorkerCount = 5;
    std::latch init_done(kWorkerCount);

    std::vector<std::thread> workers;

    for (int i = 0; i < kWorkerCount; ++i) {
        workers.emplace_back([&, i]() {
            safe_print("  [Worker ", i, "] 开始初始化...");
            std::this_thread::sleep_for(std::chrono::milliseconds(50 * (i + 1)));
            safe_print("  [Worker ", i, "] 初始化完成");

            // 通知 latch：我这部分完成了
            init_done.count_down();
        });
    }

    // 主线程等待所有 worker 初始化完成
    safe_print("  [主线程] 等待所有 worker 初始化...");
    init_done.wait();
    safe_print("  [主线程] 所有 worker 已初始化！开始执行主逻辑。");

    for (auto& th : workers) {
        th.join();
    }

    safe_print("  latch 是一次性的，到达零之后不能重置。");
    safe_print("  典型场景：线程池初始化、多阶段任务的一次性同步点。");
}

// ============================================================================
// 2. std::barrier —— 多阶段同步
//
// barrier 可以被反复使用：每当指定数量的线程到达后，所有线程同时释放，
// barrier 自动重置，准备下一阶段。
// ============================================================================
void demo_barrier() {
    safe_print("\n=== 2. std::barrier（多阶段同步）===");

    constexpr int kWorkerCount = 4;
    constexpr int kPhases = 3;

    // 创建 barrier，需要 kWorkerCount 个线程到达后释放
    std::barrier sync_point(kWorkerCount);

    std::vector<std::thread> workers;

    for (int i = 0; i < kWorkerCount; ++i) {
        workers.emplace_back([&, i]() {
            for (int phase = 1; phase <= kPhases; ++phase) {
                safe_print("  [Worker ", i, "] 阶段 ", phase, "：工作中...");
                std::this_thread::sleep_for(std::chrono::milliseconds(20 * (i + 1)));

                safe_print("  [Worker ", i, "] 阶段 ", phase, "：完成，等待同步...");
                sync_point.arrive_and_wait();
                // 所有 worker 都到达后，才能进入下一阶段
                safe_print("  [Worker ", i, "] 阶段 ", phase, "：同步通过！");
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    safe_print("  barrier 自动重置，适合多阶段流水线处理。");
}

// ============================================================================
// 3. std::barrier 带完成函数 —— 并行归约求和
//
// barrier 的构造函数可以接受一个完成函数（completion function），
// 当所有线程到达时，由其中一个线程执行该函数。
// 常见用途：并行归约，在每轮结束时合并部分结果。
// ============================================================================
void demo_barrier_with_completion() {
    safe_print("\n=== 3. std::barrier 带完成函数（并行归约）===");

    constexpr int kWorkerCount = 4;
    constexpr int kDataSize = 1000;

    // 准备数据
    std::vector<int> data(kDataSize);
    for (int i = 0; i < kDataSize; ++i) {
        data[i] = i + 1; // 1 + 2 + ... + 1000 = 500500
    }

    // 每个 worker 的部分和
    std::vector<long long> partial_sums(kWorkerCount, 0);
    long long total_sum = 0;

    // 完成函数：在所有线程到达时，由其中一个线程执行
    // 这里用来合并部分和
    int phase_counter = 0;
    auto on_completion = [&]() noexcept {
        ++phase_counter;
        long long phase_sum = 0;
        for (int i = 0; i < kWorkerCount; ++i) {
            phase_sum += partial_sums[i];
        }
        safe_print("  [完成函数] 阶段 ", phase_counter, "：本轮部分和合计 = ", phase_sum);

        // 最终阶段：保存总和
        if (phase_counter == 1) {
            total_sum = phase_sum;
        }
    };

    std::barrier sync_point(kWorkerCount, on_completion);

    int chunk_size = kDataSize / kWorkerCount;

    std::vector<std::thread> workers;
    for (int i = 0; i < kWorkerCount; ++i) {
        workers.emplace_back([&, i]() {
            int start = i * chunk_size;
            int end = (i == kWorkerCount - 1) ? kDataSize : start + chunk_size;

            // 计算 1 + 2 + ... + N 的公式
            long long sum = 0;
            for (int j = start; j < end; ++j) {
                sum += data[j];
            }
            partial_sums[i] = sum;

            safe_print("  [Worker ", i, "] 部分和 = ", sum, "（范围 [", start, ", ", end, ")）");

            sync_point.arrive_and_wait();
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    safe_print("  最终总和 = ", total_sum, "（期望 500500）");
    safe_print("  barrier 的完成函数适合在同步点执行合并操作。");
}

// ============================================================================
// 4. std::counting_semaphore —— 连接池模拟
//
// counting_semaphore<Max> 维护一个非负计数器：
//   acquire()：计数器减一，如果为零则阻塞
//   release()：计数器加一，唤醒等待的线程
// 常见用途：限制并发访问数量（连接池、资源池等）。
// ============================================================================
void demo_counting_semaphore() {
    safe_print("\n=== 4. std::counting_semaphore（连接池）===");

    // 模拟最多 3 个并发连接
    constexpr int kMaxConnections = 3;
    std::counting_semaphore<kMaxConnections> connection_pool(kMaxConnections);

    std::atomic<int> active_connections{0};
    std::atomic<int> peak_connections{0};

    auto handle_request = [&](int request_id) {
        safe_print("  [请求 ", request_id, "] 等待连接...");
        connection_pool.acquire();

        int current = ++active_connections;
        int peak = peak_connections.load();
        while (current > peak) {
            if (peak_connections.compare_exchange_weak(peak, current)) {
                break;
            }
        }

        safe_print("  [请求 ", request_id, "] 获得连接，当前活跃 = ", current);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        safe_print("  [请求 ", request_id, "] 释放连接");
        --active_connections;
        connection_pool.release();
    };

    // 模拟 8 个请求竞争 3 个连接
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(handle_request, i);
    }

    for (auto& th : threads) {
        th.join();
    }

    safe_print("  峰值活跃连接数：", peak_connections.load(), "（上限 ", kMaxConnections, "）");
    safe_print("  counting_semaphore 适合限制并发资源使用。");
}

// ============================================================================
// 5. std::binary_semaphore —— 简单信号传递
//
// binary_semaphore 是 counting_semaphore<1> 的特化，
// 只有 0 和 1 两个状态，适合简单的"信号"语义：
// 一个线程通知，另一个线程等待。
// ============================================================================
void demo_binary_semaphore() {
    safe_print("\n=== 5. std::binary_semaphore（简单信号）===");

    // 初始计数为 0，消费者必须等待
    std::binary_semaphore signal_ready(0);
    std::binary_semaphore signal_done(0);

    int shared_data = 0;

    std::thread producer([&]() {
        // 生产数据
        shared_data = 42;
        safe_print("  [Producer] 数据已准备好：", shared_data);
        signal_ready.release(); // 发送"数据就绪"信号

        // 等待消费者确认
        signal_done.acquire();
        safe_print("  [Producer] 收到消费者的完成确认");
    });

    std::thread consumer([&]() {
        // 等待数据就绪
        signal_ready.acquire();
        safe_print("  [Consumer] 收到数据：", shared_data);

        // 发送完成确认
        signal_done.release();
    });

    producer.join();
    consumer.join();

    safe_print("  binary_semaphore 是最简单的线程间信号传递机制。");
    safe_print("  比 condition_variable 更轻量，不需要 mutex 配合。");
}

// ============================================================================
// 6. 用 mutex + condition_variable 模拟 latch（对比学习）
// ============================================================================
class ManualLatch {
  public:
    explicit ManualLatch(std::ptrdiff_t count) : count_(static_cast<int>(count)) {}

    void count_down() {
        std::lock_guard<std::mutex> lock(mutex_);
        --count_;
        if (count_ <= 0) {
            cv_.notify_all();
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return count_ <= 0; });
    }

    void arrive_and_wait() {
        count_down();
        wait();
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
};

void demo_manual_latch() {
    safe_print("\n=== 6. 手动实现 latch（mutex + CV）===");

    constexpr int kWorkerCount = 4;
    ManualLatch init_done(kWorkerCount);

    std::vector<std::thread> workers;
    for (int i = 0; i < kWorkerCount; ++i) {
        workers.emplace_back([&, i]() {
            safe_print("  [Worker ", i, "] 初始化中...");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            safe_print("  [Worker ", i, "] 初始化完成");
            init_done.arrive_and_wait();
            safe_print("  [Worker ", i, "] 全部就绪，开始工作");
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    safe_print("  手动 latch 实现：", kWorkerCount, " 个线程全部通过同步点。");
    safe_print("  对比：std::latch 不需要 mutex 和 CV，代码更简洁高效。");
}

// ============================================================================
// 7. 用 mutex + condition_variable 模拟 barrier（对比学习）
// ============================================================================
class ManualBarrier {
  public:
    explicit ManualBarrier(std::ptrdiff_t count)
        : initial_count_(static_cast<int>(count)), current_count_(static_cast<int>(count)),
          generation_(0) {}

    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        int my_generation = generation_;
        --current_count_;

        if (current_count_ == 0) {
            // 最后一个到达的线程重置 barrier
            current_count_ = initial_count_;
            ++generation_;
            cv_.notify_all();
        } else {
            // 等待同代的所有线程到达
            cv_.wait(lock, [this, my_generation]() { return generation_ != my_generation; });
        }
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int initial_count_;
    int current_count_;
    int generation_; // 用于防止虚假唤醒
};

void demo_manual_barrier() {
    safe_print("\n=== 7. 手动实现 barrier（mutex + CV）===");

    constexpr int kWorkerCount = 4;
    constexpr int kPhases = 2;
    ManualBarrier sync_point(kWorkerCount);

    std::vector<std::thread> workers;
    for (int i = 0; i < kWorkerCount; ++i) {
        workers.emplace_back([&, i]() {
            for (int phase = 1; phase <= kPhases; ++phase) {
                safe_print("  [Worker ", i, "] 阶段 ", phase, "：处理中...");
                std::this_thread::sleep_for(std::chrono::milliseconds(30 * (i + 1)));
                sync_point.arrive_and_wait();
                safe_print("  [Worker ", i, "] 阶段 ", phase, "：通过同步点");
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    safe_print("  手动 barrier 实现支持多阶段同步。");
    safe_print("  关键点：generation_ 计数器防止虚假唤醒和跨阶段干扰。");
    safe_print("  std::barrier 是 C++20 的标准替代，推荐优先使用。");
}

// ============================================================================
// main
// ============================================================================
int main() {
    safe_print("C++20 同步原语：latch、barrier、semaphore");

    demo_latch();
    demo_barrier();
    demo_barrier_with_completion();
    demo_counting_semaphore();
    demo_binary_semaphore();
    demo_manual_latch();
    demo_manual_barrier();

    safe_print("\n全部演示完成。");
    return 0;
}
