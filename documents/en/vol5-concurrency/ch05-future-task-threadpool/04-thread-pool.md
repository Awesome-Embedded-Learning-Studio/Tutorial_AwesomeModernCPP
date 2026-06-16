---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Starting from a worker, task queue, and condition variable, we will build
  a thread pool that supports future returns, exception propagation, and graceful
  shutdown.
difficulty: advanced
order: 4
platform: host
prerequisites:
- jthread 与停止令牌
- promise 与 packaged_task
reading_time_minutes: 34
related:
- 线程安全队列
- std::async 与 future
tags:
- host
- cpp-modern
- advanced
- 异步编程
- mutex
title: Thread Pool Design
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/04-thread-pool.md
  source_hash: f0ffd468a2d5f7b5d74903d2a1ece77f7e1844b0c6d2fce7241f9e56885b6ff1
  translated_at: '2026-06-16T06:20:13.972188+00:00'
  engine: anthropic
  token_count: 6904
---
# Thread Pool Design

In the previous few articles, we broke down the async infrastructure components—`std::async`, `std::future`, `std::promise`, and `std::packaged_task`—one by one. We also built a single-threaded `SimpleTaskQueue` at the end of the `packaged_task` article as a teaser. While that rudimentary queue worked, it had only one worker thread. To be honest, submitting four tasks just to have them run one by one in a queue offers no parallelism; it's not fundamentally different from calling them directly in the main thread.

Now, we will expand that single-worker queue into a proper thread pool: a group of pre-created worker threads sharing a task queue, concurrently fetching and executing tasks. The thread pool is one of the most commonly used concurrency patterns in production environments. It avoids the system overhead of frequently creating and destroying threads, allows us to control the concurrency level (the number of threads), and, when combined with `packaged_task` / `future`, cleanly propagates results and exceptions back to the submitter.

In this article, we will build a fully functional thread pool from scratch, adding capabilities step by step. Specifically, we will go through several stages: first, we will build a minimal skeleton with just `enqueue()` to get multiple workers running; then we will add `submit()` to return a `future`, allowing the caller to get the result; next, we will handle exception propagation across threads; then we will design a graceful shutdown sequence—stopping accepting new tasks, draining the queue, and joining all workers; finally, we will look at how C++20's `jthread` + `stop_token` can simplify the shutdown logic.

## Step 1: A Minimal Viable Thread Pool

Let's not rush into fancy features like `submit` returning a `future` or exception propagation just yet—let's build the core skeleton first. A functional thread pool actually has a very classic structure: N worker threads share a task queue, the queue is protected by `std::mutex`, and `std::condition_variable` is used to notify workers that new tasks have arrived. It's that simple.

> **Environment Note**: All code in this article is based on C++17 (gcc 12+ / clang 15+ / MSVC 19.34+) and tested on x86-64 Linux and macOS. The C++20 refactoring in the final step requires a compiler supporting `<stop_token>` (gcc 10+ / clang 17+ (libc++ has partial support, Clang 20 has full support) / MSVC 19.28+).

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            w.join();
        }
    }

    void enqueue(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_loop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
};
```

This structure is the prototype for almost all C++ thread pools. Let's break down its core components to understand exactly what each part does.

`workers_` is a collection of pre-allocated `std::thread` objects, created in a loop within the constructor. Each thread executes the same `worker_loop()`. The number of threads is typically determined by `std::thread::hardware_concurrency()`, or manually specified based on your task characteristics. For CPU-intensive tasks, matching the thread count to the core count is usually sufficient; adding more threads can actually degrade performance due to context switching overhead. For I/O-intensive tasks, we can use a few more threads, since they often wait on I/O, leaving the CPU free to execute other threads.

`tasks_` is a `std::queue<std::function<void()>>`—all tasks are type-erased into `std::function<void()>` and pushed into this queue. Whether we submit a function returning `int`, a lambda returning `std::string`, or a function object returning nothing, they all share the `void()` signature once inside the queue. How we unify callable objects with different signatures into `void()` while preserving return values is a problem we will solve next.

`mutex_` and `cv_` are the core of the thread pool synchronization. `mutex_` protects the `tasks_` queue and the `stop_` flag, ensuring that only one thread manipulates the queue at any given moment. `cv_` is used to notify workers: that a new task has arrived (`notify_one`) or that it is time to stop (`notify_all`).

The `stop_` flag controls the shutdown sequence. When the destructor sets `stop_ = true` and calls `notify_all()`, all workers are woken up. Note that the exit condition for a worker is not simply "exit immediately when `stop_` is true," but rather "`stop_` is true **and** the queue is empty"—this guarantees that submitted but unexecuted tasks are not discarded.

We verify that it runs with a simple test code:

```cpp
#include <iostream>
#include <chrono>

