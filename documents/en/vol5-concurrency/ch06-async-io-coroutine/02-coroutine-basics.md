---
chapter: 6
cpp_standard:
- 20
description: A deep dive into C++20 coroutine syntax, the state-machine model, and lifecycle management, and how the compiler transforms co_await/co_yield/co_return
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'The Evolution of Asynchronous Programming: From Callback Hell to Coroutines'
reading_time_minutes: 25
related:
- promise_type and awaitable
- Asynchronous I/O and Event Loops
tags:
- host
- cpp-modern
- intermediate
- coroutine
- 异步编程
title: C++20 Coroutine Fundamentals
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/02-coroutine-basics.md
  source_hash: a4c8e7dee4f251189089eb5dd85d18b5aab1ffa352d893298f584ed140f2730c
  translated_at: '2026-09-26T09:01:09+00:00'
  engine: anthropic
  token_count: 14000
---
# C++20 Coroutine Fundamentals

In the previous article we saw how coroutines make asynchronous code look like synchronous code—a linear flow, no nesting, no pyramid of callbacks. That article focused on "why we need coroutines" and "what a coroutine looks like"; we showed the end result but never explained what actually goes on behind it. This time we take coroutines apart from the inside out: what transform does the compiler apply to a coroutine function? What lives in the coroutine frame? How does `coroutine_handle` manage the coroutine's lifetime? The answers to these questions form the foundation for understanding C++20 coroutines.

Let's be honest up front: the learning curve of C++20 coroutines is fairly steep. This is not a "learn `co_await` and you're done" feature—you need to understand how promise_type, coroutine_handle, awaitable, and awaiter cooperate before you can actually write correct coroutine code. The good news is that the relationships between these concepts are fixed: once you understand this model, every piece of coroutine code is a variation on the same pattern. Our goal in this article is to explain the model thoroughly.

## Environment

All code in this article compiles with GCC 12+, Clang 15+, and MSVC 19.34+; all three compilers provide complete C++20 coroutine support. There are no special platform dependencies—Linux, macOS, and Windows all work, because we use only the pure standard library. As for compiler options, `-std=c++20` is mandatory; versions before GCC 12 may additionally need the `-fcoroutines` flag, but GCC 12+ enables it by default. One thing to note in advance: this article leans heavily on the `<coroutine>` header, the library side of C++20 coroutine support, which provides infrastructure pieces such as `std::coroutine_handle`, `std::suspend_always`, and `std::suspend_never`.

## The Three Keywords: co_await, co_yield, co_return

C++20 introduces three keywords for coroutines. Each has its own job, but they share one effect: the moment any one of them appears in a function body, the compiler treats that function as a coroutine. No extra declaration, attribute, or specifier is needed—the keyword itself is the signal.

`co_await` is the most central of the three. It shows up wherever you need to "wait a moment"—suspend the current coroutine, yield control, and resume once some asynchronous operation completes. The semantics of `co_await expr` are: treat `expr` as an awaitable, and use it to decide whether to suspend, how to suspend, and what value comes back when execution resumes. Let's look at the simplest possible example:

```cpp
#include <coroutine>
#include <iostream>

// The minimal coroutine return type
struct SimpleTask
{
    struct promise_type
    {
        SimpleTask get_return_object()
        {
            return SimpleTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
};

// A simple coroutine
SimpleTask demo_coroutine()
{
    std::cout << "第一步：协程开始执行\n";

    // co_await std::suspend_always{} suspends the coroutine
    co_await std::suspend_always{};

    std::cout << "第二步：协程恢复后继续执行\n";

    co_await std::suspend_always{};

    std::cout << "第三步：协程再次恢复\n";
}

int main()
{
    std::cout << "主线程: 启动协程\n";

    // Call the coroutine function; it returns a SimpleTask
    SimpleTask task = demo_coroutine();

    // Because initial_suspend returns suspend_never,
    // the coroutine runs immediately up to the first co_await
    std::cout << "主线程: 协程已挂起，手动恢复\n";
    task.handle.resume();

    std::cout << "主线程: 再次恢复\n";
    task.handle.resume();

    std::cout << "主线程: 协程执行完毕\n";
    task.handle.destroy();
    return 0;
}
```

