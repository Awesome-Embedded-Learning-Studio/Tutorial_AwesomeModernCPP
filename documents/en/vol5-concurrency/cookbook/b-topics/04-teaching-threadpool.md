---
title: "A Teaching-Grade Thread Pool: A Five-Step Evolution"
description: "Starting from a minimal skeleton of workers plus a task queue plus condition_variable, evolving in five steps to submit returning a future, exceptions propagating across threads, and graceful shutdown, then the C++20 jthread + stop_token retrofit and the trap of two stop_sources."
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
cpp_standard: [17, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/04-teaching-threadpool.md
  source_hash: 222a3e43469458c4367453687b5aa2f9651bc344d07c317aa1cad6e993edc797
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 28491
---
# A Teaching-Grade Thread Pool: A Five-Step Evolution

Thread pools are explicitly drawn outside the Volume 5 required line — production-grade pools with their full scheduling, resource control, and maintenance demands do not enter the requirement. The whole piece migrated into the readings section; we treat it as a complete teaching-grade implementation told in five evolutionary steps, each step adding exactly one capability.

Before reading it, you need Unit 4's asynchronous infrastructure: the future usage of [Running Computations with std::async](../../chapter04-organizing-tasks/01-launch-with-async), the result delivery of [Delivering Results Yourself: std::promise and std::future](../../chapter04-organizing-tasks/03-promise-and-future), and the `std::packaged_task` dedicated piece (carrying material from the same section) at [Wrapping Computations into Storable, Dispatchable Tasks](../../chapter04-organizing-tasks/04-packaged-task). Unit 3's condition variables ([Waiting for a Condition: From Polling to Condition Variables](../../chapter03-wait-and-coordinate/01-wait-for-a-condition)) and Unit 1's [jthread and the stop protocol](../../chapter01-start-concurrency/06-jthread-and-stopping) should also be within reach.

The migration trail: mechanism explanations, code, and exercises are preserved as they were; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The material's environment notes and "tested and passing" claims remain in the text as-is; I did not recompile and re-verify them this round.

In Unit 4 we took apart the asynchronous infrastructure piece by piece: `std::async`, `std::future`, `std::promise`. The queue we had running used a single worker thread; submit four tasks and they could only run one after another in line — no parallelism to speak of, no essential difference from calling directly on the main thread.

What we do now is extend that single-worker queue into a real thread pool: a set of pre-created worker threads sharing one task queue, taking tasks and executing them concurrently. The thread pool is one of the most used concurrency patterns in production; it avoids the system overhead of constantly creating and destroying threads, lets you control the degree of concurrency (the thread count), and paired with `packaged_task` / `future` delivers results and exceptions back to the submitter cleanly.

In this piece we build a fully functional thread pool from scratch, each step adding one capability on top of the previous. The stages run like this: erect a minimal skeleton with only `enqueue()` and get multiple workers running. Then add `submit()` returning a `future` so callers can obtain results. Next handle exceptions propagating across threads. Then design the graceful shutdown sequence — stop accepting new tasks, drain the queue, and join all workers. Finally see how C++20's `jthread` + `stop_token` can simplify the shutdown logic.

> **Environment note**: all code in this piece is based on C++17, gcc 12+, clang 15+, MSVC 19.34+; we tested it passing on x86-64 Linux and macOS. The final step's C++20 retrofit needs a compiler with `<stop_token>` support: gcc 10+, clang 17+ (partial on libc++, full from Clang 20), MSVC 19.28+.

## A Minimal Viable Thread Pool

Do not rush into the fancy stuff — submit returning future, exception propagation; let us erect the core skeleton first. A runnable thread pool has a truly classic structure: N worker threads share one task queue, the queue is guarded by `std::mutex`, and a `std::condition_variable` notifies workers when new tasks arrive. That simple.

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

This structure is the prototype of nearly every C++ thread pool. Let us walk through its core components one by one and get clear on what each part does.

`workers_` is a set of pre-created `std::thread` objects, created in a loop by the constructor, each running the same `worker_loop()`. The thread count you can leave to `std::thread::hardware_concurrency()` or set manually by task character. For CPU-hungry tasks, keeping threads close to the core count is right — more actually slows things down through context switching. For tasks mostly waiting on I/O, you can go somewhat higher: while threads wait on I/O, the freed CPU is put to good use by other threads.

`tasks_` is a `std::queue<std::function<void()>>` — every task is type-erased into a `std::function<void()>` before entering the queue. Whether you submit a function returning `int`, a lambda returning `std::string`, or a callable returning nothing, inside the queue they all carry the `void()` signature. How to unify callables of different signatures into `void()` while preserving the return value is the problem we solve next.

`mutex_` and `cv_` are the heart of the pool's synchronization; know them separately: `mutex_` guards the `tasks_` queue and the `stop_` flag, ensuring only one thread operates on the queue at a time. `cv_` notifies workers: a new task has arrived (`notify_one`) or it is time to stop (`notify_all`).

The `stop_` flag controls the shutdown sequence. When the destructor sets `stop_ = true` and calls `notify_all()`, all workers wake. Note the worker's exit condition: it is not "exit the moment `stop_` is true" — the real exit condition is "stop_ is true **and** the queue is empty", which guarantees that submitted-but-not-yet-executed tasks are not dropped.

Let us verify it runs with a simple test:

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

You will see the 8 tasks spread across the 4 threads: the first 4 start almost simultaneously, and the next 4 follow once the previous batch finishes.

Good — the skeleton now stands. But this version has an obvious flaw: `enqueue()` returns nothing. You submit a task, the task runs to completion, and you cannot get the result — awkward. If the task throws, it gets worse: the exception is swallowed by the invocation of the `std::function<void()>`; the exact behavior is implementation-defined, usually a call to `std::terminate` that kills the program outright. Next we solve this.

## Making submit() Return a future

Returning a future via `packaged_task` + `shared_ptr` — the idea was already laid out in Unit 4's promise-and-future discussion ([Delivering Results Yourself](../../chapter04-organizing-tasks/03-promise-and-future)); we continue with it here. The thread pool needs the same pattern, except that now multiple workers take tasks from the queue simultaneously — no problem though: `packaged_task` itself is thread-safe (the shared state is set exactly once), as long as we never invoke the same `packaged_task` from multiple threads at once.

The goal we set for ourselves: a `submit()` template function that accepts any callable and arguments and returns a `std::future<R>`, where `R` is the callable's return type. The caller can `get()` the result from that future, or receive the exception in the failure case.

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

Several points in this code deserve careful words, because each is an important detail realized only after paying for it.

One by one. `std::invoke_result_t<F, Args...>` is the C++17 type trait deducing the return type of `F(Args...)`. It is more general than C++11's `std::result_of`, correctly handling member-function pointers, function objects with reference qualifiers, and the like. `ReturnType` is the task's return type; it decides the `packaged_task`'s signature and the `future`'s template argument.

`std::make_shared<std::packaged_task<ReturnType()>>` binds the callable and its arguments together into a `packaged_task` with signature `ReturnType()`. Here `std::bind` pre-binds the arguments — the queue stores `std::function<void()>`, which accepts no arguments, so we must bind the arguments onto the callable, forming a zero-argument callable entity.

Then we wrap the `packaged_task` in a `shared_ptr`. This step matters greatly and is exactly where many beginners get stuck — `std::function<void()>` requires its callable to be copyable, while `std::packaged_task` is move-only and cannot go into a `std::function` directly. Wrapped in a `shared_ptr`, the lambda captures a `shared_ptr` (copyable), while the `packaged_task` itself has exactly one instance managed by the `shared_ptr`. This trick is practically standard equipment in thread-pool implementations; you will find it in every serious C++ thread pool.

Look at the enqueue line: `tasks_.push([task]() { (*task)(); })` pushes a lambda into the queue. This lambda captures the `shared_ptr<packaged_task<R()>>` and, when invoked, dereferences and executes the `packaged_task`. Once invoked, the internal promise automatically sets the return value or stores the exception, and the future in the caller's hand becomes ready.

One more detail to note: we check `stop_` before pushing the task. Once the pool has entered shutdown, it should accept no new tasks — throw outright. This avoids undefined behavior from submitting during shutdown — picture it: you certainly do not want your task pushed into a queue only to discover every worker thread has exited, leaving the task forever unexecuted.

Here is a complete usage of submit:

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

Three tasks go into the pool and run in parallel on different worker threads. As you can see, the compiler deduces the future type `submit()` returns — `f1` and `f2` are `std::future<int>`, and `f3` is `std::future<std::string>`.

## Letting Exceptions Propagate Across Threads

Exception handling in asynchronous code is an easy place to go wrong; I have crashed there more than once myself. If your task throws on a worker thread and you do not handle it correctly, the exception is lost — the worker does not crash (the `std::function` invocation mechanism catches it), but you never get a result either, and the program's behavior becomes an eerie "silent failure". Such bugs are harder to chase than outright crashes — at least a crash shows you a stack.

Fortunately `packaged_task` already handles this for us. When the wrapped function throws, `packaged_task` internally captures the exception with `std::current_exception()` and stores it in the shared state. When the caller fetches via `future.get()` and the shared state holds an exception, `get()` rethrows it. The whole process is transparent to the caller — you just try-catch around `get()`.

Let us verify with an example:

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

The exception travels from the worker thread to the main thread with its type information fully intact. No error-code system to design, no exception messages to serialize into strings, no global error callback — the `packaged_task` + `future` combination wraps cross-thread exception propagation cleanly. This deserves a moment of appreciation: C++'s exception mechanism is inherently stack-unwinding oriented, a natural fit for synchronous calls. Propagating exceptions across threads was a troublesome business, but `packaged_task` captures and stores `std::current_exception()` internally, and the caller's `future.get()` rethrows it — handling it feels exactly like handling a synchronous exception.

But here is a genuine trap — if you submit a task and never call `future.get()`, the exception is silently swallowed. This differs from the future returned by `std::async`: that one blocks in its destructor until the task finishes, while a future associated with a `packaged_task` merely releases its reference to the shared state on destruction, no waiting. So: **a future obtained from the pool's submit() must either be `get()`-ed, or at least `wait()`-ed to confirm the task completed** — do not lose the exception.

## The Ordering of Graceful Shutdown

Shutting down a thread pool sounds simple — just have the workers exit, right? But the story does not end there; the real trouble lies in the shutdown ordering. At shutdown the queue may still hold unexecuted tasks, and a running task may be mid-flight. Kill the workers brutally (detaching or terminating outright) and submitted tasks are dropped while running tasks leave half-finished state behind — picture a thread mid-way through writing a file being killed, and you will grasp the scale of the disaster.

A "graceful" shutdown sequence, in three steps: stop accepting new tasks (`submit()` throws or returns an error). Then let the worker threads finish every remaining task in the queue. Finally, all workers exit normally and the destructor joins them.

Back to the exit condition inside `worker_loop()`:

```cpp
cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
if (stop_ && tasks_.empty()) {
    return;
}
```

The condition means: once woken, if `stop_` is true and the queue is empty, the worker exits. If `stop_` is true but tasks remain, the worker keeps taking and executing them until the queue drains. That is the "drain the queue" semantics — we drop no tasks, we merely stop accepting new ones.

Now look back at the destructor's shutdown sequence:

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

Several ordering points here need spelling out.

Setting `stop_` must happen while holding the lock. Although `stop_` is only read and written after acquiring the lock — theoretically not needing atomic — putting the modification inside the lock's protection makes the intent clearer. "Hold the lock when modifying shared state" is basic discipline in concurrent programming; we need not economize on this lock.

`notify_all()` is called after releasing the lock. Not mandatory — the standard permits notifying while holding the lock. But notifying after release is a common optimization: the woken workers need to acquire that same lock (they really do), so waking them after release avoids the useless context switches of "wake, fail to grab the lock, block again".

`join()` must come after `notify_all()`. Join first and notify later, and the workers never receive the stop signal — `join()` blocks forever: a deadlock. The order must be notify first, wait after.

This shutdown machinery carries an implicit guarantee: when the destructor returns, every submitted task has finished executing. `join()` blocks until the workers exit, and when a worker exits the queue is necessarily empty. This matters greatly for resource cleanup — no background thread is touching destroyed objects after destruction.

## The C++20 Retrofit: jthread + stop_token

Up to this step we have managed the pool with `std::thread` plus a manual `stop_` flag, a manual `notify_all()`, and a manual `join()`. The combination works, but writing it is genuinely verbose — every time you must remember to set the flag, notify, and join; miss one step and you get a deadlock or a resource leak. C++20 introduced `std::jthread`, `std::stop_token`, and `std::stop_source`, and with `std::condition_variable_any`'s `stop_token` support the shutdown logic shrinks considerably.

One important detail to hand over, and one many tutorials get wrong: `std::condition_variable` (without the `_any`) has **no** C++20 stop_token overload. The stop_token wait integration is provided only on `std::condition_variable_any`. The reason: `std::condition_variable` supports exactly one lock type, `std::unique_lock<std::mutex>`, while `std::condition_variable_any` is a template class accepting any lock type satisfying BasicLockable — a templated design that makes stop_token integration more natural. If you call `wait(lock, stop_token, predicate)` on a `std::condition_variable` in your code, the compiler rejects it outright — do not ask me how I know.

Here is the thread pool after the jthread + stop_token retrofit — let us look at it together:

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

Now let us look at the key differences from the previous version.

The first change: workers are now `std::jthread`. The `jthread` constructor accepts a callable taking `std::stop_token` as its first parameter, automatically creates an internal `std::stop_source`, and passes the corresponding `stop_token` to your function. You no longer maintain the `stop_` flag yourself — its lifetime management is handled inside `jthread`.

Second, the conditional wait switches to the stop_token overload of `std::condition_variable_any`. The overload's signature is `wait(lock, stop_token, predicate)`; its behavior: with predicate true it returns true immediately. When stop is requested it also returns immediately, with the predicate's current value (usually false). This replaces the manual `stop_`-flag checking — when `request_stop()` is called, `cv_any_.wait()` wakes automatically, and the destructor no longer needs a manual `notify_all()`.

Third, the destructor is simpler. A `jthread` automatically calls `request_stop()` then `join()` on destruction, so you could even leave the destructor implicit — but we keep an explicit one, because a `notify_all()` is still needed to wake workers that may be waiting.

But this version has an inelegant spot: `stop_requested()` relies on inspecting the stop_source of `workers_[0]`. That breaks when workers_ is empty (the constructor guarantees at least one worker, but leaning on such an implicit assumption always feels uncomfortable). A cleaner approach: let the pool itself hold a `std::stop_source` and pass its associated `stop_token` to every worker. The code grows slightly more complex, but the semantics get clearer. Here is the improved version:

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

This version manages the stop state with the pool's own `stop_source_`; let us sort it out: `submit()` checks `stop_source_.stop_requested()` to decide whether the pool still runs, and the destructor calls `stop_source_.request_stop()` to trigger shutdown. Every worker receives the same stop_token via `stop_source_.get_token()` — when `request_stop()` is called, every wait holding that token wakes up.

Note the subtlety here: we pass the `stop_token` to worker threads by lambda capture rather than relying on `jthread`'s automatic parameter passing. The reason: the `stop_token` a `jthread` creates automatically is associated with that `jthread`'s own `stop_source`, so calling `request_stop()` on one `jthread` cancels only that thread. What we want is one `request_stop()` call canceling all workers. Hence a shared `stop_source`, whose `stop_token` is distributed to every worker.

This version is semantically clean, but an architectural issue deserves your attention: the `stop_source` built into `jthread` and our manually created `stop_source_` are two independent stop sources. On destruction a `jthread` calls `request_stop()` on its own built-in `stop_source`, while our worker_loop listens to the one we created manually. That means `jthread`'s own stopping machinery is actually disconnected from our worker threads: calling `workers_[i].request_stop()` does not wake that worker, because worker_loop is not listening to the `jthread`'s stop_token.

It also means our explicit destructor is mandatory, not optional. Relying on the default destructor, members are destroyed in reverse declaration order: `stop_source_` and `cv_any_` die before `workers_`, and the `request_stop()` called by the dying `jthread`s cannot reach our worker_loop — the result is `join()` blocking forever: deadlock. The explicit destructor calls `stop_source_.request_stop()` + `cv_any_.notify_all()` first, ensuring the worker threads exit, so the `join()` in the `jthread` destructors can return smoothly.

You might wonder: does moving `jthread`s during vector growth cause trouble? No — after a `jthread` is moved, the original object's `joinable()` becomes `false`, and its destructor skips `request_stop()` and `join()` outright. The thread's ownership has transferred to the new `jthread` object and is unaffected.

At this point you will notice: C++20's stop_token machinery, handy as it is, does not interact with thread pools as simply as imagined. The `stop_source` `jthread` manages automatically and our manual `stop_source_` each mind their own business, and the destructor must manually coordinate their timing.

My advice: if your project still sits on C++17 or earlier, `std::thread` + a manual `stop_` flag is perfectly fine — do not import unnecessary complexity just to use new features. The thread + mutex + condition_variable combination laid down in the C++11 era has been battle-tested for over a decade; its bug probability sits far below what you get while wrestling with C++20 novelties. If you are fully on C++20 and `jthread` and `stop_source` are already widely used in your project, managing the pool's stop state with them is reasonable — but mind the "two stop_sources" issue above. For the full jthread and stop_token protocol, Unit 1's [Letting Work End in Order](../../chapter01-start-concurrency/06-jthread-and-stopping) is the home chapter.

Here is a complete, battle-tested C++17 version — no dependency on C++20's `jthread` and `stop_token`, yet clean in structure and complete in function:

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

A few easy-to-hit mistakes are also handled here. Copying and moving are disabled — the pool holds `std::thread` and `std::mutex`, both non-copyable, and the pool's lifetime management should not be scrambled by a move (picture the moved-from pool's destructor joining threads it no longer owns — quite the scene). The destructor checks `joinable()` before joining; normally the threads are certainly joinable, but defensive programming never hurts — what if someone joined them without telling you?