int main()
{
    ThreadPool pool(4);

    for (int i = 0; i < 8; ++i) {
        pool.enqueue([i] {
            std::cout << "任务 " << i << " 在线程 "
                      << std::this_thread::get_id() << " 上执行\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    }

    // 析构时等待所有任务完成
    return 0;
}
```

You will see eight tasks distributed across four threads. The first four start almost simultaneously, while the next four run immediately after the previous batch completes.

Excellent, the framework is now in place. However, this version has a significant flaw: `enqueue()` returns nothing. You submit a task, the task executes, but you cannot retrieve the result—which is quite awkward. If the task throws an exception, things get even messier: the exception will be swallowed by the `std::function<void()>` invocation. The exact behavior depends on the implementation, but it typically involves calling `std::terminate` to abort the program immediately. Let's fix this next.

## Step 2: submit() returns a future

In the previous article, we demonstrated how to return a future using `packaged_task` with `shared_ptr` within `SimpleTaskQueue`. The thread pool requires the same pattern, except that now multiple workers fetch tasks from the queue simultaneously—but that's fine. `packaged_task` is inherently thread-safe (the shared state is set only once), provided we don't invoke the same `packaged_task` instance in multiple threads at the same time.

Our goal is to provide a `submit()` template function that accepts any callable object and its arguments, returning a `std::future<R>`, where `R` is the return type of the callable. The caller can use this future to `get()` the result, or capture the exception if one occurs.

```cpp
template <typename F, typename... Args>
auto submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> fut = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("线程池已停止，无法提交新任务");
        }
        tasks_.push([task]() { (*task)(); });
    }
    cv_.notify_one();

    return fut;
}
```

There are several key points in this code that are worth discussing in detail, as each represents a crucial detail often realized only after learning things the hard way.

`std::invoke_result_t<F, Args...>` is a type trait introduced in C++17 used to deduce the return type of `F(Args...)`. It is more general than C++11's `std::result_of`—it correctly handles member function pointers, function objects with reference qualifiers, and other cases. `ReturnType` is the task's return type, which determines the signature of the `packaged_task` and the template parameter of the `future`.

`std::make_shared<std::packaged_task<ReturnType()>>` binds the callable object and arguments together, wrapping them into a `packaged_task` with the signature `ReturnType()`. Here we use `std::bind` to pre-bind the arguments—since the queue stores `std::function<void()>`, which accepts no parameters, we need to bind the arguments to the callable object to form a parameter-free callable entity.

Then we wrap the `packaged_task` in a `shared_ptr`. This step is critical and is where many beginners get stuck—because `std::function<void()>` requires the callable object to be copyable, while `std::packaged_task` is move-only and cannot be directly inserted into `std::function`. By wrapping it in a `shared_ptr`, the lambda captures a `shared_ptr` (which is copyable), while the `packaged_task` itself remains a single instance managed by the `shared_ptr`. This technique is standard practice in thread pool implementations—you will see it in almost every serious C++ thread pool implementation.

`tasks_.push([task]() { (*task)(); })` pushes a lambda into the queue. This lambda captures the `shared_ptr<packaged_task<R()>>`, dereferences it, and executes the `packaged_task` when called. Once the `packaged_task` is invoked, the internal promise automatically sets the return value or stores the exception, causing the `future` held by the caller to become ready.

One more detail requires attention: we check `stop_` before pushing the task. If the thread pool has already entered a shutdown state, it should not accept new tasks, and an exception is thrown directly. This prevents undefined behavior caused by submitting tasks during the shutdown process—think about it, you certainly wouldn't want your task to be pushed into the queue only to discover that the worker threads have all exited, leaving the task forever unexecuted.

Let's look at a complete usage example for `submit`:

```cpp
#include <iostream>
#include <string>