The output looks like this:

```text
主线程: 启动协程
第一步：协程开始执行
主线程: 协程已挂起，手动恢复
第二步：协程恢复后继续执行
主线程: 再次恢复
第三步：协程再次恢复
主线程: 协程执行完毕
```

Notice that once `demo_coroutine()` is called, it does not run to completion in one go—each time it hits a `co_await std::suspend_always{}`, the coroutine suspends and control returns to `main()`. When we call `task.handle.resume()`, the coroutine picks up where it last suspended and carries on. `std::suspend_always` is the simplest awaitable the standard library provides: its `await_ready()` always returns `false`, meaning "always suspend". Its counterpart `std::suspend_never` has an `await_ready()` that always returns `true`, meaning "never suspend".

`co_yield expr` produces a value and suspends the coroutine. It is equivalent to `co_await promise.yield_value(expr)`. `co_yield` is the foundation for building generators—each time a value is produced, the coroutine suspends, and it resumes only after the consumer has taken the value. Later in this article we will build a generator from scratch.

`co_return` ends the coroutine. It comes in two forms: `co_return;` (no return value) and `co_return expr;` (with a return value). The former is equivalent to calling `promise.return_void()`; for the latter, if the type of `expr` is not void it is equivalent to `promise.return_value(expr)`, and if the type of `expr` is void it also calls `promise.return_void()`. `co_return` is different from an ordinary `return`—plain `return` statements cannot appear in a coroutine; a coroutine must end with `co_return` (or simply let the function body run to its end, in which case the compiler implicitly inserts a `co_return;` at the closing brace).

> ⚠️ Note that `co_return` and plain `return` cannot be mixed. If a function contains any of `co_await`, `co_yield`, or `co_return`, it is a coroutine, and plain `return` statements inside its body are illegal—the compiler rejects them outright. Conversely, if the function body contains no `co_*` keyword at all, then even if the return type defines a `promise_type`, it is just an ordinary function.

## What the Compiler Does — The Coroutine State Machine

This is the core of understanding C++20 coroutines. When you write a coroutine function, the compiler does not simply emit a stretch of linear code the way it does for an ordinary function. Instead it **transforms the entire coroutine function into a state machine**—every `co_await` (including the initial suspend point and the final one) is a state, and each time the coroutine resumes, it jumps to the code position corresponding to its current state and continues from there.

Let's trace this transform with a simplified example. Suppose you write this coroutine:

```cpp
SimpleTask example(int x)
{
    int a = x + 1;
    co_await std::suspend_always{};
    int b = a + 2;
    co_await std::suspend_always{};
    co_return;
}
```

The compiler roughly transforms it into pseudocode like the following (many details simplified, but the core logic is accurate):

```text
1. Allocate the coroutine frame
2. Copy the parameter x into the coroutine frame
3. Construct the promise_type object inside the coroutine frame
4. Call promise.get_return_object() to obtain the return value
5. co_await promise.initial_suspend()
6. Enter the state machine:

   State 0: (initial state)
     a = x + 1
     Save the current suspend point as "state 1"
     co_await std::suspend_always{}
     → suspend, return to the caller

   State 1: (resumed from the first co_await)
     b = a + 2
     Save the current suspend point as "state 2"
     co_await std::suspend_always{}
     → suspend, return to the caller

   State 2: (resumed from the second co_await)
     Call promise.return_void()
     Destroy the local variables b and a
     co_await promise.final_suspend()
     → final suspend
```

Let's walk through this transform step by step.

