---
title: Thread Ownership and RAII
description: Wrapping `std::thread` with RAII to implement an exception-safe `joining_thread`
  guard and scope-exit cleanup
chapter: 1
order: 3
tags:
- host
- cpp-modern
- intermediate
- RAII
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 线程参数与生命周期
related:
- mutex 与 RAII 锁
- jthread 与停止令牌
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/03-thread-ownership-and-raii.md
  source_hash: f8c117653d9fde2694952716e1b91c176973c6f3621e15a88b4cb1a613fdbc81
  translated_at: '2026-05-20T04:35:21.082754+00:00'
  engine: anthropic
  token_count: 3758
---
# Thread Ownership and RAII

In the previous article, we clarified the parameter passing and lifetime management of `std::thread`. We learned that a `std::thread` object must be `join`ed or `detach`ed before it is destroyed, or the program will simply `terminate`. But frankly, manually calling `join` every time is annoying—not because it's hard, but because it's so easy to forget. Especially in code paths where exceptions are thrown, you might jump out of a function in the middle, and the `join` later on never gets executed. Worse yet, if your function has multiple `return` paths, you have to remember to `join` on every single one; missing one is a ticking time bomb.

What we want to do in this article is simple: wrap `std::thread` with RAII to make resource management automatic. We will start with the move semantics of `std::thread`, clarify what "thread ownership" actually means, and then step by step implement `thread_guard` and `joining_thread`—the latter being essentially the predecessor to C++20's `std::jthread`. Finally, we will discuss exception safety, managing threads in containers, and a very practical exercise.

## std::thread is Move-Only

Let's first clarify a basic fact: `std::thread` is not copyable. You cannot assign one thread object to another, nor can you transfer it by value. The reason is simple—an operating system thread can only be managed by one `std::thread` object at any given time. If copying were allowed, you would end up with two objects trying to `join` the same underlying thread, which is semantically undefined.

Therefore, `std::thread` only supports move semantics. When you move a `std::thread` object to another object, the ownership of the underlying thread transfers from the source to the target, and the source becomes "empty" (`joinable() == false`). We can verify this process with a very simple example:

```cpp
#include <thread>
#include <iostream>

void worker()
{
    std::cout << "Worker thread running\n";
}

int main()
{
    std::thread t1(worker);
    std::cout << "t1 joinable: " << t1.joinable() << "\n";  // true

    std::thread t2 = std::move(t1);  // 所有权转移
    std::cout << "t1 joinable after move: " << t1.joinable() << "\n";  // false
    std::cout << "t2 joinable after move: " << t2.joinable() << "\n";  // true

    t2.join();  // 现在是 t2 负责管理线程
    return 0;
}
```

You will notice that after the move, `t1` no longer manages any thread—it becomes an "empty shell." All operations regarding this thread (`join`, `detach`) must now go through `t2`. This move-only design ensures that at any given time, only one object has control over the underlying thread, fundamentally eliminating the chaos of "two objects joining the same thread."

This "unique owner" semantics is very similar to `std::unique_ptr`—`std::unique_ptr` is also non-copyable and only movable, and the source pointer becomes `nullptr` after a move. In fact, quite a few resource management types in the C++ standard library adopt this pattern: `std::unique_ptr`, `std::fstream`, `std::unique_lock`, they are all move-only. This is not a coincidence, but a direct reflection of the RAII design philosophy—the lifetime of a resource is managed by a single unique owner, and the resource is automatically released when the owner is destroyed.

### Returning std::thread from a Function

A very practical scenario for move semantics is returning a `std::thread` object from a function. Because in C++, return values are optimized (RVO/NRVO), even though `std::thread` is not copyable, returning a `std::thread` is perfectly legal:

```cpp
#include <thread>
#include <iostream>

void background_task(int id)
{
    std::cout << "Background task " << id << " running\n";
}

std::thread make_worker(int id)
{
    return std::thread(background_task, id);
    // 或者更明确地写：
    // std::thread t(background_task, id);
    // return t;  // 隐式 move 或 NRVO
}

int main()
{
    std::thread t = make_worker(42);
    t.join();
    return 0;
}
```

