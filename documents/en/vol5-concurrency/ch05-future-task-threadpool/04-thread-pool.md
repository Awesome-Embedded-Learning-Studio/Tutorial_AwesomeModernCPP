---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Starting from workers, a task queue, and a condition_variable, build
  a thread pool that supports future returns, exception propagation, and graceful
  shutdown
difficulty: advanced
order: 4
platform: host
prerequisites:
- jthread and Stop Tokens
- promise and packaged_task
reading_time_minutes: 34
related:
- Thread-Safe Queue
- std::async and future
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
  translated_at: '2026-09-26T08:29:51+00:00'
  engine: anthropic
  token_count: 17000
---
# Thread Pool Design

In the previous few articles we took the async infrastructure—`std::async`, `std::future`, `std::promise`, `std::packaged_task`—apart one by one, and at the end of the `packaged_task` article we built a single-threaded `SimpleTaskQueue` as a teaser. That bare-bones queue does run, but it has only one worker thread—to be honest, submitting four tasks just means they line up and run one at a time. There is no parallelism to speak of, which makes it essentially no different from calling them directly on the main thread.

What we are going to do now is extend that single-worker queue into a real thread pool: a group of pre-created worker threads sharing one task queue, pulling tasks out and executing them concurrently. The thread pool is one of the most commonly used concurrency patterns in production—it avoids the system overhead of constantly creating and destroying threads, it lets you control the degree of concurrency (the number of threads), and combined with `packaged_task` / `future` it passes results and exceptions back to the submitter cleanly.

In this article we will build a fully functional thread pool from scratch, adding one capability on top of the previous at every step. Concretely, we will go through these stages: first a minimal skeleton with nothing but `enqueue()`, just to get multiple workers running; then `submit()` returning a `future`, so the caller can get the result back; then exception propagation across threads; then a graceful shutdown sequence—stop accepting new tasks, drain the queue, then join all workers; and finally a look at how C++20's `jthread` + `stop_token` can simplify the shutdown logic.

## Step 1: A Minimal Viable Thread Pool

Don't rush into the fancy stuff—`submit` returning a `future`, exception propagation, and the like. Let's get the most essential skeleton standing first. A working thread pool has a thoroughly classic structure: N worker threads share one task queue, the queue is protected by a `std::mutex`, and a `std::condition_variable` notifies the workers that a new task has arrived. It's that simple.

> **Environment note**: All code in this article is based on C++17 (gcc 12+ / clang 15+ / MSVC 19.34+) and tested on x86-64 Linux and macOS. The C++20 makeover in the final step requires a compiler that supports `<stop_token>` (gcc 10+ / clang 17+ (libc++ partially supported, fully in Clang 20) / MSVC 19.28+).

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

This structure is the prototype of almost every C++ thread pool. Let's take its core components apart and see what each part does.

`workers_` is a group of pre-created `std::thread` objects, created in a loop by the constructor, each thread running the same `worker_loop()`. The thread count is usually decided by `std::thread::hardware_concurrency()`, or specified manually to match your workload—for CPU-bound tasks, a thread count roughly equal to the core count is right, and anything more actually slows things down through context switching; for I/O-bound tasks you can go somewhat higher, because the threads spend much of their time waiting on I/O, and the CPU capacity freed up can serve the other threads.

`tasks_` is a `std::queue<std::function<void()>>`—every task gets type-erased into a `std::function<void()>` before being pushed into this queue. Whether you submit a function returning `int`, a lambda returning `std::string`, or a function object that returns nothing at all, once in the queue they all carry the `void()` signature. How to unify callables with different signatures into `void()` while preserving the return value—that is the problem we solve in the next step.

`mutex_` and `cv_` are the heart of the pool's synchronization. `mutex_` protects the `tasks_` queue and the `stop_` flag, making sure only one thread touches the queue at any given moment. `cv_` is how the workers get notified: a new task has arrived (`notify_one`), or it's time to stop (`notify_all`).

The `stop_` flag drives the shutdown sequence. When the destructor sets `stop_ = true` and calls `notify_all()`, every worker wakes up. Note that the worker's exit condition is not "quit the moment `stop_` is true" but "`stop_` is true **and** the queue is empty"—which guarantees that already-submitted but not-yet-executed tasks are never dropped.

