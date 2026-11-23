---
title: "Wrapping a Computation into a Storable, Dispatchable Task"
description: "packaged_task bundles a callable together with its supplying end into one object: the template parameter is a function signature, invoking it executes, values and exceptions are packed into the shared state automatically, move-only keeps the task storable and passable, and a single-worker SimpleTaskQueue strings submission and result-taking into one line."
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter04-organizing-tasks/04-packaged-task.md
  source_hash: 294b708864f0bac3d958b91b5271ccd4e2be925486bb3fa70d85506ba682eb1c
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 10644
---

# Wrapping a Computation into a Storable, Dispatchable Task

This article does exactly one thing: wrap an ordinary function into something that can sit in a container, be moved to someone else, and queue up in line. The tool is `std::packaged_task`, in the standard since C++11 (per cppreference's labeling; the current draft keeps it under [futures.task]). It bundles the callable together with the supplying end: you invoke it, it executes the task, and the return value or exception goes into the shared state automatically. The task turns from "a function waiting to be called" into an object — whoever holds it grasps the authority to trigger execution and pack the value. The thread pool in the elective section later is padded, under its submit interface, with exactly this.

## Wrapping a Function into a Task

Start from the smallest example. `add` is an ordinary function; wrap it into a packaged_task, take the future, then move it into a thread to execute:

```cpp
#include <future>
#include <iostream>
#include <thread>

int add(int a, int b)
{
    return a + b;
}

int main()
{
    // 创建 packaged_task，封装一个可调用对象
    std::packaged_task<int(int, int)> task(add);

    // 获取关联的 future
    std::future<int> fut = task.get_future();

    // 在另一个线程上执行 task
    std::thread t(std::move(task), 10, 20);

    // 在主线程获取结果
    int result = fut.get();
    std::cout << "10 + 20 = " << result << "\n";

    t.join();
    return 0;
}
```

Recognize its shape against the code. The template parameter is a function signature: `int(int, int)` means taking two `int`s and returning `int`, and the callable you wrap must match that signature. `get_future()` hands you the future associated with the internal supplying end, with the same one-shot quota as 4.3's promise — a second call throws `future_error` with the error condition `future_already_retrieved` ([futures.task.members]/21.1); I verified this variant with a probe locally, and libstdc++ throws exactly that.

The entrance that triggers execution is the function call operator itself — write `task(10, 20)` and the task runs at once. It has no member named `run` or `execute`. The standard's stated effect: invoke the stored task; on normal return, store the return value into the shared state; on a throw, store the exception; the state becomes ready and all threads waiting on it are woken ([futures.task.members]/22). In the example above, execution happened on thread `t` because we moved the task into the thread constructor — where the task runs depends on who invokes it. From here, "task" and "thread" part ways, and building the queue later leans on exactly this step.

### Move-Only: Why No Copy

The copy constructor and copy assignment are both marked `= delete` in the class definition, with the standard noting no copy beside them ([futures.task.general]). Try to duplicate one and the compiler refuses on the spot; handing it to another thread or stuffing it into a container all goes through `std::move`. You have met this temperament on 4.1's future and 4.3's promise; packaged_task is the third.

We can derive the reasoning ourselves. One packaged_task is tied to one shared state underneath, with a one-shot quota for packing a value. Were copying allowed, several copies would point at the same shared state; each invoked once, the second value-packing would draw a `future_error` with the error condition `promise_already_satisfied` (/24.1) — the state is already ready and cannot take a second result. The standard simply refuses the road: one task, one owner; authority transfers with the move, and whoever invokes it owns the value-packing quota.

## Exceptions Packed Automatically: the Call Doesn't Throw, get Does

In 4.3, exception delivery took three steps: catch with `catch (...)`, pack into a pointer with `std::current_exception()`, store into the state with `set_exception`. packaged_task covers all three:

```cpp
#include <future>
#include <iostream>
#include <stdexcept>

int risky_func(int x)
{
    if (x == 0) {
        throw std::runtime_error("除零错误");
    }
    return 100 / x;
}

int main()
{
    std::packaged_task<int(int)> task(risky_func);
    std::future<int> fut = task.get_future();

    // 在当前线程调用 task（也可以在另一个线程）
    task(0);  // 传入 0，触发异常

    try {
        int result = fut.get();  // 重新抛出异常
        std::cout << "结果: " << result << "\n";
    } catch (const std::runtime_error& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }
    return 0;
}
```

```text
捕获到异常: 除零错误
```

Follow the control flow through the code. The `task(0)` line invokes the packaged_task's call operator; the `runtime_error` thrown inside `risky_func` never departs from this line — per the effect clause, the exception travels the same road as a return value, into the shared state (/22). The place the exception truly throws is `fut.get()`: the taking end waits for readiness, finds the state holding an exception, and re-throws it on your thread — the very one that was stored ([futures.unique.future]/19, the clause verified in 4.1). `main`'s `try` plus `catch` receives it, type and message identical to what `risky_func` threw.

This arrangement splits execution and aftermath into two places. The executing side only invokes; whether anyone catches the exception is not its business. The taking side only `get`s; values and exceptions come out of the same opening. In the example, invoking and taking both sit in `main`, but move that `task(0)` line onto any thread and the link still holds. The two lines you wrote by hand in 4.3 are done inside packaged_task for you here.

## Building a Single-Worker Task Queue

The value of wrapping into an object shows fully only once tasks enter a queue. This section builds a minimal task queue with a single worker thread — submission, execution, and result delivery, all three present. The whole class is laid out verbatim; read along with the comments:

```cpp
#include <future>
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class SimpleTaskQueue
{
public:
    using TaskType = std::function<void()>;

    SimpleTaskQueue()
    {
        worker_ = std::thread([this]() { worker_loop(); });
    }

    ~SimpleTaskQueue()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_one();
        worker_.join();
    }

    // 提交一个 packaged_task，返回对应的 future
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
            queue_.push([task]() { (*task)(); });
        }
        cv_.notify_one();

        return fut;
    }

private:
    void worker_loop()
    {
        while (true) {
            TaskType task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return done_ || !queue_.empty(); });
                if (done_ && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
        }
    }

    std::thread worker_;
    std::queue<TaskType> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{false};
};
```

The class is short; look at the two ends separately. `submit` is the submitting end: you pass in a callable and a set of arguments; it uses `std::bind` to bind function and arguments into a zero-argument call, packs it into a `packaged_task<ReturnType()>`, wraps that in a `shared_ptr`, dresses it as a `std::function<void()>` and pushes it into the queue, and hands the future from `get_future` straight back to you. The `shared_ptr` layer has a reason: `std::function` demands that what it holds be copyable, while packaged_task is precisely move-only and won't fit — so the copying happens on the pointer, and execution dereferences back to the task itself via `(*task)()`. `worker_loop` is the executing end: the worker thread hangs on the condition variable, waking only when the queue is non-empty or the closing notice arrives; on waking it takes one task and runs it, with the internal supplying end packing value or exception automatically.

Read the whole chain connected: you submit, the task enters the queue, the worker takes it out and executes, the supplying end packs the result automatically, and you wait for it on the future. 4.3's hand-built chain of two promise-plus-future pairs shrinks here into one call to `submit`.

Let's run three tasks on it — two function calls plus a lambda:

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

int heavy_compute(int x)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return x * x;
}