int compute(int x)
{
    return x * x;
}

int main()
{
    ThreadPool pool(4);

    auto f1 = pool.submit(compute, 5);
    auto f2 = pool.submit(compute, 10);
    auto f3 = pool.submit([]() -> std::string {
        return "hello from thread pool";
    });

    std::cout << "f1: " << f1.get() << "\n";  // 25
    std::cout << "f2: " << f2.get() << "\n";  // 100
    std::cout << "f3: " << f3.get() << "\n";  // hello from thread pool
    return 0;
}
```

Three tasks are submitted to the pool and executed in parallel by different worker threads. The compiler automatically deduces the `future` type returned by `submit()`—`f1` and `f2` are `std::future<int>`, and `f3` is `std::future<std::string>`.

## Step 3: Exception Propagation

Exception handling in asynchronous programming is a pitfall-ridden area where I have stumbled more than once. If your task throws an exception in a worker thread but you fail to handle it correctly, the exception will be lost. The worker thread will not crash (because the exception is captured by the `std::function` invocation mechanism), but you will never receive the result, and the program will exhibit a baffling "silent failure." This type of bug is even harder to debug than a direct crash—at least a crash provides a stack trace.

Fortunately, `packaged_task` handles this for us. When the wrapped function throws an exception, `packaged_task` captures it internally using `std::current_exception()` and stores it in the shared state. When the caller retrieves the result via `future.get()`, if the shared state contains an exception, `get()` rethrows it. This process is transparent to the caller—you simply need to wrap the `get()` call in a try-catch block.

Let's verify this with an example:

```cpp
#include <iostream>
#include <stdexcept>

int risky_task(int x)
{
    if (x < 0) {
        throw std::invalid_argument("参数不能为负数");
    }
    return x * x;
}

