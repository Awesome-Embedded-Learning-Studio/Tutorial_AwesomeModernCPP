---
title: C++20 Coroutine Fundamentals
description: Dive deep into C++20 coroutine syntax, state machine models, and lifecycle
  management, and understand the compiler transformations behind `co_await`, `co_yield`,
  and `co_return`.
chapter: 6
order: 2
tags:
- host
- cpp-modern
- intermediate
- coroutine
- 异步编程
difficulty: intermediate
platform: host
reading_time_minutes: 30
cpp_standard:
- 20
prerequisites:
- 异步编程演进：从回调地狱到协程
related:
- promise_type 与 awaitable
- 异步 I/O 与事件循环
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/02-coroutine-basics.md
  source_hash: ffe072d22553156ceee0efbae135d04cf3f717b498821b61c92c771e8d118899
  translated_at: '2026-05-20T04:44:59.439012+00:00'
  engine: anthropic
  token_count: 5417
---
# C++20 Coroutine Basics

In the previous article, we saw how coroutines make asynchronous code look like synchronous code—a linear flow, no nesting, no callback pyramid. That article focused on "why we need coroutines" and "what coroutines look like." We only showed the end result but didn't explain what actually happens behind the scenes. In this article, we will tear coroutines apart from the inside out: what transformation does the compiler apply to a coroutine function? What is stored in the coroutine frame? How does `coroutine_handle` manage the coroutine's lifecycle? The answers to these questions form the foundation for understanding C++20 coroutines.

Let's start with an honest truth: the C++20 coroutine learning curve is quite steep. It is not a feature where you "learn `co_await` and you're good to go"—you need to understand how `promise_type`, `coroutine_handle`, `awaitable`, and `awaiter` work together to write correct coroutine code. The good news is that the relationships between these concepts are fixed. Once you understand this model, all coroutine code is just a variation of the same pattern. Our goal in this article is to explain this model thoroughly.

## Environment

All code in this article compiles on GCC 12+, Clang 15+, and MSVC 19.34+. All three compilers provide complete C++20 coroutine support. There are no special platform dependencies—it runs on Linux, macOS, and Windows, as we only use the pure standard library. In terms of compiler flags, `-std=c++20` is required. Versions prior to GCC 12 might need an additional `-fcoroutines` flag, but GCC 12+ has it enabled by default. One thing to note upfront: this article makes extensive use of the `<coroutine>` header, which is the library support portion of C++20 coroutines, providing infrastructure like `std::coroutine_handle`, `std::suspend_always`, and `std::suspend_never`.

## Three Keywords: co_await, co_yield, co_return

C++20 introduces three keywords for coroutines. Each has its own role, but they share one common effect: the moment any of these three keywords appears inside a function body, the compiler treats that function as a coroutine. No extra declarations, attributes, or modifiers are needed—the keywords themselves are the signal.

`co_await` is the most core one. It appears where you need to "wait a moment"—suspending the current coroutine, yielding execution, and resuming once some asynchronous operation completes. The semantics of `co_await` are: treat the expression after `co_await` as an awaitable, use it to determine whether suspension is needed, how to suspend, and what value to return upon resumption. Let's look at the simplest example:

