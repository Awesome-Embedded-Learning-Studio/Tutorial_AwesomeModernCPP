/*
 * 同步策略性能基准测试
 *
 * 本文件对应教程 vol5-ch08，对比多种同步策略在多线程计数器场景下的性能：
 *   1. std::mutex（重量级锁）
 *   2. SpinLock（基于 atomic_flag 的自旋锁，含 _mm_pause）
 *   3. std::atomic fetch_add（无锁原子加）
 *   4. std::atomic CAS 循环（无锁 compare-and-swap）
 *   5. False sharing 演示：对比 alignas(64) 前后的性能差异
 *
 * 每种策略：N 个线程各递增计数器 M 次，统计总耗时。
 * 使用 std::chrono::high_resolution_clock 计时，无外部依赖。
 *
 * 编译与运行：
 *   g++ -std=c++17 -pthread -O2 -Wall -Wextra \
 *       02_sync_strategy_benchmark.cpp -o 02_sync_strategy_benchmark
 *   ./02_sync_strategy_benchmark
 *
 * 注意事项：
 *   - 性能结果受 CPU 架构、核心数、调度策略影响，仅供参考。
 *   - 自旋锁在竞争不激烈时性能好，但会浪费 CPU 时间。
 *   - False sharing 的效果在高核心数机器上更明显。
 *
 * 编译器：GCC 10+ | Clang 12+
 * 平台：x86-64 Linux
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// 配置常量
// ============================================================================
static constexpr int kThreadCount = 8;                 // 工作线程数
static constexpr int kIterationsPerThread = 2'000'000; // 每线程递增次数
static constexpr int kWarmupRounds = 2;                // 预热轮次
static constexpr int kBenchmarkRounds = 5;             // 正式测试轮次

// ============================================================================
// SpinLock：基于 std::atomic_flag 的自旋锁
//
// 使用 __builtin_ia32_pause()（x86 PAUSE 指令）在自旋等待时降低功耗，
// 同时改善超线程场景下的性能。该内建函数在 GCC 和 Clang 上均可用。
// ============================================================================
class SpinLock {
  public:
    SpinLock() : flag_(ATOMIC_FLAG_INIT) {}

    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // x86 PAUSE 指令：提示 CPU 当前处于自旋等待循环，
            // 减少功耗并避免流水线惩罚
            __builtin_ia32_pause();
        }
    }

    void unlock() { flag_.clear(std::memory_order_release); }

  private:
    std::atomic_flag flag_;
};

// RAII 守卫，配合 SpinLock 使用
class SpinLockGuard {
  public:
    explicit SpinLockGuard(SpinLock& spin) : spin_(spin) { spin_.lock(); }
    ~SpinLockGuard() { spin_.unlock(); }
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

  private:
    SpinLock& spin_;
};

// ============================================================================
// 计时工具
// ============================================================================
using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

// ============================================================================
// 策略 1：std::mutex
// ============================================================================
long benchmark_mutex(int thread_count, int iterations) {
    long counter = 0;
    std::mutex mtx;

    auto worker = [&]() {
        for (int i = 0; i < iterations; ++i) {
            std::lock_guard<std::mutex> guard(mtx);
            ++counter;
        }
    };

    auto start = Clock::now();

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    auto end = Clock::now();
    return std::chrono::duration_cast<Microseconds>(end - start).count();
}

// ============================================================================
// 策略 2：SpinLock
// ============================================================================
long benchmark_spinlock(int thread_count, int iterations) {
    long counter = 0;
    SpinLock spin;

    auto worker = [&]() {
        for (int i = 0; i < iterations; ++i) {
            SpinLockGuard guard(spin);
            ++counter;
        }
    };

    auto start = Clock::now();

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    auto end = Clock::now();
    return std::chrono::duration_cast<Microseconds>(end - start).count();
}

// ============================================================================
// 策略 3：std::atomic fetch_add（无锁）
// ============================================================================
long benchmark_atomic_fetch_add(int thread_count, int iterations) {
    std::atomic<long> counter{0};

    auto worker = [&]() {
        for (int i = 0; i < iterations; ++i) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto start = Clock::now();

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    auto end = Clock::now();
    return std::chrono::duration_cast<Microseconds>(end - start).count();
}

// ============================================================================
// 策略 4：std::atomic CAS 循环（无锁 compare-and-swap）
// ============================================================================
long benchmark_atomic_cas(int thread_count, int iterations) {
    std::atomic<long> counter{0};

    auto worker = [&]() {
        for (int i = 0; i < iterations; ++i) {
            long old_val = counter.load(std::memory_order_relaxed);
            while (!counter.compare_exchange_weak(old_val, old_val + 1, std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
                // CAS 失败，old_val 已被自动更新为当前值，重试
                // 在 x86 上可以插入 pause 减少总线压力
                __builtin_ia32_pause();
            }
        }
    };

    auto start = Clock::now();

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    auto end = Clock::now();
    return std::chrono::duration_cast<Microseconds>(end - start).count();
}

// ============================================================================
// 辅助：运行一次基准测试（含预热），返回中位数耗时（微秒）
// ============================================================================
long run_benchmark(const std::string& name, long (*bench_fn)(int, int), int thread_count,
                   int iterations) {
    // 预热阶段：避免冷缓存偏差
    for (int w = 0; w < kWarmupRounds; ++w) {
        bench_fn(thread_count, iterations);
    }

    // 正式测试
    std::vector<long> durations;
    durations.reserve(kBenchmarkRounds);
    for (int r = 0; r < kBenchmarkRounds; ++r) {
        long us = bench_fn(thread_count, iterations);
        durations.push_back(us);
    }

    // 取中位数（比平均值更抗异常值干扰）
    std::sort(durations.begin(), durations.end());
    long median = durations[durations.size() / 2];

    return median;
}

// ============================================================================
// False sharing 演示
//
// 两个线程各自写不同的计数器，但如果计数器在同一缓存行上，
// CPU 的 MESI 协议会反复使缓存行失效（RFO），造成严重的性能损失。
// 使用 alignas(64) 让每个计数器独占一条缓存行即可消除此问题。
// ============================================================================
void demo_false_sharing() {
    std::cout << "\n=== False Sharing 演示 ===\n";
    std::cout << "  每线程 " << kIterationsPerThread << " 次递增，各策略 " << kBenchmarkRounds
              << " 轮取中位数\n\n";

    constexpr int kFalseSharingIters = 10'000'000;

    // --- 版本 A：false sharing（两个 int 在同一条缓存行） ---
    struct Counters {
        volatile int a;
        volatile int b;
    };

    long us_false_sharing = 0;
    {
        Counters counters{0, 0};

        // 预热
        for (int w = 0; w < kWarmupRounds; ++w) {
            counters.a = 0;
            counters.b = 0;
            std::thread t1([&]() {
                for (int i = 0; i < kFalseSharingIters; ++i) {
                    counters.a++;
                }
            });
            std::thread t2([&]() {
                for (int i = 0; i < kFalseSharingIters; ++i) {
                    counters.b++;
                }
            });
            t1.join();
            t2.join();
        }

        // 测量
        counters.a = 0;
        counters.b = 0;
        auto start = Clock::now();
        std::thread t1([&]() {
            for (int i = 0; i < kFalseSharingIters; ++i) {
                counters.a++;
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < kFalseSharingIters; ++i) {
                counters.b++;
            }
        });
        t1.join();
        t2.join();
        auto end = Clock::now();
        us_false_sharing = std::chrono::duration_cast<Microseconds>(end - start).count();
    }

    // --- 版本 B：alignas(64) 消除 false sharing ---
    constexpr std::size_t kCacheLineSize = 64;

    struct alignas(kCacheLineSize) AlignedCounter {
        volatile int value;
    };

    long us_aligned = 0;
    {
        AlignedCounter counter_a{0};
        AlignedCounter counter_b{0};

        // 预热
        for (int w = 0; w < kWarmupRounds; ++w) {
            counter_a.value = 0;
            counter_b.value = 0;
            std::thread t1([&]() {
                for (int i = 0; i < kFalseSharingIters; ++i) {
                    counter_a.value++;
                }
            });
            std::thread t2([&]() {
                for (int i = 0; i < kFalseSharingIters; ++i) {
                    counter_b.value++;
                }
            });
            t1.join();
            t2.join();
        }

        // 测量
        counter_a.value = 0;
        counter_b.value = 0;
        auto start = Clock::now();
        std::thread t1([&]() {
            for (int i = 0; i < kFalseSharingIters; ++i) {
                counter_a.value++;
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < kFalseSharingIters; ++i) {
                counter_b.value++;
            }
        });
        t1.join();
        t2.join();
        auto end = Clock::now();
        us_aligned = std::chrono::duration_cast<Microseconds>(end - start).count();
    }

    // --- 结果 ---
    double speedup = static_cast<double>(us_false_sharing) / static_cast<double>(us_aligned);

    std::cout << "  False sharing: " << us_false_sharing / 1000.0 << " ms\n";
    std::cout << "  Aligned:       " << us_aligned / 1000.0 << " ms\n";
    std::cout << "  加速比:        " << speedup << "x\n\n";
    std::cout << "  原因：两个 int 在同一条 64 字节缓存行上时，\n";
    std::cout << "        MESI 协议反复触发 RFO 使缓存行在核心间乒乓。\n";
    std::cout << "        alignas(64) 让每个计数器独占一条缓存行，消除乒乓。\n";
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::cout << "同步策略性能基准测试\n";
    std::cout << "====================\n";
    std::cout << "  线程数: " << kThreadCount << "\n";
    std::cout << "  每线程递增: " << kIterationsPerThread << "\n";
    std::cout << "  预热轮次: " << kWarmupRounds << "，测试轮次: " << kBenchmarkRounds << "\n";
    std::cout << "  结果取中位数（微秒）\n\n";

    // --- 运行各策略基准测试 ---
    struct BenchmarkEntry {
        std::string name;
        long (*fn)(int, int);
        long median_us;
    };

    std::vector<BenchmarkEntry> benchmarks = {
        {"std::mutex", benchmark_mutex, 0},
        {"SpinLock", benchmark_spinlock, 0},
        {"atomic fetch_add", benchmark_atomic_fetch_add, 0},
        {"atomic CAS loop", benchmark_atomic_cas, 0},
    };

    for (auto& entry : benchmarks) {
        entry.median_us = run_benchmark(entry.name, entry.fn, kThreadCount, kIterationsPerThread);
    }

    // --- 打印结果表格 ---
    int name_w = 22;
    std::cout << "  +" << std::string(name_w + 2, '-') << "+-------------+\n";
    std::cout << "  | " << std::left << std::setw(name_w) << "策略"
              << " | 耗时 (ms)   |\n";
    std::cout << "  +" << std::string(name_w + 2, '-') << "+-------------+\n";

    long baseline_us = 0;
    for (const auto& entry : benchmarks) {
        if (entry.name == "std::mutex") {
            baseline_us = entry.median_us;
        }
    }

    for (const auto& entry : benchmarks) {
        double ms = entry.median_us / 1000.0;
        double ratio = static_cast<double>(baseline_us) / static_cast<double>(entry.median_us);
        std::cout << "  | " << std::left << std::setw(name_w) << entry.name << " | " << std::right
                  << std::fixed << std::setprecision(2) << std::setw(8) << ms << " ms"
                  << " |\n";
    }
    std::cout << "  +" << std::string(name_w + 2, '-') << "+-------------+\n";

    // --- 相对性能 ---
    std::cout << "\n  相对性能（以 std::mutex = 1.0x 为基准）:\n";
    for (const auto& entry : benchmarks) {
        double ratio = static_cast<double>(baseline_us) / static_cast<double>(entry.median_us);
        std::cout << "    " << std::left << std::setw(name_w) << entry.name << ": " << std::fixed
                  << std::setprecision(2) << ratio << "x\n";
    }

    // --- False sharing 演示 ---
    demo_false_sharing();

    // --- 结论 ---
    std::cout << "--- 结论 ---\n";
    std::cout << "  1. std::mutex：通用、安全，但系统调用开销较大。\n";
    std::cout << "  2. SpinLock：临界区极短时快，但浪费 CPU 且不公平。\n";
    std::cout << "  3. atomic fetch_add：最快，适用于简单计数器场景。\n";
    std::cout << "  4. atomic CAS 循环：灵活，可实现任意原子更新逻辑。\n";
    std::cout << "  5. false sharing：高竞争场景下，alignas(64) 可显著提速。\n";

    return 0;
}