## The Worker Thread Lifecycle

A pool's worker threads actually cycle among three states: idle waiting, executing a task, and shutdown exit. Understanding this lifecycle matters for troubleshooting pool-related problems; most of the "tasks not executing" and "pool stuck" bugs you meet leave their clues in the state transitions.

In the constructor, each worker thread enters `worker_loop()` immediately after creation. Walk through it: the queue is empty, so the worker blocks on `cv_.wait()`, entering idle waiting. This blocking is efficient — the OS suspends the thread, spending no CPU time slice, until `cv_.notify_one()` or `cv_.notify_all()` wakes it.

When `submit()` pushes a task and calls `cv_.notify_one()`, one — exactly one — waiting worker wakes. It takes the task from the queue, releases the lock, and executes the task outside the lock. Let us stress once: executing outside the lock is a critical design decision; execute while holding it and every other worker plus every `submit()` call blocks, the whole pool degenerates into serial execution, and multithreading loses its meaning. After finishing, the worker returns to the top of the loop, re-acquires the lock, and checks the queue. Empty: back into `wait()`. More tasks: take one and run, no waiting needed. This "check the queue after finishing a task" behavior avoids unnecessary notify overhead.

The shutdown path triggers in the destructor; walk it once more: set `stop_ = true` and call `cv_.notify_all()`. All workers wake and check `stop_ && tasks_.empty()`. Queue empty: the worker exits the loop normally and the thread ends. Tasks remaining: the worker keeps executing until the queue drains.

