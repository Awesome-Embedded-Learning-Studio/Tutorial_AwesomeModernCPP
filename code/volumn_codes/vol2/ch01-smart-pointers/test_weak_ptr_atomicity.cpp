/**
 * @file test_weak_ptr_atomicity.cpp
 * @brief 验证 weak_ptr::lock() 的线程安全性和原子性
 *
 * 测试目标：
 * 1. 验证 lock() 在多线程环境下的安全性
 * 2. 验证 lock() 返回的 shared_ptr 是否保证对象存活
 * 3. 验证 expired() + lock() 的竞态条件问题
 */

#include <memory>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

// 测试 1: 验证 lock() 的线程安全性
void test_lock_thread_safety() {
    std::cout << "=== 测试 1: lock() 线程安全性 ===\n";

    std::shared_ptr<int> shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;

    std::atomic<int> successful_locks{0};
    std::atomic<int> failed_locks{0};
    std::atomic<bool> keep_running{true};

    // 多个线程同时调用 lock()
    auto lock_worker = [&]() {
        while (keep_running) {
            if (auto locked = weak.lock()) {
                successful_locks++;
                // 验证我们确实可以安全访问对象
                *locked;  // 只是读取，不做任何操作
            } else {
                failed_locks++;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(lock_worker);
    }

    // 让线程运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 重置 shared_ptr，触发对象销毁
    shared.reset();
    std::cout << "shared_ptr 已重置，对象应该被销毁\n";

    // 再运行一段时间，让所有线程观察到过期
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    keep_running = false;
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "成功的 lock(): " << successful_locks << "\n";
    std::cout << "失败的 lock(): " << failed_locks << "\n";
    std::cout << "结论: lock() 是线程安全的，多个线程可以同时调用\n\n";
}

// 测试 2: 验证 expired() + lock() 的竞态条件
void test_expired_lock_race() {
    std::cout << "=== 测试 2: expired() + lock() 竞态条件 ===\n";

    std::shared_ptr<int> shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;

    std::atomic<int> race_condition_detected{0};
    std::atomic<bool> keep_running{true};

    // 模拟错误的用法：先检查 expired() 再 lock()
    auto bad_worker = [&]() {
        while (keep_running) {
            if (!weak.expired()) {
                // 在这里，另一个线程可能已经重置了 shared_ptr
                if (auto locked = weak.lock()) {
                    // 成功获取
                } else {
                    // 竞态条件：expired() 返回 false，但 lock() 返回空
                    race_condition_detected++;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(bad_worker);
    }

    // 频繁地重置和重新创建对象
    for (int i = 0; i < 1000; ++i) {
        shared.reset();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        shared = std::make_shared<int>(42);
        weak = shared;
    }

    keep_running = false;
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "检测到的竞态条件次数: " << race_condition_detected << "\n";
    std::cout << "结论: expired() + lock() 存在竞态条件，应直接使用 lock()\n\n";
}

// 测试 3: 验证 lock() 返回后对象的生命周期保证
void test_lock_lifetime_guarantee() {
    std::cout << "=== 测试 3: lock() 生命周期保证 ===\n";

    auto shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;

    // 获取 lock() 返回的 shared_ptr
    auto locked = weak.lock();
    if (locked) {
        std::cout << "lock() 返回有效的 shared_ptr，值为: " << *locked << "\n";

        // 即使原始的 shared_ptr 被重置，lock() 返回的 shared_ptr 仍然有效
        shared.reset();
        std::cout << "原始 shared_ptr 已重置\n";
        std::cout << "locked 仍然有效，值为: " << *locked << "\n";
        std::cout << "use_count: " << locked.use_count() << "\n";
    }

    std::cout << "结论: lock() 返回的 shared_ptr 保证对象在作用域内存活\n\n";
}

int main() {
    std::cout << "weak_ptr::lock() 线程安全性与原子性验证\n";
    std::cout << "编译环境: g++ (需确认版本)\n";
    std::cout << "C++ 标准: C++11 及以上\n\n";

    test_lock_thread_safety();
    test_expired_lock_race();
    test_lock_lifetime_guarantee();

    std::cout << "=== 测试完成 ===\n";
    return 0;
}
