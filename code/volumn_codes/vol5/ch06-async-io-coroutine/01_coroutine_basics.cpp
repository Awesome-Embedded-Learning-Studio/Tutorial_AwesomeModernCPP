/*
 * C++20 协程基础：从 co_return 到 co_yield 的最小示例
 *
 * 本文件演示 C++20 协程的三个核心概念：
 *   1. 最小 co_return 协程 —— 理解 promise_type 与 coroutine_handle 的关系
 *   2. Generator 协程 —— 用 co_yield 产生惰性求值序列
 *   3. 协程状态机 —— 挂起点(suspend point)与恢复(resume)的实际行为
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -Wall -o /tmp/01_coroutine_basics 01_coroutine_basics.cpp
 *
 * 运行：
 *   /tmp/01_coroutine_basics
 *
 * 编译器要求：GCC 12+ 或 Clang 16+
 */

#include <coroutine>
#include <iostream>
#include <memory>
#include <utility>

// ============================================================================
// 第一部分：最小 co_return 协程
// ============================================================================
//
// 一个 C++20 协程必须满足的最小契约是：返回类型包含一个嵌套的
// promise_type，它定义了协程的初始行为、最终行为以及返回值处理。
// 这里我们展示最简形态——co_return; 什么都不返回，只验证流程。

