/*
 * 演示：thread_local 与 std::call_once / Meyers' Singleton
 *
 * 背景：文章 ch01 "线程生命周期与 RAII 管理" 涵盖——
 *       1. thread_local 存储期：每个线程拥有独立实例
 *       2. thread_local 的初始化时机（首次使用时）与销毁时机（线程退出时）
 *       3. 每线程随机数生成器（避免锁竞争）
 *       4. std::call_once：跨线程的一次性初始化
 *       5. std::call_once 的异常重试行为
 *       6. Meyers' Singleton：利用 C++11 magic statics 的线程安全单例
 *
 * 预期结果：
 *   程序演示 thread_local 变量的线程隔离性、call_once 的一次性保证、
 *   以及 Meyers' Singleton 的线程安全性。
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/04_thread_local_call_once
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++17
 */

#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

// 线程安全输出
std::mutex g_cout_mutex;

void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << msg << std::endl;
}

// ============================================================
// Demo 1: thread_local 计数器 —— 每个线程拥有独立副本
// ============================================================

// thread_local 变量的生命周期：
// - 初始化：每个线程首次执行到该变量的声明时初始化
// - 销毁：线程退出时销毁
// - 每个线程拥有独立的实例，互不干扰

// 全局 thread_local 计数器
thread_local int tl_counter = 0;

void counter_worker(int thread_id, int iterations) {
    // 首次访问时，tl_counter 被初始化为 0（每个线程独立）
    safe_print("  [线程 " + std::to_string(thread_id) +
               "] 初始 tl_counter = " + std::to_string(tl_counter));

    for (int i = 0; i < iterations; ++i) {
        ++tl_counter;
    }

    safe_print("  [线程 " + std::to_string(thread_id) +
               "] 累加后 tl_counter = " + std::to_string(tl_counter));
}

void demo_thread_local_counter() {
    safe_print("\n=== Demo 1: thread_local 计数器 ===");

    // 主线程也有自己的 tl_counter
    tl_counter = 100;
    safe_print("  [主线程] tl_counter = " + std::to_string(tl_counter));

    std::thread t1(counter_worker, 1, 5);
    std::thread t2(counter_worker, 2, 10);
    std::thread t3(counter_worker, 3, 3);

    t1.join();
    t2.join();
    t3.join();

    // 主线程的 tl_counter 不受子线程影响
    safe_print("  [主线程] 子线程结束后 tl_counter = " + std::to_string(tl_counter) +
               "  (仍然是 100，不受子线程影响)");
}

// ============================================================
// Demo 2: thread_local 随机数生成器 —— 避免锁竞争
// ============================================================

// 全局共享的随机数生成器需要加锁，性能差。
// thread_local RNG 让每个线程拥有独立的生成器，无需同步。

// 每线程随机数生成器
thread_local std::mt19937 tl_rng{std::random_device{}()};

int generate_random_int(int min_val, int max_val) {
    std::uniform_int_distribution<int> dist(min_val, max_val);
    return dist(tl_rng);
}

void rng_worker(int thread_id, int count) {
    safe_print("  [线程 " + std::to_string(thread_id) + "] 随机数:");
    std::string nums;
    for (int i = 0; i < count; ++i) {
        int val = generate_random_int(1, 100);
        nums += std::to_string(val);
        if (i < count - 1) {
            nums += ", ";
        }
    }
    safe_print("  [线程 " + std::to_string(thread_id) + "] " + nums);
}

void demo_thread_local_rng() {
    safe_print("\n=== Demo 2: thread_local 随机数生成器 ===");

    std::thread t1(rng_worker, 1, 5);
    std::thread t2(rng_worker, 2, 5);

    t1.join();
    t2.join();

    safe_print("  [说明] 每个线程使用独立的 RNG，无需加锁，性能更优");
}

// ============================================================
// Demo 3: std::call_once 基本用法 —— 跨线程一次性初始化
// ============================================================

// std::call_once 保证即使多个线程同时调用，
// 传入的函数也只执行一次。内部使用 std::once_flag 记录状态。

std::once_flag g_init_flag;
int g_shared_resource = 0;

void initialize_resource() {
    safe_print("  [初始化] 正在初始化共享资源... (只应输出一次)");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    g_shared_resource = 42;
    safe_print("  [初始化] 共享资源初始化完成");
}

