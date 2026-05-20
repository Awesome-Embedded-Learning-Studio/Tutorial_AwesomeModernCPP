/*
 * C++20 Task<T>：协程链式调用与简单事件循环
 *
 * 本文件演示如何用 C++20 协程构建一个 Task<T> 类型，支持：
 *   1. co_return 返回值
 *   2. co_await 等待另一个 Task 完成（协程链式调用）
 *   3. 一个极简事件循环驱动所有协程完成
 *   4. Task<void> 特化：不需要返回值的异步任务
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -Wall -o /tmp/02_awaitable_task 02_awaitable_task.cpp
 *
 * 运行：
 *   /tmp/02_awaitable_task
 *
 * 编译器要求：GCC 12+ 或 Clang 16+
 */

#include <cassert>
#include <coroutine>
#include <functional>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <variant>

// ============================================================================
// 前向声明
// ============================================================================

class SimpleEventLoop;

/// @brief 获取全局事件循环实例（在本文件中用全局变量模拟）
SimpleEventLoop& get_event_loop();

// ============================================================================
// Task<T>：支持 co_await 链式调用的协程返回类型
// ============================================================================
//
// Task<T> 的核心设计思路：
// - 每个 Task 持有一个 coroutine_handle，指向协程帧
// - promise_type 中保存返回值 T 和"调用者"的句柄 (continuation_)
// - 当 Task 被 co_await 时，caller 挂起，被等待的 Task 开始执行
// - Task 完成后（final_suspend），恢复 caller
// - 所有"谁该被恢复"的调度由事件循环完成

template <typename T> class Task {
  public:
    struct promise_type;

    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        // 用 variant 表示三种状态：空、值、异常
        std::variant<std::monostate, T, std::exception_ptr> result_;

        // continuation_ 指向等待本 Task 的调用者协程
        // 当本协程完成时，通过它恢复调用者
        std::coroutine_handle<> continuation_ = nullptr;

        Task get_return_object() { return Task{Handle::from_promise(*this)}; }

        // 初始挂起：不立即执行，等被 co_await 或手动启动时才运行
        auto initial_suspend() { return std::suspend_always{}; }

        // 最终挂起：协程完成后挂起，在此恢复 continuation_
        // 注意必须返回自定义的 FinalAwaiter，不能直接用 suspend_always
        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            // 返回要切换到的协程句柄；返回 nullptr 表示不切换
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& promise = h.promise();
                if (promise.continuation_) {
                    // 有调用者在等我们，切换回调用者
                    return promise.continuation_;
                }
                // 没有调用者，返回 noop_coroutine（不做任何事的协程句柄）
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        auto final_suspend() noexcept { return FinalAwaiter{}; }

        // co_return value 的处理：存储返回值
        void return_value(T value) { result_.template emplace<1>(std::move(value)); }

        void unhandled_exception() { result_.template emplace<2>(std::current_exception()); }
    };

    // Awaiter：让其他协程 co_await 本 Task
    struct Awaiter {
        Handle handle_;

        // 如果 Task 已经完成，不需要挂起调用者
        bool await_ready() const noexcept { return handle_ && handle_.done(); }

        // 调用者被挂起后，设置 continuation 并启动（或恢复）被等待的 Task
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
            handle_.promise().continuation_ = caller;
            // 返回被等待协程的句柄，运行时将立即 resume 它
            return handle_;
        }

        // 恢复后从 promise 中取出结果
        T await_resume() {
            auto& result = handle_.promise().result_;
            if (std::holds_alternative<std::exception_ptr>(result)) {
                std::rethrow_exception(std::get<std::exception_ptr>(result));
            }
            return std::move(std::get<T>(result));
        }
    };

    // co_await Task<T> 时返回 Awaiter
    auto operator co_await() { return Awaiter{handle_}; }

    // 检查 Task 是否完成
    bool is_ready() const noexcept { return handle_ && handle_.done(); }

    // 显式启动一个 Task（不通过 co_await）
    void start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    Handle handle_;

    Task(Handle h) : handle_(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }
};

// ============================================================================
// Task<void> 特化：不需要返回值的异步任务
// ============================================================================