**Step one: allocate the coroutine frame.** The coroutine frame is a block of heap memory (usually) that stores everything the coroutine needs in order to resume execution. It holds several parts: copies of the function's parameters (a coroutine can outlive its caller's stack, so the parameters must be copied into the frame to avoid dangling references), local variables (the ones whose lifetimes span a suspend point—if a local variable is created before a `co_await` and still used afterward, it has to live in the coroutine frame), the promise object (part of the coroutine's state), and the current suspend-point index (so a resume knows which state it should jump to).

> ⚠️ Only local variables whose lifetimes span a suspend point get stored in the coroutine frame. If a local variable's lifetime begins and ends between two suspends, the compiler is free to optimize it into a register or onto the ordinary stack. That optimization is entirely the compiler's call.

**Step two: copy the parameters.** Every parameter passed by value is moved or copied into the coroutine frame. Parameters passed by reference keep only the reference itself—which means if you pass a reference to a local variable into a coroutine and that variable dies before the coroutine resumes, you are left with a dangling reference. This is a classic pitfall of C++20 coroutines: **capturing parameters by reference in a coroutine is dangerous**, because you cannot guarantee that the referenced object is still alive when the coroutine resumes.

**Step three: construct the promise object.** `promise_type` is the coroutine's "introspection interface"—the compiler calls promise methods at every key point of the coroutine's execution. It is not some vague notion: the compiler deduces it from the coroutine's return type via `std::coroutine_traits`. If your return type is `Task`, the compiler goes looking for `Task::promise_type`.

**Step four: call `get_return_object()`.** The return value of this method is the object the coroutine function hands back to the caller (the `SimpleTask` in our example). This call happens before the coroutine body starts executing—in other words, when the caller receives the return value, not a single line of the coroutine body has run yet.

**Step five: call `initial_suspend()`.** This method decides whether the coroutine suspends before the function body starts executing. If it returns `std::suspend_always` (lazy start), the coroutine suspends before running its first line of code, and the caller must call `resume()` manually to set it to work. If it returns `std::suspend_never` (eager start), the coroutine charges straight into the function body and runs until it hits the first `co_await`.

**Step six: run the body and handle the suspend points.** The coroutine executes its body; when it encounters a `co_await`, it first calls the awaitable's `await_ready()`. If that returns `true`, no suspension is needed and execution simply continues. If it returns `false`, the current state is saved (the suspend-point index and the live local variables), `await_suspend(handle)` is called, and then the coroutine suspends—control goes back to the caller or the resumer. When the coroutine is `resume()`d, it picks up from the saved suspend point, calls `await_resume()` to obtain the result of the `co_await` expression, and continues from there.

**The final state: `final_suspend()`.** When the coroutine reaches `co_return` (or the end of the body), it calls `promise.return_void()` or `promise.return_value()`, destroys all live local variables, then calls `promise.final_suspend()` and `co_await`s its result. This `final_suspend` is the coroutine's final stop—if it returns `std::suspend_always`, the coroutine suspends in its final state and waits for the outside world to destroy the coroutine frame via `coroutine_handle::destroy()`. If it returns `std::suspend_never`, the coroutine frame destroys itself automatically—but you must then guarantee that nobody still holds a `coroutine_handle` to this coroutine, otherwise it is use-after-free.

## coroutine_handle: A Handle to the Coroutine Frame

`std::coroutine_handle<>` (or its specialization `std::coroutine_handle<Promise>`) is a non-owning handle to the coroutine frame. You can think of it as a "raw pointer"—it points at the coroutine frame but takes no responsibility for its life or death.

The most used operation is `resume()`, which resumes the coroutine so it continues from its last suspend point. But there is one precondition: the coroutine must not have reached the final suspend state yet. If `done()` has already returned `true`, calling `resume()` again is undefined behavior—on some compilers it may happen not to crash, while at a different optimization level it may segfault on the spot. `destroy()` destroys the coroutine frame: it invokes the promise's destructor, then the parameters' destructors, and finally releases the coroutine frame's memory. `done()` checks whether the coroutine has already reached its final suspend point—that is, whether the function body has finished executing and the coroutine sits in the `final_suspend` state. There is also a static method `from_promise(promise)`, which recovers the corresponding `coroutine_handle` from a reference to the promise object. It is used all the time inside promise_type's methods, because you often need to grab your own handle inside a promise method and hand it to the outside world.

Let's use a complete example to demonstrate the basic operations of `coroutine_handle`:

```cpp
#include <coroutine>
#include <iostream>

struct Resumable
{
    struct promise_type
    {
        Resumable get_return_object()
        {
            return Resumable{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // Lazy start: the coroutine suspends immediately after creation, without running the body
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;

    // RAII: destroy the coroutine frame automatically on destruction
    ~Resumable()
    {
        if (handle) {
            handle.destroy();
        }
    }
};

Resumable countdown(int from)
{
    while (from > 0) {
        std::cout << "  countdown: " << from << "\n";
        --from;
        co_await std::suspend_always{};   // Suspend after each loop iteration
    }
    std::cout << "  countdown: 发射!\n";
}

int main()
{
    std::cout << "创建协程...\n";
    Resumable task = countdown(5);

    // Because initial_suspend returns suspend_always,
    // the coroutine has not started executing yet

    std::cout << "开始恢复协程:\n";
    while (!task.handle.done()) {
        task.handle.resume();
        if (!task.handle.done()) {
            std::cout << "  (协程已挂起，可以干别的事)\n";
        }
    }

    std::cout << "协程已完成\n";
    // Resumable's destructor calls handle.destroy()
    return 0;
}
```

Output:

```text
创建协程...
开始恢复协程:
  countdown: 5
  (协程已挂起，可以干别的事)
  countdown: 4
  (协程已挂起，可以干别的事)
  countdown: 3
  (协程已挂起，可以干别的事)
  countdown: 2
  (协程已挂起，可以干别的事)
  countdown: 1
  countdown: 发射!
协程已完成
```

Watch how the coroutine suspends every time the loop reaches `co_await std::suspend_always{}`, returning control to `main()`. `main()` can check `done()` to see whether the coroutine has finished, then decide whether to `resume()` again or go do something else. This is the fundamental difference between a coroutine and an ordinary function: an ordinary function is either executing or has already returned; a coroutine can "pause"—after suspending it does not vanish, but rests with its full state preserved in the coroutine frame, ready to resume at any moment.

Here is a vitally important detail: `coroutine_handle` is non-owning. It does not destroy the coroutine frame when it is destructed. If you obtain a `coroutine_handle` and never call `destroy()`, the coroutine frame leaks—that heap block is never released. So you should almost always wrap the `coroutine_handle` in an RAII class (like the `Resumable` above) and let the destructor handle cleanup automatically.

> ⚠️ Neither `resume()` nor `destroy()` on a `coroutine_handle` should be called after the coroutine is already `done()`. Calling `resume()` on a finished coroutine is undefined behavior—it may happen "not to crash" in your build, but under another compiler or optimization level it may segfault immediately.

## The Coroutine Lifecycle

A coroutine's lifetime begins the moment it is called and ends the moment its coroutine frame is destroyed. Let's walk through the whole process.

**Creation**. When you call a coroutine function, the compiler-generated code first allocates the coroutine frame, then copies the parameters, constructs the promise, and calls `get_return_object()`. At this point the coroutine body has not started executing—the caller already holds the return object (which contains the `coroutine_handle`), but the coroutine's "actual execution" still waits on the result of `initial_suspend()`.

**Execution**. If `initial_suspend()` returns `suspend_never`, the coroutine starts executing the body immediately and runs until it hits the first genuine `co_await` (the one whose `await_ready()` returns `false`). If it returns `suspend_always`, the coroutine suspends before the body even begins and waits for an external `resume()`. During execution, each time the coroutine encounters a `co_await` that requires suspension, it saves its current state and then returns control to the caller or the resumer.

**Completion**. When the coroutine reaches `co_return` (or the end of the body, provided the promise has `return_void()`), it calls `promise.return_void()` or `promise.return_value()`, destroys the local variables, and then calls `promise.final_suspend()`. Here lies a key design point: **`final_suspend()` should return `std::suspend_always`**.

Why should `final_suspend` return `suspend_always`? Because if it returns `suspend_never`, the coroutine frame is destroyed automatically right after `final_suspend` returns—at that moment the coroutine body has ended and the local variables are gone, but the outside world may still be holding a `coroutine_handle`. If the outside does not know the coroutine has already destroyed itself, calling `resume()` or `destroy()` afterwards is use-after-free. Returning `suspend_always` keeps the coroutine suspended in its final state with the frame still alive, so external code can detect completion through `done()` and then safely call `destroy()` to tear the coroutine frame down.

> ⚠️ Dangling coroutines are among the most common coroutine bugs. The typical scenario: you return an object containing a `coroutine_handle`, but the caller fails to manage its lifetime properly—either forgetting to call `destroy()` and leaking memory, or continuing to use the `coroutine_handle` after the coroutine frame has been destroyed. The best practice is to always wrap `coroutine_handle` in RAII and never let it wander bare outside an API boundary.

## Building a Generator from Scratch

Good—by now we have a handle on the basic mechanics of coroutines. Next we will do something very practical: build, from scratch, a generator that produces integer values with `co_yield`. This implementation exercises `promise_type`, `coroutine_handle`, and `co_yield` working in full concert, and it is a superb exercise for understanding C++20 coroutines.

We build the generator in three steps. First the skeleton—get the generator producing values with `co_yield` and get the outside iterating to fetch them. Then exception handling—make exceptions thrown inside the coroutine propagate correctly to the outside. Finally RAII—make sure the coroutine frame is properly destroyed when the generator is destructed.

### Step 1: The Skeleton — Producing and Consuming Values

```cpp
#include <coroutine>
#include <iostream>
#include <memory>

template<typename T>
class Generator
{
public:
    // ---- promise_type: how the compiler customizes coroutine behavior ----
    struct promise_type
    {
        T current_value;    // Stores the value produced by co_yield

        Generator get_return_object()
        {
            // Create a coroutine_handle from the promise, wrap it in a Generator, and return it
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // Initial suspend: the coroutine suspends immediately after creation (lazy start)
        // The caller must resume it manually before values are produced
        std::suspend_always initial_suspend() { return {}; }

        // Final suspend: the coroutine suspends after finishing, waiting for an external destroy()
        // Must not return suspend_never, or the coroutine frame destroys itself
        // and the external handle becomes a dangling pointer
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield expr is equivalent to co_await promise.yield_value(expr)
        // We store the value in current_value, then suspend
        std::suspend_always yield_value(T value)
        {
            current_value = value;
            return {};   // Return suspend_always to suspend the coroutine
        }

        // Called when the coroutine has no co_return, or co_return;
        void return_void() {}

        // Unhandled exception — just terminate for now
        void unhandled_exception() { std::terminate(); }
    };

    // ---- Iterator interface ----

    // Resume the coroutine, advancing to the next yield point
    bool next()
    {
        handle_.resume();
        return !handle_.done();
    }

    // Get the value at the current yield point
    T value() const
    {
        return handle_.promise().current_value;
    }

    // ---- Construction / destruction / moving ----

    explicit Generator(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    {
    }

    ~Generator()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    // Copying is forbidden — a coroutine_handle cannot share ownership
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    // Moving is allowed
    Generator(Generator&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;    // Prevent other's destructor from destroying
    }

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

private:
    std::coroutine_handle<promise_type> handle_;
};
```

First let's sort out the logic of this code. `promise_type` is the bridge between the compiler and the coroutine. When the compiler sees your coroutine function returning `Generator<int>`, it goes looking for `Generator<int>::promise_type`, and then calls the promise's methods at every key point of the coroutine's execution.

`get_return_object()` is the first to be called—it creates the `coroutine_handle`, wraps it in a `Generator`, and returns it to the caller. `initial_suspend()` returns `suspend_always`, which means the coroutine suspends before executing its body—after receiving the Generator, the caller must call `next()` (which calls `resume()` internally) before any values are produced. This is the standard generator design: **lazy start**, because a generator's consumer may only need the first few values, so there is no reason to produce them all at creation time.

`yield_value(T value)` is the actual operation behind `co_yield`. When the coroutine reaches `co_yield 42`, the compiler transforms it into `co_await promise.yield_value(42)`. Our implementation stores the value in `current_value` and then returns `suspend_always`—the coroutine suspends, and control returns to whoever called `next()`. The caller reads `current_value` through `value()`, then calls `next()` again to produce the next value.

`final_suspend()` returns `suspend_always` for the reason we explained earlier—after the coroutine finishes, it stays suspended, waiting for the outside Generator's destructor to call `destroy()`.

The Generator itself is an RAII wrapper around `coroutine_handle`. The destructor calls `destroy()` to tear down the coroutine frame; the move constructor/assignment use a `nullptr` marker to prevent double destruction; and copying is forbidden because a `coroutine_handle` does not support shared ownership.

Now let's use it to produce the Fibonacci sequence:

```cpp
Generator<int> fibonacci()
{
    int a = 0, b = 1;
    while (true) {
        co_yield a;         // Produce the current value, then suspend
        int temp = a + b;
        a = b;
        b = temp;
    }
    // This coroutine never co_returns — an infinite sequence
}

int main()
{
    auto gen = fibonacci();

    std::cout << "斐波那契数列前 15 项:\n";
    for (int i = 0; i < 15 && gen.next(); ++i) {
        std::cout << "  fib(" << i << ") = " << gen.value() << "\n";
    }

    // gen's destructor destroys the coroutine frame automatically
    return 0;
}
```

Output:

```text
斐波那契数列前 15 项:
  fib(0) = 0
  fib(1) = 1
  fib(2) = 1
  fib(3) = 2
  fib(4) = 3
  fib(5) = 5
  fib(6) = 8
  fib(7) = 13
  fib(8) = 21
  fib(9) = 34
  fib(10) = 55
  fib(11) = 89
  fib(12) = 144
  fib(13) = 233
  fib(14) = 377
```

Notice that the `fibonacci()` function looks like an utterly ordinary loop generating Fibonacci numbers—the only difference is that `co_yield` replaces `return` or `push_back`. Yet this function does not run to completion in one shot: each `co_yield` produces one value and then suspends, and the loop continues only when `next()` is called. This is lazy evaluation—values are produced on demand, with no need to compute and store all results up front. For infinite sequences or very large data sets, this property is extremely valuable.

### Step 2: Adding Exception Handling

The generator above has a problem: what if the coroutine body throws an exception? Right now our `unhandled_exception()` just calls `std::terminate()`, which is far too blunt. A better approach is to catch and store the exception, then rethrow it when the outside calls `next()` or `value()`:

```cpp
template<typename T>
class SafeGenerator
{
public:
    struct promise_type
    {
        T current_value;
        std::exception_ptr exception;   // Stores the exception

        SafeGenerator get_return_object()
        {
            return SafeGenerator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value)
        {
            current_value = value;
            return {};
        }

        void return_void() {}

        // Capture the exception and store it in the exception_ptr
        void unhandled_exception()
        {
            exception = std::current_exception();
        }
    };

    bool next()
    {
        handle_.resume();

        // After resume, check whether there is an exception
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }

        return !handle_.done();
    }

    T value() const
    {
        return handle_.promise().current_value;
    }

    explicit SafeGenerator(std::coroutine_handle<promise_type> handle)
        : handle_(handle)
    {
    }

    ~SafeGenerator()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    SafeGenerator(const SafeGenerator&) = delete;
    SafeGenerator& operator=(const SafeGenerator&) = delete;

    SafeGenerator(SafeGenerator&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    SafeGenerator& operator=(SafeGenerator&& other) noexcept
    {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

private:
    std::coroutine_handle<promise_type> handle_;
};
```

`unhandled_exception()` now captures the exception into a `std::exception_ptr`. After `resume()`, `next()` checks `exception`—if there is one, it rethrows it via `std::rethrow_exception`. External code can now handle exceptions from the coroutine with `try/catch`:

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

SafeGenerator<int> risky_range(int max)
{
    for (int i = 0; i < max; ++i) {
        if (i == 7) {
            throw std::runtime_error("7 是不吉利的数字!");
        }
        co_yield i;
    }
}

int main()
{
    auto gen = risky_range(15);

    try {
        while (gen.next()) {
            std::cout << "  值: " << gen.value() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "  捕获异常: " << e.what() << "\n";
    }

    return 0;
}
```

Output:

```text
  值: 0
  值: 1
  值: 2
  值: 3
  值: 4
  值: 5
  值: 6
  捕获异常: 7 是不吉利的数字!
```

The exception traveled from inside the coroutine to the outside `catch` block—exactly the exception behavior of synchronous code. This is the elegance of coroutines: asynchronous code not only reads like synchronous code, even its error handling works the same way as synchronous code.

### Step 3: Supporting range-for Loops

A real generator should support range-for loops. That requires providing an iterator type plus `begin()`/`end()` methods. Let's add this to `SafeGenerator`:

```cpp
// Add inside the SafeGenerator class:

class Iterator
{
public:
    // Type aliases such as iterator_category and value_type must be provided
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    Iterator() : generator_(nullptr) {}

    explicit Iterator(SafeGenerator* gen) : generator_(gen)
    {
        // Initially, advance to the first value
        advance();
    }

    T operator*() const
    {
        return generator_->value();
    }

    Iterator& operator++()
    {
        advance();
        return *this;
    }

    void operator++(int) { advance(); }

    bool operator==(const Iterator& other) const
    {
        return generator_ == other.generator_;
    }

    bool operator!=(const Iterator& other) const
    {
        return !(*this == other);
    }

private:
    SafeGenerator* generator_;
    bool exhausted_ = false;

    void advance()
    {
        if (!generator_->next()) {
            exhausted_ = true;
            generator_ = nullptr;   // Iteration is over; become end()
        }
    }
};

Iterator begin()
{
    return Iterator(this);
}

Iterator end()
{
    return Iterator();
}
```

Now you can iterate over the generator with a range-for:

```cpp
SafeGenerator<int> squares(int n)
{
    for (int i = 1; i <= n; ++i) {
        co_yield i * i;
    }
}

int main()
{
    std::cout << "前 8 个完全平方数:\n";
    for (int val : squares(8)) {
        std::cout << "  " << val << "\n";
    }
    return 0;
}
```

Output:

```text
前 8 个完全平方数:
  1
  4
  9
  16
  25
  36
  49
  64
```

Once `for (int val : squares(8))` is expanded, it amounts to calling `begin()` to obtain the iterator, calling `operator++()` on each loop iteration (which calls `next()` internally), fetching values with `operator*()`, and repeating until `operator==()` returns `true` (the coroutine has finished and the iterator has become end). The whole thing reads exactly like iterating over a `std::vector`, yet underneath it is a lazily evaluating coroutine.

> ⚠️ This iterator is **single-pass** (an input iterator)—once you have iterated through it, there is no going back, because a `coroutine_handle` can only move forward, never backward. If you need to iterate multiple times, you have to create a new generator. It also means `Iterator` does not meet the requirements of a ForwardIterator—do not run multi-pass operations such as `std::sort` on it.

## Where We Are

In this article we took C++20 coroutines apart down to their internals, more or less completely. The three keywords—`co_await` suspends and waits, `co_yield` produces a value and suspends, `co_return` returns and finishes—tell the compiler that a function is a coroutine and trigger a whole series of transforms. The compiler turns the coroutine function into a state machine: it allocates a coroutine frame storing the parameters, the local variables, and the promise object; every `co_await` is a state transition point; when suspending, the coroutine saves its current state, and when resuming, it jumps to the matching position and continues. `coroutine_handle` is the non-owning handle to the coroutine frame, providing the `resume()`, `destroy()`, and `done()` operations—it takes no responsibility for the frame's life and death, so you must wrap it in RAII. `final_suspend()` should return `suspend_always`, so that after the coroutine finishes it stays suspended and external code can safely detect `done()` and call `destroy()`. We also built a complete generator from scratch, step by step adding exception handling and range-for support—an implementation that exercises the full cooperation of promise_type, coroutine_handle, and co_yield.

So far, however, every awaitable we have used was either the standard library's `std::suspend_always` and `std::suspend_never`, or a simple struct we wrote ourselves. Real asynchronous programming needs far more flexible awaitables—waiting for an I/O operation to complete, for a timer to expire, or for another coroutine's result. That is where the customization machinery of awaitable/awaiter comes in: the semantics and return types of the three methods `await_ready()`, `await_suspend()`, and `await_resume()`, and how `await_suspend` behaves differently when it returns `bool`, `void`, or `coroutine_handle`. We will unpack all of that in the next article—it is the crucial step that takes coroutines from "understanding the machinery" to "actually using it".

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Coroutine support library — cppreference](https://en.cppreference.com/w/cpp/coroutine)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine Theory — Lewis Baker](https://lewissbaker.github.io/2017/09/25/coroutine-theory)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
- [C++20's Coroutines for Beginners — Andreas Fertig (CppCon 2022)](https://www.youtube.com/watch?v=8sEe-4tig_A)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