Let's verify it runs with a bit of simple test code:

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

    // The destructor waits for all tasks to complete
    return 0;
}
```

You will see the 8 tasks distributed across the 4 threads: the first four start almost simultaneously, and the next four run once the previous batch finishes.

Good—the skeleton is standing. But this version has an obvious defect: `enqueue()` returns nothing. You submit a task, the task runs, and you cannot get the result—awkward. And if the task throws, it gets worse: the exception is swallowed by the invocation of the `std::function<void()>`, with behavior that depends on the implementation—usually a call to `std::terminate` taking the whole program down. We fix this next.

## Step 2: submit() Returning a future

Last time, in the `SimpleTaskQueue`, we demonstrated how to return a future using `packaged_task` + `shared_ptr`. The thread pool needs the same pattern—except now multiple workers are pulling tasks from the queue at the same time. That's fine: `packaged_task` itself is thread-safe (the shared state is set exactly once), as long as we never invoke the same `packaged_task` from multiple threads simultaneously.

Our goal is a `submit()` function template: it accepts any callable and any arguments, and returns a `std::future<R>`, where `R` is the callable's return type. The caller can use that future to `get()` the result—or to receive the exception, if things went wrong.

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

There are several points in this code worth going through carefully, because each one is a detail you only appreciate after falling into the pit.

`std::invoke_result_t<F, Args...>` is the type trait provided by C++17 for deducing the return type of `F(Args...)`. It is more general than C++11's `std::result_of`—it correctly handles member function pointers, function objects with reference qualifiers, and similar cases. `ReturnType` is the task's return type; it determines both the signature of the `packaged_task` and the template argument of the `future`.

`std::make_shared<std::packaged_task<ReturnType()>>` binds the callable together with its arguments and wraps the whole thing into a `packaged_task` with signature `ReturnType()`. Here `std::bind` pre-binds the arguments—because what the queue stores is `std::function<void()>`, which takes no arguments, we have to bind the arguments onto the callable to form a zero-argument callable entity.

Then we wrap the `packaged_task` in a `shared_ptr`. This step is crucial, and it is exactly where many beginners get stuck—`std::function<void()>` requires its callable to be copyable, while `std::packaged_task` is move-only and cannot be pushed into a `std::function` directly. With the `shared_ptr` wrapper, what the lambda captures is a `shared_ptr` (copyable), and the `packaged_task` itself exists as the single instance managed by the `shared_ptr`. This trick is practically standard equipment in thread pool implementations—you will see it in almost every serious C++ thread pool out there.

`tasks_.push([task]() { (*task)(); })` pushes a lambda onto the queue. The lambda captures the `shared_ptr<packaged_task<R()>>`; when invoked, it dereferences and executes the `packaged_task`. Once the `packaged_task` runs, the promise inside it automatically sets the return value or stores the exception, and the future in the caller's hand becomes ready.

One more detail deserves attention: we check `stop_` before pushing the task. Once the pool has entered shutdown, it must not accept new tasks—it throws instead. This avoids the indeterminate behavior of submitting during shutdown—think about it, you definitely don't want your task pushed onto a queue only to discover that every worker thread has already left, and the task will never run.

Let's look at a complete example of using submit:

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

The three tasks are submitted to the pool and executed in parallel by different worker threads. The type of the future returned by `submit()` is deduced by the compiler—`f1` and `f2` are `std::future<int>`, and `f3` is `std::future<std::string>`.

## Step 3: Exception Propagation

Exception handling in asynchronous code is a field full of pitfalls; your author has personally crashed and burned here more than once. If your task throws inside a worker thread and you don't handle it correctly, the exception is simply lost—the worker thread does not crash (the exception is caught by the invocation machinery of `std::function`), but you never get the result either, and the program's behavior becomes an eerie "silent failure". This kind of bug is even harder to track down than an outright crash—at least a crash hands you a stack trace.

Fortunately, `packaged_task` has already handled this for us. When the wrapped function throws, `packaged_task` captures the exception internally with `std::current_exception()` and stores it in the shared state. When the caller fetches the result through `future.get()`, if what is stored in the shared state is an exception, `get()` rethrows it. The whole process is transparent to the caller—you just put your try-catch around `get()`.

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

    // Normal path
    auto f1 = pool.submit(risky_task, 5);
    try {
        std::cout << "结果: " << f1.get() << "\n";  // 25
    } catch (const std::exception& e) {
        std::cout << "异常（不该走到这里）: " << e.what() << "\n";
    }

    // Exception path
    auto f2 = pool.submit(risky_task, -3);
    try {
        std::cout << "结果: " << f2.get() << "\n";  // never reached
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";  // parameter must not be negative
    }

    return 0;
}
```

