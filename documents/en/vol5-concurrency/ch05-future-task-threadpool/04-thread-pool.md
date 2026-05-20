---
title: Thread Pool Design
description: Starting from a worker, task queue, and `condition_variable`, we build
  a thread pool that supports `future` returns, exception propagation, and graceful
  shutdown.
chapter: 5
order: 4
tags:
- host
- cpp-modern
- advanced
- 异步编程
- mutex
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- jthread 与停止令牌
- promise 与 packaged_task
related:
- 线程安全队列
- std::async 与 future
translation:
  source: documents/vol5-concurrency/ch05-future-task-threadpool/04-thread-pool.md
  source_hash: 5c05ded33db6e734648e2ea6af2aa67c24671c415aaaf22427eee02526695729
  translated_at: '2026-05-20T04:44:28.015432+00:00'
  engine: anthropic
  token_count: 6911
---
# Thread Pool Design

In the previous chapters, we broke down the async infrastructure of `std::async`, `std::future`, `std::promise`, and `std::packaged_task` one by one, and at the end of the `packaged_task` chapter, we built a single-threaded `SimpleTaskQueue` as a teaser. That rudimentary queue worked, but it had only one worker thread—to be honest, submitting four tasks just meant they lined up and ran one by one with zero parallelism, which isn't fundamentally different from calling them directly on the main thread.

Now we are going to extend that single-worker queue into a real thread pool: a group of pre-created worker threads sharing a task queue, concurrently fetching and executing tasks. The thread pool is one of the most commonly used concurrency patterns in production—it avoids the system overhead of frequently creating and destroying threads, lets you control the concurrency level (number of threads), and when paired with `packaged_task` / `future`, cleanly propagates results and exceptions back to the submitter.

In this chapter, we will build a fully functional thread pool from scratch, adding one capability at a time on top of the previous step. Specifically, we will go through these phases: first, build a minimal skeleton with just `enqueue()` to get multiple workers running; then add `submit()` returning `future` so callers can get results; next, handle exception propagation across threads; then design a graceful shutdown sequence—stopping accepting new tasks, draining the queue, and joining all workers; and finally, see how C++20's `jthread` + `stop_token` can simplify the shutdown logic.

## Step 1: A Minimal Viable Thread Pool

Let's not rush into fancy features like `submit` returning a future or exception propagation—let's build the most core skeleton first. The structure of a working thread pool is actually quite classic: N worker threads share a task queue, the queue is protected by a `std::mutex`, and a `std::condition_variable` notifies workers when new tasks arrive. It's that simple.

> **Environment note**: All code in this chapter is based on C++17 (gcc 12+ / clang 15+ / MSVC 19.34+) and tested on x86-64 Linux and macOS. The C++20 refactoring in the final step requires a compiler that supports `<stop_token>` (gcc 10+ / clang 17+ (partial libc++ support, full support in Clang 20) / MSVC 19.28+).

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

This structure is practically the prototype for all C++ thread pools. Let's break down its core components and understand what each part does.

`workers_` is a group of pre-created `std::thread` objects, created in a loop inside the constructor, with each thread executing the same `worker_loop()`. The number of threads is typically determined by `std::thread::hardware_concurrency()`, or manually specified based on your task characteristics—if the tasks are CPU-bound, having roughly as many threads as cores is sufficient; having more would actually slow things down due to context switching. If the tasks are I/O-bound, you can have a few more, since threads often wait on I/O, leaving CPU time available for other threads.

`tasks_` is a `std::queue<std::function<void()>>`—all tasks are type-erased into `std::function<void()>` and pushed into this queue. Whether you submit a function returning `int`, a lambda returning `std::string`, or a function object that returns nothing, they all become the `void()` signature once inside the queue. As for how to unify callables with different signatures into `void()` while preserving the return value—that's the problem we will solve in the next step.