Here, the `std::thread` object returned by `create_worker` is passed to `t` in `main` via a move (or constructed directly on the caller's stack via NRVO optimization), and the thread ownership transfers from inside the function to the caller. This pattern is very common in scenarios like creating thread pools and task schedulers—factory functions are responsible for creating threads, and the caller is responsible for managing their lifetimes.

## Thread Ownership Semantics: Who is Responsible for join/detach

In the previous article, we mentioned that the destructor of `std::thread` calls `std::terminate`—if the thread is still `joinable`. This design is intentional: the standard committee believed that if a thread object is destroyed without being joined or detached, it is almost certainly a programmer's error (forgotten or a logic gap). Silently joining could lead to hard-to-debug hangs, and silently detaching could lead to accessing destroyed variables. So the standard chose the most "jarring" approach—terminating the program immediately, forcing you to face the problem.

But this brings up a very practical problem: in complex code paths, how do you guarantee that every path correctly handles the thread? Consider the following function:

```cpp
void process_with_thread()
{
    std::thread t([]() {
        // 一些后台工作...
    });

    do_something();        // 如果这里抛异常呢？
    do_something_else();   // 如果这里抛异常呢？

    t.join();              // 只有执行到这里才会 join
}
```

If `do_something()` throws an exception, `t.join()` will never be executed. The exception propagates up the call stack, the destructor of `t` is called, it finds the thread is still `joinable`, and the program meets its `std::terminate` demise. The program crashes, and you might still be left scratching your head.

You might think: just add a `try-catch`, right? That works, but the code gets ugly, and everywhere you use `std::thread` you would have to do this. The real solution is to automate resource management—and that is exactly what RAII excels at.

## thread_guard: Automatic join in the Destructor

Anthony Williams presented a classic RAII wrapper in *C++ Concurrency in Action*—`thread_guard`. The idea is very straightforward: it takes a reference to a `std::thread` upon construction, and ensures the thread is joined upon destruction. This way, no matter how the function exits (normal return, exception thrown, early return), the thread will be properly cleaned up.

Let's first implement a basic version:

```cpp
#include <thread>

class ThreadGuard {
public:
    enum class Action { kJoin, kDetach };

    explicit ThreadGuard(std::thread& t, Action action = Action::kJoin)
        : thread_(t), action_(action)
    {}

    ~ThreadGuard()
    {
        if (thread_.joinable()) {
            if (action_ == Action::kJoin) {
                thread_.join();
            }
            else {
                thread_.detach();
            }
        }
    }

    // 禁止复制和移动——guard 不应该被转移
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

private:
    std::thread& thread_;  // 注意：持有引用，不是拥有线程
    Action action_;
};
```

Using it looks like this:

```cpp
#include <iostream>

void background_work()
{
    std::cout << "Working in background...\n";
}

void process()
{
    std::thread t(background_work);
    ThreadGuard guard(t);  // guard 绑定到 t

    // 现在无论这里发生什么，guard 的析构函数都会 join t
    do_something();        // 即使这里抛异常
    do_something_else();   // 即使这里也抛异常

    // 不需要手动 t.join()——guard 会处理的
}
```

This design has one inelegant aspect: `thread_guard` holds a reference to `std::thread`, which means the `std::thread` object must exist externally, and its lifetime must be longer than that of `thread_guard`. If the reverse happens—the guard destructs first, that's fine, the guard will join the thread; but if the `std::thread` object destructs first (for example, if it was created in a more inner scope), the guard's destructor will access an object that no longer exists—a dangling reference, UB.

Another issue is that after `thread_guard` joins, the original `std::thread` object still exists but is already `!joinable()`. This "guard and thread being separate" state can cause confusion in complex code—who exactly owns this thread? Who is responsible for its lifetime?

## joining_thread: An RAII Wrapper That Takes Ownership

A cleaner design is to have the wrapper directly **own** the `std::thread`—not holding a reference, but moving the thread object in. This way, ownership is completely clear: the wrapper owns the thread, and it automatically joins when the wrapper destructs. The implementation of this idea is `joining_thread`, which is essentially a version of C++20's `std::jthread` that you could write in C++11:

```cpp
#include <thread>
#include <utility>

class JoiningThread {
public:
    JoiningThread() noexcept = default;

    // 接受任意可调用对象和参数，直接构造线程
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...)
    {}

    // 从 std::thread move 构造——接管所有权
    explicit JoiningThread(std::thread t) noexcept
        : thread_(std::move(t))
    {}

    // 支持从另一个 JoiningThread move
    JoiningThread(JoiningThread&& other) noexcept
        : thread_(std::move(other.thread_))
    {}

    JoiningThread& operator=(JoiningThread&& other) noexcept
    {
        if (this != &other) {
            // 先处理当前持有的线程
            if (joinable()) {
                join();
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    // 也可以从一个新的 std::thread 赋值
    JoiningThread& operator=(std::thread other) noexcept
    {
        if (joinable()) {
            join();
        }
        thread_ = std::move(other);
        return *this;
    }

    ~JoiningThread()
    {
        if (joinable()) {
            join();
        }
    }

    void join()
    {
        thread_.join();
    }

    void detach()
    {
        thread_.detach();
    }

    bool joinable() const noexcept
    {
        return thread_.joinable();
    }

    // 获取底层 std::thread（用于 native_handle 等）
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

    // 禁止复制
    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

private:
    std::thread thread_;
};
```

You will notice that this class has almost exactly the same interface as `std::thread`, with the only addition being the automatic `join` in the destructor. This is the essence of RAII—without changing how the interface is used, it simply adds automation to the resource cleanup step. The usage is almost identical to a raw `std::thread`:

```cpp
#include <iostream>

void task(int id)
{
    std::cout << "Task " << id << " running\n";
}

int main()
{
    JoiningThread t1(task, 1);  // 自动 join
    JoiningThread t2([]() {
        std::cout << "Lambda task running\n";
    });

    // 从 std::thread 构造
    JoiningThread t3(std::thread(task, 3));

    // 不需要手动 join——析构时自动完成
    return 0;
}
```

There is a detail in the move assignment operator worth noting: before taking on a new thread, you must first handle the currently held thread. If the current thread is still `joinable`, you must join it first, otherwise it becomes an unowned thread—no one will handle it when it destructs, and the program will `terminate`. This "clean up the old before taking on the new" pattern is very common in RAII classes; `std::unique_ptr`'s assignment operator does the same thing (deleting the old pointer before taking over the new one).

### C++20's std::jthread

The C++20 standard finally introduced `std::jthread`, and its behavior is very similar to our `joining_thread`—it automatically joins upon destruction. But `std::jthread` also has an additional important feature: **cooperative cancellation**, internally holding a `std::stop_token` that can be used to request the thread to stop execution via `request_stop()`. We will elaborate on this feature in a later chapter, "jthread and Stop Tokens."

If you are already using C++20, just use `std::jthread` directly. If you are still on C++11/14/17, the `joining_thread` above is a perfectly viable alternative. The core idea behind both is the same: use RAII to automate the lifetime management of threads, letting the compiler guarantee no resource leaks.

## Exception Safety: What Happens When join() Throws an Exception

Now we have an RAII wrapper that automatically joins, and it seems like all our problems are solved. But the real pitfall lies ahead—`join()` itself can throw an exception.

Under what circumstances would `join()` throw an exception? The most direct example is if the underlying `pthread_join` call fails—although this almost never happens in normal programs, the standard does not guarantee that `join()` is `noexcept`. If your program calls `join()` in the destructor of `joining_thread`, and `join()` throws an exception, what happens?

The answer is: an exception thrown in a destructor triggers `std::terminate`. C++ dictates that if a destructor is executing (whether during normal destruction or stack unwinding), and a new exception is thrown and not caught, the program terminates. So if your `joining_thread`'s `join` throws during destruction, the program will still crash.

This is not a pleasant reality. In fact, *C++ Concurrency in Action*, Second Edition, also discusses this problem, and the final conclusion is: joining a thread in a destructor is a "reasonable but not perfect" strategy—if `join()` fails, you really don't have a good way to handle it, because destructors should not throw exceptions. A pragmatic approach is to wrap the `join()` in a `try-catch` inside the destructor, catching the exception, logging it, but not rethrowing:

```cpp
~JoiningThread()
{
    if (joinable()) {
        try {
            join();
        }
        catch (const std::system_error& e) {
            // join 失败了，记录日志但不抛出
            // 在实际项目中应该用正式的日志系统
            std::fprintf(stderr,
                         "JoiningThread: join() failed: %s\n", e.what());
        }
    }
}
```

This approach is not elegant, but it is the only safe exception handling method in a destructor—swallow the exception, log it, and move on. If your scenario has zero tolerance for `join()` failure, you might need a different strategy: don't join in the destructor, but require the caller to explicitly join, and if they forget, let the program terminate (just like a raw `std::thread`). This is a trade-off between "safety" and "reliability"—automatic join makes the problem of forgetting to join disappear, but it introduces the exception safety issue when `join()` fails.

## Using Threads in Containers

`std::thread` is move-only, and `std::vector` has supported move-only types since C++11. So `std::vector<std::thread>` is perfectly legal and can be used to manage a group of worker threads. This is very practical in scenarios like implementing thread pools and parallel processing.

Let's look at a concrete example—processing a set of data in parallel:

```cpp
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>

// 将 range 并行地分配给多个线程处理
template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func func,
                       unsigned thread_count)
{
    std::size_t length = std::distance(first, last);
    if (length == 0) return;

    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }

    std::size_t block_size = length / thread_count;
    std::vector<JoiningThread> threads;
    threads.reserve(thread_count);

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back([block_start, block_end, &func]() {
            std::for_each(block_start, block_end, func);
        });
        block_start = block_end;
    }

    // 最后一个块由当前线程自己处理
    std::for_each(block_start, last, func);

    // 析构时所有 threads 自动 join
}
```

There are a few details here worth noting. First is `emplace_back`—because `std::thread`'s constructor accepts a callable object, we can construct the thread object in place inside the `vector`, without needing to construct it first and then move it. Then there is the handling of the last chunk—we let the current thread (the caller) process the last chunk of data itself, rather than spawning an additional thread. This is a common optimization: the caller thread is also doing work, so it doesn't need to sit idle waiting for all worker threads to finish.

When the `parallel_process` function returns, the `threads` `std::vector` is destroyed, each `joining_thread`'s destructor is called in turn, and all threads are joined. The entire process requires no manual management of any thread's lifetime.

However, note that when `std::vector` expands, it will move elements to a new memory area. For `joining_thread`, this is safe (because we defined a move constructor), but if you store raw `std::thread` directly, the original object becomes empty after a move, which is also safe—as long as you don't forget to join at the new location. Using `reserve` to pre-allocate space for the `std::vector` can avoid the extra move operations caused by expansion.

## Applying the scope(guard) Pattern to Thread Cleanup

`joining_thread` is a general-purpose RAII thread wrapper suitable for most scenarios. But sometimes you might want more flexible control—for example, joining under certain conditions, detaching under others, or doing some cleanup work before the thread finishes. In these cases, you can use a more general-purpose tool: a scope guard.

The core idea of a scope guard is "execute a piece of code when a scope exits," regardless of whether the exit is due to a normal return, an exception, or `break`/`continue`. C++ does not have a language-level scope guard (unlike Go's `defer` or Rust's RAII destructors), but you can easily implement one using C++ destructors:

```cpp
#include <functional>
#include <utility>

class ScopeGuard {
public:
    template <typename Func>
    explicit ScopeGuard(Func&& func)
        : callback_(std::forward<Func>(func))
    {}

    ~ScopeGuard()
    {
        if (callback_) {
            callback_();
        }
    }

    void dismiss() noexcept
    {
        callback_ = nullptr;
    }

    ScopeGuard(ScopeGuard&& other) noexcept
        : callback_(std::move(other.callback_))
    {
        other.dismiss();
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    std::function<void()> callback_;
};
```

Using a scope guard to manage thread joining:

```cpp
#include <thread>
#include <iostream>

void worker(int id)
{
    std::cout << "Worker " << id << " done\n";
}

void process()
{
    std::thread t(worker, 1);

    // 作用域退出时自动 join
    ScopeGuard join_guard([&t]() {
        if (t.joinable()) {
            t.join();
        }
    });

    // 一些可能抛异常的操作
    do_something();

    // 如果一切顺利，也可以手动 dismiss，然后自己 join
    // join_guard.dismiss();
    // t.join();
}
```

A scope guard is more flexible than `thread_guard`—you can do anything in the guard's callback (join, detach, log, update state, etc.), not limited to just joining. But it is also more primitive—it lacks type safety guarantees, and the overhead of `std::function`, while small, is not zero. In general scenarios, `joining_thread` is the better choice; in situations requiring more flexible control, a scope guard is a valuable tool.

It is worth mentioning that the C++ standard committee has discussed the standardization of scope guards multiple times (proposals like P0052), but as of C++23, it has not been formally adopted into the standard. The latest proposal is P3610 (targeting C++29), planning to provide three utilities: `std::scope_exit`, `std::scope_fail`, and `std::scope_success` in the `<scope>` header file. Before this happens, some compilers provide it in the form of `std::experimental::scope_exit` in the Library Fundamentals TS, and you can also use Boost.ScopeExit or implement it yourself (just like we did above).

## Summary

In this article, starting from the move-only characteristic of `std::thread`, we established the concept of "thread ownership"—a `std::thread` object is the unique owner of the underlying operating system thread, and ownership can only be transferred via a move, not copied. This design is in the same vein as `std::unique_ptr`, ensuring clarity in resource management.

Then we used the RAII pattern to solve the most common thread management error: "forgetting to join/detach." `thread_guard` is a basic implementation (holding a reference, joining on destruction), while `joining_thread` is a more complete implementation (directly owning the thread, automatically joining on destruction). The latter is essentially a manual implementation of C++20's `std::jthread` in C++11. We also discussed the tricky problem of `join()` potentially throwing exceptions, and the safe way to handle it in a destructor.

Finally, we looked at the application of `std::vector` in parallel processing, as well as the more general scope guard pattern. RAII is not just a programming trick—it is the core philosophy of C++ resource management. When you start using it to manage resources like threads, locks, and file handles, you will find your code becomes cleaner, safer, and less prone to bugs.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Implement a JoiningThread with Cancellable Join

Add a `detach_on_destroy()` method to the `joining_thread` above—after it is called, the destructor will no longer automatically join the thread, but will detach it instead. Consider this: under what conditions should `detach_on_destroy()` be called? If the thread has already finished executing but hasn't been joined yet, what happens after `detach_on_destroy()`? Write a test case to verify your implementation.

```cpp
// 提示：你需要在类中加一个 bool 标志
class JoiningThread {
    // ...
    void cancel_join() noexcept
    {
        should_join_ = false;
    }

private:
    std::thread thread_;
    bool should_join_{true};
};
```

### Exercise 2: Implement Parallel Accumulation Using JoiningThread

Implement a function `parallel_accumulate` that accepts an iterator range and an initial value, divides the range into N chunks, uses a `joining_thread` to accumulate each chunk, and finally sums up all the partial results. Be careful to handle the case where the last chunk might be smaller than the others. Compare the result with `std::accumulate` to ensure consistency.

### Exercise 3: Scope Guard and Multi-Thread Cleanup

Write a program that launches three threads, each executing a simulated long-running task (such as `std::this_thread::sleep_for`). Use `scope_guard` at different points in the function to ensure all threads are joined when the function exits. Then simulate an exception at a "possible failure" checkpoint, and verify that the threads are still properly cleaned up.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::jthread (C++20) — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams (Manning)](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) — The design inspiration for `thread_guard` and `joining_thread` in this chapter
- [P0052: Generic Scope Guard and RAII Wrapper for the C++ Standard Library](https://wg21.link/p0052)
- [RAII and the Rule of Zero — CppCon 2021](https://www.youtube.com/watch?v=7Qgd9B1KuMQ)
