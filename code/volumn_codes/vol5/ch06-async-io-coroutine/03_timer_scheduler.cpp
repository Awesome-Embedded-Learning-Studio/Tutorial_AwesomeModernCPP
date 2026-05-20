/*
 * C++20 协程定时器调度器
 *
 * 本文件实现一个基于协程的定时器调度系统，不依赖任何外部 I/O 库
 * （如 epoll、libuv、Boost.Asio），纯粹用标准库演示协程与事件循环
 * 的集成方式。
 *
 * 核心组件：
 *   1. TimerAwaiter —— co_await 它会让协程挂起指定时长
 *   2. SimpleScheduler —— 管理所有待触发的定时器，按到期时间排序
 *   3. 多个协程并发挂起、按时间顺序恢复的完整演示
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -Wall -o /tmp/03_timer_scheduler 03_timer_scheduler.cpp
 *
 * 运行：
 *   /tmp/03_timer_scheduler
 *
 * 编译器要求：GCC 12+ 或 Clang 16+
 */

#include <chrono>
#include <coroutine>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// ============================================================================
// 前向声明与全局调度器
// ============================================================================

class SimpleScheduler;

/// @brief 获取全局调度器实例
SimpleScheduler& get_scheduler();

// ============================================================================
// SimpleScheduler：按时间驱动协程恢复的调度器
// ============================================================================
//
// 调度器维护一个按到期时间排序的优先队列。当 run() 被调用时，
// 它逐个取出到期的定时器，resume 对应的协程。这模拟了一个真实的
// 事件循环在没有 I/O 多路复用时的行为——用时间来驱动一切。
//
// 在真实的异步框架中，这一层通常会被 epoll_wait / io_uring_submit
// 等系统调用替代，但"定时器到期 → 恢复协程"的核心模式是一样的。

class SimpleScheduler {
  public:
    /// @brief 定时器条目：到期时间 + 要恢复的协程句柄
    struct TimerEntry {
        std::chrono::steady_clock::time_point deadline_;
        std::coroutine_handle<> handle_;

        // 优先队列按到期时间升序排列（最小的在最前）
        bool operator>(const TimerEntry& other) const { return deadline_ > other.deadline_; }
    };

    /// @brief 添加一个延迟恢复的协程
    void schedule_after(std::chrono::steady_clock::duration delay, std::coroutine_handle<> handle) {
        auto deadline = std::chrono::steady_clock::now() + delay;
        timer_queue_.push(TimerEntry{deadline, handle});
    }

    /// @brief 立即调度一个协程（零延迟）
    void schedule_now(std::coroutine_handle<> handle) { ready_queue_.push_back(handle); }

    /// @brief 运行调度器，直到所有定时器和就绪任务处理完毕
    ///
    /// 这个函数是整个系统的"心跳"。在真实框架中，它会是一个
    /// 长期运行的循环，而不是跑完就退出。
    void run() {
        // 先处理所有零延迟的就绪任务
        process_ready_queue();

        // 然后按时间顺序处理定时器
        while (!timer_queue_.empty()) {
            auto entry = timer_queue_.top();
            timer_queue_.pop();

            // 计算需要等待的时间
            auto now = std::chrono::steady_clock::now();
            if (entry.deadline_ > now) {
                auto wait_time =
                    std::chrono::duration_cast<std::chrono::milliseconds>(entry.deadline_ - now);
                std::cout << "[调度器] 等待 " << wait_time.count() << "ms ...\n";
                // 在真实系统中这里会调用 epoll_wait 之类的接口
                // 我们用 sleep_for 模拟阻塞等待
                std::this_thread::sleep_for(entry.deadline_ - now);
            }

            // 恢复协程
            if (!entry.handle_.done()) {
                entry.handle_.resume();
            }

            // 协程可能在 resume 过程中产生新的就绪任务
            process_ready_queue();
        }
    }

    /// @brief 获取当前待处理的定时器数量
    std::size_t pending_count() const { return timer_queue_.size() + ready_queue_.size(); }

  private:
    void process_ready_queue() {
        for (auto& handle : ready_queue_) {
            if (!handle.done()) {
                handle.resume();
            }
        }
        ready_queue_.clear();
    }

    // 最小堆：按到期时间排序
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> timer_queue_;

    // 立即执行的就绪队列
    std::vector<std::coroutine_handle<>> ready_queue_;
};

// 全局调度器
static SimpleScheduler kScheduler;

SimpleScheduler& get_scheduler() {
    return kScheduler;
}

// ============================================================================
// TimerAwaiter：让协程挂起一段时间的 awaitable
// ============================================================================
//
// 当你在协程里写 co_await sleep_for(100ms) 时，实际上发生的是：
// 1. 创建一个 TimerAwaiter，记录延迟时间
// 2. 协程在 await_suspend 中被注册到调度器的定时器队列
// 3. 协程挂起，控制权回到调度器
// 4. 调度器等到定时器到期后，resume 协程
// 5. 协程从 await_resume() 继续执行

/// @brief 协程挂起的时长（强类型包装，避免裸 duration 参数）
struct SleepDuration {
    std::chrono::steady_clock::duration value_;
};