```cpp
#include <coroutine>
#include <iostream>

// 最简协程返回类型
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

// 一个简单的协程
SimpleTask demo_coroutine()
{
    std::cout << "第一步：协程开始执行\n";

    // co_await std::suspend_always{} 挂起协程
    co_await std::suspend_always{};

    std::cout << "第二步：协程恢复后继续执行\n";

    co_await std::suspend_always{};

    std::cout << "第三步：协程再次恢复\n";
}

int main()
{
    std::cout << "主线程: 启动协程\n";

    // 调用协程函数，返回 SimpleTask
    SimpleTask task = demo_coroutine();

    // 因为 initial_suspend 返回 suspend_never，
    // 协程会立刻执行到第一个 co_await
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

You'll notice that after `my_coroutine()` is called, it doesn't run to completion in one breath—every time it hits a `co_await`, the coroutine suspends and control returns to `main()`. When we call `handle.resume()`, the coroutine continues from where it last suspended. `std::suspend_always` is the simplest awaitable provided by the standard library; its `await_ready` always returns `false`, meaning "always suspend." Correspondingly, `std::suspend_never`'s `await_ready` always returns `true`, meaning "never suspend."

`co_yield` produces a value and suspends the coroutine. It is equivalent to `co_await promise.yield_value(value)`. `co_yield` is the foundation for building generators—each time a value is produced, the coroutine suspends, the consumer takes the value, and then the coroutine resumes. We will implement a generator from scratch later.

`co_return` ends the coroutine. It has two forms: `co_return;` (no return value) and `co_return value;` (with a return value). The former is equivalent to calling `promise.return_void()`. For the latter, if the `promise_type`'s return type is non-void, it is equivalent to `promise.return_value(value)`; if the return type is void, it also calls `promise.return_void()`. `co_return` is different from a plain `return`—a plain `return` statement cannot appear in a coroutine. A coroutine must use `co_return` to end (or let the function body end naturally, in which case the compiler implicitly inserts a `co_return` at the end).

> ⚠️ Note that `co_return` and a plain `return` cannot be mixed. If a function contains any of `co_await`, `co_yield`, or `co_return`, it is a coroutine, and a plain `return` statement inside the function body is illegal—the compiler will error out directly. Conversely, if the function body contains no coroutine keywords, even if the return type defines a `promise_type`, it is just a plain function.

## What the Compiler Does — The Coroutine State Machine

This is the core of understanding C++20 coroutines. When you write a coroutine function, the compiler doesn't simply generate a linear block of code like it does for a plain function. It **transforms the entire coroutine function into a state machine**—each `co_await` (including the initial and final suspend points) is a state, and each time the coroutine resumes, it jumps to the corresponding code position based on the current state.

Let's use a simplified example to trace this transformation. Suppose you wrote this coroutine:

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

The compiler roughly transforms it into pseudocode like this (many details simplified, but the core logic is correct):

```text
1. 分配协程帧（coroutine frame）
2. 把参数 x 拷贝到协程帧里
3. 在协程帧里构造 promise_type 对象
4. 调用 promise.get_return_object() 拿到返回值
5. co_await promise.initial_suspend()
6. 进入状态机:

   状态 0:（初始状态）
     a = x + 1
     保存当前挂起点为"状态 1"
     co_await std::suspend_always{}
     → 挂起，返回到调用者

   状态 1:（从第一个 co_await 恢复）
     b = a + 2
     保存当前挂起点为"状态 2"
     co_await std::suspend_always{}
     → 挂起，返回到调用者

   状态 2:（从第二个 co_await 恢复）
     调用 promise.return_void()
     销毁局部变量 b, a
     co_await promise.final_suspend()
     → 最终挂起