You might ask: what happens if a worker is deep in a long task when the destructor runs? The answer — that worker does not respond to the stop request immediately. It keeps executing the current task and checks the `stop_` flag only after returning to the top of the loop. So: **if your tasks may run long, the pool's destruction may block long**. This is not a bug; it is the price of graceful shutdown — either wait it out or go aggressive (say `timed_wait` + detach as a fallback), but a detached thread may touch destroyed objects, a bad deal however you count it.

## A Complete Practical Example

Now let us string every capability together in a comprehensive example: parallel-processing a batch of data where the processing function may throw, and we must handle both normal results and exceptions correctly. It simulates a scenario common in production — batch processing where some items fail, and you need to know which succeeded and which failed.

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

This code shows the pool's typical use in a real scenario: submit a batch of tasks, then collect the results one by one. You will find the whole experience very close to synchronous code; the only difference is that tasks execute in parallel in the background and you receive results through `future.get()`. Exceptions propagate automatically through the future, and the caller handles asynchronous exceptions just like synchronous ones.

## Mistakes Easy to Make in Practice

By now the pool's core functionality is all implemented, but a few common mistakes in real use deserve their own words. I have made every one of them personally and hope to save you the detours.

The first is the `std::bind` and reference-passing problem. Our `submit()` uses `std::bind` to bind arguments, but `std::bind` stores arguments by value by default — a large object gets copied. To pass a reference, wrap it in `std::ref()` or `std::cref()`. The better approach: replace `std::bind` with a lambda outright. A lambda's capture list gives you precise control over each argument by value or by reference, and the code is usually more readable than `std::bind`. If you want the lambda in place of `std::bind`, submit simplifies to this:

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