template <> class Task<void> {
  public:
    struct promise_type;

    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_ = nullptr;

        Task get_return_object() { return Task{Handle::from_promise(*this)}; }

        auto initial_suspend() { return std::suspend_always{}; }

        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().continuation_) {
                    return h.promise().continuation_;
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        auto final_suspend() noexcept { return FinalAwaiter{}; }

        void return_void() {}

        void unhandled_exception() { exception_ = std::current_exception(); }
    };

    struct Awaiter {
        Handle handle_;

        bool await_ready() const noexcept { return handle_ && handle_.done(); }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
            handle_.promise().continuation_ = caller;
            return handle_;
        }

        void await_resume() {
            if (handle_.promise().exception_) {
                std::rethrow_exception(handle_.promise().exception_);
            }
        }
    };

    auto operator co_await() { return Awaiter{handle_}; }

    bool is_ready() const noexcept { return handle_ && handle_.done(); }

    void start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    Handle handle_;

    Task(Handle h) : handle_(h) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }
};

// ============================================================================
// SimpleEventLoop：最简事件循环
// ============================================================================
//
// 真正的异步框架（如 io_uring、libuv）会在事件循环中等待 I/O 完成。
// 这里我们用一个简单的任务队列来模拟：把协程投入队列，逐个 resume
// 直到全部完成。虽然不涉及真正的 I/O 阻塞，但足以展示"协程 + 事件循环"
// 的基本协作模式。

class SimpleEventLoop {
  public:
    /// @brief 将一个协程句柄投入就绪队列
    void schedule(std::coroutine_handle<> handle) { ready_queue_.push(handle); }

    /// @brief 运行事件循环，直到所有已调度任务完成
    void run() {
        while (!ready_queue_.empty()) {
            auto handle = ready_queue_.front();
            ready_queue_.pop();
            if (!handle.done()) {
                handle.resume();
            }
        }
    }

    /// @brief 启动一个 Task<void> 并将其投入事件循环
    void spawn(Task<void>&& task) {
        // 先设置一个空的 continuation（顶层任务没有调用者）
        // 然后 schedule 它的第一次 resume
        schedule(task.handle_);
    }

  private:
    std::queue<std::coroutine_handle<>> ready_queue_;
};

// 全局事件循环实例
static SimpleEventLoop kGlobalLoop;

SimpleEventLoop& get_event_loop() {
    return kGlobalLoop;
}

// ============================================================================
// 演示用例：链式异步计算
// ============================================================================

/// @brief 模拟异步计算：返回一个固定值
Task<int> compute_a() {
    std::cout << "  [compute_a] 开始计算\n";
    co_return 42;
}

/// @brief 模拟异步计算：接收上游结果，做进一步处理
Task<int> compute_b(int input) {
    std::cout << "  [compute_b] 收到 " << input << "，开始计算\n";
    co_return input * 2;
}

/// @brief 模拟异步计算：第三步
Task<int> compute_c(int input) {
    std::cout << "  [compute_c] 收到 " << input << "，开始计算\n";
    co_return input + 8;
}

/// @brief 流水线：通过 co_await 将多个 Task 串联
Task<int> pipeline() {
    std::cout << "[pipeline] 启动三步异步流水线\n";

    int a = co_await compute_a();
    std::cout << "[pipeline] 第一步完成: " << a << "\n";

    int b = co_await compute_b(a);
    std::cout << "[pipeline] 第二步完成: " << b << "\n";

    int c = co_await compute_c(b);
    std::cout << "[pipeline] 第三步完成: " << c << "\n";

    co_return c;
}

/// @brief 一个独立的 void 任务
Task<void> independent_task() {
    std::cout << "[independent_task] 我是一个不需要返回值的任务\n";
    co_return;
}

/// @brief 演示异常传播
Task<int> failing_task() {
    std::cout << "[failing_task] 即将抛出异常\n";
    throw std::runtime_error("故意的错误！");
    co_return 0;
}

/// @brief 主入口：驱动事件循环
Task<void> main_task() {
    std::cout << "========== 链式 co_await 演示 ==========\n";

    // pipeline() 创建 Task，co_await 启动并等待完成
    auto result = co_await pipeline();
    std::cout << "[main_task] pipeline 最终结果: " << result << "\n\n";

    // 独立的 void 任务
    std::cout << "========== Task<void> 演示 ==========\n";
    co_await independent_task();
    std::cout << "[main_task] independent_task 完成\n\n";

    // 异常传播演示
    std::cout << "========== 异常传播演示 ==========\n";
    try {
        co_await failing_task();
    } catch (const std::runtime_error& e) {
        std::cout << "[main_task] 捕获到异常: " << e.what() << "\n";
    }

    std::cout << "\n[main_task] 所有演示完成\n";
}

// ============================================================================
// main
// ============================================================================

int main() {
    auto& loop = get_event_loop();

    // 创建主任务并投入事件循环
    auto task = main_task();
    loop.spawn(std::move(task));

    // 运行事件循环，驱动所有协程完成
    loop.run();

    return 0;
}