The exception travels from the worker thread to the main thread with its type information fully intact. You don't need to design an error-code scheme, serialize exception messages into strings, or install a global error-handling callback—the `packaged_task` + `future` combination wraps cross-thread exception propagation up cleanly. This genuinely deserves a moment of appreciation: C++'s exception mechanism was designed around stack unwinding and is naturally suited to synchronous calls. Propagating an exception across threads is ordinarily a painful business, but `packaged_task` captures and stores `std::current_exception()` for you internally and rethrows it when the caller calls `future.get()`—to the caller, the whole process is indistinguishable from handling a synchronous exception.

But here is the real trap: if you submit a task and never call `future.get()`, the exception is silently swallowed. This differs from the future returned by `std::async`—the `std::async` future blocks in its destructor until the task completes, whereas the future associated with a `packaged_task` merely releases its reference to the shared state on destruction; it does not wait. So, **for a future obtained from the pool's submit(), either call `get()`, or at least call `wait()` to confirm the task has completed**—don't lose the exception.

## Step 4: Graceful Shutdown

Shutting down a thread pool sounds simple—just make the worker threads exit, right? But we're not done yet; the real traps are in the timing of the shutdown. At shutdown time the queue may still hold unexecuted tasks, and the tasks currently executing may not have finished. If you kill the workers brutally (say, by detaching them outright or terminating), the already-submitted tasks are dropped, and in-flight tasks may leave half-finished state behind—picture a thread in the middle of writing a file being shot, and you'll understand what a disaster that is.

A "graceful" shutdown sequence should go like this: first, stop accepting new tasks (`submit()` throws or returns an error); then, let the worker threads execute every task remaining in the queue; finally, all worker threads exit normally and the destructor joins them.

Let's return to the exit condition inside `worker_loop()`:

```cpp
cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
if (stop_ && tasks_.empty()) {
    return;
}
```

What this condition says: once a worker wakes up, if `stop_` is true and the queue is empty, it exits. If `stop_` is true but the queue still has tasks, the worker keeps pulling and executing the remaining tasks, exiting only when the queue drains. That is the "drain the queue" semantics—we don't drop tasks; we just stop accepting new ones.

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

A few points about the ordering here need to be spelled out.

Setting `stop_` must happen while holding the lock. Admittedly, since `stop_` is only ever read or written after acquiring the lock, it doesn't strictly need to be atomic—but putting the modification inside the lock's protection makes the code's intent clearer. "Hold the lock whenever you modify shared state" is basic discipline in concurrent programming; this is not the place to economize on a lock.

`notify_all()` is called after the lock is released. This is not mandatory—the standard permits notifying while holding the lock—but notifying after release is a common optimization: if the awakened worker thread needs to acquire the same lock (and it does), then waking it before releasing the lock invites the useless context switches of "wake up → fail to grab the lock → block again".

`join()` must come after `notify_all()`. If you join first and notify afterwards, the workers never receive the stop signal and `join()` blocks forever—that is a deadlock. The order must be: notify first, then wait.

This shutdown machinery carries one implicit guarantee: when the destructor returns, every submitted task has finished executing. `join()` blocks until the worker threads exit, and by the time a worker exits, the queue is necessarily empty. This is critical for resource cleanup—you will never have background threads touching already-destroyed objects after the destructor has run.

## Step 5: The C++20 Makeover—jthread + stop_token

