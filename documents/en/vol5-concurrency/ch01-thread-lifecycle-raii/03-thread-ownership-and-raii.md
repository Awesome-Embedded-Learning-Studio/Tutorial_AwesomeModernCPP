---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Wrap std::thread with RAII: an exception-safe joining_thread guard and scope-exit cleanup'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- Thread Arguments and Lifetime
reading_time_minutes: 18
related:
- mutex and RAII Locks
- jthread and Stop Tokens
tags:
- host
- cpp-modern
- intermediate
- RAII
title: Thread Ownership and RAII
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/03-thread-ownership-and-raii.md
  source_hash: 706a89b2a62ad0156e29fedfdb57dc1c52a1c0be4725a6a67ab6fb41e15ccb1a
  translated_at: '2026-09-26T06:31:40+00:00'
  engine: anthropic
  token_count: 5400
---
# Thread Ownership and RAII

In the previous article we sorted out `std::thread` argument passing and lifetime management, and learned that a `std::thread` object must be `join()`ed or `detach()`ed before it is destroyed—otherwise the program calls `std::terminate()` outright. But honestly, calling `join()` manually every time is a chore. Not because it's hard, but because it is far too easy to forget. Especially on code paths where exceptions get thrown, you might leave a function somewhere in the middle and never reach the `join()` at the end. Worse yet, if your function has multiple `return` paths, every single one of them has to remember to `join()`—miss one, and you're sitting on a time bomb.

What we do in this article is simple: wrap `std::thread` in RAII and make resource management automatic. We'll start from the move semantics of `std::thread` and pin down what "thread ownership" actually means, then implement `thread_guard` and `joining_thread` step by step—the latter being essentially the forerunner of C++20 `std::jthread`. Finally we'll discuss exception safety, managing threads in containers, and a set of practical exercises.

## std::thread Is Move-Only

First, get one basic fact straight: `std::thread` is not copyable. You cannot assign one thread object to another, and you cannot transfer it by passing it by value. The reason is simple—an operating system thread can be managed by only one `std::thread` object at a time. If copying were allowed, you could end up with two objects trying to `join()` the same underlying thread, which is semantically impossible to define.

So `std::thread` supports move semantics only. When you move a `std::thread` object into another object, ownership of the underlying thread transfers from the source object to the destination, and the source becomes "empty" (`joinable() == false`). The simplest example is enough to verify this:

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

    std::thread t2 = std::move(t1);  // Ownership transfer
    std::cout << "t1 joinable after move: " << t1.joinable() << "\n";  // false
    std::cout << "t2 joinable after move: " << t2.joinable() << "\n";  // true

    t2.join();  // t2 is now the one managing the thread
    return 0;
}
```

You will find that after the move, `t1` no longer manages any thread—it has become an "empty shell". Every operation on that thread (`join()`, `detach()`) must now go through `t2`. This move-only design guarantees that at any moment exactly one object holds control of the underlying thread, rooting out the chaos of "two objects joining the same thread" once and for all.

This "sole owner" semantics is very similar to `std::unique_ptr`—a `unique_ptr` is likewise non-copyable and movable only, and after a move the source pointer becomes `nullptr`. In fact, quite a few resource-managing types in the C++ standard library follow this pattern: `std::fstream`, `std::unique_lock`, and `std::future` are all move-only. That is no coincidence but the direct expression of the RAII design philosophy—the lifetime of a resource is managed by a single unique owner, and the resource is released automatically when that owner is destroyed.

### Returning a std::thread from a Function

One very practical use case for move semantics is returning a `std::thread` object from a function. Because return values in C++ are optimized (RVO/NRVO), returning a `std::thread` is perfectly legal even though `std::thread` is not copyable:

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
    // Or, written more explicitly:
    // std::thread t(background_task, id);
    // return t;  // Implicit move or NRVO
}

int main()
{
    std::thread t = make_worker(42);
    t.join();
    return 0;
}
```