`mutex_` and `cv_` are the core of thread pool synchronization. `mutex_` protects the `tasks_` queue and the `stop_` flag, ensuring only one thread operates on the queue at any given moment. `cv_` is used to notify workers: a new task has arrived (`notify_one`) or it's time to stop (`notify_all`).

The `stop_` flag controls the shutdown sequence. When the destructor sets `stop_ = true` and calls `notify_all()`, all workers are woken up. Note that the worker's exit condition is not "exit immediately when `stop_` is true," but rather "`stop_` is true **and** the queue is empty"—this guarantees that submitted but not-yet-executed tasks are not dropped.

Let's verify it works with a simple test:

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

You will see eight tasks distributed across four threads, with the first four starting almost simultaneously, and the next four running after the first batch completes.

Great, the skeleton is up. But this version has an obvious flaw: `enqueue()` doesn't return anything. You submit a task, the task finishes executing, but you can't get the result—which is awkward. If the task throws an exception, things get worse: the exception gets swallowed by the `std::function<void()>` invocation, and the exact behavior depends on the implementation, usually calling `std::terminate` and terminating the program outright. Let's fix this next.

## Step 2: submit() Returning a Future

In the previous chapter, we demonstrated how to use the `packaged_task` + `shared_ptr` pattern to return a future in `SimpleTaskQueue`. The thread pool needs the same pattern, except now multiple workers fetch tasks from the queue concurrently—but that's fine, `packaged_task` itself is thread-safe (setting the shared state happens only once), as long as we don't call the same `packaged_task` from multiple threads simultaneously.

Our goal is to provide a `submit()` template function: it accepts any callable and arguments, and returns a `std::future<R>`, where `R` is the return type of the callable. The caller can use this future to `get()` the result, or catch an exception in case of failure.

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

There are a few key points in this code worth discussing in detail, because each one is an important detail realized only after falling into pitfalls.

`std::invoke_result_t<F, Args...>` is a type trait provided by C++17 for deducing the return type of a `F(Args...)`. It's more general than C++11's `std::result_of`—it correctly handles member function pointers, function objects with reference qualifiers, and so on. `ReturnType` is the task's return type, which determines the signature of `packaged_task` and the template parameter of `future`.

`std::make_shared<std::packaged_task<ReturnType()>>` binds the callable and arguments together, wrapping them into a `packaged_task` with the signature `ReturnType()`. Here we use `std::bind` to pre-bind the arguments—because the queue stores `std::function<void()>`, which accepts no arguments, we need to bind the arguments to the callable to form a parameterless callable entity.

Then we wrap the `packaged_task` inside a `shared_ptr`. This step is crucial, and it's where many beginners get stuck—because `std::function<void()>` requires the callable to be copyable, but `std::packaged_task` is move-only and can't be pushed directly into a `std::function`. After wrapping with `shared_ptr`, the lambda captures a `shared_ptr` (which is copyable), while the `packaged_task` itself has only one instance managed by the `shared_ptr`. This trick is almost standard in thread pool implementations—you'll see it in virtually every serious C++ thread pool out there.

`tasks_.push([task]() { (*task)(); })` pushes a lambda into the queue. This lambda captures the `shared_ptr<packaged_task<R()>>`, and when called, dereferences and executes the `packaged_task`. After `packaged_task` is called, the internal promise automatically sets the return value or stores the exception, and the future in the caller's hand becomes ready.

Another detail to note: we checked `stop_` before pushing the task. If the thread pool has already entered the shutdown state, it should not accept new tasks, and we throw an exception directly. This avoids undefined behavior from submitting tasks during shutdown—imagine pushing a task into the queue only to find that all worker threads have already exited, leaving the task forever unexecuted.

Let's look at a complete usage of `submit`:

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

Three tasks are submitted to the pool and executed in parallel by different worker threads. The future type returned by `submit()` is automatically deduced by the compiler—`f1` and `f2` are `std::future<int>`, and `f3` is `std::future<std::string>`.

