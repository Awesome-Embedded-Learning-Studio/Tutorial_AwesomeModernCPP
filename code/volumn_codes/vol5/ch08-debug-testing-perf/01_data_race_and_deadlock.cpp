/*
 * 数据竞争与死锁：诊断与修复
 *
 * 本文件对应教程 vol5-ch08，演示两类最常见的并发缺陷：
 *   1. 数据竞争（data race）—— 多线程无同步访问共享变量
 *   2. 死锁（deadlock）—— AB-BA 经典锁反序问题
 *
 * 每个问题都展示了"有缺陷版本"和"修复版本"，并附带了
 * ThreadSanitizer (TSan) 和 Helgrind 的使用说明。
 *
 * 编译与运行（普通模式）：
 *   g++ -std=c++17 -pthread -O2 -Wall -Wextra \
 *       01_data_race_and_deadlock.cpp -o 01_data_race_and_deadlock
 *   ./01_data_race_and_deadlock
 *
 * 使用 ThreadSanitizer 检测数据竞争：
 *   g++  -fsanitize=thread -g -O1 -std=c++17 -pthread \
 *        01_data_race_and_deadlock.cpp -o tsan_demo
 *   clang++ -fsanitize=thread -g -O1 -std=c++17 -pthread \
 *           01_data_race_and_deadlock.cpp -o tsan_demo
 *   ./tsan_demo
 *
 * 使用 Helgrind 检测（Valgrind 工具之一）：
 *   g++ -std=c++17 -pthread -g -O1 \
 *       01_data_race_and_deadlock.cpp -o helgrind_demo
 *   valgrind --tool=helgrind ./helgrind_demo
 *
 * 编译器：GCC 10+ | Clang 12+
 * 平台：x86-64 Linux
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// ============================================================================
// 辅助工具
// ============================================================================
static constexpr int kThreadCount = 8;
static constexpr int kIterationsPerThread = 1'000'000;

// ============================================================================
// 第一部分：数据竞争
//
// 多个线程同时对同一个非原子变量执行写操作，构成"数据竞争"（data race），
// 这是 C++ 标准中的未定义行为（UB）。结果不可预测，通常表现为计数丢失。
//
// TSan 能精确报告此类问题，包括：
//   - 竞争发生的源码位置
//   - 涉及的线程
//   - 读写操作的调用栈
// ============================================================================

// --- 1a. 有缺陷版本：裸计数器，无任何同步 ---
void demo_data_race_buggy() {
    std::cout << "\n=== 1a. 数据竞争（有缺陷版本）===\n";

    int counter = 0; // 裸变量，多线程写 = 数据竞争！

    auto worker = [&counter]() {
        for (int i = 0; i < kIterationsPerThread; ++i) {
            ++counter; // UB：非原子写，TSan 会报告
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    long expected = static_cast<long>(kThreadCount) * kIterationsPerThread;
    std::cout << "  期望值: " << expected << "\n";
    std::cout << "  实际值: " << counter << "\n";
    if (counter != expected) {
        std::cout << "  [!] 计数丢失！这就是数据竞争的后果。\n";
    }
    std::cout << "  提示：用 TSan 编译后运行，可以看到详细的竞争报告。\n";
    std::cout << "        g++ -fsanitize=thread -g -O1 ...\n";
}

// --- 1b. 修复版本：使用 std::atomic ---
void demo_data_race_fixed() {
    std::cout << "\n=== 1b. 数据竞争（修复版本：std::atomic）===\n";

    std::atomic<int> counter{0}; // 原子变量，无数据竞争

    auto worker = [&counter]() {
        for (int i = 0; i < kIterationsPerThread; ++i) {
            // relaxed 足以避免数据竞争，且性能最好
            // 如果还需要与其他变量建立顺序关系，应使用更强的 memory order
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    long expected = static_cast<long>(kThreadCount) * kIterationsPerThread;
    std::cout << "  期望值: " << expected << "\n";
    std::cout << "  实际值: " << counter.load() << "\n";
    std::cout << "  使用 std::atomic<int> 后，结果始终正确。\n";
}

// ============================================================================
// 第二部分：死锁
//
// 经典 AB-BA 死锁：线程 1 先锁 A 再锁 B，线程 2 先锁 B 再锁 A。
// 如果双方各持一把锁并在等待另一把，就会永久阻塞。
//
// 本示例使用 std::timed_mutex + try_lock_for() 作为超时保护，
// 确保即使发生死锁，程序也能在有限时间内继续执行。
//
// TSan 的 lock-order-inversion 检查可以报告潜在的死锁风险。
// Helgrind 同样可以检测锁顺序反转。
// ============================================================================

// --- 2a. 有缺陷版本：AB-BA 锁反序死锁（超时保护） ---
void demo_deadlock_buggy() {
    std::cout << "\n=== 2a. 死锁（有缺陷版本：AB-BA 反序）===\n";

    std::timed_mutex mtx_a;
    std::timed_mutex mtx_b;
    std::atomic<bool> deadlock_detected{false};

    // 线程 1：先锁 A 再锁 B
    auto worker_ab = [&](int id) {
        std::unique_lock<std::timed_mutex> lock_a(mtx_a, std::defer_lock);
        if (!lock_a.try_lock_for(std::chrono::milliseconds(100))) {
            std::cout << "  线程 " << id << "：获取 mtx_a 超时，放弃\n";
            return;
        }
        // 持有 A，sleep 一小段让线程 2 有时间锁住 B
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::unique_lock<std::timed_mutex> lock_b(mtx_b, std::defer_lock);
        if (!lock_b.try_lock_for(std::chrono::milliseconds(300))) {
            std::cout << "  线程 " << id << "：获取 mtx_b 超时 -> 死锁！\n";
            deadlock_detected.store(true);
            return; // lock_a 自动释放（RAII）
        }

        std::cout << "  线程 " << id << "：成功持有两把锁\n";
    };

    // 线程 2：先锁 B 再锁 A（反序！）
    auto worker_ba = [&](int id) {
        std::unique_lock<std::timed_mutex> lock_b(mtx_b, std::defer_lock);
        if (!lock_b.try_lock_for(std::chrono::milliseconds(100))) {
            std::cout << "  线程 " << id << "：获取 mtx_b 超时，放弃\n";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::unique_lock<std::timed_mutex> lock_a(mtx_a, std::defer_lock);
        if (!lock_a.try_lock_for(std::chrono::milliseconds(300))) {
            std::cout << "  线程 " << id << "：获取 mtx_a 超时 -> 死锁！\n";
            deadlock_detected.store(true);
            return;
        }

        std::cout << "  线程 " << id << "：成功持有两把锁\n";
    };

    std::thread t1(worker_ab, 1);
    std::thread t2(worker_ba, 2);
    t1.join();
    t2.join();

    if (deadlock_detected.load()) {
        std::cout << "  [!] 检测到死锁场景！线程因锁反序而互相等待。\n";
    } else {
        std::cout << "  本次未触发死锁（时序相关，不保证每次复现）。\n";
    }
    std::cout << "  教训：永远以相同的顺序获取多把锁。\n";
}

// --- 2b. 修复版本：使用 std::scoped_lock 一次性获取多把锁 ---
void demo_deadlock_fixed() {
    std::cout << "\n=== 2b. 死锁（修复版本：std::scoped_lock）===\n";

    std::timed_mutex mtx_a;
    std::timed_mutex mtx_b;
    std::atomic<int> success_count{0};

    auto worker = [&](int id) {
        for (int i = 0; i < 10'000; ++i) {
            // std::scoped_lock 使用 deadlock-avoidance 算法
            // 即使两个线程传入的参数顺序不同，也不会死锁
            if (id % 2 == 0) {
                std::scoped_lock lock(mtx_a, mtx_b);
            } else {
                std::scoped_lock lock(mtx_b, mtx_a); // 反序，但仍然安全
            }
            ++success_count;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) {
        th.join();
    }

    long expected = static_cast<long>(kThreadCount) * 10'000;
    std::cout << "  成功操作次数: " << success_count.load() << "（期望 " << expected << "）\n";
    std::cout << "  std::scoped_lock（C++17）内部使用 deadlock-avoidance\n";
    std::cout << "  算法，无论参数顺序如何都能安全获取所有锁。\n";
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::cout << "数据竞争与死锁：诊断与修复\n";
    std::cout << "============================\n";

    // 数据竞争演示
    demo_data_race_buggy();
    demo_data_race_fixed();

    // 死锁演示
    demo_deadlock_buggy();
    demo_deadlock_fixed();

    std::cout << "\n--- 诊断工具速查 ---\n";
    std::cout << "  TSan:      g++ -fsanitize=thread -g -O1 ...\n";
    std::cout << "  Helgrind:  valgrind --tool=helgrind ./program\n";
    std::cout << "  TSan 能精确报告数据竞争和锁顺序反转。\n";
    std::cout << "  Helgrind 侧重锁使用问题（锁顺序、未释放等）。\n";

    return 0;
}