So far our pool has used `std::thread` plus a hand-rolled `stop_` flag, a manual `notify_all()`, and a manual `join()`. Honestly, that combination works, but it is verbose to write—every time you must remember to set the flag, notify, and join; miss one step and you get a deadlock or a resource leak. C++20 introduced `std::jthread`, `std::stop_token`, and `std::stop_source`, and together with `std::condition_variable_any`'s support for `stop_token`, they can simplify the shutdown logic considerably.

First, an important detail—one that many tutorials get wrong: `std::condition_variable` (without the `_any`) does **not** have the C++20 stop_token overloads. The stop_token wait integration is provided only on `std::condition_variable_any`. The reason is that `std::condition_variable` supports only one specific lock type, `std::unique_lock<std::mutex>`, while `std::condition_variable_any` is a template class that supports any lock type satisfying the BasicLockable requirements—the templated design makes stop_token integration more natural. If you use a `std::condition_variable` in your code to call `wait(lock, stop_token, predicate)`, the compiler will reject it outright—don't ask me how I know.

Here is what the pool looks like after the jthread + stop_token makeover:

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <stop_token>
#include <mutex>
#include <condition_variable>  // condition_variable_any is also in this header
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
        // Request all jthreads to stop
        for (auto& w : workers_) {
            w.request_stop();
        }
        cv_any_.notify_all();
        // jthread joins automatically on destruction; no manual join needed
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
        // If any jthread has had stop requested, consider the pool shutting down
        return !workers_.empty() && workers_[0].get_stop_source().stop_requested();
    }

    void worker_loop(std::stop_token st)
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // Use condition_variable_any's stop_token overload
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    // Stop requested; check whether the queue still has tasks
                    if (tasks_.empty()) {
                        return;
                    }
                    // Tasks remain; keep executing
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

Now let's look at the key differences between this version and the previous ones.

First, the worker threads are now `std::jthread`. `jthread`'s constructor accepts a callable taking `std::stop_token` as its first parameter, automatically creates an internal `std::stop_source`, and passes the corresponding `stop_token` to your function. You no longer maintain the `stop_` flag yourself—the flag's lifetime management is handled inside `jthread`.

Second, the condition wait now uses `std::condition_variable_any`'s stop_token overload. This overload's signature is `wait(lock, stop_token, predicate)`, and its behavior is: if the predicate is true, it returns true immediately; if stop has been requested, it also returns immediately, but the return value is the predicate's current value (usually false). This replaces the manual `stop_` flag checking—when `request_stop()` is called, `cv_any_.wait()` is woken automatically, and the destructor no longer needs a manual `notify_all()`.

Third, the destructor is simpler. When a `jthread` is destroyed it calls `request_stop()` and then `join()` automatically, so you could even leave the destructor out entirely—but we keep an explicit one, because we need to `notify_all()` before stopping, to wake any workers that might be waiting.

But frankly, this version has one inelegant spot: `stop_requested()` is implemented by checking the stop_source of `workers_[0]`. That breaks when `workers_` is empty (the constructor guarantees at least one worker, but depending on such implicit assumptions is never comfortable). A cleaner approach is for the pool itself to hold a `std::stop_source` and pass its associated `stop_token` to each worker. The code gets slightly more involved, but the semantics are clearer. Let's look at this improved version:

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
        // jthread joins automatically when the vector is destroyed
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
                    // Stop requested
                    if (tasks_.empty()) {
                        return;
                    }
                    // Tasks remain; finish executing them, then exit
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

This version manages the stop state through the pool's own `stop_source_`. `submit()` checks `stop_source_.stop_requested()` to decide whether the pool is still running, and the destructor calls `stop_source_.request_stop()` to trigger the shutdown. Each worker thread obtains the same stop_token via `stop_source_.get_token()`—when `request_stop()` is called, every wait operation holding that token is woken.

Note the subtlety here: we pass the `stop_token` to the worker thread by capturing it in the lambda, rather than relying on `jthread`'s automatic token-passing mechanism. That is because the `stop_token` `jthread` creates automatically is associated with each `jthread`'s own `stop_source`—calling `request_stop()` on a particular `jthread` cancels only that thread. What we want is a single `request_stop()` call that cancels all workers. So we need a shared `stop_source` whose `stop_token` we distribute to every worker.

