---
chapter: 1
cpp_standard:
- 11
- 14
- 17
- 20
description: Wrap `std::thread` using RAII to implement an exception-safe `joining_thread`
  guard and scope-exit cleanup.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 线程参数与生命周期
reading_time_minutes: 18
related:
- mutex 与 RAII 锁
- jthread 与停止令牌
tags:
- host
- cpp-modern
- intermediate
- RAII
title: Thread Ownership and RAII
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/03-thread-ownership-and-raii.md
  source_hash: 706a89b2a62ad0156e29fedfdb57dc1c52a1c0be4725a6a67ab6fb41e15ccb1a
  translated_at: '2026-06-16T04:03:18.472569+00:00'
  engine: anthropic
  token_count: 3752
---
# Thread Ownership and RAII

In the previous post, we clarified parameter passing and lifetime management for `std::thread`. We learned that a `std::thread` object must be either `join()`ed or `detach()`ed before destruction, or the program will immediately `std::terminate`. Frankly, manually calling `join()` every time is tedious—not because it's difficult, but because it's so easy to forget. This is especially true in code paths where exceptions might be thrown; you might jump out of a function in the middle, and the `join()` at the end never gets reached. Even worse, if your function has multiple `return` paths, you have to remember to `join()` in every single one. Missing one is a ticking time bomb.

In this post, our goal is simple: wrap `std::thread` using RAII to automate resource management. We will start with the move semantics of `std::thread` to understand what "thread ownership" really means. Then, we will implement `thread_guard` and `joining_thread` step-by-step—the latter is essentially the predecessor to C++20's `std::jthread`. Finally, we will discuss exception safety, managing threads in containers, and a practical exercise.

## `std::thread` is Move-Only

First, let's establish a basic fact: `std::thread` is non-copyable. You cannot assign a thread object to another, nor pass it around by value. The reason is simple: an operating system thread can only be managed by one `std::thread` object at any given moment. If copying were allowed, two objects would attempt to manage the same underlying thread, creating a semantic ambiguity.

Therefore, `std::thread` only supports move semantics. When you move a `std::thread` object to another object, ownership of the underlying thread transfers from the source to the target, leaving the source "empty" (as if it were default constructed). We can verify this with a simple example:

```cpp
std::thread t1([]{
    std::cout << "Thread 1 running\n";
});

std::thread t2 = std::move(t1);

// t1 is no longer associated with any thread
if (t1.joinable()) {
    // This will not execute
    t1.join();
}

t2.join();
```

You will notice that after the move, `t1` no longer manages any thread—it becomes a "shell." All operations on this thread (`join`, `detach`) must now go through `t2`. This move-only design ensures that there is always only one owner with control over the underlying thread, fundamentally eliminating the chaos of "two objects joining the same thread."

This "unique owner" semantics is very similar to `std::unique_ptr`—`std::unique_ptr` is also non-copyable and move-only, leaving the source as a `nullptr` after the move. In fact, many resource management types in the C++ standard library adopt this pattern: `std::unique_ptr`, `std::fstream`, `std::unique_lock`. This is no coincidence; it is a direct reflection of RAII philosophy—the resource lifecycle is managed by a unique owner, and the resource is automatically released when the owner is destroyed.

### Returning `std::thread` from Functions

A very practical scenario for move semantics is returning `std::thread` objects from functions. Because return values in C++ are optimized (RVO/NRVO), returning a `std::thread` is perfectly legal even though it is not copyable:

```cpp
std::thread create_worker() {
    return std::thread([]{
        std::cout << "Worker thread running\n";
    });
}

int main() {
    std::thread t = create_worker();
    t.join();
}
```