Here the `std::thread` object returned by `make_worker` is passed to `t` in `main` via a move (or, with the NRVO optimization, constructed directly on the caller's stack), so ownership of the thread moves from inside the function to the caller. This pattern is very common when building thread pools, task schedulers, and the like—the factory function is responsible for creating the thread, and the caller is responsible for managing its lifetime.

## Thread Ownership Semantics: Who Is Responsible for join/detach

As we said in the previous article, the destructor of `std::thread` calls `std::terminate()`—if the thread is still `joinable()`. This design is intentional: the standards committee's reasoning was that if a thread object is destroyed without having been joined or detached, that is almost certainly a programmer error (a forgotten call or a logic hole); silently joining could cause hard-to-debug hangs, and silently detaching could lead to accesses to already-destroyed variables. So the standard chose the harshest-sounding option—terminate the program on the spot and force you to face the problem.

But that creates a very real question: in complex code paths, how do you guarantee that every path handles the thread correctly? Consider this function:

```cpp
void process_with_thread()
{
    std::thread t([]() {
        // Some background work...
    });

    do_something();        // What if this throws?
    do_something_else();   // What if this throws?

    t.join();              // join happens only if execution reaches here
}
```

If `do_something()` throws an exception, `t.join()` never executes. The exception propagates up the call stack, `t`'s destructor runs, finds the thread still `joinable()`, and it all ends in `std::terminate()`. The program crashes, and you may still be completely puzzled about why.

You might think: just add a `try-catch` and be done with it? That does work, but the code gets ugly, and you would have to do it everywhere a `std::thread` is used. The real solution is to make resource management automatic—which is precisely what RAII is good at.

## thread_guard: Automatic Join in the Destructor

Anthony Williams gives a classic RAII wrapper in *C++ Concurrency in Action*—`thread_guard`. The idea is plain: take a reference to a `std::thread` in the constructor, and make sure the thread is joined in the destructor. That way, no matter how the function exits (normal return, thrown exception, early return), the thread is cleaned up correctly.

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

    // Copying and moving are forbidden—a guard must not be moved around
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

private:
    std::thread& thread_;  // Note: holds a reference, does not own the thread
    Action action_;
};
```

Usage looks like this:

```cpp
#include <iostream>

void background_work()
{
    std::cout << "Working in background...\n";
}

void process()
{
    std::thread t(background_work);
    ThreadGuard guard(t);  // The guard is bound to t

    // Now, no matter what happens here, guard's destructor will join t
    do_something();        // Even if this throws
    do_something_else();   // Even if this throws too

    // No manual t.join() needed—the guard takes care of it
}
```

This design has one inelegant aspect: `ThreadGuard` holds a reference to the `std::thread`, which means the `std::thread` object must exist on the outside, and its lifetime must be longer than the `ThreadGuard`'s. The other way around is fine—if the guard is destroyed first, no problem, the guard joins the thread. But if the `std::thread` object is destroyed first (say it was created in a more deeply nested scope), the guard's destructor would access an object that no longer exists—a dangling reference, UB.

Another issue is that after the join, the original `std::thread` object is still there, but it is now `joinable() == false`. This "guard and thread separated" state can breed confusion in complex code—who exactly owns this thread? Who is responsible for its lifetime?

## joining_thread: An RAII Wrapper That Takes Over Ownership

A cleaner design is to let the wrapper directly **own** the `std::thread`—not hold a reference to it, but move the thread object in. That makes ownership completely unambiguous: the wrapper owns the thread, and the wrapper joins it automatically on destruction. The implementation of this idea is `joining_thread`, which is essentially the version of C++20 `std::jthread` you could already write in C++11:

```cpp
#include <thread>
#include <utility>

class JoiningThread {
public:
    JoiningThread() noexcept = default;