This version is semantically clean, but there is an architectural issue you should be aware of: the `stop_source` built into `jthread` and our manually created `stop_source_` are two independent stop sources. When a `jthread` is destroyed, the `request_stop()` it invokes targets its own built-in `stop_source`, while our worker_loop listens to the one we created manually. This means `jthread`'s own stop mechanism is effectively disconnected from our worker threads—calling `workers_[i].request_stop()` will not wake that worker, because worker_loop is not listening to that `jthread`'s stop_token.

It also means our explicit destructor is mandatory, not optional. If we relied on the default one, members would be destroyed in reverse declaration order: `stop_source_` and `cv_any_` would be destroyed before `workers_`, and the `request_stop()` called by the destructing `jthread`s would never reach our worker_loop—the result is `join()` blocking forever: deadlock. The explicit destructor first calls `stop_source_.request_stop()` + `cv_any_.notify_all()` to make sure the worker threads exit; only then can the `jthread` destructor's `join()` return smoothly.

You might wonder: could moving the `jthread`s during vector reallocation cause problems? The answer is no—once a `jthread` has been moved, the original object's `joinable()` becomes `false`, and its destructor skips both `request_stop()` and `join()`. Ownership of the thread's execution has already transferred to the new `jthread` object, completely unaffected.

At this point you have probably noticed: C++20's stop_token mechanism is pleasant to use, but its interaction with a thread pool is not as simple as you might imagine—the `stop_source` `jthread` manages automatically and the `stop_source_` we create manually each mind their own business, and we have to coordinate their timing by hand in the destructor.

My advice: if your project is still on C++17 or an earlier standard, `std::thread` plus a manual `stop_` flag is perfectly fine—don't introduce unnecessary complexity just to use new features. The thread + mutex + condition_variable combination settled in the C++11 era has been battle-tested for over a decade; the odds of it biting you are far lower than the odds of you wrestling with C++20 new features. If you are fully on C++20 and `jthread` and `stop_source` are already in wide use in your project, then using them to manage the pool's stop state is reasonable—just be mindful of the "two stop_sources" issue mentioned above.

Below is a complete, battle-tested C++17 version. It does not depend on C++20's `jthread` or `stop_token`, yet its structure is clear and its functionality complete:

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

    // Forbid copying and moving
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

We have also dealt with a few easy-to-hit pitfalls in one go. Copying and moving are disabled—the pool holds `std::thread`s and a `std::mutex`, neither of which is copyable, and the pool's lifetime management should not be scrambled by a move (imagine a moved-from pool joining, in its destructor, threads it no longer owns—what a scene that would be). The destructor checks `joinable()` before joining—under normal circumstances the threads are certainly joinable, but defensive programming never hurts; what if someone joined them behind your back?

## The Worker Thread Lifecycle

A pool's worker threads actually cycle among three states: idle waiting, task execution, and shutdown exit. Understanding this lifecycle matters a lot when debugging pool-related problems—most of the "tasks don't run" and "pool is stuck" bugs you will encounter leave their clues in the state transitions.

In the constructor, each worker thread enters `worker_loop()` immediately after being created. Since the queue is empty at that point, the worker blocks on `cv_.wait()`, entering the idle-waiting state. This blocking is efficient—the operating system suspends the thread, and it consumes no CPU time slices until `cv_.notify_one()` or `cv_.notify_all()` wakes it.

When `submit()` pushes a task and calls `cv_.notify_one()`, one—and exactly one—waiting worker is woken. It takes a task from the queue, releases the lock, and executes the task outside the lock. Executing outside the lock is a critical design decision—if the task ran while holding the lock, every other worker thread and every `submit()` call would be blocked, and the whole pool would degrade into serial execution, at which point multiple threads would be pointless. When the task finishes, the worker returns to the top of the loop, reacquires the lock, and checks the queue. If the queue is empty, it blocks in `wait()` again; if tasks remain, it takes one and executes it right away, without waiting—this "after finishing a task, check the queue yourself" behavior avoids unnecessary notify overhead.