int main()
{
    ThreadPool pool(2);

    // 正常路径
    auto f1 = pool.submit(risky_task, 5);
    try {
        std::cout << "结果: " << f1.get() << "\n";  // 25
    } catch (const std::exception& e) {
        std::cout << "异常（不该走到这里）: " << e.what() << "\n";
    }

    // 异常路径
    auto f2 = pool.submit(risky_task, -3);
    try {
        std::cout << "结果: " << f2.get() << "\n";  // 不会执行到
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";  // 参数不能为负数
    }

    return 0;
}
```

Exceptions propagate from the worker thread to the main thread with type information intact. We do not need to design an error code system, serialize exception messages into strings, or implement a global error callback—the combination of `packaged_task` and `future` encapsulates cross-thread exception propagation cleanly. This is truly remarkable: the C++ exception mechanism is inherently guided by stack unwinding, making it naturally suited for synchronous calls. Cross-thread exception propagation is normally troublesome, but `packaged_task` internally captures and stores the result via `std::current_exception()`. When the caller invokes `future.get()`, the exception is rethrown, making the process feel exactly like handling a synchronous exception.

However, there is a real pitfall here: if you submit a task but never call `future.get()`, the exception is silently swallowed. This differs from the `future` returned by `std::async`—the destructor of a `std::async` future blocks to wait for task completion, whereas the destructor of a `future` associated with a `packaged_task` simply releases the reference to the shared state without waiting. Therefore, **for a `future` obtained from the thread pool's `submit()`, we must either call `get()` or at least call `wait()` to confirm task completion**. Don't let the exceptions get lost.

## Step 4: Graceful Shutdown

Shutting down a thread pool sounds simple—just let the worker threads exit. However, the challenge lies in the shutdown sequence. The queue might still contain pending tasks, and currently executing tasks might not be finished. If we brutally terminate all workers (for example, by detaching or terminating), submitted tasks are lost, and active tasks might be left in a half-finished state—imagine a thread in the middle of writing to a file being killed, and you will understand how disastrous this can be.

A "graceful" shutdown sequence should look like this: first, stop accepting new tasks (have `submit()` throw an exception or return an error); second, let worker threads finish executing all remaining tasks in the queue; and finally, have all worker threads exit normally so the destructor can join them.

Let's return to the exit condition in `worker_loop()`:

```cpp
cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
if (stop_ && tasks_.empty()) {
    return;
}
```

The meaning of this condition is: after the worker is woken up, if `stop_` is true and the queue is empty, it exits. If `stop_` is true but there are still tasks in the queue, the worker will continue to fetch and execute the remaining tasks until the queue is empty before exiting. This embodies the "drain the queue" semantics—we do not drop tasks, we simply stop accepting new ones.

Let's review the shutdown sequence in the destructor:

```cpp
~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        w.join();
    }
}
```

There are a few critical timing details we need to clarify.

Setting `stop_` must be done while holding the lock. Although reads and writes to `stop_` only occur after acquiring the lock—meaning `atomic` isn't strictly theoretically necessary—placing the modification within the lock's protection makes the code's intent clearer. "Modifying shared state requires holding the lock" is a fundamental discipline of concurrent programming, so we shouldn't skip the lock here.

`notify_all()` is called *after* releasing the lock. This isn't mandatory—the standard allows notification while holding the lock—but notifying after releasing is a common optimization. If worker threads need to acquire the same lock immediately upon waking (which they do), releasing the lock before the wake-up call avoids the useless context switch of "wake up -> fail to acquire lock -> block again."

`join()` must happen *after* `notify_all()`. If we join before notifying, the workers will never receive the stop signal, and `join()` will block forever—resulting in a deadlock. The order must be: notify first, then wait.

This shutdown mechanism provides an implicit guarantee: when the destructor returns, all submitted tasks are guaranteed to be complete. Since `join()` blocks until the worker threads exit, and the worker threads only exit when the queue is empty, this is critical for resource cleanup. We won't have background threads accessing destroyed objects after the destructor finishes.

## Step 5: C++20 Upgrade — `jthread` + `stop_token`

So far, our thread pool uses `std::thread` combined with a manual `stop_` flag, manual `notify_all()`, and manual `join()`. Honestly, this combination works, but it is verbose to write. We have to remember to set the flag, notify, and join every time; missing a single step leads to deadlocks or resource leaks. C++20 introduced `std::jthread`, `std::stop_token`, and `std::stop_source`. Combined with `std::condition_variable_any`'s support for `stop_token`, we can significantly simplify the shutdown logic.

First, an important detail—one that many tutorials get wrong: `std::condition_variable` (not `_any`) **does not** have a C++20 `stop_token` overload. The wait integration for `stop_token` is provided only on `std::condition_variable_any`. The reason is that `std::condition_variable` only supports specific lock types like `std::unique_lock<std::mutex>`, whereas `std::condition_variable_any` is a templated class that supports any lock type satisfying the *BasicLockable* requirement. This templated design makes the `stop_token` integration more natural. If you try to call `wait(lock, stop_token, predicate)` with `std::condition_variable`, the compiler will error out—don't ask me how I know.

Here is what the thread pool looks like refactored with `jthread` + `stop_token`:

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <stop_token>
#include <mutex>
#include <condition_variable>  // condition_variable_any 也在这个头文件
#include <functional>
#include <future>
#include <memory>
#include <type_traits>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) {
                worker_loop(st);
            });
        }
    }

    ~ThreadPool()
    {
        // 请求所有 jthread 停止
        for (auto& w : workers_) {
            w.request_stop();
        }
        cv_any_.notify_all();
        // jthread 析构时自动 join，不需要手动 join
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested()) {
                throw std::runtime_error("线程池已停止，无法提交新任务");
            }
            tasks_.push([task]() { (*task)(); });
        }
        cv_any_.notify_one();

        return fut;
    }

private:
    bool stop_requested() const
    {
        // 如果任意 jthread 已经被请求停止，就认为池在关闭中
        return !workers_.empty() && workers_[0].get_stop_source().stop_requested();
    }

    void worker_loop(std::stop_token st)
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // 使用 condition_variable_any 的 stop_token 重载
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    // stop 被请求了，检查队列是否还有任务
                    if (tasks_.empty()) {
                        return;
                    }
                    // 还有剩余任务，继续执行
                }
                if (tasks_.empty()) {
                    continue;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable_any cv_any_;
};
```

Next, let's examine the key differences between this version and the previous one.