The caller binds arguments and references in the lambda itself; an example:

```cpp
std::string large_data = "...";
auto fut = pool.submit([&large_data, x, y] {
    return process(large_data, x, y);
});
```

Far more flexible than `std::bind`, and the lifetime relationships are plain at the call site: capturing by reference means the caller must guarantee `large_data` stays valid until the task finishes. There is no negotiating this point in asynchronous programming; no tool lets you route around it.

The second is future leakage. Submit a task and never call `get()` or `wait()`, and no error surfaces — the task may have finished quietly in the background, or thrown an exception that was then swallowed, all unnoticed. One defensive move: state clearly in submit's documentation that "every future must be consumed", or track unconsumed futures in debug builds. I paid this price in a project once: a background task's future was ignored, the task's exception vanished without a sound, and it took a long hunt to pin it down.

The last, and the most insidious: the pool's lifetime mismatching the lifetime of objects its tasks reference. If a task captures references to stack variables while the pool's destruction happens after those variables die (say the pool is global or static), you face dangling references. The root of this problem is not the pool itself; it lands on the fundamental asynchronous-programming question of "who guarantees whose lifetime". A task's execution time is uncertain, so every external reference you capture must remain valid across the whole window in which the task might run. There is no good cure — think it through when designing the API, and prefer capture by value or `shared_ptr` to extend lifetimes.