The shutdown path is triggered in the destructor: set `stop_ = true` and call `cv_.notify_all()`. All workers wake up and check `stop_ && tasks_.empty()`. If the queue is empty, the worker exits the loop normally and the thread ends; if the queue still has tasks, the worker keeps executing and exits only when the queue drains.

You might ask: what happens if a worker is in the middle of a very long task and the destructor is called at that moment? The answer: that worker will not respond to the stop request immediately. It keeps executing the current task, and only after the task completes and it returns to the top of the loop does it check the `stop_` flag. So, **if your tasks may run for a long time, the pool's destructor may block for a long time**. This is not a bug—it is the price of graceful shutdown. Either you wait for the task to finish, or you use a more aggressive approach (such as `timed_wait` plus detach as a fallback)—but a detached thread may access already-destroyed objects, and that trade never works out well.

## A Complete Practical Example

Now let's string all of the capabilities above together and write a comprehensive example: computing the processing results of a batch of data in parallel, where the processing function may throw, and we need to correctly handle both normal results and exceptions. This example simulates a very common production scenario—batch processing a pile of data where some items are bad and cause failures, and you need to know which ones succeeded and which failed.

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <stdexcept>

// Simulate a processing function that may fail
double process_data(int id, double value)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (value < 0) {
        throw std::runtime_error(
            "数据 " + std::to_string(id) + " 无效: 值为负数");
    }

    // Simulate the computation
    return value * value + std::sqrt(value);
}

int main()
{
    ThreadPool pool(4);

    std::vector<double> inputs = {1.0, 4.0, -2.0, 9.0, 16.0, -5.0, 25.0, 36.0};
    std::vector<std::future<double>> futures;

    // Submit all tasks
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        futures.push_back(
            pool.submit(process_data, static_cast<int>(i), inputs[i]));
    }

    // Collect the results
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

This code shows the pool's typical usage in a real-world scenario: submit a batch of tasks, then collect the results one by one. You will notice the overall experience is very close to synchronous code—the only difference is that the tasks execute in parallel in the background while you pick up the results through `future.get()`. Exceptions propagate automatically through the future, and the caller handles asynchronous exceptions exactly as it would synchronous ones.

## Common Pitfalls in Practice

At this point we have implemented all of the pool's core functionality, but a few common traps in real-world use deserve their own discussion. I have personally stepped in every one of these—hopefully this saves you some detours.

First, the trouble with `std::bind` and passing by reference. Our `submit()` uses `std::bind` to bind arguments, but `std::bind` stores arguments by value by default—if your argument is a large object, it gets copied. To pass a reference, you need to wrap it in `std::ref()` or `std::cref()`. The better approach is replacing `std::bind` with a lambda outright: the capture list lets you precisely control whether each argument is passed by value or by reference, and the code is usually more readable than `std::bind`. If you want to replace `std::bind` with a lambda, submit's implementation can be simplified to this:

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

The caller can then bind arguments and references in the lambda themselves:

```cpp
std::string large_data = "...";
auto fut = pool.submit([&large_data, x, y] {
    return process(large_data, x, y);
});
```

This is far more flexible than `std::bind`, and the lifetime relationships are plain at the call site—capturing a reference means the caller must guarantee that `large_data` stays valid until the task finishes executing. This is an iron law of asynchronous programming; no tool will bend it for you.

Next, future leaks. If you submit a task but never call `get()` or `wait()`, you receive no error of any kind—the task may have quietly finished in the background, or it may have thrown and the exception was swallowed, and you would be none the wiser. One defensive practice is to state clearly in submit's documentation that "every future must be consumed", or to track the count of unconsumed futures in debug mode. I have paid for this in a real project: a background task's future was ignored, the exception inside the task vanished without a sound, and it took a long investigation to pin it down.

Finally, the most insidious one: the pool's lifetime mismatching the lifetime of the objects its tasks reference. If your task captures a reference to a stack variable, and the pool's destruction happens after the stack variable's (say the pool is global or static), you are facing a dangling reference. The root of this problem is not the thread pool itself but the fundamental question of asynchronous programming—"who guarantees whose lifetime": the moment an async task executes is indeterminate, so every external reference you capture must remain valid across the entire window in which the task might execute. There is no good solution—the best you can do is think about this question when designing the API, and prefer capture by value or `shared_ptr` to extend lifetimes.