```

Let's walk through this transformation line by line.

**Step one: allocate the coroutine frame.** The coroutine frame is a block of heap memory (usually), used to store all data needed to resume the coroutine. It contains several parts: copies of function parameters (because the coroutine might outlive the caller's stack, so parameters must be copied into the frame to avoid dangling references), local variables (those whose lifetimes span a suspend point—if a local variable is created before a `co_await` and still used after it, it must live in the coroutine frame), the promise object (part of the coroutine state), and the current suspend-point index (so that resumption knows which state to jump to).

> ⚠️ Only local variables whose lifetimes span a suspend point are stored in the coroutine frame. If a local variable's lifetime ends between two suspend points, the compiler can optimize it into a register or the normal stack. This optimization is up to the compiler.

**Step two: copy parameters.** All pass-by-value parameters are moved or copied into the coroutine frame. Pass-by-reference parameters only store the reference itself—this means if you pass a reference to a local variable to a coroutine, and that local variable goes out of scope before the coroutine resumes, you get a dangling reference. This is a classic pitfall of C++20 coroutines: **capturing coroutine parameters by reference is dangerous**, because you cannot guarantee the referenced object is still alive when the coroutine resumes.

**Step three: construct the promise object.** `promise_type` is the coroutine's "introspection interface"—the compiler calls promise methods at various key points during coroutine execution. It is not an ordinary concept, but rather something the compiler deduces from the coroutine's return type via `Return_Type::promise_type`. If your return type is `MyTask`, the compiler looks for `MyTask::promise_type`.

**Step four: call `get_return_object()`.** The return value of this method is the object that the coroutine function returns to the caller (`task` in our example). This call happens before the coroutine function body starts executing—meaning by the time the caller gets the return value, the first line of the coroutine function body hasn't executed yet.

**Step five: call `initial_suspend()`.** This method determines whether the coroutine suspends before the function body starts executing. If it returns `suspend_always` (lazy start), the coroutine suspends before executing the first line of code, and the caller must manually `resume()` it to get it working. If it returns `suspend_never` (eager start), the coroutine immediately starts executing the function body until it hits the first `co_await`.

**Step six: execute the function body and handle suspend points.** The coroutine executes the function body. When it hits a `co_await`, it first calls the awaitable's `await_ready`. If it returns `true`, no suspension is needed, and execution continues directly. If it returns `false`, it saves the current state (suspend-point index, active local variables), calls `await_suspend`, and then suspends—control returns to the caller or resumer. When the coroutine is `resume()`d, it resumes from the saved suspend point, calls `await_resume` to get the return value of the `co_await` expression, and then continues executing.

**Final state: `final_suspend`.** When the coroutine reaches `co_return` (or the end of the function body), it calls `return_value` or `return_void`, destroys all active local variables, and then calls `final_suspend` and `co_await`s its result. This `final_suspend` is the coroutine's "terminal station"—if it returns `suspend_always`, the coroutine suspends at the final state, waiting for the outside world to destroy the coroutine frame via `destroy()`. If it returns `suspend_never`, the coroutine frame is automatically destroyed—but you must ensure that no one still holds a `coroutine_handle` to this coroutine at that point, otherwise it is use-after-free.

## coroutine_handle: The Handle to the Coroutine Frame

`std::coroutine_handle` (or its specialized version `std::coroutine_handle<PromiseType>`) is a non-owning handle to the coroutine frame. You can think of it as a "raw pointer"—it points to the coroutine frame but does not manage its lifetime.

The most commonly used operation is `resume()`, which resumes coroutine execution from the last suspend point. But there is a prerequisite: the coroutine must not have reached the final suspend state yet. If `final_suspend` has already returned `suspend_always`, calling `resume()` again is undefined behavior—it might happen not to crash on some compilers, but change the optimization level and you might get a segfault. `destroy()` destroys the coroutine frame: it calls the promise's destructor, then the parameters' destructors, and then frees the coroutine frame's memory. `done()` checks whether the coroutine has reached the final suspend point—that is, whether the function body has finished executing and is in the `final_suspend` state. There is also a static method `from_promise()`, which can reverse-engineer the corresponding `coroutine_handle` from a reference to the promise object. This is very commonly used inside `promise_type` methods, because you often need to get your own handle inside a promise method to pass it to the outside world.

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
        // 懒启动：协程创建后立刻挂起，不执行函数体
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;

    // RAII: 析构时自动销毁协程帧
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
        co_await std::suspend_always{};   // 每次循环后挂起
    }
    std::cout << "  countdown: 发射!\n";
}

int main()
{
    std::cout << "创建协程...\n";
    Resumable task = countdown(5);

    // 因为 initial_suspend 返回 suspend_always，
    // 协程还没开始执行

    std::cout << "开始恢复协程:\n";
    while (!task.handle.done()) {
        task.handle.resume();
        if (!task.handle.done()) {
            std::cout << "  (协程已挂起，可以干别的事)\n";
        }
    }

    std::cout << "协程已完成\n";
    // Resumable 的析构函数会调用 handle.destroy()
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

See? Each time the coroutine loops to `co_await`, it suspends and returns to `main()`. `main()` can check `done()` to determine whether the coroutine is finished, and then decide whether to continue `resume()`ing or do something else. This is the fundamental difference between coroutines and plain functions: a plain function is either executing or has already returned; a coroutine can "pause"—after suspending, it doesn't disappear, but its state is fully preserved in the coroutine frame, ready to be resumed at any time.

There is a very important detail here: `coroutine_handle` is non-owning. It does not automatically destroy the coroutine frame when it is destructed. If you obtain a `coroutine_handle` but never call `destroy()`, the coroutine frame leaks—that heap memory is never freed. So you almost always need to wrap `coroutine_handle` in a RAII class (like our `ScopedCoroutine` above), letting the destructor automatically handle cleanup.

> ⚠️ Neither `resume()` nor `destroy()` of `coroutine_handle` should be called after the coroutine has `done()`. Calling `resume()` on a completed coroutine is undefined behavior—it might "not crash" in your code, but under a different compiler or optimization level, it might segfault directly.

## Coroutine Lifecycle

A coroutine's lifecycle begins the moment it is called and ends the moment its coroutine frame is destroyed. Let's walk through this process completely.

**Creation phase.** When you call a coroutine function, the compiler-generated code first allocates the coroutine frame, then copies parameters, constructs the promise, and calls `get_return_object()`. At this point, the coroutine function body hasn't started executing yet—the caller already has the return object (which contains a `coroutine_handle`), but the coroutine's "actual execution" still depends on the result of `initial_suspend`.

**Execution phase.** If `initial_suspend` returns `suspend_never`, the coroutine immediately starts executing the function body until it hits the first real `co_await` (the one where `await_ready` returns `false`). If it returns `suspend_always`, the coroutine suspends before the function body begins, waiting for the outside world to call `resume()`. During execution, each time it hits a `co_await` and needs to suspend, the coroutine saves its current state and returns control to the caller or resumer.

**Final phase.** When the coroutine reaches `co_return` (or the end of the function body, provided the promise has `return_void`), it calls `return_value` or `return_void`, destroys local variables, and then calls `final_suspend`. This is a key design point: **`final_suspend` should return `suspend_always`**.

Why should `final_suspend` return `suspend_always`? Because if it returns `suspend_never`, the coroutine frame is automatically destroyed immediately after `final_suspend` returns—at this point, the coroutine function body has ended and local variables have been destroyed, but the outside world might still hold a `coroutine_handle`. If the outside world doesn't know the coroutine has already been destroyed and calls `resume()` or `destroy()`, that is use-after-free. Returning `suspend_always` lets the coroutine suspend at the final state. The coroutine frame is still alive, the outside world can detect completion via `done()`, and then safely call `destroy()` to destroy the coroutine frame.

> ⚠️ The dangling coroutine problem is one of the most common coroutine bugs. A typical scenario: you return an object containing a `coroutine_handle`, but the caller doesn't properly manage its lifecycle after receiving it—either forgetting to call `destroy()` causing a memory leak, or continuing to use the `coroutine_handle` after the coroutine frame has already been destroyed. The best practice is to always wrap `coroutine_handle` with RAII, and never let it leak outside the API boundary naked.

## Implementing a Generator from Scratch

Great, now we have an understanding of the basic mechanisms of coroutines. Next, we will do something very practical: implement a generator from scratch that can yield integers using `co_yield`. This implementation will involve the complete cooperation of `promise_type`, `coroutine_handle`, and `co_yield`, making it an excellent exercise for understanding C++20 coroutines.

We will build this generator in three steps. First, we set up the skeleton—letting the generator produce values with `co_yield` and letting the outside world iterate to retrieve them. Then we add exception handling—letting exceptions inside the coroutine propagate correctly to the outside. Finally, we add RAII—ensuring the coroutine frame is properly destroyed when the generator is destructed.

### Step One: Skeleton — Produce and Retrieve Values

```cpp
#include <coroutine>
#include <iostream>
#include <memory>

