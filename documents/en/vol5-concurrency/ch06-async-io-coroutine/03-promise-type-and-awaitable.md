---
chapter: 6
cpp_standard:
- 20
description: 'Master the two key customization points of C++20 coroutines: `promise_type`
  controls coroutine behavior, while `awaitable` controls suspension and resumption.'
difficulty: advanced
order: 3
platform: host
prerequisites:
- C++20 协程基础
reading_time_minutes: 28
related:
- 异步 I/O 与事件循环
- 协程 Echo Server 实战
tags:
- host
- cpp-modern
- advanced
- coroutine
- 异步编程
title: promise_type and awaitable
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/03-promise-type-and-awaitable.md
  source_hash: 0003fd3e2253999e0879eb282322f1f782e2a8260b9dfc6f4d74158ea7ded190
  translated_at: '2026-06-16T04:06:33.615944+00:00'
  engine: anthropic
  token_count: 5936
---
# promise_type and awaitable

In the previous post, we looked at the basic syntax of C++20 coroutines—`co_await`, `co_yield`, and `co_return`—and the state machine the compiler generates for us. But honestly, just knowing those keywords is only scratching the surface. The true power of C++20 coroutines—or rather, the real "pitfall"—lies in the fact that it delegates almost all behavioral decisions to two customization points: `promise_type` and `awaitable` (more accurately, the awaiter). This turns coroutines into a "framework" rather than a "feature": the language standard only specifies *when* the compiler calls which methods; how you implement those methods is entirely up to you.