int main()
{
    SimpleTaskQueue queue;

    auto f1 = queue.submit(heavy_compute, 5);
    auto f2 = queue.submit(heavy_compute, 10);
    auto f3 = queue.submit([]() {
        return std::string("hello from task queue");
    });

    std::cout << "f1: " << f1.get() << "\n";
    std::cout << "f2: " << f2.get() << "\n";
    std::cout << "f3: " << f3.get() << "\n";
    return 0;
}
```

```text
f1: 25
f2: 100
f3: hello from task queue
```

All three output lines are there; read the timing against the single worker's discipline. `heavy_compute(5)` entered the queue first and executed first, and `f1`'s `get` waited around one second for 25. `heavy_compute(10)` queued second, another second — by the time `f2` got its 100, roughly two seconds had passed since submission. `f3`'s lambda costs nothing itself, but it sat at the queue's tail and only got its turn after two seconds; its `get` returns instantly. Whoever submits first runs first — that is a single worker thread's discipline. Parallelism is thread-pool work; the single-thread queue gives the skeleton: submission separated from execution, results and exceptions traveling back to the submitter along the future.

### Keeping the Return Type in Step: invoke_result_t and Trailing Return Types

`submit`'s return type sits after the parameter list — `auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>` — the trailing-return spelling. The `invoke_result_t<F, Args...>` inside the return type must reference the parameters F and Args, but a leading return type would be written before the function name, where they haven't entered yet and can't be reached. Moved behind the parameter list, the compiler meets F and Args first and deduces the return type second, and the order falls into place.

`std::invoke_result_t<F, Args...>` is a type trait that asks the compiler one thing for you: invoke F with this set of arguments, and whatever type comes back is what it gives. This tool entered only in C++17. On a compiler parked at C++11 or C++14, the same job was spelled `std::result_of_t<F(Args...)>`. `result_of` was deprecated in C++17 and removed in C++20; new code follows this article's `invoke_result_t`, and the queue code here is compiled with it too.

## Timed Waiting: the Wait Can Give Up, the Task Cannot Be Cancelled

A future has two timed waits, and `wait_for` is the common one: it returns when the state is ready or the time runs out, handing back a `std::future_status`. Give a three-second task a two-second deadline:

```cpp
#include <future>
#include <iostream>
#include <chrono>
#include <thread>

int slow_computation()
{
    // 模拟一个耗时 3 秒的计算
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 42;
}