## Exercises

If you want to truly internalize this article, the following three exercises are worth doing by hand. They each extend our thread pool in one direction—priority scheduling, timed shutdown, and work stealing—and every one is a common requirement in production environments.

### Exercise 1: A Priority Thread Pool

Add priority support to the pool's task queue. Replace `std::queue` with `std::priority_queue`, and extend the task type to a pair containing a priority and a callable. Submission allows specifying a priority, and worker threads always take out the highest-priority task to execute.

Hint: `std::priority_queue` is a max-heap by default; you can define a `Task` struct containing `int priority` and `std::function<void()> func`, and overload `operator<` so that larger priority values dequeue first.

### Exercise 2: Time-Limited Shutdown

Add time-limited shutdown logic to the pool's destructor: if some workers still haven't exited within a given time (say, 5 seconds), give up waiting and detach them. Mind the risks of detaching—a detached thread may access already-destroyed objects. Think about how to implement time-limited shutdown safely (hint: let tasks check an "is the pool still alive" flag).

### Exercise 3: Work Stealing

Implement simple work stealing for the pool: each worker has its own local task queue and takes tasks from the local queue first. When the local queue runs dry, it tries to "steal" tasks from other workers' queues. Work stealing reduces contention between threads (because most of the time each thread only touches its own local queue) and is a common optimization in high-performance thread pools.

## Summary

Here, we have built a complete thread pool from scratch, covering virtually every core question in C++ thread pool design.

The pool's basic anatomy is worker threads, a task queue, and synchronization primitives (mutex + condition_variable). Worker threads are created at construction time, settle into idle waiting, and take tasks from the queue to execute when notified. At shutdown, first set the stop flag, then notify_all to wake every worker, and the workers exit after executing the remaining tasks—this flow looks simple, but every one of its timing details (notifying while holding the lock, the semantics of the stop condition, the order of join) deserves careful thought.

The `submit()` interface achieves type erasure and future returns through `packaged_task` + `shared_ptr`. The `packaged_task` binds the callable and its arguments together and automatically handles the propagation of return values and exceptions; the `shared_ptr` wrapper solves the problem that `packaged_task` cannot be copied; the lambda capturing the `shared_ptr` implements the type erasure from `packaged_task<R()>` to `std::function<void()>`. The combination of these three is the "standard play" of C++ thread pools—master it and you will be able to read the implementations of the vast majority of open-source thread pools.

Exceptions propagate automatically through `packaged_task`'s internal machinery: when a task throws, the exception is stored in the shared state, and the caller receives it via `future.get()`. This makes cross-thread exception handling as natural as synchronous code—provided you remember to call `get()`; otherwise the exception is silently swallowed.

C++20's `jthread` and `stop_token` can simplify the pool's shutdown logic, but note that `std::condition_variable` does not support `stop_token`—you need to switch to `std::condition_variable_any`. Additionally, a manually created `stop_source` and the `stop_source` built into `jthread` can be inconsistent with each other, which requires careful handling in practice. If you are on C++17, the manual stop flag approach is entirely sufficient—no need to force your way onto C++20.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); see `code/volumn_codes/vol5/ch05-future-task-threadpool/`.

## References

- [std::packaged_task — cppreference](https://en.cppreference.com/w/cpp/thread/packaged_task)
- [std::condition_variable_any::wait — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait)
- [std::jthread — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_token — cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Why does C++20 std::condition_variable not support std::stop_token? — Stack Overflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
- [Thread Pool C++ Implementation — Code Review Stack Exchange](https://codereview.stackexchange.com/questions/221617/thread-pool-c-implementation)

---

> **Self-assessment**: If you are not yet comfortable with the basic usage of packaged_task, future, and condition_variable, it is worth reviewing the first three articles of ch05 first. A thread pool is, in essence, the combined application of these components—once you understand the parts, the assembly follows naturally.