The benefit of this design philosophy is extreme flexibility—you can use coroutines to implement generators, async tasks, lazy evaluation, cooperative scheduling, or even state machines. The downside is that the C++20 standard library provides almost no ready-made coroutine types (`std::generator` doesn't arrive until C++23), so you have to build the entire infrastructure from scratch. In this post and the next, we will dissect these two customization points so that you can write a usable coroutine framework yourself.

## Environment Setup

All code in this post has been tested in the following environment:

- **Operating System**: Linux (WSL2, kernel 6.6+)
- **Compiler**: GCC 13+ or Clang 17+ (both have fairly complete support for C++20 coroutines)
- **Compiler Flags**: `-std=c++20 -fcoroutines-ts` (GCC might need `-fcoroutines-ts`, Clang usually supports it by default)
- **Platform**: This post covers platform-independent pure C++20 only, with no OS-specific APIs (we will introduce epoll in the next post)

## The Full Picture of promise_type

If you read the last post, you should remember: whenever the compiler encounters a function containing `co_await`, `co_yield`, or `co_return`, it transforms that function into a coroutine. The "behavior" of the coroutine—how its return value is constructed, whether it suspends at startup, what happens when it ends—is entirely controlled by a nested type called `promise_type`.

This `promise_type` isn't anything mysterious; it's simply a nested class of the coroutine's return type (or a type specified via `std::coroutine_traits`). The compiler constructs a `promise_type` object for you within the coroutine's "coroutine frame," and then calls methods on this object at various nodes of the coroutine's lifecycle.

What we're going to do now is walk through the lifecycle of a coroutine and break down every hook of `promise_type`.

### Lifecycle Overview

From the moment a coroutine is called until it is finally destroyed, it roughly goes through several stages. First, the compiler allocates a block of memory on the heap to save the coroutine state—local variables, suspension points, the promise object, etc.—this is the so-called "coroutine frame." You can customize the allocation strategy via `operator new` in `promise_type`, but in most cases, the default heap allocation is sufficient. Once the coroutine frame is allocated, the compiler constructs a `promise_type` instance inside it and immediately calls `get_return_object`. The return value of this method is the object returned to the caller by the coroutine function—usually, it grabs the coroutine's handle and wraps it inside the return type.

Next, before the first statement of the coroutine body executes, `initial_suspend` is called. It returns an awaitable that decides whether the coroutine "starts executing immediately" or "suspends first." After that, your code runs. During this time, `co_await` (suspend), `co_yield` (yield value and suspend), or `co_return` (return and end) may occur. When `co_return` is executed, it triggers `return_value` or `return_void`—the former if there is a return value, the latter if not. After the coroutine body finishes (or exits via exception), `final_suspend` is called, which decides whether the coroutine suspends upon ending. If `final_suspend` returns `std::suspend_never`, the coroutine frame is automatically destroyed; if it returns `std::suspend_always`, the frame remains suspended until someone manually calls `destroy`. If an uncaught exception is thrown during execution, `unhandled_exception` is called, and then it jumps directly to `final_suspend`.

Here is a minimal `promise_type` implementation containing all necessary hooks:

```cpp
#include <coroutine>
#include <iostream>

// 1. Define the return type of the coroutine
struct Task {
    struct promise_type {
        Task get_return_object() {
            // 2. Create the return object, usually holding the handle
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept { return {}; } // 3. Start immediately
        std::suspend_always final_suspend() noexcept { return {}; }  // 4. Suspend at end (manual lifecycle)

        void return_void() {} // 5. Handle co_return with no value
        void unhandled_exception() { std::terminate(); } // 6. Handle exceptions

        // Optional: co_yield support
        std::suspend_always yield_value(int value) {
            current_value = value;
            return {};
        }

        int current_value;
    };

    std::coroutine_handle<promise_type> handle;
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }

    // Prevent copying, allow moving
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
};

// 7. Example usage
Task my_coroutine() {
    std::cout << "Start\n";
    co_yield 42;
    std::cout << "End\n";
    co_return;
}

int main() {
    Task t = my_coroutine();
    // ... do something else ...
    if (t.handle.done()) {
        t.handle.destroy();
    }
    return 0;
}
```

You will notice that while this example is simple, it already covers the core responsibilities of `promise_type`. Let's break down each hook one by one.

### get_return_object(): Creating the Return Object

This hook is called immediately after the coroutine frame is allocated and the promise object is constructed. Its return value is the object returned to the caller by the coroutine function. There is a critical detail here: when `get_return_object` executes, the coroutine body has not yet started, but the coroutine frame already exists. Therefore, you can obtain the coroutine's handle via `std::coroutine_handle::from_promise` and stuff it into the return object, allowing the caller to control the coroutine's execution through this handle.

This design is essentially a "handshake" between the coroutine and the caller: the coroutine says, "I'm ready, here is my handle," and the caller, upon receiving the handle, can choose to immediately `resume` it or store it for later.

### initial_suspend(): Startup Suspension Decision

This hook decides whether the coroutine suspends before executing its first statement. It returns an awaitable object, and the usual choices are two: `std::suspend_never` (do not suspend, execute immediately) or `std::suspend_always` (suspend, wait for the caller to manually `resume`).

When should you use `std::suspend_never` versus `std::suspend_always`? It depends on your use case. If you want a "fire-and-forget" style coroutine, use `std::suspend_never`. If you want "lazy evaluation," where the caller needs to explicitly start it, use `std::suspend_always`. The latter is very common when implementing generators—you create a generator, but the coroutine body doesn't start executing until you call `next()` or `resume()` for the first time.

### final_suspend() noexcept: The Critical Decision at the End

This hook is likely the most error-prone part of the entire `promise_type`.

`final_suspend` is called after the coroutine body has finished (either via a normal `co_return` or after `unhandled_exception` has dealt with an exception). It also returns an awaitable, deciding whether the coroutine suspends upon completion.

The key question is: why do most implementations choose to return `std::suspend_always`?

> ⚠️ **If you return `std::suspend_never`, the coroutine frame will be destroyed immediately after `final_suspend` returns. This means any operation on the coroutine handle at that point is dangling—your program could crash at any time.**
>
> Returning `std::suspend_always` keeps the coroutine suspended in a completed state, keeping the coroutine frame valid. The caller can safely check the coroutine state, retrieve the return value, and then manually call `destroy` to clean up. This is a safer "manual lifecycle management" pattern.

Also, `final_suspend` is not optional—the standard mandates that `final_suspend` must be `noexcept`. The reason is straightforward: if the `await_suspend` of the awaitable returned by `final_suspend` throws an exception, the coroutine is already finished. Who should the exception be thrown to? There is no reasonable receiver, so the standard simply forbids this possibility at compile time.

### return_value() / return_void(): Handling co_return

When the coroutine executes `co_return`, `return_value` is called. If `co_return` has no return value (or the coroutine body implicitly `returns` at the end), `return_void` is called.

Note that the choice between `return_value` and `return_void` depends on your coroutine design: if your coroutine always returns a value via `co_return`, define `return_value`; if your coroutine exits via `co_return` without a value (or executes to the end of the function), define `return_void`. Technically you can define both—`co_return value` calls `return_value`, `co_return;` calls `return_void`—but in practice, a well-designed coroutine type usually uses only one to avoid confusing the caller.

A typical `return_value` implementation stores the value in the promise object, to be retrieved later via the handle:

```cpp
struct Task {
    struct promise_type {
        int result;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() = delete; // Disable return_void

        void return_value(int v) {
            result = v;
        }

        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    // ... (constructors/destructors omitted for brevity) ...

    int get_result() {
        if (handle.done()) {
            return handle.promise().result;
        }
        throw std::runtime_error("Coroutine not finished");
    }
};

Task compute_value() {
    co_return 42;
}
```

### yield_value(): Handling co_yield

`co_yield` is essentially equivalent to `co_await promise.yield_value(value)`. That is, the return value of `yield_value` must be an awaitable. The most common practice is to return `std::suspend_always`, indicating that the coroutine suspends after every yield, handing control back to the caller.

`yield_value` is core to implementing generators. Each time the caller fetches a value from the generator, the generator executes until the next `co_yield`, yields the value, suspends, and waits for the next fetch.

```cpp
template<typename T>
struct Generator {
    struct promise_type {
        T current_value;

        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; } // Lazy start
        std::suspend_always final_suspend() noexcept { return {}; }  // Keep frame for cleanup

        void return_void() {}

        std::suspend_always yield_value(T value) {
            current_value = value;
            return {}; // Suspend
        }

        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    Generator(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Generator() { if (handle) handle.destroy(); }

    // Prevent copying
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    // Move semantics
    Generator(Generator&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    // Iterator interface
    struct Iter {
        std::coroutine_handle<promise_type> handle;
        bool operator!=(const Iter&) const { return !handle.done(); }
        void operator++() { handle.resume(); }
        T operator*() const { return handle.promise().current_value; }
    };

    Iter begin() {
        if (handle) handle.resume(); // Start the coroutine
        return Iter{handle};
    }
    Iter end() { return Iter{nullptr}; }
};

Generator<int> range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}
```

While simple, this generator demonstrates the core usage of `yield_value`: each `co_yield` outputs a value and suspends, and the caller advances to the next value via `resume` (hidden in the iterator's `++`). This is the mechanism behind Python's `yield` keyword, except in C++, you have to build the framework yourself.

### unhandled_exception(): The Last Line of Defense

If an exception is thrown within the coroutine body and is not caught, `unhandled_exception` is called. You can do a few things in this hook:

The simplest approach is to do nothing (the implicit call of `return_void`), or simply `std::terminate` to re-throw the exception to the caller. However, both approaches are rather crude. A more refined approach is to store the `std::exception_ptr` in the promise object and re-throw it later when the caller retrieves the result via the handle. This turns exception propagation into "on-demand" rather than "immediate explosion."

```cpp
struct Task {
    struct promise_type {
        int result;
        std::exception_ptr e_ptr;

        // ... (other methods omitted) ...

        void return_value(int v) { result = v; }

        void unhandled_exception() {
            e_ptr = std::current_exception();
        }
    };

    // ... (Task wrapper omitted) ...

    int get_result() {
        if (handle.promise().e_ptr) {
            std::rethrow_exception(handle.promise().e_ptr);
        }
        return handle.promise().result;
    }
};
```

Great, we have now gone through all the major hooks of `promise_type`. Looking back, you'll realize that `promise_type` is essentially a "coroutine behavior controller": it controls how the coroutine starts, ends, and handles return values and exceptions. The "suspension and resumption" within the coroutine body—controlled by `co_await`—is managed by another mechanism, which is the awaiter/awaitable protocol we will discuss next.

## The awaiter/awaitable Protocol

If `promise_type` controls the "macroscopic lifecycle" of a coroutine, then awaiter/awaitable controls the "microscopic suspension and resumption." Every time you write `co_await expr` in a coroutine, the compiler executes a fixed protocol on the expression: first asking "are you ready?", then "what to do after suspending?", and finally "what result to give me after resuming?".

### The Expansion of co_await expr

Let's look step-by-step at what the compiler does when processing `co_await expr`.

First, the compiler needs to obtain an awaiter object from `expr`, a process that happens in two steps.

The first step is to get the awaitable. If `expr` defines an `operator co_await` member function, the compiler calls `operator co_await` first to get an intermediate result; this intermediate result is the awaitable. If there is no `operator co_await`, the original `expr` itself is the awaitable. (Note that expressions produced by `co_await`, `co_yield`, and `co_return` skip `operator co_await` and are used directly as awaitables.)

The second step is to get the awaiter from the awaitable. The compiler performs overload resolution on `await_transform` (if available in `promise_type`), then `operator co_await`. Both member functions and non-member functions participate in the candidates together—it's not a "find member then ADL" sequential search, but a unified overload resolution. If there is exactly one best match, its return value is used as the awaiter. If `operator co_await` is completely missing, the awaitable itself is treated as the awaiter—provided it has `await_ready`, `await_suspend`, and `await_resume` methods. If overload resolution is ambiguous (e.g., both member and non-member match), the program is ill-formed, and the compiler will error.

Once the awaiter is obtained, the compiler executes the following steps:

```text
1. Call awaiter.await_ready():
   - If returns true: skip suspension, go directly to step 3.
   - If returns false: proceed to step 2.

2. Call awaiter.await_suspend(handle):
   - If returns void: suspend coroutine, return to caller/resumer.
   - If returns true: suspend coroutine.
   - If returns false: do not suspend, resume immediately (go to step 3).
   - If returns coroutine_handle: suspend current coroutine, resume the returned handle (symmetric transfer).

3. Call awaiter.await_resume():
   - Return value is the result of the co_await expr.
```

You will notice that these three methods form a precise "query-suspend-resume" protocol:

**`await_ready`** returns a `bool`. If it returns `true`, it means "no need to suspend, I'm ready," and it jumps directly to `await_resume`. If it returns `false`, it means "I'm not ready, need to suspend." This method is a fast-path optimization—if you know the operation is already complete (e.g., a cached result), returning `true` avoids the overhead of suspension/resumption.

**`await_suspend`** is called after confirming the need to suspend and receives the current coroutine's `std::coroutine_handle`. This is the most flexible part of the protocol—it has three legal return types. Returning `void` means the coroutine unconditionally suspends, returning control to the caller or resumer. Returning `bool` allows you to change your mind at the last minute—`true` means suspend, `false` means don't suspend (resume immediately). Returning a `std::coroutine_handle` is what's known as symmetric transfer—the coroutine suspends but doesn't return to the caller; instead, it immediately resumes the coroutine corresponding to the returned handle. This mechanism is crucial in coroutine chain calls to avoid stack overflow. We will dedicate a section later to breaking down these three forms.

One more easily overlooked point: if `await_suspend` throws an exception, the coroutine is automatically resumed, and the exception is immediately re-thrown into the coroutine body. That is, the exception doesn't escape to the caller; it stays inside the coroutine—you can catch it with `try-catch` in the coroutine body, or let it bubble up to `unhandled_exception`.

> ⚠️ **The bool semantics of `await_ready` and `await_suspend` are inverted!** `await_ready` returning `true` means "don't suspend," while `await_suspend` returning `true` means "suspend." This design trips many people up when they first encounter it. You can remember it this way: `await_ready` asks "are you ready?", if ready, no need to suspend; `await_suspend` asks "should I suspend?", `true` means "yes, suspend."

**`await_resume`** is called when the coroutine resumes execution (or immediately if `await_suspend` returns `false`). Its return value becomes the value of the entire `co_await expr`. If you don't need to return any value, returning `void` is fine.

### An Async Timer Awaiter

Enough theory. Let's use a concrete example to tie these concepts together. We want to implement an `AsyncTimer`—an awaiter that makes a coroutine "sleep" for a specified number of milliseconds.

Of course, real async sleep requires an event loop and timers, but for now, we'll use a simplified synchronous version to demonstrate the complete structure of an awaiter:

```cpp
#include <coroutine>
#include <chrono>
#include <thread>

struct AsyncTimer {
    int milliseconds;
};

// Awaiter object
struct TimerAwaiter {
    int milliseconds;

    bool await_ready() const noexcept { return false; } // Always need to wait

    void await_suspend(std::coroutine_handle<> handle) const {
        // Simulate async wait (in reality, this would register with an event loop)
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        // After "sleep", we resume. Note: this blocks the thread!
        handle.resume();
    }

    void await_resume() const noexcept {}
};

// operator co_await overload
TimerAwaiter operator co_await(AsyncTimer timer) {
    return TimerAwaiter{timer.milliseconds};
}

// Usage
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }
};

Task example() {
    std::cout << "Start\n";
    co_await AsyncTimer{1000}; // "Sleep" for 1 second
    std::cout << "End\n";
}
```

Wait, there's a problem with the code above: `operator co_await` is a free function, so ADL (Argument-Dependent Lookup) needs to consider namespaces. For the built-in type `int` (if we were to use it directly), ADL doesn't work—`int` has no associated namespace. A more correct approach is to intercept via `promise_type`'s `await_transform`:

```cpp
struct Task {
    struct promise_type {
        // ... (other members) ...

        // 1. Intercepts co_await in the coroutine
        TimerAwaiter await_transform(AsyncTimer timer) {
            return TimerAwaiter{timer.milliseconds};
        }
    };
    // ... (Task wrapper) ...
};
```

In this example, `await_transform` acts as a "middleman"—it converts `AsyncTimer` into `TimerAwaiter`. This pattern is very common in real projects: you can perform type checks, logging, cancellation checks, etc., inside `await_transform`.

### The Three Return Forms of await_suspend

Now the question arises: why does `await_suspend` have three return forms? Isn't this just making things more complicated?

Actually, each form has its use case. Let's break them down one by one.

**Returning `void`** is the simplest—the coroutine suspends, and control returns to the caller or the initiator of the most recent `co_await`. This applies to scenarios where "suspension is entirely managed externally," such as storing the handle in a queue for an event loop to resume later.

**Returning `bool`** gives you a chance to make a final decision between suspending and not suspending. For example, you check and find that the I/O operation is actually already complete, so you return `false` to let the coroutine continue executing, avoiding the overhead of unnecessary suspension/resumption.

**Returning `std::coroutine_handle`** is the most powerful but also the most error-prone form. This is the so-called symmetric transfer. When your `await_suspend` returns a handle, the compiler suspends the current coroutine and **immediately** resumes the coroutine corresponding to the returned handle—it does not return to the caller. The standard design intent is to allow the compiler to perform tail call optimization, thereby not increasing call stack depth—mainstream compilers (GCC, Clang, MSVC) do indeed do this at higher optimization levels. But strictly speaking, tail call optimization is a "quality of implementation" issue, not a "standard guarantee": both GCC and Clang have had bugs where symmetric transfer still caused stack overflows (GCC #100897, LLVM #42853). In practice, this mechanism reliably avoids stack overflow, but don't rely on it at `-O0`.

Here is an example demonstrating symmetric transfer:

```cpp
struct SymmetricTransferAwaiter {
    std::coroutine_handle<> next;

    bool await_ready() const noexcept { return false; }

    // Return the handle of the next coroutine
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> current) noexcept {
        return next; // Transfer execution to 'next'
    }

    void await_resume() const noexcept {}
};
```

> ⚠️ **Symmetric transfer is a key mechanism for avoiding coroutine stack overflow.** If your coroutine A calls B, B calls C, C calls D... and every layer is "suspend A → resume B → suspend B → resume C," the call stack gets deeper and deeper without symmetric transfer. Symmetric transfer gives the compiler a chance to avoid stack growth via tail call optimization—this is crucial in scenarios with long coroutine chains (e.g., deep recursive coroutine chains). Note that tail call optimization is "quality of implementation," not a "standard guarantee," so stack overflow is still possible at low optimization levels.

## operator co_await and ADL

Earlier we discussed how the compiler obtains an awaiter from an awaitable via `operator co_await` overload resolution. There is a problem often encountered in real engineering: if you have a type from a third-party library and cannot modify its source code, how do you add `operator co_await` to it?

The answer is to use ADL (Argument-Dependent Lookup). When overload resolution looks for candidate functions for `operator co_await`, in addition to looking for member functions in the scope of the awaitable's class, it also looks for free functions in the associated namespaces of the awaitable type via ADL. This gives us a backdoor to extend a type's await capability without modifying the original type. Here is a concrete example:

```cpp
namespace third_party {
    struct Socket {
        int fd;
        // ... no coroutine support here ...
    };
}

// 1. Extend Socket via ADL
namespace third_party {
    struct SocketAwaiter {
        Socket& socket;
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) const;
        void await_resume() const;
    };

    SocketAwaiter operator co_await(Socket& socket) {
        return SocketAwaiter{socket};
    }
}

// 2. Now you can co_await a Socket
Task network_task(third_party::Socket& sock) {
    co_await sock; // Finds operator co_await via ADL
}
```

This is the power of ADL—you don't need to modify the original type, just provide a free function `operator co_await` overload in its namespace. Of course, if you can modify the type itself, adding a member `operator co_await` is simpler. However, note that if both a member and a non-member `operator co_await` exist for the same type and both match, overload resolution will be ambiguous, and the compiler will error. So don't provide both methods for the same type.

## From awaitable to Scheduler

So far, all our awaiters have been doing "immediate" things in `await_suspend`—either synchronous blocking or immediate resumption. But in a real async framework, what `await_suspend` does is usually submit the coroutine handle to a scheduler (event loop, thread pool, etc.), and then let the scheduler resume the coroutine at the appropriate time.

This is the bridge between awaitable and the scheduler: **`await_suspend` is the key integration point for the scheduler**. When a coroutine suspends, `await_suspend` gets the coroutine's handle. It can store this handle anywhere—a queue, a timer list, the data field of an epoll event—and then let the scheduler `resume` it later.

Next, let's look at a minimal scheduler framework that shows how a complete "coroutine + scheduler" works.

```cpp
#include <coroutine>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>

// Minimal Scheduler
class Scheduler {
public:
    void enqueue(std::coroutine_handle<> handle) {
        std::lock_guard lock(mutex);
        ready_queue.push(handle);
        cv.notify_one();
    }

    void run() {
        while (true) {
            std::unique_lock lock(mutex);
            cv.wait(lock, [this] { return !ready_queue.empty(); });

            auto handle = ready_queue.front();
            ready_queue.pop();
            lock.unlock();

            if (!handle.done()) {
                handle.resume();
            } else {
                handle.destroy();
            }
        }
    }

private:
    std::queue<std::coroutine_handle<>> ready_queue;
    std::mutex mutex;
    std::condition_variable cv;
};

// Global scheduler instance (for demo purposes)
Scheduler global_scheduler;

// Awaiter for yielding execution
struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        // Put current coroutine back into the ready queue
        global_scheduler.enqueue(handle);
    }

    void await_resume() const noexcept {}
};

// Awaiter for async sleep
struct SleepAwaiter {
    int seconds;
    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        // Launch a thread to simulate async timer
        std::thread([handle, seconds = this->seconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            global_scheduler.enqueue(handle);
        }).detach();
    }

    void await_resume() const noexcept {}
};

// Task wrapper
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }
};

// Helper functions
YieldAwaiter yield() { return {}; }
SleepAwaiter sleep(int seconds) { return {seconds}; }

// Example usage
Task producer() {
    for (int i = 0; i < 5; ++i) {
        std::cout << "Producing " << i << "\n";
        co_await yield(); // Yield to next task
    }
}

Task consumer() {
    for (int i = 0; i < 3; ++i) {
        std::cout << "Consuming...\n";
        co_await sleep(1); // Async sleep
    }
}

int main() {
    producer();
    consumer();
    global_scheduler.run();
}
```

While rudimentary, this scheduler demonstrates the core model of coroutine scheduling. `YieldAwaiter` shows the most basic cooperative scheduling: the coroutine voluntarily yields execution, puts itself back in the ready queue, and lets other coroutines run. `SleepAwaiter` shows the basic pattern of an async timer: suspend the coroutine, set a timer (simulated here with a thread), and when the timer expires, put the coroutine back in the ready queue.

The real challenges come later—when we combine this scheduler with I/O multiplexing (epoll), things get more complex, but the basic model remains unchanged: **the awaiter's `await_suspend` is responsible for submitting the coroutine handle to the scheduler, and the scheduler resumes the coroutine at the appropriate time**.

## Where We Are

In this post, we dissected the two main customization points of C++20 coroutines. `promise_type` controls the macroscopic lifecycle of the coroutine—how to create the return object, whether to suspend at startup, what to do at the end, and how to handle return values and exceptions. The awaiter/awaitable protocol controls the microscopic suspension and resumption—`await_ready` asks "are you ready?", `await_suspend` does the work upon suspension, and `await_resume` gets the result upon resumption. The three return forms of `await_suspend` (void / bool / coroutine_handle) provide progressive flexibility from simple suspension to symmetric transfer. Finally, we saw that `await_suspend` is the key integration point for schedulers—it submits the coroutine handle to the scheduler, letting the scheduler decide when to resume the coroutine.

But so far, our scheduler is just a toy consisting of a "ready queue + sequential execution." Real async I/O needs to interface with the OS's I/O multiplexing mechanisms. In the next post, we will combine coroutines with epoll (Linux's I/O multiplexing) to build an event loop capable of handling real network I/O. That is where coroutines truly shine.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `src/coroutines`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — The authoritative reference for C++20 coroutines, including complete language specifications
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — A practical article on building a coroutine async framework from scratch
- [My Tutorial and Take on C++20 Coroutines — David Mazieres (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html) — A coroutine tutorial by a Stanford professor, deep and practical
- [C++ Coroutines: Defining the co_await operator — Raymond Chen (Microsoft)](https://devblogs.microsoft.com/oldnewthing/20191218-00/?p=103221) — Explains `operator co_await` member vs. free function overloading and overload resolution rules
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/) — A practical guide, including a reminder about the bool semantic difference between `await_ready` and `await_suspend`
- [C++20 Coroutines — Complete Guide — Simon Toth (ITNEXT)](https://itnext.io/c-20-coroutines-complete-guide-7c3fc08db89d) — A comprehensive guide covering the coroutine mechanism
