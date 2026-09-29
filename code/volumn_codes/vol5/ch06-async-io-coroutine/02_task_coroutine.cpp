// 02_task_coroutine.cpp —— 卷五 ch06/02《手写 task<T>：惰性任务与对称转移》配套完整代码
//
// 编译：g++ -std=c++20 -Wall -Wextra -pedantic 02_task_coroutine.cpp -o task_coroutine
// 运行：./task_coroutine            （demo A：三层链 + 异常通道）
//       ./task_coroutine 1000000    （demo B：深链栈深实验，参数为链深度）
//
// 环境口径（规范层）：GCC 11 起 -std=c++20 自动启用协程（GCC 10 需 -fcoroutines）；
//                   Clang 用 -std=c++20。本机实测版本见文章「实验回填」处声明。

#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <utility>

// ---------------------------------------------------------------------------
// 日志助手：单线程演示，不需要时间戳，打印顺序即执行顺序
// ---------------------------------------------------------------------------
static void log(const char* msg) { std::printf("%s\n", msg); }

// ---------------------------------------------------------------------------
// FinalAwaiter：final_suspend 专用的awaiter。
// 协程体跑完、return_value/unhandled_exception 收尾之后被 co_await，
// await_suspend 把控制权对称转移给续体（等着这个结果的父协程）。
// 根任务没有续体时，continuation 是 noop_coroutine，resume 立即返回。
// ---------------------------------------------------------------------------
struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }
    template <typename Promise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<Promise> h) noexcept {
        return h.promise().continuation;   // 对称转移：尾调用语义恢复父协程
    }
    void await_resume() const noexcept {}
};

// ---------------------------------------------------------------------------
// Task<T>：惰性协程任务。创建不执行，被 co_await 才启动；
// 值与异常存进 promise，结束后停在 final suspend 点，销毁权归 Task 析构。
// ---------------------------------------------------------------------------
template <typename T>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        T value{};                          // 要求 T 可默认构造
        std::exception_ptr exception;       // 异常通道：先存后抛
        // 续体（continuation）：等这个任务完成的父协程句柄。
        // 存 promise 而不是 Task——co_await 的右值 Task 是临时对象。
        std::coroutine_handle<> continuation = std::noop_coroutine();

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }  // lazy
        FinalAwaiter final_suspend() noexcept { return {}; }           // 必须 noexcept
        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    // Task 自己就是awaiter：父协程 co_await 一个 Task 时的三个函数
    bool await_ready() const noexcept {
        // done() 的前置条件是协程处于挂起态；lazy 任务创建后停在初始挂起点，
        // 满足前置。已完成的任务短路为「不用挂」，避免对 final 点 resume（UB）。
        return !handle_ || handle_.done();
    }
    // 参数是父协程的句柄：promise 类型未知，所以收类型擦除的 coroutine_handle<>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().continuation = awaiting;   // 续体登记：记住谁在等我
        return handle_;   // 对称转移：挂起父，直接启动子
    }
    T await_resume() {
        if (handle_.promise().exception) {           // 先异常后值
            std::rethrow_exception(handle_.promise().exception);
        }
        return std::move(handle_.promise().value);
    }

    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;                     // move 置空，防 double-destroy
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) { handle_.destroy(); }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~Task() {
        // destroy 只对挂起中的协程合法：lazy 启动 + final 挂起保证了这一点
        if (handle_) { handle_.destroy(); }
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    // 非协程上下文（main）驱动根任务用。本篇内层等待全是 Task，
    // 一次 resume 会沿对称转移一路跑到底；接上真调度器后的驱动方式见 ch06/04。
    void start() { handle_.resume(); }

private:
    handle_type handle_;
};

// ---------------------------------------------------------------------------
// Task<void> 特化：return_void 代替 return_value，await_resume 不返回值
// ---------------------------------------------------------------------------
template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr exception;
        std::coroutine_handle<> continuation = std::noop_coroutine();

        Task get_return_object() {
            return Task{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    bool await_ready() const noexcept { return !handle_ || handle_.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().continuation = awaiting;
        return handle_;
    }
    void await_resume() {
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
    }

    explicit Task(handle_type h) noexcept : handle_(h) {}
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) { handle_.destroy(); }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~Task() {
        if (handle_) { handle_.destroy(); }
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    void start() { handle_.resume(); }

private:
    handle_type handle_;
};

// ---------------------------------------------------------------------------
// sync_wait：在普通函数里把一个 Task 跑到底并取结果（异常原样重抛）
// ---------------------------------------------------------------------------
template <typename T>
T sync_wait(Task<T> task) {
    task.start();
    return task.await_resume();   // 复用同一套「先异常后值」
}

inline void sync_wait(Task<void> task) {
    task.start();
    task.await_resume();
}

// ---------------------------------------------------------------------------
// demo A：三层链 main_task -> worker -> co_add，值沿 await_resume 逐层交回
// ---------------------------------------------------------------------------
static Task<int> co_add(int a, int b) {
    log("  co_add: running");
    co_return a + b;   // return_value(3) 存进 promise
}

static Task<void> worker(const char* name, int a, int b) {
    log("  worker: enter");
    int result = co_await co_add(a, b);   // 挂起 worker，对称转移启动 co_add
    std::printf("  worker: %s: %d + %d = %d\n", name, a, b, result);
    co_return;
}

static Task<void> main_task() {
    log("main_task: enter");
    co_await worker("TaskA", 1, 2);
    co_await worker("TaskB", 3, 4);
    co_await worker("TaskC", 5, 6);
    log("main_task: all done");
}

// ---------------------------------------------------------------------------
// demo B：异常通道。子协程抛出，父协程在 co_await 处接住
// ---------------------------------------------------------------------------
static Task<int> failing() {
    log("  failing: about to throw");
    throw std::runtime_error("boom from child coroutine");
    co_return 0;   // 不会执行到，只为让返回类型是 Task<int>
}

static Task<void> exception_parent() {
    log("exception_parent: enter");
    try {
        int v = co_await failing();
        std::printf("exception_parent: got %d (unexpected)\n", v);
    } catch (const std::runtime_error& e) {
        std::printf("exception_parent: caught '%s'\n", e.what());
    }
    log("exception_parent: continue after catch");
    co_return;
}

// ---------------------------------------------------------------------------
// demo C：深链。descend(N) 递归 co_await，用来观察对称转移下的栈行为
// （栈深是否随 N 增长属于实验回填内容，见文章）
// ---------------------------------------------------------------------------
static Task<void> descend(int depth) {
    if (depth > 0) {
        co_await descend(depth - 1);
    }
    co_return;
}

int main(int argc, char** argv) {
    std::printf("=== demo A: three-layer chain ===\n");
    sync_wait(main_task());

    std::printf("\n=== demo B: exception channel ===\n");
    sync_wait(exception_parent());

    if (argc > 1) {
        const int depth = std::atoi(argv[1]);
        std::printf("\n=== demo C: deep chain, depth = %d ===\n", depth);
        sync_wait(descend(depth));
        std::printf("deep chain finished, depth = %d\n", depth);
    }
    return 0;
}