/// @brief 最小协程返回对象，仅用于演示协程生命周期
struct ReturnObject {
    struct promise_type {
        // co_return 的返回值（这里为 void，什么都不存）
        auto get_return_object() {
            // 从 promise 创建 ReturnObject，内部持有 coroutine_handle
            return ReturnObject{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // 协程首次进入时立即开始执行（不挂起）
        auto initial_suspend() { return std::suspend_never{}; }

        // 协程执行到 co_return 后，挂起在最终点
        // 如果返回 suspend_never，协程帧会在 co_return 后立即销毁
        // 我们返回 suspend_always 以便手动控制销毁时机
        auto final_suspend() noexcept { return std::suspend_always{}; }

        // co_return; 的处理
        void return_void() {}

        // 协程内抛出未捕获异常时调用
        void unhandled_exception() { throw; }
    };

    // 持有协程句柄，用于手动 resume / destroy
    std::coroutine_handle<promise_type> handle_;

    // 移动语义：协程句柄是唯一的，不可复制
    ReturnObject(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ReturnObject(const ReturnObject&) = delete;
    ReturnObject& operator=(const ReturnObject&) = delete;
    ReturnObject(ReturnObject&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    ReturnObject& operator=(ReturnObject&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~ReturnObject() {
        // RAII：析构时销毁协程帧（如果还活着的话）
        if (handle_) {
            handle_.destroy();
        }
    }
};

/// @brief 最简单的协程：打印一行消息后 co_return
ReturnObject minimal_coroutine() {
    std::cout << "[minimal_coroutine] 协程体开始执行\n";
    co_return; // co_return 触发 return_void()，然后进入 final_suspend
    std::cout << "[minimal_coroutine] 这行永远不会执行\n";
}

// ============================================================================
// 第二部分：Generator——用 co_yield 产生惰性序列
// ============================================================================
//
// Generator 是协程最经典的应用之一。调用者通过迭代器按需索取下一个值，
// 协程在每次 co_yield 处挂起，直到被再次 resume。这实现了惰性求值：
// 值只有在被请求时才计算，而且可以表示无限序列。

/// @brief 惰性生成器：按需产生 int 值序列，支持 range-for
struct Generator {
    struct promise_type {
        int current_value_ = 0;

        auto get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // 初始挂起：协程在第一次 co_yield 前暂停，等调用者来 resume
        // 这样调用者可以先拿到迭代器，再开始求值
        auto initial_suspend() { return std::suspend_always{}; }

        auto final_suspend() noexcept { return std::suspend_always{}; }

        // co_yield expr → 将值存入 promise，然后挂起
        auto yield_value(int value) {
            current_value_ = value;
            return std::suspend_always{};
        }

        void return_void() {}

        void unhandled_exception() { throw; }
    };

    // 迭代器：充当调用者与协程之间的桥梁
    struct Iterator {
        std::coroutine_handle<promise_type> handle_;

        Iterator(std::coroutine_handle<promise_type> h) : handle_(h) {}

        // 解引用：从 promise 中取出当前 yield 的值
        int operator*() const { return handle_.promise().current_value_; }

        // 前进到下一个 yield 点
        Iterator& operator++() {
            handle_.resume();
            return *this;
        }

        // 序列结束条件：协程已经执行完毕（到达 final_suspend）
        bool operator!=(std::default_sentinel_t) const { return !handle_.done(); }
    };

    std::coroutine_handle<promise_type> handle_;

    Generator(std::coroutine_handle<promise_type> h) : handle_(h) {}
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    Generator(Generator&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

    ~Generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    Iterator begin() {
        // 第一次 resume：从 initial_suspend 恢复，执行到第一个 co_yield
        handle_.resume();
        return Iterator{handle_};
    }

    std::default_sentinel_t end() { return std::default_sentinel; }
};

/// @brief 生成 [start, end) 范围内的整数序列
Generator iota(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

/// @brief 生成斐波那契数列的前 n 个数
Generator fibonacci(int count) {
    int a = 0, b = 1;
    for (int i = 0; i < count; ++i) {
        co_yield a;
        int tmp = a;
        a = b;
        b = tmp + b;
    }
}

// ============================================================================
// 第三部分：演示协程的状态机行为
// ============================================================================
//
// 协程本质上是一个可以被挂起和恢复的函数。每次 co_await / co_yield
// 都是一个挂起点，协程在这些点保存局部变量和执行位置到协程帧
// (coroutine frame) 中，然后返回给调用者。再次 resume 时从上次
// 挂起的位置继续执行。我们来手动操作句柄，观察这个过程。

/// @brief 演示手动 resume 的协程返回对象
struct StepByStep {
    struct promise_type {
        auto get_return_object() {
            return StepByStep{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        auto initial_suspend() { return std::suspend_always{}; }
        auto final_suspend() noexcept { return std::suspend_always{}; }
        auto yield_value(int value) {
            current_value_ = value;
            return std::suspend_always{};
        }
        void return_void() {}
        void unhandled_exception() { throw; }

        int current_value_ = 0;
    };

    std::coroutine_handle<promise_type> handle_;

    StepByStep(std::coroutine_handle<promise_type> h) : handle_(h) {}
    StepByStep(const StepByStep&) = delete;
    StepByStep& operator=(const StepByStep&) = delete;
    StepByStep(StepByStep&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    ~StepByStep() {
        if (handle_) {
            handle_.destroy();
        }
    }
};

/// @brief 一个分三步执行的协程，每步 yield 一个不同的值
StepByStep three_step_coroutine() {
    std::cout << "  [协程] 第一步执行，即将 yield 10\n";
    co_yield 10;

    std::cout << "  [协程] 第二步执行，即将 yield 20\n";
    co_yield 20;

    std::cout << "  [协程] 第三步执行，即将 yield 30\n";
    co_yield 30;

    std::cout << "  [协程] 执行完毕\n";
}

// ============================================================================
// main：串联上述所有演示
// ============================================================================

int main() {
    // ------------------------------------------------------------------
    // 演示 1：最小 co_return 协程
    // ------------------------------------------------------------------
    std::cout << "=== 演示 1：最小 co_return 协程 ===\n";
    {
        // 注意：因为 initial_suspend 返回 suspend_never，
        // 协程体在调用 minimal_coroutine() 时就开始执行了
        auto obj = minimal_coroutine();
        std::cout << "[main] minimal_coroutine 返回后，协程已经执行完\n";
        // obj 析构时 destroy 协程帧
    }
    std::cout << '\n';

    // ------------------------------------------------------------------
    // 演示 2：Generator 惰性序列
    // ------------------------------------------------------------------
    std::cout << "=== 演示 2：Generator 惰性序列 ===\n";
    {
        // iota(1, 6) 创建协程，但 initial_suspend 让它暂停在入口
        auto gen = iota(1, 6);
        std::cout << "[main] iota(1, 6) 创建完毕，尚未执行协程体\n";

        // range-for 会调用 begin()（触发第一次 resume）
        // 和 ++iterator（触发后续 resume）
        std::cout << "[main] 遍历 iota 结果：";
        for (int val : gen) {
            std::cout << val << ' ';
        }
        std::cout << '\n';
    }
    std::cout << '\n';

    // ------------------------------------------------------------------
    // 演示 3：斐波那契 Generator
    // ------------------------------------------------------------------
    std::cout << "=== 演示 3：斐波那契数列 ===\n";
    {
        auto fib = fibonacci(12);
        std::cout << "[main] 前 12 个斐波那契数：";
        for (int val : fib) {
            std::cout << val << ' ';
        }
        std::cout << '\n';
    }
    std::cout << '\n';

    // ------------------------------------------------------------------
    // 演示 4：手动 resume 观察状态机
    // ------------------------------------------------------------------
    std::cout << "=== 演示 4：手动 resume 观察状态机 ===\n";
    {
        auto coro = three_step_coroutine();
        std::cout << "[main] 协程已创建，initial_suspend 已挂起\n";

        // 第一次 resume：从 initial_suspend 恢复，执行到第一个 co_yield
        std::cout << "[main] 第一次 resume\n";
        coro.handle_.resume();
        std::cout << "[main] 获取值: " << coro.handle_.promise().current_value_ << "\n";

        // 第二次 resume
        std::cout << "[main] 第二次 resume\n";
        coro.handle_.resume();
        std::cout << "[main] 获取值: " << coro.handle_.promise().current_value_ << "\n";

        // 第三次 resume
        std::cout << "[main] 第三次 resume\n";
        coro.handle_.resume();
        std::cout << "[main] 获取值: " << coro.handle_.promise().current_value_ << "\n";

        // 第四次 resume：协程执行完毕，到达 final_suspend
        std::cout << "[main] 第四次 resume（协程将执行完毕）\n";
        coro.handle_.resume();
        std::cout << "[main] 协程 done? " << coro.handle_.done() << "\n";
    }

    return 0;
}