    // Accepts any callable and arguments, constructing the thread directly
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...)
    {}

    // Move-construct from a std::thread—takes over ownership
    explicit JoiningThread(std::thread t) noexcept
        : thread_(std::move(t))
    {}

    // Supports moving from another JoiningThread
    JoiningThread(JoiningThread&& other) noexcept
        : thread_(std::move(other.thread_))
    {}

    JoiningThread& operator=(JoiningThread&& other) noexcept
    {
        if (this != &other) {
            // Deal with the currently held thread first
            if (joinable()) {
                join();
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    // Can also be assigned a brand-new std::thread
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

    // Access the underlying std::thread (for native_handle, etc.)
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

    // Copying is forbidden
    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

private:
    std::thread thread_;
};
```

You will notice this class has almost exactly the same interface as `std::thread`; the only thing added is the automatic `join()` in the destructor. This is precisely the essence of RAII—do not change how the interface is used, just add automation at the resource-cleanup step. Usage is nearly identical to a bare `std::thread`:

```cpp
#include <iostream>

void task(int id)
{
    std::cout << "Task " << id << " running\n";
}

int main()
{
    JoiningThread t1(task, 1);  // Joins automatically
    JoiningThread t2([]() {
        std::cout << "Lambda task running\n";
    });

    // Construct from a std::thread
    JoiningThread t3(std::thread(task, 3));

    // No manual join needed—it happens automatically at destruction
    return 0;
}
```

One detail in the move assignment operator is worth noting: before accepting the new thread, you must deal with the thread currently held. If the current thread is still `joinable()`, it must be joined first; otherwise it becomes an ownerless thread—nobody handles it at destruction time, and the program calls `terminate()`. This "clean up the old before taking over the new" pattern is common in RAII classes; `std::unique_ptr`'s assignment operator does the same thing (delete the old pointer first, then take over the new one).

### C++20 std::jthread

The C++20 standard finally introduced `std::jthread`. Its behavior closely matches our `JoiningThread`—automatic join on destruction. But `std::jthread` comes with one more important capability: **cooperative cancellation**. It holds a `std::stop_source` internally, and you can request that the thread stop executing via `request_stop()`. We will expand on this in detail in the later "jthread and Stop Token" chapter.

If you are already on C++20, just use `std::jthread`. If you are still on C++11/14/17, the `JoiningThread` above is a perfectly workable substitute. The core idea of the two is the same: automate thread lifetime management with RAII and let the compiler guarantee that resources do not leak.

## Exception Safety: What Happens When join() Throws

Now we have an RAII wrapper with automatic join, and the problem looks solved. But the real trap is still ahead—`join()` itself can throw.

When can `join()` throw? The most direct example is the underlying `pthread_join` call failing—though this almost never happens in a healthy program, the standard does not guarantee that `join()` is `noexcept`. If your program calls `join()` in `JoiningThread`'s destructor and `join()` throws an exception, what happens?

The answer: an exception thrown during the destructor triggers `std::terminate()`. C++ dictates that if a destructor is executing (whether during normal destruction or stack unwinding) and a new exception is thrown and not caught, the program terminates. So if your `JoiningThread` hits a throwing `join()` during destruction, the program still crashes.

That is not a pleasant reality. In fact, the second edition of *C++ Concurrency in Action* discusses this issue too, and the final conclusion is that joining a thread in the destructor is a "reasonable but imperfect" strategy—if `join()` fails, there really is no good way to handle it, because destructors should not throw. A pragmatic approach is to wrap `join()` in a `try-catch` inside the destructor, log the caught exception, and do not rethrow:

```cpp
~JoiningThread()
{
    if (joinable()) {
        try {
            join();
        }
        catch (const std::system_error& e) {
            // join failed—log it, but do not throw
            // In a real project, use a proper logging system
            std::fprintf(stderr,
                         "JoiningThread: join() failed: %s\n", e.what());
        }
    }
}
```

This approach is not elegant, but it is the only safe way to handle exceptions in a destructor—swallow the exception, log it, and carry on. If your use case has zero tolerance for `join()` failures, you may need a different strategy: do not join in the destructor; instead require the caller to join explicitly, and let the program terminate if they forget (just like a bare `std::thread`). This is a trade-off between "safety" and "reliability"—automatic join makes the forgotten-join problem disappear, but introduces an exception-safety problem when `join()` fails.

## Using Threads in Containers

`std::thread` is move-only, and `std::vector` has supported move-only types since C++11. So `std::vector<JoiningThread>` is perfectly legal and can be used to manage a group of worker threads. This is very practical when implementing thread pools, parallel processing, and the like.

Let's look at a concrete example—processing a set of data in parallel:

```cpp
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>

// Distribute the range across multiple threads for parallel processing
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

    // The last block is handled by the current thread itself
    std::for_each(block_start, last, func);

    // All threads join automatically on destruction
}
```

There are several details worth noting here. First, `emplace_back`—because `JoiningThread`'s constructor accepts a callable, we can construct the thread objects in place inside the `vector`, with no need to construct first and move afterwards. Then there is the handling of the last block—we let the current thread (the caller) process the final chunk of data itself, rather than spawning one extra thread. This is a common optimization: the calling thread is working too, so it doesn't have to sit idle waiting for all the worker threads to finish.

When the `parallel_for_each` function returns, the `threads` vector is destroyed, each `JoiningThread`'s destructor is called in turn, and every thread gets joined. Throughout the whole process, no thread's lifetime is managed by hand.

One caveat, though: when a `std::vector` grows, it moves elements to a new memory region. For `JoiningThread` this is safe (because we defined a move constructor), and it is equally safe if you store bare `std::thread` objects directly—after the move, the original object becomes empty—as long as you do not forget to join at the new location. Using `reserve()` to preallocate space avoids the extra moves that reallocation would cause.

## Applying the scope(guard) Pattern to Thread Cleanup

`JoiningThread` is a general-purpose RAII thread wrapper that suits most scenarios. But sometimes you may want more flexible control—join under some conditions, detach under others, or do some cleanup work before the thread ends. For that, there is a more general tool: the scope guard.

The core idea of the scope guard is "execute a piece of code when the scope exits", regardless of whether the exit is caused by a normal return, an exception, or `break`/`continue`. C++ has no language-level scope guard (unlike Go with `defer`, or Rust with RAII destructors), but using C++ destructors, implementing one is quite easy:

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

Using a scope guard to manage a thread's join:

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

    // Join automatically when the scope exits
    ScopeGuard join_guard([&t]() {
        if (t.joinable()) {
            t.join();
        }
    });

    // Some operations that might throw
    do_something();

    // If all goes well, you can also dismiss it manually and join yourself
    // join_guard.dismiss();
    // t.join();
}
```

The scope guard is more flexible than `JoiningThread`—you can do anything in the guard's callback (join, detach, log, update state, and so on), not just join. But it is also more primitive—there is no type-safety guarantee, and while the overhead of `std::function` is small, it is not zero after all. In typical scenarios, `JoiningThread` is the better choice; when more flexible control is needed, the scope guard is a valuable tool.

Worth a mention: the C++ standards committee has discussed standardizing the scope guard several times (P0052 and similar proposals), but as of C++23 it has not officially made it into the standard. The latest proposal is P3610 (targeting C++29), which plans to provide `std::scope_exit`, `std::scope_fail`, and `std::scope_success` in the `<scope>` header. Before that happens, some compilers provide `std::experimental::scope_exit` as part of the Library Fundamentals TS; you can also use Boost.ScopeExit or implement your own (as we did above).

## Summary

In this article we started from the move-only nature of `std::thread` and built up the concept of "thread ownership"—a `std::thread` object is the sole owner of the underlying operating system thread; ownership can only be transferred via move, never copied. This design follows the same lineage as `std::unique_ptr`, ensuring clarity of resource management.

Then we used the RAII pattern to solve "forgetting to join/detach", the most common thread-management mistake. `ThreadGuard` is a basic implementation (holds a reference, joins on destruction), and `JoiningThread` is a more complete one (owns the thread outright, joins automatically on destruction). The latter is essentially a hand-written C++11 version of C++20 `std::jthread`. We also discussed the thorny problem of `join()` potentially throwing, and the safe way to handle it in a destructor.

Finally, we looked at `std::vector<JoiningThread>` in parallel processing, plus the more general scope guard pattern. RAII is not merely a programming trick—it is the core philosophy of resource management in C++. Once you start using it to manage threads, locks, file handles, and the like, you will find your code becoming cleaner, safer, and less prone to bugs.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)—browse to `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`.