## Exercises

If you want to truly internalize this piece, the three exercises below are worth your hands. They extend our pool in three directions — priority scheduling, timed shutdown, and work stealing — each a common production need.

### Exercise 1: A Thread Pool with Priorities

Add priority support to the pool's task queue. Replace `std::queue` with `std::priority_queue`, extending the task type to a pair of priority and callable. Submission allows specifying a priority, and workers always take the highest-priority task.

Hint: `std::priority_queue` is a max-heap by default. Define a `Task` struct holding `int priority` and `std::function<void()> func`, and overload `operator<` so tasks with larger priority values dequeue first.

### Exercise 2: Timed Shutdown

Add timed-shutdown logic to the destructor: if workers still have not exited within a deadline (say 5 seconds), give up waiting and detach them. Mind the risks of detach — a detached thread may touch destroyed objects. Think about how to implement timed shutdown safely; as a hint, let tasks check an "is the pool still alive" flag.

### Exercise 3: Work Stealing

Implement simple work stealing for the pool: each worker owns a local task queue and takes from it first. When the local queue empties, try to "steal" tasks from other workers' queues. Work stealing reduces inter-thread contention (threads mostly touch only their own queues) and is a common optimization in high-performance pools.

## Summary

Here we have built a complete thread pool from zero, covering nearly every core question in C++ thread-pool design.