## Step 3: Exception Propagation

Exception handling in asynchronous programming is an area full of pitfalls, and I've tripped up here more than once myself. If your task throws an exception in a worker thread but you don't handle it correctly, the exception is lost—the worker thread won't crash (because the exception is caught by the `std::function` invocation mechanism), but you'll never get the result either, and the program's behavior degrades into an eerie "silent failure." This kind of bug is harder to track down than a direct crash—at least with a crash you get a stack trace.

Fortunately, `packaged_task` already handles this for us. When the wrapped function throws an exception, `packaged_task` internally catches it with `std::current_exception()` and stores it in the shared state. When the caller retrieves the result via `future.get()`, if the shared state holds an exception, `get()` rethrows it. The whole process is transparent to the caller—you just need to try-catch where you `get()`.

Let's verify with an example:

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

The exception travels from the worker thread to the main thread with its type information fully intact. You don't need to design an error code system, serialize exception information into strings, or set up global error handling callbacks—the `packaged_task` + `future` combination encapsulates cross-thread exception propagation cleanly. This is really worth appreciating: C++'s exception mechanism is inherently stack unwinding-oriented and naturally suited for synchronous calls. Cross-thread exception propagation is normally quite troublesome, but `packaged_task` internally catches the `std::current_exception()` and stores it, and when the caller calls `future.get()`, it rethrows—making the whole process feel exactly like handling a synchronous exception to the caller.

However, there is a real pitfall here—if you submit a task but never call `future.get()`, the exception gets silently swallowed. This is different from a future returned by `std::async`—a future from `std::async` blocks on destruction waiting for the task to complete, whereas a future associated with `packaged_task` simply releases the shared state reference on destruction without waiting. So, **for futures obtained from a thread pool's submit(), either call `get()`, or at least call `wait()` to confirm the task has completed**—don't lose the exception.

## Step 4: Graceful Shutdown

Shutting down a thread pool sounds simple—just make the worker threads exit, right? But we're not done yet; the real pitfalls lie in the shutdown timing. When shutting down, there might still be unexecuted tasks in the queue, and currently executing tasks might not have finished. If you brutally kill all workers (for example, by directly detaching or terminating them), submitted tasks get dropped, and in-progress tasks might leave things in a half-finished state—imagine a thread in the middle of writing a file getting killed, and you'll understand how disastrous that can be.

A "graceful" shutdown sequence should look like this: first, stop accepting new tasks (`submit()` throws an exception or returns an error); then, let worker threads finish executing all remaining tasks in the queue; finally, all worker threads exit normally, and the destructor joins them.

Let's look back at the exit condition in `worker_loop()`:

```cpp
cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
if (stop_ && tasks_.empty()) {
    return;
}
```

The meaning of this condition is: after a worker is woken up, if `stop_` is true and the queue is empty, it exits. If `stop_` is true but the queue still has tasks, the worker continues to fetch and execute the remaining tasks, exiting only when the queue is empty. This is the "drain the queue" semantics—we don't drop tasks, we just stop accepting new ones.

Looking back at the destructor's shutdown sequence:

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

There are a few timing details here that need to be made clear.

Setting `stop_` must be done while holding the lock. Although reads and writes to `stop_` only happen after acquiring the lock and theoretically don't require atomicity, putting the modification under the lock's protection makes the code's intent clearer—"modify shared state only while holding the lock" is a basic discipline of concurrent programming; there's no need to save a lock acquisition here.

`notify_all()` is called after releasing the lock. This isn't mandatory—the standard allows you to notify while holding the lock—but notifying after releasing the lock is a common optimization: if worker threads need to acquire the same lock after being woken up (which they do), waking them after releasing the lock avoids the useless context switch of "wake up -> fail to acquire lock -> block again."