## Exercises

### Exercise 1: Implement a JoiningThread with a Cancellable Join

Add a `cancel_join()` method to the `JoiningThread` above—after it is called, the destructor no longer joins the thread automatically, but detaches it instead. Think it over: under what conditions should `cancel_join()` be called? If the thread has already finished executing but has not been joined yet, what happens after `cancel_join()`? Write a test case to verify your implementation.

```cpp
// Hint: you need to add a bool flag to the class
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

### Exercise 2: Parallel Accumulation with JoiningThread

Implement a function `parallel_accumulate` that takes an iterator range and an initial value, splits the range into N chunks, accumulates each chunk with its own `JoiningThread`, and finally combines all the partial sums. Be careful to handle the case where the last chunk may be smaller than the others. Compare your result with `std::accumulate` to check that they agree.

### Exercise 3: scope guard and Multithreaded Cleanup

Write a program that starts 3 threads, each running a simulated long task (for example, `std::this_thread::sleep_for`). Use `ScopeGuard` at different places in the function to make sure all threads are joined when the function exits. Then simulate an exception at a "may fail" checkpoint, and verify that the threads are still cleaned up correctly.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::jthread (C++20) — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams (Manning)](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) — the design inspiration for this chapter's `thread_guard` and `joining_thread`
- [P0052: Generic Scope Guard and RAII Wrapper for the C++ Standard Library](https://wg21.link/p0052)
- [RAII and the Rule of Zero — CppCon 2021](https://www.youtube.com/watch?v=7Qgd9B1KuMQ)