First, the worker thread has been changed to `std::jthread`. The `jthread` constructor accepts a callable object that takes a `std::stop_token` as its first argument. It automatically creates an internal `std::stop_source` and passes the corresponding `stop_token` to your function. We no longer need to maintain the `stop_` flag manually—the lifecycle of this flag is handled internally by `jthread`.

Second, the conditional wait now uses the `stop_token` overload of `std::condition_variable_any`. The signature of this overload is `wait(lock, stop_token, predicate)`. Its behavior is as follows: if the predicate is true, it returns true immediately; if a stop is requested, it also returns immediately, but the return value is the current value of the predicate (usually false). This replaces the manual logic for checking the `stop_` flag—when `request_stop()` is called, `cv_any_.wait()` is automatically woken up, eliminating the need to manually call `notify_all()` in the destructor.

The third point is that the destructor is now more concise. When a `jthread` is destroyed, it automatically calls `request_stop()` followed by `join()`. We could even omit the explicit destructor, but we have retained it because we need to call `notify_all()` before stopping to wake up any workers that might be waiting.

However, to be honest, this version has one slightly inelegant aspect—the implementation of `stop_requested()` relies on checking the `stop_source` of `workers_[0]`. This would cause issues if `workers_` were empty (although the constructor guarantees at least one worker, relying on this implicit assumption is always uncomfortable). A cleaner approach is for the thread pool to hold its own `std::stop_source` and pass the associated `stop_token` to each worker. The code is slightly more complex, but the semantics are clearer. Let's look at this improved version:

```cpp
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
        : stop_source_()
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            auto st = stop_source_.get_token();
            workers_.push_back(std::jthread([this, st] {
                worker_loop(st);
            }));
        }
    }

    ~ThreadPool()
    {
        stop_source_.request_stop();
        cv_any_.notify_all();
        // jthread 在 vector 析构时自动 join
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_source_.stop_requested()) {
                throw std::runtime_error("线程池已停止，无法提交新任务");
            }
            tasks_.push([task]() { (*task)(); });
        }
        cv_any_.notify_one();

        return fut;
    }

private:
    void worker_loop(std::stop_token st)
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    // stop 被请求了
                    if (tasks_.empty()) {
                        return;
                    }
                    // 还有剩余任务，继续执行完后退出
                }
                if (tasks_.empty()) {
                    continue;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable_any cv_any_;
    std::stop_source stop_source_;
};
```

In this version, we use the `stop_source_` held by the thread pool to manage the stop state. Inside `submit()`, we check `stop_source_.stop_requested()` to determine if the pool is still running. The destructor calls `stop_source_.request_stop()` to initiate shutdown. Each worker thread obtains the same `stop_token` via `stop_source_.get_token()`—when `request_stop()` is called, all waiting operations holding this token are woken up.

Note a subtle point here: we pass the `stop_token` to the worker threads via lambda capture, rather than relying on `jthread`'s automatic argument passing mechanism. This is because the `stop_token` automatically created by `jthread` is associated with that specific `jthread`'s internal `stop_source`—calling `request_stop()` on a specific `jthread` only cancels that particular thread. We want a single `request_stop()` call to cancel all workers. Therefore, we need a shared `stop_source` and distribute its `stop_token` to all workers.

While this version is semantically clean, there is an architectural issue to be aware of: the `stop_source` built into `jthread` and our manually created `stop_source_` are two independent sources. When a `jthread` is destroyed, it calls `request_stop()` on its own built-in `stop_source`, but our `worker_loop` listens to the one we created manually. This means the `jthread`'s native stop mechanism is effectively disconnected from our worker threads—calling `workers_[i].request_stop()` won't wake up that worker, because `worker_loop` isn't listening to the `jthread`'s `stop_token`.

This also implies that our explicit destructor is mandatory, not optional. If we relied on the default destructor, members would be destroyed in reverse order of declaration: `stop_source_` and `cv_any_` would be destroyed before `workers_`. When the `jthread`s in `workers_` are destroyed, they call their own `request_stop()`, which fails to reach our `worker_loop`—resulting in `join()` blocking forever and causing a deadlock. The explicit destructor first calls `stop_source_.request_stop()` + `cv_any_.notify_all()` to ensure worker threads exit, so that the subsequent `join()` during `jthread` destruction can return successfully.