template<typename T>
class Generator
{
public:
    // ---- promise_type：编译器通过它定制协程行为 ----
    struct promise_type
    {
        T current_value;    // 存储 co_yield 产出的值

        Generator get_return_object()
        {
            // 从 promise 创建 coroutine_handle，包装成 Generator 返回
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // 初始挂起：协程创建后立刻挂起（懒启动）
        // 调用者需要手动 resume 才开始产出值
        std::suspend_always initial_suspend() { return {}; }

        // 最终挂起：协程结束后挂起，等待外部 destroy()
        // 不能返回 suspend_never，否则协程帧会自动销毁
        // 外部的 handle 就变成悬垂指针了
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield expr 等价于 co_await promise.yield_value(expr)
        // 我们把值存到 current_value 里，然后挂起
        std::suspend_always yield_value(T value)
        {
            current_value = value;
            return {};   // 返回 suspend_always，挂起协程
        }

        // 协程没有 co_return 或 co_return; 时调用
        void return_void() {}

        // 未处理的异常——先简单 terminate
        void unhandled_exception() { std::terminate(); }
    };

    // ---- 迭代器接口 ----

    // 恢复协程，移动到下一个 yield 点
    bool next()
    {
        handle_.resume();
        return !handle_.done();
    }

    // 获取当前 yield 的值
    T value() const
    {
        return handle_.promise().current_value;
    }