`join()` must come after `notify_all()`. If you join first and then notify, the workers will never receive the stop signal, and `join()` will block forever—that's a dead lock. The order must be: notify first, then wait.

This shutdown mechanism has an implicit guarantee: when the destructor returns, all submitted tasks have definitely finished executing. Because `join()` blocks until the worker threads exit, and when worker threads exit, the queue is guaranteed to be empty. This is crucial for resource cleanup—you won't have background threads still accessing already-destroyed objects after the destructor returns.

## Step 5: C++20 Refactoring—jthread + stop_token

So far, our thread pool uses `std::thread` + a manual `stop_` flag + manual `notify_all()` + manual `join()`. To be honest, this combination works, but it's verbose to write—you have to remember to set the flag, notify, and join every time; miss one step and you get a dead lock or resource leak. C++20 introduced `std::jthread`, `std::stop_token`, and `std::stop_source`, along with `std::condition_variable_any`'s support for `stop_token`, which can significantly simplify the shutdown logic.

Let's start with an important detail—which many tutorials get wrong: `std::condition_variable` (not `_any`) does **not** have a C++20 stop_token overload. The stop_token wait integration is only provided on `std::condition_variable_any`. The reason is that `std::condition_variable` only supports the specific lock type `std::unique_lock<std::mutex>`, whereas `std::condition_variable_any` is a template class that supports any lock type satisfying the BasicLockable requirements, making the templated design more natural for stop_token integration. If you try to call `wait(lock, stop_token, predicate)` with `std::condition_variable` in your code, the compiler will error out directly—don't ask me how I know.

The thread pool refactored with jthread + stop_token looks like this:

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

Let's look at the key differences between this version and the previous one.

First, the worker threads are changed to `std::jthread`. The `jthread` constructor accepts a callable with a `std::stop_token` as its first parameter, automatically creates an internal `std::stop_source`, and passes the corresponding `stop_token` to your function. You no longer need to maintain your own `stop_` flag—the lifecycle of this flag is managed internally by `jthread`.

Second, the condition wait now uses the stop_token overload of `std::condition_variable_any`. The signature of this overload is `wait(lock, stop_token, predicate)`, and its behavior is: if the predicate is true, it returns true immediately; if a stop is requested, it also returns immediately, but the return value is the current value of the predicate (usually false). This replaces the manual logic of checking the `stop_` flag—when `request_stop()` is called, `cv_any_.wait()` is automatically woken up, eliminating the need to manually `notify_all()` in the destructor.

The third point is that the destructor is simpler. When `jthread` is destroyed, it automatically calls `request_stop()` and then `join()`, so you don't even need to write an explicit destructor—though we still keep one because we need to `notify_all()` before stopping to wake up any workers that might be waiting.

But to be honest, this version also has an inelegant aspect—the `stop_requested()` implementation relies on checking the `workers_[0]`'s stop_source. This breaks when workers_ is empty (although the constructor guarantees at least one worker, relying on such implicit assumptions is always uncomfortable). A cleaner approach is to have the thread pool hold its own `std::stop_source`, and pass its associated `stop_token` to each worker. The code is slightly more complex, but the semantics are clearer. Let's look at this improved version:

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

This version uses a `stop_source_` held by the thread pool itself to manage the stop state. `submit()` checks `stop_source_.stop_requested()` to determine if it's still running, and the destructor calls `stop_source_.request_stop()` to trigger shutdown. Each worker thread gets the same stop_token via `stop_source_.get_token()`—when `request_stop()` is called, all wait operations holding this token are woken up.

Note a subtle point here: we pass the `stop_token` to worker threads via lambda capture, rather than relying on `jthread`'s automatic parameter passing mechanism. This is because the `stop_token` automatically created by `jthread` is associated with each `jthread`'s own `stop_source`—calling `request_stop()` on a particular `jthread` only cancels that thread. What we want is: calling `request_stop()` once to cancel all workers. So we need a shared `stop_source` and distribute its `stop_token` to all workers.