Here, the `std::thread` object returned by `create_worker` is passed to `t` in `main` via a move (or constructed directly on the caller's stack via NRVO). The thread ownership transfers from inside the function to the caller. This pattern is common in scenarios like thread pools and task schedulers—factory functions create threads, and callers manage their lifecycles.

## Thread Ownership Semantics: Who is Responsible for join/detach

In the last post, we mentioned that the destructor of `std::thread` calls `std::terminate()`—if the thread is still `joinable()`. This design is intentional. The standard committee reasoned that if a thread object is destroyed without being joined or detached, it is almost certainly a programmer error (forgotten or logic hole). Silently joining could lead to hard-to-debug hangs, and silently detaching could lead to accessing destroyed variables. So, the standard chose the most "jarring" approach—terminate the program immediately to force you to face the issue.

But this presents a practical problem: in complex code paths, how do you ensure every path handles the thread correctly? Consider this function:

```cpp
void risky_operation() {
    std::thread t([]{
        std::cout << "Doing work\n";
    });

    // ... some code that might throw an exception ...

    t.join(); // If exception thrown above, this is skipped!
}
```

If an exception is thrown in `// ... some code ...`, `t.join()` is never executed. The exception propagates up the call stack, `t`'s destructor is called, finds the thread is still `joinable()`, and the program ends in `std::terminate`. The program crashes, potentially leaving you confused.

You might think: just add a `try-catch`? You can, but the code becomes ugly, and you have to do it everywhere `std::thread` is used. The real solution is to automate resource management—this is exactly what RAII excels at.

## `thread_guard`: Automatic Join in Destructor

Anthony Williams, in *C++ Concurrency in Action*, presents a classic RAII wrapper—`thread_guard`. The idea is straightforward: take a reference to a `std::thread` upon construction, and ensure the thread is joined upon destruction. This way, no matter how the function exits (normal return, exception thrown, early return), the thread is cleaned up correctly.

Let's implement a basic version:

```cpp
class thread_guard {
    std::thread& t;
public:
    explicit thread_guard(std::thread& t_) : t(t_) {}

    ~thread_guard() {
        if (t.joinable()) {
            t.join();
        }
    }

    // Delete copy operations to prevent copying the reference
    thread_guard(const thread_guard&) = delete;
    thread_guard& operator=(const thread_guard&) = delete;
};
```

Usage looks like this:

```cpp
void guarded_operation() {
    std::thread t([]{
        std::cout << "Work in thread\n";
    });

    thread_guard g(t);

    // ... code that might throw ...

    // No need to manually join, 'g' handles it
}
```

This design has a slightly inelegant aspect: `thread_guard` holds a reference to `std::thread`. This means the `std::thread` object must exist externally and must outlive the `thread_guard`. If the guard is destroyed first, that's fine; it joins the thread. But if the `std::thread` object is destroyed first (e.g., created in a nested scope), the guard's destructor will access a non-existent object—dangling reference, UB.

Another issue is that after `thread_guard` joins, the original `std::thread` object still exists but is now "empty" (non-joinable). This state of "guard and thread separation" can cause confusion in complex code—who actually owns this thread? Who is responsible for its lifecycle?

## `joining_thread`: An RAII Wrapper that Takes Ownership

A cleaner design is to let the wrapper directly **own** the `std::thread`—not hold a reference, but move the thread object into it. This makes ownership crystal clear: the wrapper owns the thread, and the wrapper automatically joins upon destruction. This implementation is `joining_thread`, which is essentially the C++20 `std::jthread` written in C++11:

```cpp
class joining_thread {
    std::thread t;
public:
    joining_thread() noexcept = default;

    // Constructor that takes a std::thread
    explicit joining_thread(std::thread t_) noexcept : t(std::move(t_)) {}

    // Constructor for forwarding arguments directly to std::thread
    template<typename... Args>
    explicit joining_thread(Args&&... args) : t(std::forward<Args>(args)...) {}

    ~joining_thread() {
        if (t.joinable()) {
            t.join();
        }
    }

    // Delete copy
    joining_thread(const joining_thread&) = delete;
    joining_thread& operator=(const joining_thread&) = delete;

    // Support move
    joining_thread(joining_thread&& other) noexcept : t(std::move(other.t)) {}

    joining_thread& operator=(joining_thread&& other) noexcept {
        // First check if we currently hold a thread that needs joining
        if (t.joinable()) {
            t.join();
        }
        t = std::move(other.t);
        return *this;
    }

    // Expose the underlying thread interface if needed
    std::thread& get_thread() { return t; }
    const std::thread& get_thread() const { return t; }

    bool joinable() const noexcept { return t.joinable(); }
    void join() { t.join(); }
    void detach() { t.detach(); }
};
```

You'll find this class interface is almost identical to `std::thread`, with the only addition being the automatic `join` in the destructor. This is the essence of RAII—without changing the interface usage, we add automation to the resource cleanup phase. Usage is nearly identical to raw `std::thread`:

```cpp
void auto_join_demo() {
    joining_thread jt([]{
        std::cout << "Task running\n";
    });

    // No need to call jt.join() manually
}
```

There is a detail in the move assignment operator worth noting: before taking ownership of the new thread, we must handle the currently held thread. If the current thread is still `joinable()`, we must join it first; otherwise, it becomes an orphaned thread—no one handles it during destruction, and the program will `std::terminate`. This "clean up old before taking new" pattern is common in RAII classes; `std::unique_ptr`'s assignment operator does the same (delete the old pointer before taking ownership of the new one).

### C++20's `std::jthread`

C++20 finally introduced `std::jthread`. Its behavior is very similar to our `joining_thread`—it automatically joins upon destruction. However, `std::jthread` adds an important feature: **cooperative cancellation**. It internally holds a `std::stop_token`, allowing the thread to be requested to stop execution via `std::stop_source`. We will cover this feature in detail in the later chapter "jthread and Stop Tokens".

If you are already using C++20, just use `std::jthread`. If you are on C++11/14/17, the `joining_thread` above is a perfectly viable alternative. The core idea is the same: use RAII to automate thread lifecycle management and let the compiler guarantee no resource leaks.

## Exception Safety: What Happens if `join()` Throws?

Now we have an RAII wrapper that auto-joins, so it seems the problem is solved. But the real trap lies ahead—`join()` itself can throw exceptions.

When can `join()` throw? The most direct example is if the underlying OS call fails—though this almost never happens in a normal program, the standard does not guarantee `join()` is `noexcept`. If your program calls `join()` in the destructor of `joining_thread`, and `join()` throws an exception, what happens?

The answer is: throwing an exception in a destructor triggers `std::terminate`. C++ dictates that if a destructor is executing (whether during normal destruction or stack unwinding) and a new exception is thrown and not caught, the program terminates. So if your `joining_thread` throws while `join()`ing during destruction, the program will still crash.

This is an unpleasant reality. In fact, *C++ Concurrency in Action* (2nd Edition) discusses this issue, concluding that joining a thread in a destructor is a "reasonable but not perfect" strategy—if `join()` fails, there isn't much you can do because destructors shouldn't throw. A pragmatic approach is to wrap `join()` in a `try-catch` block inside the destructor, log the exception, but not rethrow it:

```cpp
~joining_thread() {
    if (t.joinable()) {
        try {
            t.join();
        } catch (const std::exception& e) {
            // Log the error, but do not rethrow
            std::cerr << "Thread join failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Thread join failed with unknown exception" << std::endl;
        }
    }
}
```

This approach isn't elegant, but it is the only safe way to handle exceptions in a destructor—swallow the exception, log it, and continue. If your scenario has zero tolerance for `join()` failure, you might need a different strategy: don't join in the destructor, require the caller to explicitly join, and if they forget, let the program terminate (just like raw `std::thread`). This is a trade-off between "safety" and "reliability"—auto-join eliminates the problem of forgetting to join, but introduces exception safety issues if `join()` fails.

## Using Threads in Containers

`std::thread` is move-only, and `std::vector` has supported move-only types since C++11. Therefore, `std::vector<std::thread>` is perfectly legal and can be used to manage a group of worker threads. This is very practical for implementing thread pools or parallel processing.

Let's look at a concrete example—processing a set of data in parallel:

```cpp
void parallel_process(std::vector<int>& data) {
    std::vector<std::thread> threads;

    const size_t hardware_threads = std::thread::hardware_concurrency();
    const size_t block_size = data.size() / hardware_threads;

    for (unsigned int i = 0; i < (hardware_threads - 1); ++i) {
        // emplace_back constructs the thread in place
        threads.emplace_back([&, i]{
            size_t start = i * block_size;
            size_t end = start + block_size;
            // Process data[start...end]
            for (size_t j = start; j < end; ++j) {
                data[j] *= 2;
            }
        });
    }

    // Main thread processes the last block
    size_t start = (hardware_threads - 1) * block_size;
    for (size_t j = start; j < data.size(); ++j) {
        data[j] *= 2;
    }

    // Loop implicitly calls join() for each thread in vector
    for (auto& t : threads) {
        t.join();
    }
}
```

There are details worth noting here. First is `emplace_back`—since `std::thread`'s constructor accepts a callable, we can construct the thread object in place inside `emplace_back` without needing to construct then move. Then there is the handling of the last block—we let the current thread (the caller) handle the last chunk of data instead of spawning an extra thread. This is a common optimization: the caller thread is already working, no need to sit idle waiting for worker threads.

When the `parallel_process` function returns, the `std::vector<std::thread>` named `threads` is destroyed, and each `std::thread` destructor is called in sequence, joining all threads. The whole process requires no manual lifecycle management.

However, note that when `std::vector` resizes, it moves elements to new memory. For `std::thread`, this is safe (because we have defined move constructors), but if you store raw `std::thread` directly, the source becomes empty after the move, which is also safe—as long as you don't forget to join at the new location. Using `reserve()` on the vector can avoid extra move operations caused by resizing.

## Applying the Scope Guard Pattern to Thread Cleanup

`joining_thread` is a generic RAII thread wrapper suitable for most scenarios. But sometimes you might want more flexible control—for example, joining under certain conditions, detaching under others, or doing cleanup work before the thread ends. In such cases, you can use a more generic tool: scope guard.

The core idea of scope guard is "execute a piece of code when the scope exits," regardless of the reason (normal return, exception, or `break`/`continue`). C++ doesn't have a language-level scope guard (unlike Go's `defer` or Rust's RAII destructors), but we can easily implement one using C++ destructors:

```cpp
template<typename F>
class scope_guard {
    F func;
    bool active;
public:
    explicit scope_guard(F f) : func(std::move(f)), active(true) {}

    ~scope_guard() {
        if (active) {
            func();
        }
    }

    // Disallow copying
    scope_guard(const scope_guard&) = delete;
    scope_guard& operator=(const scope_guard&) = delete;

    // Allow move
    scope_guard(scope_guard&& other) noexcept : func(std::move(other.func)), active(other.active) {
        other.active = false;
    }

    scope_guard& operator=(scope_guard&& other) noexcept {
        if (this != &other) {
            // If we currently hold a func, execute it before replacing
            if (active) {
                func();
            }
            func = std::move(other.func);
            active = other.active;
            other.active = false;
        }
        return *this;
    }

    void dismiss() {
        active = false;
    }
};

// Deduction guide (C++17)
template<typename F>
scope_guard(F) -> scope_guard<F>;
```

Using scope guard to manage thread join:

```cpp
void flexible_cleanup() {
    std::thread t([]{
        std::cout << "Working...\n";
    });

    scope_guard cleanup([&]{
        if (t.joinable()) {
            t.join();
        }
    });

    // ... logic that might throw or return early ...
}
```

Scope guard is more flexible than `joining_thread`—you can do anything in the guard's callback (join, detach, log, update state, etc.), not limited to join. But it is also more primitive—no type safety guarantees, and the overhead of `std::function` (if used) is small but non-zero. In general scenarios, `joining_thread` is the better choice; when more flexible control is needed, scope guard is a valuable tool.

It is worth mentioning that the C++ standard committee has discussed standardizing scope guard several times (proposals like P0052), but as of C++23, it has not been officially adopted. The latest proposal is P3610 (targeting C++29), planning to provide `std::scope_exit`, `std::scope_fail`, and `std::scope_success` in the `<scope>` header. Before that, some compilers provide it as `std::experimental::scope_guard` in the Library Fundamentals TS, or you can use Boost.ScopeExit or implement it yourself (just like we did above).

## Summary

In this post, starting from `std::thread`'s move-only nature, we established the concept of "thread ownership"—a `std::thread` object is the unique owner of the underlying OS thread. Ownership can only be transferred via move, not copy. This design aligns with `std::unique_ptr`, ensuring clarity in resource management.

We then used the RAII pattern to solve the most common thread management error: "forgetting to join/detach." `thread_guard` is a basic implementation (holds a reference, joins on destruction), while `joining_thread` is a more robust implementation (owns the thread directly, auto-joins on destruction). The latter is essentially a manual implementation of C++20's `std::jthread` in C++11. We also discussed the tricky issue of `join()` potentially throwing exceptions and how to safely handle it in a destructor.

Finally, we looked at `std::thread` in parallel processing applications and the more generic scope guard pattern. RAII is not just a programming trick—it is the core philosophy of C++ resource management. When you start using it to manage threads, locks, and file handles, you will find your code becomes cleaner, safer, and less prone to bugs.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `demos/threads`.

## Exercises

### Exercise 1: Implement a Cancellable `JoiningThread`

Add a `cancel_join()` method to the `JoiningThread` class above—after calling it, the destructor no longer automatically joins the thread, but detaches it. Consider: under what conditions should `cancel_join()` be called? If the thread has already finished execution but hasn't been joined yet, what happens after `cancel_join()`? Write a test case to verify your implementation.

```cpp
// Add this to the joining_thread class
void cancel_join() {
    // Your implementation
}
```

### Exercise 2: Parallel Accumulation with `JoiningThread`

Implement a function `parallel_accumulate`, accepting an iterator range and an initial value. Split the range into N blocks, accumulate each block with a `JoiningThread`, and finally sum all partial results. Be careful to handle the case where the last block might be smaller than the others. Compare your results with `std::accumulate` to ensure consistency.

### Exercise 3: Scope Guard and Multi-thread Cleanup

Write a program that starts 3 threads, each executing a simulated long task (like `std::this_thread::sleep_for`). Use `scope_guard` at different points in the function to ensure all threads are joined when the function exits. Then, simulate an exception at a "possible failure" checkpoint to verify that threads are still correctly cleaned up.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::jthread (C++20) — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams (Manning)](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) — Inspiration for the `thread_guard` and `joining_thread` designs in this chapter
- [P0052: Generic Scope Guard and RAII Wrapper for the C++ Standard Library](https://wg21.link/p0052)
- [RAII and the Rule of Zero — CppCon 2021](https://www.youtube.com/watch?v=7Qgd9B1KuMQ)