void call_once_worker(int thread_id) {
    safe_print("  [线程 " + std::to_string(thread_id) + "] 准备访问资源，先确保初始化...");
    std::call_once(g_init_flag, initialize_resource);
    safe_print("  [线程 " + std::to_string(thread_id) +
               "] 资源已就绪, 值 = " + std::to_string(g_shared_resource));
}

void demo_call_once_basic() {
    safe_print("\n=== Demo 3: std::call_once 基本用法 ===");

    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(call_once_worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    safe_print("  [结论] 5 个线程并发调用，initialize_resource 只执行一次");
}

// ============================================================
// Demo 4: std::call_once 异常重试行为
// ============================================================

// 如果 std::call_once 传入的函数抛出异常，
// 该次调用被视为"未成功"，其他线程可以重试。
// 直到有一个线程成功执行（不抛异常），初始化才算完成。

std::once_flag g_retry_flag;
int g_retry_attempt_count = 0;

void maybe_failing_init() {
    ++g_retry_attempt_count;
    safe_print("  [初始化] 第 " + std::to_string(g_retry_attempt_count) + " 次尝试初始化...");

    if (g_retry_attempt_count < 3) {
        safe_print("  [初始化] 模拟初始化失败，抛出异常!");
        throw std::runtime_error("初始化失败（模拟）");
    }

    safe_print("  [初始化] 初始化成功!");
}

void retry_worker(int thread_id) {
    try {
        std::call_once(g_retry_flag, maybe_failing_init);
        safe_print("  [线程 " + std::to_string(thread_id) + "] 初始化完成（成功）");
    } catch (const std::exception& e) {
        safe_print("  [线程 " + std::to_string(thread_id) + "] 捕获异常: " + std::string(e.what()));
    }
}

void demo_call_once_exception_retry() {
    safe_print("\n=== Demo 4: std::call_once 异常重试行为 ===");

    // 使用足够多的线程确保重试行为能够展示
    // 注意：实际行为取决于线程调度，可能需要多次运行才能看到重试
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back(retry_worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    safe_print("  [说明] 如果初始化函数抛出异常，"
               "call_once 允许其他线程重试");
    safe_print("  [说明] 直到某次调用不抛异常，"
               "初始化才被视为成功");
}

// ============================================================
// Demo 5: Meyers' Singleton —— 线程安全的单例模式
// ============================================================

// C++11 起，函数内的 static 局部变量（"magic statics"）初始化
// 是线程安全的。编译器保证只有一个线程执行初始化，
// 其他线程阻塞等待。这提供了最简洁的线程安全单例实现。

class DatabaseConnection {
  public:
    // 删除拷贝与移动
    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;

    // 获取单例实例
    static DatabaseConnection& instance() {
        // magic static: 线程安全的首次初始化
        // 等价于 call_once + static 变量的组合，但更简洁
        static DatabaseConnection inst;
        return inst;
    }

    void query(const std::string& sql) {
        safe_print("  [DatabaseConnection] 执行查询: " + sql +
                   " (conn_id=" + std::to_string(connection_id_) + ")");
    }

  private:
    DatabaseConnection() {
        // 模拟昂贵的初始化操作
        safe_print("  [DatabaseConnection] 构造中... (只应输出一次)");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        connection_id_ = 9999;
        safe_print("  [DatabaseConnection] 构造完成");
    }

    int connection_id_ = 0;
};

void singleton_worker(int thread_id) {
    safe_print("  [线程 " + std::to_string(thread_id) + "] 获取单例实例...");
    DatabaseConnection::instance().query("SELECT * FROM users WHERE id=" +
                                         std::to_string(thread_id));
}

void demo_meyers_singleton() {
    safe_print("\n=== Demo 5: Meyers' Singleton（magic statics）===");

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(singleton_worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    safe_print("  [结论] 多线程并发访问，构造函数只执行一次");
    safe_print("  [结论] C++11 magic statics 是最简洁的线程安全单例实现");
}

// ============================================================
// 主函数：依次运行所有 Demo
// ============================================================

int main() {
    std::cout << "===== ch01: thread_local 与 call_once =====\n";

    demo_thread_local_counter();
    demo_thread_local_rng();
    demo_call_once_basic();
    demo_call_once_exception_retry();
    demo_meyers_singleton();

    std::cout << "\n===== 所有 Demo 运行完毕 =====" << std::endl;
    return 0;
}