However, while this version is semantically clean, there's an architectural issue you need to be aware of: the `stop_source` built into `jthread` and our manually created `stop_source_` are two independent stop sources. When `jthread` is destroyed, it calls `request_stop()` on its built-in `stop_source`, but our worker_loop listens to the one we manually created. This means `jthread`'s own stop mechanism is actually disconnected from our worker threads—calling `workers_[i].request_stop()` won't wake that worker, because worker_loop isn't listening to `jthread`'s stop_token.

This also means our explicit destructor is mandatory, not optional. If we relied on the default destructor, members would be destroyed in reverse order of declaration: `stop_source_` and `cv_any_` would be destroyed before `workers_`, and the `jthread` in `workers_` calling `request_stop()` during destruction wouldn't reach our worker_loop—the result is that `join()` blocks forever, a dead lock. The explicit destructor calls `stop_source_.request_stop()` + `cv_any_.notify_all()` first, ensuring worker threads exit, and then the `join()` during `jthread`'s destruction can return smoothly.

You might wonder: won't moving `jthread` during vector reallocation cause problems? The answer is no—after a `jthread` is moved from, the original object's `joinable()` becomes `false`, and its destructor simply skips `request_stop()` and `join()`. The thread's execution has already been transferred to the new `jthread` object and is unaffected.

At this point you'll notice that while C++20's stop_token mechanism is nice to use, its interaction with thread pools isn't as simple as you might imagine—the `stop_source` automatically managed by `jthread` and our manually created `stop_source_` each do their own thing, requiring us to manually coordinate their timing in the destructor.

My recommendation is: if your project is still on C++17 or earlier, the `std::thread` + manual `stop_` flag approach works perfectly fine—don't introduce unnecessary complexity just to use new features. The thread + mutex + condition_variable combination established in the C++11 era has been battle-tested for over a decade, and the probability of bugs is far lower than when wrestling with C++20 new features. If you're fully on C++20, and `jthread` and `stop_source` are already widely used in your project, then using them to manage the thread pool's stop state is reasonable, but you must pay attention to the "two stop_sources" issue mentioned above.

Below is a complete, battle-tested C++17 version that doesn't depend on C++20's `jthread` or `stop_token`, but has a clear structure and complete functionality:

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

We've also handled a few common pitfalls here. Disabling copy and move—the thread pool holds `std::thread` and `std::mutex`, both of which are non-copyable, and the thread pool's lifecycle management shouldn't be disrupted by moves (imagine the original thread pool's destructor joining threads that no longer belong to it after a move—that would be quite a spectacle). Checking `joinable()` before joining in the destructor—although under normal circumstances threads are always joinable, defensive programming is always good; what if someone joined them without your knowledge?

## Worker Thread Lifecycle

A thread pool's worker threads actually cycle through three states: idle waiting, executing a task, and shutting down. Understanding this lifecycle is important for troubleshooting thread pool issues—most "task not executing" or "thread pool stuck" bugs can be traced back to state transitions.

In the constructor, after each worker thread is created, it immediately enters `worker_loop()`. Since the queue is empty at this point, the worker blocks on `cv_.wait()`, entering the idle waiting state. This blocking is efficient—the thread is suspended by the OS, consuming no CPU time slices, until `cv_.notify_one()` or `cv_.notify_all()` wakes it up.

When `submit()` pushes a task and calls `cv_.notify_one()`, one (and only one) waiting worker is woken up. It fetches the task from the queue, releases the lock, and then executes the task outside the lock. Executing outside the lock is a critical design decision—if you executed tasks while holding the lock, other worker threads and `submit()` calls would all be blocked, and the entire thread pool would degrade to serial execution, defeating the purpose of multithreading. After the task finishes, the worker returns to the top of the loop, reacquires the lock, and checks the queue. If the queue is empty, it blocks on `wait()` again; if there are still tasks in the queue, it fetches and executes one directly without waiting—this behavior of "proactively checking the queue after finishing a task" avoids unnecessary notify overhead.