/// @brief 创建一个定时器 awaiter，用法：co_await sleep_for(100ms)
SleepDuration sleep_for(std::chrono::steady_clock::duration dur) {
    return SleepDuration{dur};
}

/// @brief TimerAwaiter：在 await_suspend 中注册定时器
struct TimerAwaiter {
    std::chrono::steady_clock::duration delay_;

    explicit TimerAwaiter(std::chrono::steady_clock::duration delay) : delay_(delay) {}

    // 永远不直接就绪——我们总是需要挂起
    bool await_ready() const noexcept { return false; }

    // 协程被挂起后，将自己注册到调度器的定时器队列
    void await_suspend(std::coroutine_handle<> handle) {
        get_scheduler().schedule_after(delay_, handle);
    }

    // 恢复时不需要返回任何值
    void await_resume() const noexcept {}
};

/// @brief 让 SleepDuration 可以被 co_await
auto operator co_await(SleepDuration sd) {
    return TimerAwaiter{sd.value_};
}

// ============================================================================
// TimerTask：定时器协程的返回类型
// ============================================================================
//
// 这是一个极简的协程返回类型，只关心"什么时候结束"。
// 不需要返回值，不需要 result 存储，只需要正确管理协程句柄的生命周期。

class TimerTask {
  public:
    struct promise_type {
        TimerTask get_return_object() {
            return TimerTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() { return std::suspend_always{}; }

        // final_suspend 必须返回 suspend_always，否则协程帧
        // 会在 final_suspend 返回后被自动销毁——而我们的
        // 调度器还需要访问这个帧来做清理
        auto final_suspend() noexcept { return std::suspend_always{}; }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle_;

    TimerTask(std::coroutine_handle<promise_type> h) : handle_(h) {}
    TimerTask(const TimerTask&) = delete;
    TimerTask& operator=(const TimerTask&) = delete;

    TimerTask(TimerTask&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

    ~TimerTask() {
        if (handle_) {
            handle_.destroy();
        }
    }
};

// ============================================================================
// 演示协程：多个任务并发挂起与按序恢复
// ============================================================================

using namespace std::chrono_literals;

/// @brief 任务 A：先等 100ms，打印，再等 200ms，打印
TimerTask task_a() {
    auto t0 = std::chrono::steady_clock::now();

    std::cout << "[task_a] 启动，等待 100ms\n";
    co_await sleep_for(100ms);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    std::cout << "[task_a] 第一次唤醒 (实际耗时 " << elapsed.count() << "ms)\n";

    std::cout << "[task_a] 再等 200ms\n";
    co_await sleep_for(200ms);
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    std::cout << "[task_a] 第二次唤醒 (总耗时 " << elapsed.count() << "ms)\n";
}

/// @brief 任务 B：等 150ms 后打印
TimerTask task_b() {
    auto t0 = std::chrono::steady_clock::now();

    std::cout << "[task_b] 启动，等待 150ms\n";
    co_await sleep_for(150ms);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    std::cout << "[task_b] 唤醒 (实际耗时 " << elapsed.count() << "ms)\n";
}

/// @brief 任务 C：等 400ms 后打印（最长等待）
TimerTask task_c() {
    auto t0 = std::chrono::steady_clock::now();

    std::cout << "[task_c] 启动，等待 400ms\n";
    co_await sleep_for(400ms);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    std::cout << "[task_c] 唤醒 (实际耗时 " << elapsed.count() << "ms)\n";
}

/// @brief 任务 D：演示链式等待（多步串行）
TimerTask task_d() {
    auto t0 = std::chrono::steady_clock::now();

    std::cout << "[task_d] 启动三步链式等待 (50ms + 50ms + 50ms)\n";

    co_await sleep_for(50ms);
    std::cout << "[task_d] 步骤 1 完成\n";

    co_await sleep_for(50ms);
    std::cout << "[task_d] 步骤 2 完成\n";

    co_await sleep_for(50ms);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    std::cout << "[task_d] 步骤 3 完成 (总耗时 " << elapsed.count() << "ms)\n";
}

// ============================================================================
// main
// ============================================================================

int main() {
    std::cout << "========== 协程定时器调度器演示 ==========\n\n";

    auto& sched = get_scheduler();
    auto total_start = std::chrono::steady_clock::now();

    // 创建所有任务（协程在此处创建但尚未执行，因为 initial_suspend）
    auto ta = task_a();
    auto tb = task_b();
    auto tc = task_c();
    auto td = task_d();

    // 将所有任务注册到调度器
    // 第一次 resume 会触发协程体执行，直到遇到第一个 co_await
    sched.schedule_now(ta.handle_);
    sched.schedule_now(tb.handle_);
    sched.schedule_now(tc.handle_);
    sched.schedule_now(td.handle_);

    std::cout << "[main] 已调度 " << sched.pending_count() << " 个任务，开始运行事件循环\n\n";

    // 运行调度器：按时间顺序恢复所有挂起的协程
    sched.run();

    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - total_start);

    std::cout << "\n[main] 所有任务完成，总耗时 " << total_elapsed.count() << "ms\n";
    std::cout << "[main] 预期总耗时约 400ms "
              << "(由最长的 task_c 决定，其余任务交错执行)\n";

    return 0;
}