You might wonder: does moving `jthread` objects during vector reallocation cause problems? The answer is no—after a `jthread` is moved, the source object's `joinable()` becomes `false`, so its destructor skips `request_stop()` and `join()`. The thread ownership has transferred to the new `jthread` object and remains unaffected.

At this point, you will realize that while C++20's `stop_token` mechanism is useful, its interaction with a thread pool isn't as simple as one might imagine—the `stop_source` automatically managed by `jthread` and our manually created `stop_source_` operate independently, requiring us to manually coordinate their timing in the destructor.

My suggestion is: if your project is still on C++17 or earlier, using `std::thread` with a manual `stop_` flag is perfectly fine. Don't introduce unnecessary complexity just to use new features. The combination of thread + mutex + condition_variable established in the C++11 era has been battle-tested for over a decade; the probability of bugs is far lower than when wrestling with C++20 novelties. If you have fully adopted C++20, and `jthread`/`stop_source` are already widely used in your project, then using them to manage the thread pool's stop state is reasonable, but you must strictly handle the "two `stop_source`" issue mentioned above.

Below is a complete, battle-tested C++17 version. It does not rely on C++20's `jthread` or `stop_token`, but offers a clear structure and complete functionality:

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <stdexcept>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    // 禁止拷贝和移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) {
                throw std::runtime_error("线程池已停止，无法提交新任务");
            }
            tasks_.push([task]() { (*task)(); });
        }
        cv_.notify_one();

        return fut;
    }

private:
    void worker_loop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
};
```

We also address several common pitfalls here. We disable copying and moving—the thread pool holds `std::thread` and `std::mutex`, both of which are not copyable, and the life cycle management of the thread pool should not be disrupted by move operations (imagine the chaos if the original thread pool's destructor joins threads that no longer belong to it after a move). We check `joinable()` before joining in the destructor—although threads are normally joinable, defensive programming is always good; what if someone joined them without your knowledge?

## Worker Thread Life Cycle

The worker threads in a thread pool essentially cycle through three states: idle waiting, executing tasks, and shutting down. Understanding this life cycle is crucial for troubleshooting thread pool issues—most bugs related to "tasks not executing" or "thread pool getting stuck" can be traced back to these state transitions.

In the constructor, each worker thread enters `worker_loop()` immediately after creation. Since the queue is empty at this point, the worker blocks on `cv_.wait()`, entering the idle waiting state. This blocking is efficient—the operating system suspends the thread, consuming no CPU time slices, until `cv_.notify_one()` or `cv_.notify_all()` wakes it up.

When `submit()` pushes a task and calls `cv_.notify_one()`, one (and only one) waiting worker is woken up. It retrieves a task from the queue, releases the lock, and executes the task outside the lock. Executing outside the lock is a critical design decision—if the task were executed while holding the lock, other worker threads and `submit()` calls would be blocked, causing the entire thread pool to degrade into serial execution, defeating the purpose of multithreading. After the task completes, the worker returns to the top of the loop, reacquires the lock, and checks the queue. If the queue is empty, it blocks on `wait()` again; if tasks remain, it retrieves and executes one immediately without waiting—this behavior of "actively checking the queue after finishing a task" avoids unnecessary notification overhead.

The shutdown path is triggered in the destructor: it sets `stop_ = true` and calls `cv_.notify_all()`. All workers are woken up and check `stop_ && tasks_.empty()`. If the queue is empty, the worker exits the loop normally, and the thread terminates. If tasks remain in the queue, the worker continues to execute until the queue is cleared before exiting.

You might ask: what happens if a worker is executing a long-running task when the destructor is called? The answer is—that worker will not respond to the stop request immediately. It will continue executing the current task until it returns to the top of the loop, where it checks the `stop_` flag. Therefore, **if your tasks may run for a long time, the thread pool's destructor might block for a long time**. This is not a bug; it is the cost of a graceful shutdown—you either wait for it to finish, or use a more aggressive approach (like `timed_wait` with `detach` as a fallback), but a detached thread might access destroyed objects, which is a risky trade-off.

## A Complete Practical Example

Now let's combine all the capabilities we have discussed and write a comprehensive example: we will parallelize the processing of a dataset. The processing function might throw exceptions, so we need to handle both successful results and exceptions correctly. This example simulates a common scenario in production environments—batch processing a set of data where some items might be problematic and cause failures, and we need to identify which ones succeeded and which ones failed.

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <stdexcept>

// 模拟一个可能失败的处理函数
double process_data(int id, double value)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (value < 0) {
        throw std::runtime_error(
            "数据 " + std::to_string(id) + " 无效: 值为负数");
    }

    // 模拟计算
    return value * value + std::sqrt(value);
}

int main()
{
    ThreadPool pool(4);

    std::vector<double> inputs = {1.0, 4.0, -2.0, 9.0, 16.0, -5.0, 25.0, 36.0};
    std::vector<std::future<double>> futures;

    // 提交所有任务
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        futures.push_back(
            pool.submit(process_data, static_cast<int>(i), inputs[i]));
    }

    // 收集结果
    int success_count = 0;
    int fail_count = 0;

    for (std::size_t i = 0; i < futures.size(); ++i) {
        try {
            double result = futures[i].get();
            std::cout << "数据 " << i << " (" << inputs[i]
                      << ") -> 结果: " << result << "\n";
            ++success_count;
        } catch (const std::runtime_error& e) {
            std::cout << "数据 " << i << " (" << inputs[i]
                      << ") -> 失败: " << e.what() << "\n";
            ++fail_count;
        }
    }

    std::cout << "\n总计: " << success_count << " 成功, "
              << fail_count << " 失败\n";
    return 0;
}
```