    // ---- 构造/析构/移动 ----

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

    // 禁止拷贝——coroutine_handle 不能共享所有权
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    // 允许移动
    Generator(Generator&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;    // 防止 other 析构时 destroy
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

Let's walk through the logic of this code. `promise_type` is the bridge between the compiler and the coroutine. When the compiler sees your coroutine function returning `Generator`, it looks for `Generator::promise_type`, and then calls promise methods at various key points during coroutine execution.

`get_return_object()` is called first—it creates a `coroutine_handle` and wraps it into a `Generator` to return to the caller. `initial_suspend()` returns `suspend_always`, which means the coroutine suspends before executing the function body—after the caller gets the Generator, they must call `next()` (which internally calls `resume()`) to start producing values. This is the standard design for generators: **lazy start**, because the generator's consumer might only need the first few values, and there is no need to produce all values at creation time.

`yield_value()` is the actual operation behind `co_yield`. When the coroutine executes `co_yield value`, the compiler transforms it into `co_await promise.yield_value(value)`. Our implementation stores the value in `current_value` and then returns `suspend_always`—the coroutine suspends, and control returns to the caller of `next()`. The caller reads the value via `value()`, and then calls `next()` again to continue producing the next value.

`final_suspend()` returns `suspend_always`, which we explained earlier—the coroutine remains suspended after finishing, waiting for the Generator's destructor to call `destroy()`.

The Generator itself is a RAII wrapper around `coroutine_handle`. The destructor calls `destroy()` to destroy the coroutine frame. Move construction/assignment uses a null-handle check to prevent double destruction. Copying is disabled because `coroutine_handle` does not support shared ownership.

Now let's use it to produce a Fibonacci sequence:

```cpp
Generator<int> fibonacci()
{
    int a = 0, b = 1;
    while (true) {
        co_yield a;         // 产出当前值，挂起
        int temp = a + b;
        a = b;
        b = temp;
    }
    // 这个协程永远不会 co_return——无限序列
}

int main()
{
    auto gen = fibonacci();

    std::cout << "斐波那契数列前 15 项:\n";
    for (int i = 0; i < 15 && gen.next(); ++i) {
        std::cout << "  fib(" << i << ") = " << gen.value() << "\n";
    }

    // gen 析构时自动 destroy 协程帧
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

You'll notice that the `fibonacci()` function looks just like an ordinary loop generating a Fibonacci sequence—the only difference is that `co_yield` replaces `cout <<` or `return`. But this function doesn't run to completion in one breath: each time `co_yield` produces a value, it suspends, waiting for the next `next()` call to continue the loop. This is lazy evaluation—values are produced on demand, with no need to precompute and store all results. For infinite sequences or large datasets, this property is extremely valuable.

### Step Two: Adding Exception Handling

The generator above has a problem: what if an exception is thrown inside the coroutine function body? Currently, our `unhandled_exception()` simply calls `std::terminate()`, which is too brutal. A better approach is to catch and store the exception, then rethrow it when the outside world calls `next()` or `value()`:

```cpp
template<typename T>
class SafeGenerator
{
public:
    struct promise_type
    {
        T current_value;
        std::exception_ptr exception;   // 存储异常

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

        // 捕获异常，存到 exception_ptr 里
        void unhandled_exception()
        {
            exception = std::current_exception();
        }
    };

    bool next()
    {
        handle_.resume();

        // resume 之后检查是否有异常
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

`unhandled_exception()` now catches the exception into `exception`. `next()` checks `exception` after `resume()`—if there is an exception, it rethrows it via `std::rethrow_exception()`. This way, external code can use `try/catch` to handle exceptions from inside the coroutine:

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

The exception propagated from inside the coroutine to the outside `catch` block—completely consistent with synchronous code's exception behavior. This is the elegance of coroutines: asynchronous code not only reads like synchronous code, but even error handling is the same as in synchronous code.

### Step Three: Supporting range-for Loops

A real generator should support range-for loops. This requires us to provide an iterator type and `begin()`/`end()` methods. Let's add this to `Generator`:

```cpp
// 在 SafeGenerator 类里添加：

class Iterator
{
public:
    // 必须提供 iterator_category、value_type 等类型别名
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    Iterator() : generator_(nullptr) {}

    explicit Iterator(SafeGenerator* gen) : generator_(gen)
    {
        // 初始时先前进到第一个值
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
            generator_ = nullptr;   // 迭代结束，变成 end()
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

Now you can use range-for to iterate over the generator:

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

When `range-for` is expanded, it is equivalent to calling `begin()` to get an iterator, calling `operator++` (which internally calls `next()`) each loop iteration, using `operator*` to get the value, until `operator!=` returns `false` (the coroutine is finished, and the iterator becomes `end`). The whole thing reads exactly like iterating over a `std::vector`, but underneath it is a lazily evaluated coroutine.

> ⚠️ This iterator is **single-pass** (input iterator)—you cannot go back after iterating through it once. This is because `coroutine_handle` can only move forward, not backward. If you need to iterate multiple times, you have to create a new generator. This also means the iterator does not satisfy ForwardIterator requirements—do not perform operations on it that require multiple passes, such as `std::sort`.

## Where We Are

In this article, we have mostly torn apart the internal mechanisms of C++20 coroutines. The three keywords—`co_await` to suspend and wait, `co_yield` to produce a value and suspend, `co_return` to return and finish—their appearance tells the compiler that this function is a coroutine and triggers a series of transformations. The compiler transforms the coroutine function into a state machine: it allocates a coroutine frame to store parameters, local variables, and the promise object. Each `co_await` is a state transition point. When the coroutine suspends, it saves the current state; when it resumes, it jumps to the corresponding position and continues executing. `coroutine_handle` is a non-owning handle to the coroutine frame, providing `resume()`, `destroy()`, and `done()` operations—it does not manage the coroutine frame's lifetime, so you must wrap it with RAII. `final_suspend` should return `suspend_always`, so the coroutine remains suspended after finishing, allowing the outside world to safely check `done()` and call `destroy()`. We implemented a complete generator from scratch, progressively adding exception handling and range-for support—this implementation covers the full cooperation of `promise_type`, `coroutine_handle`, and `co_yield`.

But so far, the awaitables we have used are either `std::suspend_always` and `std::suspend_never` from the standard library, or simple structs we wrote ourselves. Real asynchronous programming requires more flexible awaitables—such as waiting for an I/O operation to complete, waiting for a timer to expire, or waiting for another coroutine's result. This involves the customization mechanism for awaitables/awaiters: the semantics and return value types of the three methods `await_ready`, `await_suspend`, and `await_resume`, as well as the different behaviors when `await_suspend` returns `void`, `bool`, or a `coroutine_handle`. We will dive into these topics in the next article—that is the crucial step from "understanding the mechanism" to "practical use" of coroutines.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

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
