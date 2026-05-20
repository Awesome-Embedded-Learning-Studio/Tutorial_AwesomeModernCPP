/*
 * shared_mutex（读写锁）与读多写少优化
 *
 * 本文件对应教程 vol5-ch02，演示 C++17 的 std::shared_mutex：
 *   1. ThreadSafeConfig —— 多读单写的配置管理
 *   2. ThreadSafeCache<Key, Value> —— get_or_compute + 双重检查锁定
 *   3. shared_mutex vs mutex 的性能对比（读多写少场景）
 *   4. 锁"升级"模式：unlock shared -> lock unique
 *
 * 编译命令（需要 C++17）：
 *   g++ -std=c++17 -pthread -O2 -Wall -Wextra \
 *       04_shared_mutex.cpp -o 04_shared_mutex
 *
 * 运行：
 *   ./04_shared_mutex
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
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
// 1. ThreadSafeConfig —— 多读单写的配置管理器
// ============================================================================
class ThreadSafeConfig {
  public:
    void set(const std::string& key, const std::string& value) {
        // 写操作：unique_lock（独占锁）
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        config_[key] = value;
        safe_print("  [Config] 设置 ", key, " = ", value);
    }

    std::string get(const std::string& key) const {
        // 读操作：shared_lock（共享锁，允许多线程同时读）
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        auto it = config_.find(key);
        if (it != config_.end()) {
            return it->second;
        }
        return "";
    }

    std::string get_or_default(const std::string& key, const std::string& default_value) const {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : default_value;
    }

    void for_each(std::function<void(const std::string&, const std::string&)> fn) const {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        for (const auto& [key, value] : config_) {
            fn(key, value);
        }
    }

  private:
    mutable std::shared_mutex rw_mutex_;
    std::map<std::string, std::string> config_;
};

void demo_thread_safe_config() {
    safe_print("\n=== 1. ThreadSafeConfig（多读单写）===");

    ThreadSafeConfig config;

    // 多线程同时写入
    std::vector<std::thread> writers;
    writers.emplace_back([&]() { config.set("host", "localhost"); });
    writers.emplace_back([&]() { config.set("port", "8080"); });
    writers.emplace_back([&]() { config.set("timeout", "30s"); });
    writers.emplace_back([&]() { config.set("retries", "3"); });

    for (auto& th : writers) {
        th.join();
    }

    // 多线程同时读取（shared_lock 允许并发读）
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&, i]() {
            safe_print("  [Reader ", i, "] host = ", config.get("host"));
            safe_print("  [Reader ", i, "] port = ", config.get_or_default("port", "unknown"));
        });
    }

    for (auto& th : readers) {
        th.join();
    }

    safe_print("  配置遍历：");
    config.for_each([](const std::string& key, const std::string& value) {
        safe_print("    ", key, " = ", value);
    });
}

// ============================================================================
// 2. ThreadSafeCache<Key, Value> —— get_or_compute + 双重检查锁定
// ============================================================================
template <typename Key, typename Value> class ThreadSafeCache {
  public:
    // 获取缓存值，如果不存在则计算并缓存
    Value get_or_compute(const Key& key, std::function<Value(const Key&)> compute_fn) {
        // 第一层：共享锁读取
        {
            std::shared_lock<std::shared_mutex> read_lock(rw_mutex_);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        // 第二层：独占锁写入（双重检查）
        {
            std::unique_lock<std::shared_mutex> write_lock(rw_mutex_);
            // 再次检查：可能其他线程在我们释放读锁到获取写锁之间已经写入了
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second; // 另一个线程已经计算过了
            }

            // 计算并缓存
            Value value = compute_fn(key);
            cache_[key] = value;
            return value;
        }
    }

    bool contains(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        return cache_.find(key) != cache_.end();
    }

    void invalidate(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        cache_.erase(key);
    }

    std::size_t size() const {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        return cache_.size();
    }

  private:
    mutable std::shared_mutex rw_mutex_;
    std::unordered_map<Key, Value> cache_;
};

void demo_thread_safe_cache() {
    safe_print("\n=== 2. ThreadSafeCache（get_or_compute）===");

    ThreadSafeCache<int, std::string> cache;
    std::atomic<int> compute_count{0};

    // 模拟耗时计算
    auto compute_fn = [&](const int& key) -> std::string {
        ++compute_count;
        safe_print("  [Cache] 计算中... key = ", key);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return "value_" + std::to_string(key * key);
    };

    // 多线程并发获取同一个 key
    // 双重检查确保只计算一次
    constexpr int kThreadCount = 8;
    constexpr int kKey = 42;
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i]() {
            std::string value = cache.get_or_compute(kKey, compute_fn);
            safe_print("  [Thread ", i, "] key=", kKey, " -> ", value);
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    safe_print("  缓存大小：", cache.size());
    safe_print("  实际计算次数：", compute_count.load(), "（期望 1，双重检查生效）");

    // 再获取其他 key
    for (int i = 0; i < 5; ++i) {
        cache.get_or_compute(i, compute_fn);
    }
    safe_print("  最终缓存大小：", cache.size());
    safe_print("  总计算次数：", compute_count.load());
}

// ============================================================================
// 3. 性能对比：shared_mutex vs mutex（读多写少场景）
// ============================================================================
void demo_benchmark_shared_vs_exclusive() {
    safe_print("\n=== 3. 性能对比：shared_mutex vs mutex ===");

    constexpr int kReaders = 8;
    constexpr int kWriters = 2;
    constexpr int kReadOpsPerThread = 500'000;
    constexpr int kWriteOpsPerThread = 10'000;

    // --- 使用普通 mutex ---
    {
        std::mutex exclusive_mtx;
        int data = 0;
        std::atomic<long long> read_sum{0};

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;

        // 读者
        for (int i = 0; i < kReaders; ++i) {
            threads.emplace_back([&]() {
                long long local_sum = 0;
                for (int j = 0; j < kReadOpsPerThread; ++j) {
                    std::lock_guard<std::mutex> lock(exclusive_mtx);
                    local_sum += data;
                }
                read_sum += local_sum;
            });
        }

        // 写者
        for (int i = 0; i < kWriters; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < kWriteOpsPerThread; ++j) {
                    std::lock_guard<std::mutex> lock(exclusive_mtx);
                    ++data;
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        safe_print("  std::mutex（独占锁）：     ", ms.count(), " ms");
    }

    // --- 使用 shared_mutex ---
    {
        std::shared_mutex shared_mtx;
        int data = 0;
        std::atomic<long long> read_sum{0};

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;

        // 读者：使用 shared_lock
        for (int i = 0; i < kReaders; ++i) {
            threads.emplace_back([&]() {
                long long local_sum = 0;
                for (int j = 0; j < kReadOpsPerThread; ++j) {
                    std::shared_lock<std::shared_mutex> lock(shared_mtx);
                    local_sum += data;
                }
                read_sum += local_sum;
            });
        }

        // 写者：使用 unique_lock
        for (int i = 0; i < kWriters; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < kWriteOpsPerThread; ++j) {
                    std::unique_lock<std::shared_mutex> lock(shared_mtx);
                    ++data;
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        safe_print("  std::shared_mutex（读写锁）：", ms.count(), " ms");
    }

    safe_print("  读多写少场景下，shared_mutex 通常有明显的性能优势。");
    safe_print("  因为多个读者可以同时持有 shared_lock，不会互相阻塞。");
}

// ============================================================================
// 4. 锁升级模式：unlock shared -> lock unique
//
// C++17 的 shared_mutex 不支持原子性的锁升级（shared -> unique），
// 必须先释放 shared_lock，再获取 unique_lock。这期间可能有其他线程修改数据。
// 因此需要双重检查来确保数据一致性。
// ============================================================================
void demo_lock_upgrade() {
    safe_print("\n=== 4. 锁升级模式（shared -> unique）===");

    std::shared_mutex rw_mutex;
    std::map<std::string, int> counters;

    // 初始化
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex);
        counters["alpha"] = 0;
        counters["beta"] = 0;
    }

    auto increment_or_insert = [&](const std::string& key) {
        // 第一步：shared_lock 检查是否存在
        {
            std::shared_lock<std::shared_mutex> read_lock(rw_mutex);
            auto it = counters.find(key);
            if (it != counters.end()) {
                // key 存在，需要升级到写锁来递增
                // 注意：必须先释放 shared_lock 再获取 unique_lock
            } else {
                // key 不存在，释放读锁后用写锁插入
                read_lock.unlock();

                std::unique_lock<std::shared_mutex> write_lock(rw_mutex);
                // 双重检查：其他线程可能已经插入了
                counters.try_emplace(key, 1);
                return;
            }
        }
        // shared_lock 已释放

        // 第二步：unique_lock 执行修改
        {
            std::unique_lock<std::shared_mutex> write_lock(rw_mutex);
            // 双重检查：数据可能在释放读锁到获取写锁之间被修改
            counters[key] += 1;
        }
    };

    // 多线程并发更新
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 1000; ++j) {
                if (i % 2 == 0) {
                    increment_or_insert("alpha");
                } else {
                    increment_or_insert("beta");
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // 验证结果
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex);
        for (const auto& [key, count] : counters) {
            safe_print("  ", key, " = ", count);
        }
    }

    safe_print("  锁升级不是原子操作，必须配合双重检查。");
    safe_print("  如果锁升级很频繁，考虑直接使用 unique_lock。");
}

// ============================================================================
// main
// ============================================================================
int main() {
    safe_print("shared_mutex（读写锁）与读多写少优化");

    demo_thread_safe_config();
    demo_thread_safe_cache();
    demo_benchmark_shared_vs_exclusive();
    demo_lock_upgrade();

    safe_print("\n全部演示完成。");
    return 0;
}