The shutdown path is triggered in the destructor: setting `stop_ = true` and calling `cv_.notify_all()`. All workers are woken up and check `stop_ && tasks_.empty()`. If the queue is empty, the worker exits the loop normally and the thread ends; if the queue still has tasks, the worker continues executing until the queue is drained before exiting.

You might ask: what happens if a worker is executing a long-running task when the destructor is called? The answer is—that worker won't immediately respond to the stop request. It will continue executing the current task, and only when the task completes and it returns to the top of the loop will it check the `stop_` flag. So, **if your tasks might run for a long time, the thread pool's destructor might block for a long time**. This isn't a bug; it's the cost of graceful shutdown—you either wait for it to finish, or use a more aggressive approach (like `timed_wait` + detach as a fallback), but detached threads might access already-destroyed objects, and that trade-off never works out in your favor.

## A Complete Practical Example

Now let's string together all the capabilities we've built and write a comprehensive example: parallel-computing the processing results of a dataset, where the processing function might throw exceptions, and we need to handle both normal results and exceptions correctly. This example simulates a very common scenario in production—batch-processing a set of data where some items might have issues causing processing to fail, and you need to know which succeeded and which failed.

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

This code demonstrates the typical usage of a thread pool in a real-world scenario: submit a batch of tasks, then collect results one by one. You'll find that the overall experience is very close to synchronous code—the only difference is that tasks execute in parallel in the background, and you get the results via `future.get()`. Exceptions are automatically propagated through the future, and callers can handle asynchronous exceptions just like synchronous ones.

## Common Pitfalls in Practice

By now we've implemented all the core functionality of the thread pool, but there are a few common pitfalls in actual usage worth discussing separately. I've personally fallen into every one of these, and I hope this saves you some detours.

First, the issue with `std::bind` and passing by reference. Our `submit()` uses `std::bind` to bind arguments, but `std::bind` stores arguments by value by default—if your argument is a large object, it gets copied. If you want to pass by reference, you need to wrap it with `std::ref()` or `std::cref()`. A better approach is to use a lambda directly instead of `std::bind`—the lambda's capture list lets you precisely control whether each argument is passed by value or by reference, and the code is usually more readable than `std::bind`. If you want to replace `std::bind` with a lambda, the submit implementation can be simplified to this:

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

Callers can bind arguments and references themselves inside the lambda:

```cpp
std::string large_data = "...";
auto fut = pool.submit([&large_data, x, y] {
    return process(large_data, x, y);
});
```

This is much more flexible than `std::bind`, and the lifetime relationships are clear at the call site—capturing by reference means the caller must ensure that `large_data` remains valid until the task finishes executing. This is an iron rule in asynchronous programming, and no tool can help you bypass it.

Next, the issue of future leaks. If you submit a task but never call `get()` or `wait()`, you won't receive any error—the task might silently finish executing in the background, or it might have thrown an exception that got swallowed, and you'd be none the wiser. A defensive approach is to clearly document in submit's documentation that "every future must be consumed," or to track the number of unconsumed futures in debug mode. I've been burned by this in my own projects: a background task's future was ignored, the exception in the task silently disappeared, and it took a long time to track down.

Finally, and most insidiously: a mismatch between the thread pool's lifecycle and the lifecycle of objects referenced by tasks. If your task captures references to stack variables, and the thread pool's destruction happens after those stack variables are destroyed (for example, if the thread pool is global or static), you face the risk of dangling references. The root of this problem isn't in the thread pool itself, but in the fundamental question of "who guarantees whose lifecycle" in asynchronous programming—the execution timing of asynchronous tasks is uncertain, and all external references you capture must be valid within the possible execution time window of the task. There's no good universal solution; you just have to think about this problem when designing your API, and try to use value captures or `shared_ptr` to extend lifetimes.

