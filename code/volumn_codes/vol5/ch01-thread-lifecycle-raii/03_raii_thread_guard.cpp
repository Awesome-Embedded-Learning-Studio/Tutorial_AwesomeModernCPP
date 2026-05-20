/*
 * 演示：RAII 风格的线程管理 —— ThreadGuard / JoiningThread / ScopeGuard
 *
 * 背景：文章 ch01 "线程生命周期与 RAII 管理" 涵盖——
 *       1. 忘记 join/detach 会导致 std::terminate（~thread 中 joinable 为 true）
 *       2. ThreadGuard：通用 RAII 包装，策略化选择 join 或 detach
 *       3. JoiningThread：拥有 std::thread 的所有权，析构自动 join，支持移动
 *       4. parallel_for_each：基于 JoiningThread 的简易并行算法
 *       5. ScopeGuard：通用作用域守卫，用于管理任意资源
 *
 * 预期结果：
 *   程序演示各种 RAII 包装器的使用，展示异常安全的线程管理。
 *   析构函数中的自动 join 保证线程不会泄漏。
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/03_raii_thread_guard
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++17
 */

#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// 线程安全输出
std::mutex g_cout_mutex;

void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << msg << std::endl;
}

// ============================================================
// ThreadGuard: 策略化 RAII 线程守卫
// ============================================================

// 析构策略：join 或 detach
enum class ThreadGuardPolicy { Join, Detach };

// ThreadGuard 在析构时自动执行 join 或 detach，
// 防止 std::thread 析构时因 joinable() == true 而调用 std::terminate。
//
// 设计思路参考：Anthony Williams《C++ Concurrency in Action》
template <ThreadGuardPolicy Policy = ThreadGuardPolicy::Join> class ThreadGuard {
  public:
    explicit ThreadGuard(std::thread t) : thread_(std::move(t)) {
        if (!thread_.joinable()) {
            throw std::logic_error("ThreadGuard: 线程必须 joinable");
        }
    }

    ~ThreadGuard() {
        if (thread_.joinable()) {
            if constexpr (Policy == ThreadGuardPolicy::Join) {
                thread_.join();
            } else {
                thread_.detach();
            }
        }
    }

    // 禁止拷贝
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

    // 允许移动（转移线程所有权）
    ThreadGuard(ThreadGuard&&) noexcept = default;
    ThreadGuard& operator=(ThreadGuard&&) noexcept = default;

    // 访问底层 thread
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

  private:
    std::thread thread_;
};

// ============================================================
// JoiningThread: 拥有所有权的自动 join 线程包装器
// ============================================================

// JoiningThread 类似 std::jthread（C++20），但在 C++17 中可用。
// 它拥有 std::thread 的所有权，析构时自动 join。
// 支持移动语义，禁止拷贝。
class JoiningThread {
  public:
    JoiningThread() noexcept = default;

    // 从 std::thread 构造
    explicit JoiningThread(std::thread t) noexcept : thread_(std::move(t)) {}