The pool's basic composition: worker threads, a task queue, plus a set of synchronization primitives (mutex + condition_variable). Workers are created at construction, enter idle waiting, and take tasks from the queue when notified. Shutdown sets the stop flag, then notify_all wakes every worker, and workers exit after finishing remaining tasks — the flow looks simple, but the ordering details (notify timing relative to the lock, the semantics of the stop condition, the order of join) each deserve careful thought.

The `submit()` interface achieves type erasure and future return via `packaged_task` + `shared_ptr`. The `packaged_task` binds callable and arguments together and handles return-value and exception propagation automatically. The `shared_ptr` wrapper solves `packaged_task`'s non-copyability. The lambda capturing the `shared_ptr` performs the type erasure from `packaged_task<R()>` to `std::function<void()>`. This trio is the "standard play" of C++ thread pools; with it in hand you can read the implementation of most open-source pools.

Exceptions propagate automatically through `packaged_task`'s internal machinery: when a task throws, the exception is stored in the shared state, and the caller receives it via `future.get()`. Cross-thread exception handling thus feels as natural as synchronous code — provided you remember to call `get()`, or the exception is swallowed silently.

C++20's `jthread` and `stop_token` can simplify pool shutdown, but note that `std::condition_variable` does not support `stop_token` — you must switch to `std::condition_variable_any`. Also, a manually created `stop_source` and the `jthread`'s built-in `stop_source` can disagree, and real use demands careful handling. In a C++17 environment, the manual stop flag serves perfectly well; no need to force C++20.

> Some of the sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) under `code/volumn_codes/vol5/ch05-future-task-threadpool/`; for the rest, the code blocks in this text are authoritative — take and compare as you like.

## Reference Resources

- [std::packaged_task — cppreference](https://en.cppreference.com/w/cpp/thread/packaged_task)
- [std::condition_variable_any::wait — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait)
- [std::jthread — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_token — cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Why does C++20 std::condition_variable not support std::stop_token? — Stack Overflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
- [Thread Pool C++ Implementation — Code Review Stack Exchange](https://codereview.stackexchange.com/questions/221617/thread-pool-c-implementation)

---

> **Self-assessed difficulty**: if packaged_task, future, and condition_variable basics still feel shaky, review the first three pieces of Unit 4. A thread pool is essentially the combined use of these components — understand the parts, and the assembly follows naturally.