## Exercises

If you want to truly internalize the content of this chapter, the three exercises below are worth trying. They extend our thread pool in three directions: priority scheduling, timed shutdown, and work stealing—each being a common requirement in production environments.

### Exercise 1: Priority Thread Pool

Add priority support to the thread pool's task queue. Replace `std::queue` with `std::priority_queue`, and extend the task type to a pair containing a priority and a callable. Allow specifying priority when submitting, and worker threads always fetch the highest-priority task to execute.

Hint: `std::priority_queue` is a max-heap by default. You can define a `Task` struct containing `int priority` and `std::function<void()> func`, and overload `operator<` so that tasks with higher priority values are dequeued first.

### Exercise 2: Timed Shutdown

Add timed shutdown logic to the thread pool's destructor: if some workers haven't exited within a certain time (say, five seconds), give up waiting and detach them. Note the risks of detaching—detached threads might access already-destroyed objects. Think about how to implement timed shutdown safely (hint: you can have tasks check a "is the pool still alive?" flag).

### Exercise 3: Work Stealing

Implement simple work stealing for the thread pool: each worker has its own local task queue and优先 fetches tasks from its local queue. When the local queue is empty, it tries to "steal" tasks from other workers' queues. Work stealing can reduce contention between threads (since most of the time threads only operate on their own local queues) and is a common optimization in high-performance thread pools.

## Summary

At this point, we've built a complete thread pool from scratch, covering almost all core issues in C++ thread pool design.

The basic components of a thread pool are worker threads, a task queue, and synchronization primitives (mutex + condition_variable). Worker threads are created during construction, enter the idle waiting state, and fetch tasks from the queue to execute after being notified. During shutdown, the stop flag is set first, then notify_all wakes all workers, and workers exit after executing remaining tasks—this process looks simple, but the timing details (notifying while holding the lock, the semantics of the stop condition, the order of joins) are each worth careful thought.

The `submit()` interface implements type erasure and future returns through `packaged_task` + `shared_ptr`. `packaged_task` binds the callable and arguments together, automatically handling return value and exception propagation; `shared_ptr` wrapping solves the problem of `packaged_task` being non-copyable; and lambda capture of `shared_ptr` implements type erasure from `packaged_task<R()>` to `std::function<void()>`. The combination of these three is the "standard pattern" for C++ thread pools—master it and you'll be able to understand the vast majority of open-source thread pool implementations.

Exceptions are automatically propagated through `packaged_task`'s internal mechanism: when a task throws, the exception is stored in the shared state, and the caller receives it via `future.get()`. This makes cross-thread exception handling feel as natural as synchronous code—but the prerequisite is that you remember to call `get()`, otherwise the exception gets silently swallowed.

C++20's `jthread` and `stop_token` can simplify the thread pool's shutdown logic, but note that `std::condition_variable` doesn't support `stop_token`—you need to use `std::condition_variable_any` instead. Additionally, manually creating a `stop_source` and the `stop_source` built into `jthread` can have inconsistency issues that need careful handling in practice. If you're in a C++17 environment, the manual stop flag approach is perfectly sufficient—no need to force C++20.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

## References

- [std::packaged_task — cppreference](https://en.cppreference.com/w/cpp/thread/packaged_task)
- [std::condition_variable_any::wait — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait)
- [std::jthread — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_token — cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Why does C++20 std::condition_variable not support std::stop_token? — Stack Overflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
- [Thread Pool C++ Implementation — Code Review Stack Exchange](https://codereview.stackexchange.com/questions/221617/thread-pool-c-implementation)

---

> **Difficulty self-assessment**: If you're not yet familiar with the basic usage of `packaged_task`, `future`, and `condition_variable`, I recommend reviewing the first three chapters of ch05 first. A thread pool is essentially a combination of these components—once you understand the parts, assembling them follows naturally.