int main()
{
    std::packaged_task<int()> task(slow_computation);
    std::future<int> fut = task.get_future();

    // 在独立线程执行
    std::thread t(std::move(task));

    // 设定 2 秒超时
    auto status = fut.wait_for(std::chrono::seconds(2));

    if (status == std::future_status::ready) {
        std::cout << "结果: " << fut.get() << "\n";
    } else if (status == std::future_status::timeout) {
        std::cout << "计算超时，放弃等待\n";
        // 注意：工作线程仍在运行，我们需要等待它结束
    } else {
        std::cout << "任务被延迟\n";
    }

    t.join();  // 确保线程正常结束
    return 0;
}
```

```text
计算超时，放弃等待
```

At the two-second mark, `wait_for` comes back with `timeout`; we print a line and give up the wait. What is given up must be seen clearly: only the waiting — the task itself is still running on its thread. The C++ standard library has no such thing as thread cancellation; once a thread runs, nothing outside can press it to a stop, and `main` still has to wait at `t.join()` for it to finish by itself. I ran this code locally: the timeout line appeared at two seconds, `join` waited one more second before returning, and the program took three seconds in all — not one second saved.

The real way out is making the task itself able to hear a stop. The cooperative-cancellation tools were prepared in Unit 1's [*jthread and the Stop Protocol*](../chapter01-start-concurrency/06-jthread-and-stopping): pass the stop token into the task, let the task glance at suitable checkpoints, and finish early when it sees one — then `join` can afford the wait. One more boundary from 4.2 bears reminding: on a deferred task, `wait_for` does nothing and forever returns `deferred`. This article waits on genuinely running tasks, so the problem doesn't arise. But someday swap your wrapped task to a deferred launch, and the timeout poll spins instantly — the scene 4.2's trap two played out.

## Your Turn to Try

Before putting pen to paper, I line up this article's banked questions; walk them against your own code:

1. Does the template parameter's function signature match the callable you wrapped? Parameter count, types, return value — whichever mismatches, the compiler will point at `packaged_task<R(ArgTypes...)>`; read the error along it instead of rushing around it.
2. Have you counted the `get_future` call sites? One-shot quota; the second is `future_already_retrieved`, same as 4.3's promise, probe-verified.
3. Can the submitted task throw? In whose hand is the `catch`, and is its type specific enough to match `what()`? The exception comes out of `get`; uncaught, it stays in the shared state and is destroyed with it — the destination covered in 4.1.
4. One task running twice is beyond packaged_task; a second `operator()` is `promise_already_satisfied`. The standard prepared a road in `reset()`: swap in a fresh shared state and abandon the old one (/28). If your task must re-run, which road do you take?
5. After the timed wait gives up, is the worker thread's destination arranged? `join` waits to the end; if you can't afford that, the task must learn to hear a stop.

One small experiment is left for you. Build a packaged_task, take its future via `get_future`, then never invoke it — let it destruct straight away, then call `get`. Guess first what the waiting side receives. Here is the answer from my machine: `future_error` with the error condition `broken_promise`. The destructor's effect clause writes that it abandons the shared state ([futures.task.members]/14) — leaving without fulfilling — and the waiting side receives a traceable word of explanation. 4.2's extension demonstrated abandonment for promise; packaged_task is the same breed.

## Reference Resources

Source note. This article's content comes from the packaged_task portion of the old ch05 directory's *promise and packaged_task*, with examples and prose fully rewritten; the promise and shared_future pieces have gone home to 4.3 and 4.1 respectively. During the rewrite I recompiled and re-ran all of this article's code locally: GCC 16.2.1, `-std=c++20 -pthread`, plus `-Wall -Wextra`, no warnings. The outputs quoted are single-run readings; the fixed strings printed by the three programs are verbatim-stable, while the two timestamps in the timeout section (the timeout line at two seconds, `join` waiting one more) follow the machine, and readings shift on other environments. The two small probes — the second `get_future` case and the never-invoked-then-destructed case — were also verified locally; the `future_error` messages are libstdc++'s wording, other implementations may differ, and the error-condition comparison is what the standard guarantees. The clauses on quotas, error conditions, and `reset` were checked against the current draft's [futures.task]; clause names match the official standard, and paragraph numbers may drift. Version accounting: packaged_task itself and the first two demos are C++11 material; the queue section's `submit` return type uses C++17's `invoke_result_t`, with the C++11/C++14 alternative `result_of_t` noted in the text, and the frontmatter follows the main line with 11.

- [std::packaged_task (cppreference)][cppref-pt], since C++11, move-only, get_future's one-shot quota, operator() packing value or exception, reset swapping in a new shared state.
- [std::promise (cppreference)][cppref-promise], the mechanism packaged_task uses internally for its supplying end; the protagonist of 4.3.
- [std::future (cppreference)][cppref-future], the taking end; get waits for readiness and re-throws the stored exception.
- [Class template packaged_task (current draft, futures.task)][draft-task], the class definition and no copy, the error conditions of get_future and operator(), the destructor abandoning the shared state, and reset's semantics.
- [C++11 Concurrency Tutorial - Futures (Baptiste Wicht)][wicht], the beginner tutorial cited by the old article, thorough on async and future.

[cppref-pt]: https://en.cppreference.com/w/cpp/thread/packaged_task
[cppref-promise]: https://en.cppreference.com/w/cpp/thread/promise
[cppref-future]: https://en.cppreference.com/w/cpp/thread/future
[draft-task]: https://eel.is/c++draft/futures.task
[wicht]: https://baptiste-wicht.com/posts/2017/09/cpp11-concurrency-tutorial-futures.html