This code demonstrates a typical use case for a thread pool in a real-world scenario: submitting a batch of tasks and collecting the results one by one. You will notice that the usage feels very similar to synchronous code—the only difference is that the tasks execute in parallel in the background, while you retrieve the results via `future.get()`. Exceptions are automatically propagated through the future, allowing the caller to handle asynchronous exceptions just like synchronous ones.

## Common Pitfalls in Practice

At this point, we have implemented the core functionality of the thread pool. However, there are several common pitfalls in actual usage that are worth mentioning individually. I have encountered these pitfalls personally, so I hope this helps you avoid the same detours.

First, let's discuss the issue with `std::bind` and passing by reference. We used `std::bind` inside `submit()` to bind arguments, but `std::bind` stores arguments by value by default. If your argument is a large object, it will be copied. If you want to pass by reference, you need to wrap the argument with `std::ref()` or `std::cref()`. A better approach is to use a lambda expression directly instead of `std::bind`. The lambda capture list allows you to precisely control whether each argument is passed by value or by reference, and the code is usually more readable than `std::bind`. If you want to replace `std::bind` with a lambda, the implementation of `submit` can be simplified to this:

```cpp
template <typename F>
auto submit(F&& f) -> std::future<std::invoke_result_t<F>>
{
    using ReturnType = std::invoke_result_t<F>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::forward<F>(f));

    std::future<ReturnType> fut = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("线程池已停止，无法提交新任务");
        }
        tasks_.push([task]() { (*task)(); });
    }
    cv_.notify_one();

    return fut;
}
```

Callers can bind arguments and references themselves within the lambda expression:

```cpp
std::string large_data = "...";
auto fut = pool.submit([&large_data, x, y] {
    return process(large_data, x, y);
});
```

This approach is much more flexible than `std::bind`, and the lifetime relationships are clear at the call site—capturing a reference implies the caller must ensure `large_data` remains valid until the task completes. This is a golden rule in asynchronous programming; no tool can help you bypass it.

Now, let's discuss the issue of future leakage. If you submit a task but never call `get()` or `wait()`, you won't receive any error message—the task might silently complete in the background, or it might throw an exception that gets swallowed, leaving you completely unaware. A defensive approach is to document clearly in `submit` that "every future must be consumed," or to track the number of unconsumed futures in debug mode. I learned this the hard way in a project: a future from a background task was ignored, and the exception within the task vanished without a trace. It took a long time to track down the root cause.