    // 从可调用对象和参数直接构造
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...) {}

    ~JoiningThread() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // 移动构造与赋值
    JoiningThread(JoiningThread&& other) noexcept : thread_(std::move(other.thread_)) {}

    JoiningThread& operator=(JoiningThread&& other) noexcept {
        if (this != &other) {
            // 先 join 当前线程（如果正在运行）
            if (thread_.joinable()) {
                thread_.join();
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    // 从 std::thread 赋值
    JoiningThread& operator=(std::thread t) noexcept {
        if (thread_.joinable()) {
            thread_.join();
        }
        thread_ = std::move(t);
        return *this;
    }

    // 禁止拷贝
    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

    // 交换
    void swap(JoiningThread& other) noexcept { std::swap(thread_, other.thread_); }

    // 访问
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

    // 委托给 std::thread 的接口
    bool joinable() const noexcept { return thread_.joinable(); }
    void join() { thread_.join(); }
    void detach() { thread_.detach(); }
    std::thread::id get_id() const noexcept { return thread_.get_id(); }

  private:
    std::thread thread_;
};

// ============================================================
// ScopeGuard: 通用作用域守卫
// ============================================================

// ScopeGuard 在作用域结束时自动执行指定的清理函数。
// 用途不仅限于线程管理，还可以管理文件句柄、锁等资源。
//
// 设计思路参考：Andrei Alexandrescu "ScopeGuard" 及
//               C++17 std::experimental::scope_exit

class ScopeGuard {
  public:
    // 从任意可调用对象构造
    template <typename Func> explicit ScopeGuard(Func&& func)
        : func_(std::forward<Func>(func)), active_(true) {}

    ~ScopeGuard() {
        if (active_) {
            try {
                func_();
            } catch (...) {
                // 析构函数中不应抛出异常，静默吞掉
            }
        }
    }

    // 允许手动解除（不再执行清理）
    void dismiss() noexcept { active_ = false; }

    // 禁止拷贝
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    // 允许移动
    ScopeGuard(ScopeGuard&& other) noexcept
        : func_(std::move(other.func_)), active_(other.active_) {
        other.active_ = false;
    }

    ScopeGuard& operator=(ScopeGuard&&) = delete;

  private:
    std::function<void()> func_;
    bool active_;
};

// 辅助宏：创建 ScopeGuard 的便捷方式
// 用法：auto guard = make_scope_guard([&]() { cleanup(); });
// 注意：不使用宏，使用函数模板推导更安全
template <typename Func> ScopeGuard make_scope_guard(Func&& func) {
    return ScopeGuard(std::forward<Func>(func));
}

// ============================================================
// Demo 1: ThreadGuard —— 异常安全的线程管理
// ============================================================

void demo_thread_guard() {
    safe_print("\n=== Demo 1: ThreadGuard（策略化 RAII 守卫）===");

    // 使用 Join 策略的 ThreadGuard
    {
        ThreadGuard<ThreadGuardPolicy::Join> guard(std::thread([]() {
            safe_print("  [ThreadGuard/Join 线程] 正在工作...");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            safe_print("  [ThreadGuard/Join 线程] 工作完成");
        }));
        // guard 离开作用域时自动 join —— 无需手动调用
        safe_print("  [主线程] ThreadGuard 作用域结束，自动 join...");
    }
    safe_print("  [主线程] ThreadGuard/Join 已析构，线程已结束");

    // 使用 Detach 策略的 ThreadGuard
    {
        ThreadGuard<ThreadGuardPolicy::Detach> guard(
            std::thread([]() { safe_print("  [ThreadGuard/Detach 线程] 后台任务..."); }));
        safe_print("  [主线程] ThreadGuard/Detach 作用域结束，自动 detach...");
    }
    // 等待 detach 线程输出
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    safe_print("  [主线程] ThreadGuard/Detach 后台线程已完成");
}

// ============================================================
// Demo 2: JoiningThread —— 自动 join 的线程包装器
// ============================================================

void demo_joining_thread() {
    safe_print("\n=== Demo 2: JoiningThread（自动 join 包装器）===");

    // 直接从可调用对象构造
    {
        JoiningThread jt([]() { safe_print("  [JoiningThread #1] 正在工作..."); });
        // jt 析构时自动 join
    }
    safe_print("  [主线程] JoiningThread #1 已析构");

    // 从 std::thread 构造，然后移动赋值
    {
        JoiningThread jt;
        jt = std::thread([]() { safe_print("  [JoiningThread #2] 通过移动赋值创建"); });

        JoiningThread jt2 = std::move(jt);
        // jt 现在不持有线程
        safe_print("  [主线程] 移动后 jt.joinable() = " +
                   std::string(jt.joinable() ? "true" : "false"));
        // jt2 析构时自动 join
    }
    safe_print("  [主线程] JoiningThread #2 已析构");

    // 异常场景：即使抛出异常，JoiningThread 仍然会 join
    try {
        JoiningThread jt([]() { safe_print("  [JoiningThread #3] 异常安全测试"); });
        throw std::runtime_error("模拟异常");
        // jt 析构时仍然 join —— 不会导致 std::terminate
    } catch (const std::exception& e) {
        safe_print("  [主线程] 捕获异常: " + std::string(e.what()));
        safe_print("  [主线程] JoiningThread #3 在栈展开时已自动 join");
    }
}

// ============================================================
// Demo 3: parallel_for_each —— 基于 JoiningThread 的并行算法
// ============================================================

// 简易并行 for_each：将工作均匀分配到多个线程上
// 每个线程处理 [begin, end) 范围内的元素
template <typename Iterator, typename Func>
void parallel_for_each(Iterator begin, Iterator end, Func func) {
    // 计算元素数量
    auto range_length = std::distance(begin, end);
    if (range_length == 0) {
        return;
    }

    // 根据硬件并发能力确定线程数
    unsigned int const hardware_threads = std::thread::hardware_concurrency();
    unsigned int const num_threads = hardware_threads != 0 ? hardware_threads : 2;

    // 每个线程处理的元素数量
    auto block_size = range_length / num_threads;

    std::vector<JoiningThread> threads;
    threads.reserve(num_threads);

    Iterator block_start = begin;

    // 为前 (num_threads - 1) 个块各启动一个线程
    for (unsigned int i = 0; i < num_threads - 1 && block_start != end; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end,
                     std::min(static_cast<long>(block_size), std::distance(block_start, end)));

        threads.emplace_back([block_start, block_end, &func]() {
            for (auto it = block_start; it != block_end; ++it) {
                func(*it);
            }
        });

        block_start = block_end;
    }

    // 主线程处理最后一个块
    for (auto it = block_start; it != end; ++it) {
        func(*it);
    }

    // JoiningThread 析构时自动 join 所有工作线程
}

void demo_parallel_for_each() {
    safe_print("\n=== Demo 3: parallel_for_each ===");

    std::vector<int> data;
    for (int i = 0; i < 20; ++i) {
        data.push_back(i);
    }

    safe_print("  使用 parallel_for_each 处理 20 个元素：");
    parallel_for_each(data.begin(), data.end(), [](int& value) {
        value *= 2;
        safe_print("    处理元素: " + std::to_string(value / 2) + " -> " + std::to_string(value));
    });

    // 验证结果
    bool all_doubled = true;
    for (int i = 0; i < 20; ++i) {
        if (data[i] != i * 2) {
            all_doubled = false;
            break;
        }
    }
    safe_print("  验证: 所有元素均已翻倍? " + std::string(all_doubled ? "是" : "否"));
}

// ============================================================
// Demo 4: ScopeGuard —— 通用作用域守卫
// ============================================================

void demo_scope_guard() {
    safe_print("\n=== Demo 4: ScopeGuard（通用作用域守卫）===");

    // 用于线程 join 的 ScopeGuard
    {
        std::thread t([]() { safe_print("  [ScopeGuard 线程] 正在工作..."); });

        // 创建守卫：确保无论如何都会 join
        auto guard = make_scope_guard([&t]() {
            if (t.joinable()) {
                t.join();
                safe_print("  [ScopeGuard] 已自动 join 线程");
            }
        });

        safe_print("  [主线程] 正常执行...");
        // 如果这里抛出异常，guard 仍然会 join
    }

    // 用于资源清理的 ScopeGuard
    {
        int* raw_ptr = new int(42);
        auto ptr_guard = make_scope_guard([raw_ptr]() {
            delete raw_ptr;
            safe_print("  [ScopeGuard] 已释放 raw_ptr");
        });

        safe_print("  [主线程] 使用 raw_ptr, 值 = " + std::to_string(*raw_ptr));

        // 可以手动解除守卫
        // ptr_guard.dismiss();
    }

    // 演示 dismiss：守卫被解除后不再执行
    {
        bool cleanup_executed = false;
        {
            auto guard = make_scope_guard([&cleanup_executed]() { cleanup_executed = true; });
            guard.dismiss(); // 解除守卫
        } // guard 析构，但不执行清理
        safe_print("  [主线程] dismiss 后 cleanup 执行? " +
                   std::string(cleanup_executed ? "是" : "否 (符合预期)"));
    }
}

// ============================================================
// 主函数：依次运行所有 Demo
// ============================================================

int main() {
    std::cout << "===== ch01: RAII 线程管理 =====\n";

    demo_thread_guard();
    demo_joining_thread();
    demo_parallel_for_each();
    demo_scope_guard();

    std::cout << "\n===== 所有 Demo 运行完毕 =====" << std::endl;
    return 0;
}