Finally, the most insidious issue: a mismatch between the lifetime of the thread pool and the objects referenced by tasks. If your task captures a reference to a stack variable, and the thread pool is destroyed after the stack variable goes out of scope (for instance, if the thread pool is global or static), you face the risk of a dangling reference. The root cause lies not in the thread pool itself, but in the fundamental question of "who guarantees whose lifetime" in asynchronous programming. Since the execution time of an asynchronous task is non-deterministic, all external references you capture must remain valid for the entire possible execution duration of the task. There is no perfect solution; we can only say that you must consider this issue when designing the API, preferring value captures or `shared_ptr` to extend object lifetimes.

## Exercises

If you want to truly internalize the concepts from this article, these three exercises are worth trying. They extend our thread pool in the directions of priority scheduling, timed shutdown, and work stealing, respectively—each representing a common requirement in production environments.

### Exercise 1: Priority Thread Pool

Add priority support to the thread pool's task queue. Replace `std::queue` with `std::priority_queue` and extend the task type to a pair containing a priority and a callable object. Allow priority specification during submission, and have worker threads always execute the highest priority task.

**Hint:** `std::priority_queue` is a max-heap by default. You can define a `Task` struct containing `int priority` and `std::function<void()> func`, and overload `operator<` so that tasks with higher priority values are dequeued first.

### Exercise 2: Timed Shutdown

Add timed shutdown logic to the thread pool's destructor: if workers haven't exited within a certain time (e.g., five seconds), stop waiting and detach them. Be aware of the risks of `detach`—a detached thread might access destroyed objects. Think about how to implement timed shutdown safely (Hint: you can have tasks check a "is the pool still alive" flag).

### Exercise 3: Work Stealing

Implement simple work stealing for the thread pool: each worker has its own local task queue and prioritizes taking tasks from it. When the local queue is empty, it attempts to "steal" tasks from other workers' queues. Work stealing reduces contention between threads (since threads mostly operate on their own local queues) and is a common optimization in high-performance thread pools.

## Summary

At this point, we have built a complete thread pool from scratch, covering almost all core issues in C++ thread pool design.

The basic components of a thread pool are worker threads, a task queue, and synchronization primitives (mutex + condition_variable). Worker threads are created during construction, enter an idle wait state, and fetch tasks from the queue after being notified. Upon shutdown, we set a stop flag and use `notify_all` to wake all workers; workers finish remaining tasks and then exit. This workflow seems simple, but every timing detail (holding the lock while notifying, the semantics of the stop condition, the order of joins) deserves careful consideration.

The `submit()` interface uses `packaged_task` + `shared_ptr` to implement type erasure and future returns. `packaged_task` binds the callable object and arguments together, automatically handling return value and exception propagation; `shared_ptr` wrapping solves the non-copyable problem of `packaged_task`; and lambda capturing of `shared_ptr` implements type erasure from `packaged_task<R()>` to `std::function<void()>`. This combination is the "standard pattern" for C++ thread pools. Master it, and you will understand the implementation of most open-source thread pools.

Exceptions propagate automatically through `packaged_task`'s internal mechanism: when a task throws, the exception is stored in the shared state, and the caller receives it via `future.get()`. This makes cross-thread exception handling as natural as synchronous code—provided you remember to call `get()`, otherwise the exception is silently swallowed.

C++20's `jthread` and `stop_token` can simplify thread pool shutdown logic, but note that `std::condition_variable` does not support `stop_token`—you need to switch to `std::condition_variable_any`. Additionally, manually creating a `stop_source` might conflict with the one built into `jthread`, requiring careful handling in practice. If you are in a C++17 environment, the manual stop flag approach is fully sufficient; there is no need to force an upgrade to C++20.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

## References

- [std::packaged_task — cppreference](https://en.cppreference.com/w/cpp/thread/packaged_task)
- [std::condition_variable_any::wait — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait)
- [std::jthread — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_token — cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Why does C++20 std::condition_variable not support std::stop_token? — Stack Overflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
- [Thread Pool C++ Implementation — Code Review Stack Exchange](https://codereview.stackexchange.com/questions/221617/thread-pool-c-implementation)

---

> **Self-Assessment of Difficulty**: If you are not yet familiar with the basic usage of `packaged_task`, `future`, and `condition_variable`, it is recommended to review the first three articles of Chapter 05. A thread pool is essentially a combination of these components—once you understand the parts, the assembly comes naturally.
