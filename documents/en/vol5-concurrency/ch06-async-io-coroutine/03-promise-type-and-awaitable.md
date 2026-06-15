---
chapter: 6
cpp_standard:
- 20
description: Master the two major customization extension points of C++20 coroutines—`promise_type`
  controls coroutine behavior, while awaitable controls suspension and resumption.
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
  engine: anthropic
  source: documents/vol5-concurrency/ch06-async-io-coroutine/03-promise-type-and-awaitable.md
  source_hash: 7e640814d8bf6b9d34b83e4824465ebecdf8cc0017be5b5da2fed0cf4bc85799
  token_count: 5943
  translated_at: '2026-05-20T04:46:59.715535+00:00'
---
# promise_type and awaitable

In the previous article, we saw the basic syntax of C++20 coroutines—what `co_await`, `co_yield`, and `co_return` look like, and what kind of state machine the compiler generates for us. But honestly, just knowing how to use those keywords barely scratches the surface. The real power of C++20 coroutines—or rather, the real "pitfalls"—lies in the fact that it delegates almost all behavioral decisions to two customization points: `promise_type` and `awaitable` (more precisely, the awaiter). This turns coroutines into a "framework" rather than a "feature": the language standard only specifies when the compiler calls which methods, and how those methods are implemented is entirely up to you.

The benefit of this design philosophy is extreme flexibility—you can use coroutines to implement generators, async tasks, lazy evaluation, cooperative scheduling, or even state machines. The downside is that the C++20 standard library provides almost no ready-made coroutine types (`std::generator` won't arrive until C++23), so you have to build the entire infrastructure from scratch. In this article and the next, we will thoroughly break down these two customization points so that, by the time you finish reading, you can write a usable coroutine framework yourself.

## Environment Setup

All code in this article has been tested in the following environment:

- **Operating System**: Linux (WSL2, kernel 6.6+)
- **Compiler**: GCC 13+ or Clang 17+ (both have fairly complete support for C++20 coroutines)
- **Compiler flags**: `-std=c++20 -fcoroutines` (GCC might require `-fcoroutines`, Clang usually supports it by default)
- **Platform**: All content in this article is platform-agnostic pure C++20, with no OS-specific APIs involved (we will introduce epoll in the next article)

## The Full Picture of promise_type

If you read the previous article, you should remember: whenever the compiler encounters a function containing `co_await`, `co_yield`, or `co_return`, it transforms that function into a coroutine. And the "behavior" of the coroutine—how its return value is constructed, whether it suspends at startup, what happens when it finishes—is entirely controlled by a nested type called `promise_type`.

This `promise_type` isn't anything mysterious; it's simply a nested class of the coroutine's return type (or a type specified via `std::coroutine_traits`). The compiler constructs a `promise_type` object for you inside the coroutine's "coroutine frame," and then calls methods on this object at various nodes in the coroutine's lifecycle.

What we are going to do now is walk through the coroutine's lifecycle and break down every hook in `promise_type`.

### Lifecycle Overview

From the moment a coroutine is called to its final destruction, it roughly goes through several stages. First, the compiler allocates a block of memory on the heap to store the coroutine state—local variables, suspension points, the promise object, and so on—this is the so-called "coroutine frame." You can customize the allocation strategy through `promise_type`'s `operator new`, but in most cases, the default heap allocation is sufficient. After the coroutine frame is allocated, the compiler constructs a `promise_type` instance inside it, immediately followed by a call to `get_return_object()`. The return value of this method is the object that the coroutine function returns to the caller—it typically grabs the coroutine's handle and wraps it inside the return type.

Next, before the coroutine body executes its first statement, it first calls `initial_suspend()`, which returns an awaitable that decides whether the coroutine "starts executing immediately" or "suspends first." After that comes the time when your code actually runs, during which `co_await` (suspend), `co_yield` (yield a value and suspend), or `co_return` (return and finish) may occur. When `co_return` executes, it triggers either `return_value()` or `return_void()`—if there is a return value, the former is called; if not, the latter. After the coroutine body finishes executing (or exits via exception), `final_suspend() noexcept` is called, which decides whether the coroutine suspends when it ends. If `final_suspend` returns `suspend_never`, the coroutine frame is automatically destroyed; if it returns `suspend_always`, the coroutine frame remains suspended until someone manually calls `handle.destroy()`. If an uncaught exception is thrown during execution, `unhandled_exception()` is called, and then execution jumps directly to `final_suspend`.

Below is the simplest `promise_type` implementation, containing all the necessary hooks:

```cpp
#include <coroutine>
#include <cstdio>

/// 一个最简单的协程返回类型——不做任何有用的事情，
/// 只是完整展示 promise_type 的所有钩子
struct SimpleTask {
    struct promise_type {
        // ① 构造返回给调用者的对象
        SimpleTask get_return_object()
        {
            // 把协程 handle 包装进返回对象
            return SimpleTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        // ② 协程启动前——这里选择不挂起，立即开始执行
        std::suspend_never initial_suspend() { return {}; }

        // ③ 协程结束时——挂起来，防止协程帧被自动销毁
        //    注意：noexcept 是必须的
        std::suspend_always final_suspend() noexcept { return {}; }

        // ④ co_return 没有返回值时调用
        void return_void() {}

        // ⑤ 异常处理
        void unhandled_exception()
        {
            // 最简单的做法：直接 rethrow
            // 也可以把异常存起来，等后面再抛
            throw;
        }
    };

    // 协程句柄——持有对协程帧的引用
    std::coroutine_handle<promise_type> handle;
};

// 一个使用 SimpleTask 的协程函数
SimpleTask hello_coroutine()
{
    std::puts("你好，协程世界！");
    co_return; // 触发 return_void()
}

int main()
{
    auto task = hello_coroutine();   // 此时协程已经执行完毕（因为 initial_suspend 返回 suspend_never）
    task.handle.destroy();           // 必须手动销毁（因为 final_suspend 返回 suspend_always）
    return 0;
}
```

You'll notice that although this example is simple, it already covers the core responsibilities of `promise_type`. Next, we will expand on each hook one by one.

### get_return_object(): Creating the Return Object

This hook is called immediately after the coroutine frame is allocated and the promise object is constructed. Its return value is the object that the coroutine function returns to the caller. There is a key detail here: when `get_return_object()` executes, the coroutine body has not yet started executing, but the coroutine frame already exists. So you can grab the coroutine's handle via `std::coroutine_handle<promise_type>::from_promise(*this)` and stuff it into the return object, allowing the caller to control the coroutine's execution through this handle.

This design is essentially a "handshake" between the coroutine and the caller: the coroutine says, "I'm ready, here is my handle," and after the caller gets the handle, they can choose to immediately `resume()` it, or save it for later resumption.

### initial_suspend(): Startup Suspension Decision

This hook decides whether the coroutine suspends before executing its first statement. It returns an awaitable object, and there are usually only two choices: `std::suspend_never` (don't suspend, execute the coroutine body immediately) and `std::suspend_always` (suspend, wait for the caller to manually `resume()`).

When should you use `suspend_never`, and when should you use `suspend_always`? This depends on your use case. If you want the coroutine to "fire and forget," use `suspend_never`. If you want the coroutine to use "lazy evaluation," where the caller needs to explicitly start it, use `suspend_always`. The latter is very common when implementing generators—you create a generator, and the coroutine body doesn't start executing until you call `begin()` or `next()` for the first time.

### final_suspend() noexcept: The Critical Decision at the End

This hook is probably the most error-prone part of the entire `promise_type`.

`final_suspend` is called after the coroutine body finishes executing (either returning normally through `co_return`, or after `unhandled_exception` handles an exception). It also returns an awaitable, deciding whether the coroutine suspends at the end.

The key question is: why do most implementations choose to return `suspend_always`?

> ⚠️ **If you return `suspend_never`, the coroutine frame will be destroyed immediately after `final_suspend` returns. This means any operations on the coroutine handle at this point are dangling—your program could crash at any time.**
>
> Returning `suspend_always` lets the coroutine suspend in its final state, keeping the coroutine frame valid. The caller can safely inspect the coroutine state, retrieve the return value, and then manually call `handle.destroy()` to clean up. This is a safer "manual lifecycle management" pattern.

Additionally, `noexcept` is not optional—the standard mandates that `final_suspend` must be `noexcept`. The reason is straightforward: if the awaitable operations of `final_suspend` threw an exception, the coroutine has already finished executing, so who would you throw the exception to? There is no reasonable receiver, so the standard simply forbids this possibility at compile time.

### return_value() / return_void(): Handling co_return

When the coroutine executes `co_return expr;`, `promise_type::return_value(expr)` is called. If `co_return;` has no return value (or there is an implicit `co_return` at the end of the coroutine body), `promise_type::return_void()` is called.

Note that the choice between `return_value` and `return_void` depends on your coroutine design: if your coroutine always returns a value via `co_return expr;`, define `return_value()`; if your coroutine exits via `co_return;` (or reaches the end of the function with an implicit return), define `return_void()`. Technically you can define both—`co_return;` will call `return_void()`, and `co_return expr;` will call `return_value(expr)`—but in practice, a well-designed coroutine type usually only uses one of them, to avoid confusing the caller.

A typical `return_value` implementation stores the value in the promise object, to be retrieved later via the handle:

```cpp
struct TaskWithValue {
    struct promise_type {
        int kResultValue; // 存储返回值

        TaskWithValue get_return_object()
        {
            return TaskWithValue{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_return value; 时调用
        void return_value(int value) { kResultValue = value; }

        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    int get_result() const { return handle.promise().kResultValue; }
};

TaskWithValue compute_something()
{
    co_return 42;
}
```

### yield_value(): Handling co_yield

`co_yield expr;` is actually equivalent to `co_await promise.yield_value(expr);`. In other words, the return value of `yield_value` must be an awaitable. The most common approach is to return `std::suspend_always`, meaning the coroutine suspends after each yield, handing control back to the caller.

`yield_value` is the core when implementing generators. Each time the caller fetches a value from the generator, the generator executes to the next `co_yield`, yields the value, suspends, and waits for the next fetch.

```cpp
#include <coroutine>
#include <cstdio>

// 一个简单的整数生成器
struct IntGenerator {
    struct promise_type {
        int kCurrentValue; // 当前产出的值

        IntGenerator get_return_object()
        {
            return IntGenerator{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield value; → 产出值并挂起
        std::suspend_always yield_value(int value)
        {
            kCurrentValue = value;
            return {}; // 返回 suspend_always，挂起协程
        }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    // 获取当前值
    int current_value() const { return handle.promise().kCurrentValue; }

    // 推进到下一个值，返回 false 表示生成器结束
    bool next()
    {
        handle.resume();
        return !handle.done();
    }

    ~IntGenerator()
    {
        if (handle) {
            handle.destroy();
        }
    }
};

// 使用生成器产出斐波那契数列
IntGenerator fibonacci()
{
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        int kTemp = a + b;
        a = b;
        b = kTemp;
    }
}

int main()
{
    auto gen = fibonacci();
    for (int i = 0; i < 10 && gen.next(); ++i) {
        std::printf("%d ", gen.current_value());
    }
    std::puts("");
    // 输出: 0 1 1 2 3 5 8 13 21 34
    return 0;
}
```

Although this generator is simple, it demonstrates the core usage of `yield_value`: each `co_yield` yields a value and suspends, and the caller advances to the next value via `resume()`. This is the mechanism behind Python's `yield` keyword, except that in C++ you need to build the framework yourself.

### unhandled_exception(): The Last Line of Defense for Exceptions

If an exception is thrown inside the coroutine body and is not caught, `unhandled_exception()` will be called. You can do a few things in this hook:

The simplest approach is to do nothing (the implicit call of `std::terminate()`), or directly `throw;` to rethrow the exception to the caller. But both approaches are rather crude. A more refined approach is to store the `std::current_exception()` in the promise object, and `std::rethrow_exception()` it later when the caller retrieves the result via the handle. This way, exception propagation becomes "on-demand" rather than "immediately blowing up."

```cpp
struct SafeTask {
    struct promise_type {
        std::exception_ptr kException;

        SafeTask get_return_object()
        {
            return SafeTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}

        void unhandled_exception()
        {
            // 捕获异常，存起来稍后处理
            kException = std::current_exception();
        }
    };

    std::coroutine_handle<promise_type> handle;

    void rethrow_if_failed()
    {
        if (handle.promise().kException) {
            std::rethrow_exception(handle.promise().kException);
        }
    }

    ~SafeTask()
    {
        if (handle) {
            handle.destroy();
        }
    }
};
```

Great, at this point we have gone through all the main hooks of `promise_type`. Looking back now, you'll find that `promise_type` is essentially a "coroutine behavior controller": it controls how the coroutine starts, how it ends, and how return values and exceptions are handled. The `co_await` inside the coroutine body—that is, "suspension and resumption"—is controlled by another mechanism, which is the awaiter/awaitable protocol we will discuss next.

## The awaiter/awaitable Protocol

If `promise_type` controls the "macro lifecycle" of the coroutine, then awaiter/awaitable controls the "micro suspension and resumption." Every time you write `co_await expr;` in a coroutine, the compiler executes a fixed protocol on `expr`: first asking "are you ready," then asking "what to do after suspension," and finally asking "what result to give me after resumption."

### The Expansion Process of co_await expr

Let's look step by step at what the compiler actually does when processing `co_await expr;`.

First, the compiler needs to obtain an awaiter object from `expr`, and this process happens in two steps.

The first step is to get the awaitable. If `promise_type` defines a `await_transform` member function, the compiler will first call `promise.await_transform(expr)` to get an intermediate result, and this intermediate result is the awaitable. If there is no `await_transform`, then the original `expr` itself is the awaitable. (Note that expressions produced by `initial_suspend`, `final_suspend`, and `yield_value` skip `await_transform` and are used directly as the awaitable.)

The second step is to get the awaiter from the awaitable. The compiler performs overload resolution on `operator co_await`, with the member function `awaitable.operator co_await()` and the non-member function `operator co_await(awaitable)` participating as candidates together—it's not a sequential lookup of "find the member first, then ADL," but a single unified overload resolution. If there is exactly one best match, its return value is used as the awaiter; if `operator co_await` cannot be found at all, then the awaitable itself is treated as the awaiter—provided it has the three methods `await_ready`, `await_suspend`, and `await_resume`; if overload resolution is ambiguous (for example, both the member and non-member can match), the program is ill-formed, and the compiler will report an error.

After obtaining the awaiter, the compiler executes the following steps:

```cpp
if (!awaiter.await_ready()) {
    // 情况 A：需要挂起
    // 保存当前协程状态，然后：
    awaiter.await_suspend(handle);
    // 此时协程已经挂起，控制权返回给调用者/恢复者
}
// 情况 B：不需要挂起（await_ready 返回 true），或恢复时：
auto result = awaiter.await_resume();
// result 就是 co_await 表达式的值
```

You'll notice that these three methods form a precise "query-suspend-resume" protocol:

**`await_ready()`** returns `bool`. If it returns `true`, it means "no need to suspend, I'm already ready," and it jumps directly to `await_resume()`. If it returns `false`, it means "I'm not ready yet, I need to suspend." This method is a fast-path optimization—if you know the operation is already complete (for example, a cached result), simply returning `true` avoids the overhead of suspension and resumption.

**`await_suspend(handle)`** is called after it is confirmed that suspension is needed, receiving the current coroutine's `std::coroutine_handle`. This is the most flexible part of the entire protocol—it has three legal return types. When returning `void`, the coroutine unconditionally suspends, and control returns to the caller or resumer; when returning `bool`, `true` means suspend, and `false` means don't suspend (resume directly), giving you a chance to change your mind at the last minute; when returning `std::coroutine_handle<>`, this is the so-called symmetric transfer—after the coroutine suspends, it doesn't return to the caller, but instead immediately resumes the coroutine corresponding to the returned handle. This mechanism is very important in chained coroutine calls, as it can prevent stack overflow. We will dedicate a small section later to break down these three forms.

There is another easily overlooked point: if `await_suspend` throws an exception, the coroutine is automatically resumed, and the exception is immediately rethrown into the coroutine body. That is to say, the exception doesn't escape to the caller, but stays inside the coroutine—you can catch it in the coroutine body with `try/catch`, or let it bubble up to `unhandled_exception()`.

> ⚠️ **The bool semantics of `await_ready()` and `await_suspend()` are inverted!** `await_ready()` returning `true` means "don't suspend," while `await_suspend()` returning `true` means "suspend." This design trips up many people when they first encounter it. You can remember it this way: `await_ready` asks "are you ready," and if you're ready, of course you don't need to suspend; `await_suspend` asks "do you want to suspend," and `true` means "go ahead and suspend."

**`await_resume()`** is called when the coroutine resumes execution (or immediately when `await_ready()` returns `true`). Its return value becomes the value of the entire `co_await` expression. If you don't need to return any value, simply returning `void` is fine.

### An Async Timer Awaiter

After all this theory, let's use a concrete example to tie these concepts together. We are going to implement a `SleepAwaiter`—an awaiter that makes a coroutine "sleep" for a specified number of milliseconds.

Of course, real async sleep requires the cooperation of an event loop and timers, but here we will first use a simplified synchronous version to demonstrate the complete structure of an awaiter:

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>

/// 异步休眠 awaiter（同步阻塞版本，仅作演示）
struct SleepAwaiter {
    int kMilliSeconds; // 休眠时长

    explicit SleepAwaiter(int ms) : kMilliSeconds(ms) {}

    // ① 永远需要挂起——因为我们确实需要等待
    bool await_ready() const noexcept { return false; }

    // ② 挂起时执行休眠
    //    返回 void = 无条件挂起，控制权返回调用者
    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        // 在实际的事件循环里，这里应该是"注册定时器，把 handle 存起来"
        // 这里简化为直接 sleep，然后恢复
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kMilliSeconds)
        );
        // 休眠结束后立刻恢复协程
        handle.resume();
    }

    // ③ 恢复时不返回任何值
    void await_resume() const noexcept {}
};

/// 期望让 co_await 可以直接接受一个整数（毫秒）
/// 看起来很直觉——但这个写法实际上有问题，见下文分析
SleepAwaiter operator co_await(int ms)
{
    return SleepAwaiter(ms);
}
```

Wait, there is a problem with the approach above: `operator co_await(int ms)` is a free function, and ADL needs to consider namespaces when looking it up. For the built-in type `int`, ADL doesn't work—`int` has no associated namespaces. So a more correct approach is to intercept it via `promise_type`'s `await_transform`:

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>

/// 异步休眠 awaiter
struct SleepAwaiter {
    int kMilliSeconds;

    explicit SleepAwaiter(int ms) : kMilliSeconds(ms) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kMilliSeconds)
        );
        handle.resume();
    }

    void await_resume() const noexcept {}
};

/// 协程任务类型
struct TimerTask {
    struct promise_type {
        TimerTask get_return_object()
        {
            return TimerTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }

        // ④ await_transform：拦截 co_await 表达式
        //    当你写 co_await 100; 时，编译器会调用这个方法
        SleepAwaiter await_transform(int ms)
        {
            return SleepAwaiter(ms);
        }
    };

    std::coroutine_handle<promise_type> handle;

    ~TimerTask()
    {
        if (handle) {
            handle.destroy();
        }
    }
};

// 使用示例
TimerTask countdown()
{
    for (int i = 5; i > 0; --i) {
        std::printf("倒计时: %d\n", i);
        co_await 1000; // 等待 1 秒（通过 await_transform 转换为 SleepAwaiter）
    }
    std::puts("发射！");
}

int main()
{
    auto task = countdown(); // 协程立即开始执行（initial_suspend 返回 suspend_never）
    // 协程已经执行完毕，因为 SleepAwaiter 在 await_suspend 中同步恢复了自己
    return 0;
}
```

In this example, `await_transform` plays the role of a "middleman"—it converts `int` into `SleepAwaiter`. This pattern is very common in real projects: you can perform type checking, logging, cancellation checks, and so on inside `await_transform`.

### The Three Return Forms of await_suspend

Now the question arises: why does `await_suspend` need three return forms? Doesn't this just make things more complicated?

Actually, each form has its own use case. Let's break them down one by one.

**Returning `void`** is the simplest—the coroutine suspends, and control returns to the caller or the initiator of the most recent `resume()`. This is suitable for scenarios where "leaving the suspension entirely to external management," such as storing the handle in a queue for an event loop to resume later.

**Returning `bool`** gives you a chance to make a final decision between suspending and not suspending. For example, you might check and find that the I/O operation is actually already complete, so you return `false` to let the coroutine continue executing, avoiding the unnecessary overhead of suspension and resumption.

**Returning `std::coroutine_handle<>`** is the most powerful but also the most error-prone form. This is the so-called symmetric transfer. When your `await_suspend` returns a handle, the compiler suspends the current coroutine and **immediately** resumes the coroutine corresponding to the returned handle—it does not return to the caller. The standard's design intent is to allow the compiler to perform tail call optimization, thereby not increasing the call stack depth—mainstream compilers (GCC, Clang, MSVC) do indeed do this at higher optimization levels. But strictly speaking, tail call optimization is a "quality of implementation" rather than a "standard guarantee": both GCC and Clang have had bugs where symmetric transfer still led to stack overflow (GCC #100897, LLVM #42853). In practice, this mechanism can reliably prevent stack overflow, but don't rely on it at `-O0`.

Let's look at an example demonstrating symmetric transfer:

```cpp
#include <coroutine>
#include <cstdio>

/// 一个简单的任务类型，支持链式执行
struct ChainTask {
    struct promise_type {
        // 存储调用者协程的 handle，等自己结束后恢复它
        std::coroutine_handle<> kCaller;

        ChainTask get_return_object()
        {
            return ChainTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }

        // 结束时通过 symmetric transfer 恢复调用者
        std::coroutine_handle<> final_suspend() noexcept
        {
            return kCaller; // 如果 kCaller 为空，行为是未定义的
        }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    /// 当 co_await 一个 ChainTask 时，挂起当前协程，启动被 await 的协程
    bool await_ready() noexcept { return false; }

    // symmetric transfer：挂起自己，恢复对方
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> caller) noexcept
    {
        // 存住调用者，等自己结束后恢复它
        handle.promise().kCaller = caller;
        // 返回自己的 handle——调度器会直接恢复这个协程
        return handle;
    }

    void await_resume() noexcept {}
};
```

> ⚠️ **Symmetric transfer is a key mechanism for avoiding coroutine stack overflow.** If your coroutine A calls coroutine B, B calls C, C calls D... and each layer is "suspend A → resume B → suspend B → resume C," the call stack will grow deeper and deeper without symmetric transfer. Symmetric transfer gives the compiler the opportunity to avoid stack growth through tail call optimization—this is crucial in scenarios with long coroutine chains (such as deep recursive coroutine chains). Note that tail call optimization is a "quality of implementation" rather than a "standard guarantee," and stack overflow can still occur at low optimization levels.

## operator co_await and ADL

Earlier we discussed how the compiler obtains the awaiter from the awaitable through overload resolution of `operator co_await`. Here is a problem often encountered in real-world engineering: if you are dealing with a type from a third-party library and you can't modify its source code, how do you add `operator co_await` to it?

The answer is to leverage ADL (Argument-Dependent Lookup). When overload resolution searches for candidate functions of `operator co_await`, in addition to looking for member functions in the scope of the awaitable's class, it also searches for free functions in the associated namespaces of the awaitable's type via ADL. This gives us a backdoor to extend a type's await capability without modifying the original type. Let's look at a concrete example:

```cpp
namespace third_party {
    // 你无法修改的第三方类型
    struct Future {
        // ... 内部实现
    };
}

// 在 third_party 命名空间里添加 operator co_await
// ADL 会找到这个重载
namespace third_party {
    struct FutureAwaiter {
        third_party::Future& kFuture;

        bool await_ready();
        void await_suspend(std::coroutine_handle<> handle);
        int await_resume();
    };

    FutureAwaiter operator co_await(third_party::Future& f)
    {
        return FutureAwaiter{f};
    }
}

// 现在你可以这样写：
third_party::Future fut;
auto result = co_await fut; // ADL 找到 operator co_await
```

This is the power of ADL—you don't need to modify the original type; you just need to provide a free function `operator co_await` overload in its namespace. Of course, if you can modify the type itself, adding a member `operator co_await()` directly is simpler. But note one thing: if both a member and a non-member `operator co_await` exist for the same type and both can match, overload resolution will be ambiguous, and the compiler will report an error directly. So don't provide both forms for the same type.

## From awaitable to Scheduler

So far, all of our awaiters have been doing "immediate" things inside `await_suspend`—either blocking synchronously or resuming immediately. But in a real async framework, what `await_suspend` does is usually submit the coroutine handle to some scheduler (event loop, thread pool, etc.), and then let the scheduler resume the coroutine at the appropriate time.

This is the bridge between awaitable and the scheduler: **`await_suspend` is the key integration point for schedulers**. When a coroutine suspends, `await_suspend` gets the coroutine's handle, and it can store this handle anywhere—a queue, a timer list, a data field of an epoll event—and then let the scheduler `resume()` it later.

Next, let's look at a minimal scheduler framework that demonstrates how a complete "coroutine + scheduler" works.

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <deque>
#include <functional>

/// 最小调度器——维护一个就绪队列，循环执行
class Scheduler {
public:
    static Scheduler& instance()
    {
        static Scheduler kScheduler;
        return kScheduler;
    }

    /// 把协程 handle 放入就绪队列
    void schedule(std::coroutine_handle<> handle)
    {
        kReadyQueue.push_back(handle);
    }

    /// 运行调度循环，直到队列为空
    void run()
    {
        while (!kReadyQueue.empty()) {
            auto handle = kReadyQueue.front();
            kReadyQueue.pop_front();
            handle.resume();
        }
    }

private:
    std::deque<std::coroutine_handle<>> kReadyQueue;
};

/// 调度器友好的任务类型
struct ScheduledTask {
    struct promise_type {
        ScheduledTask get_return_object()
        {
            return ScheduledTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        // 惰性启动：协程创建时不执行，等调度器来调度
        std::suspend_always initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;
};

/// 让出一个时间片——挂起自己，把自己重新放回就绪队列
struct YieldAwaiter {
    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // 核心：把当前协程放回就绪队列，让其他协程先跑
        Scheduler::instance().schedule(handle);
    }

    void await_resume() noexcept {}
};

/// 异步休眠——挂起自己，设定时间后重新入队
/// （这里用 sleep 模拟定时器，实际应该用 epoll + timerfd）
struct AsyncSleepAwaiter {
    int kMilliSeconds;

    explicit AsyncSleepAwaiter(int ms) : kMilliSeconds(ms) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // 在真实调度器里，这里应该注册一个定时器
        // 简化版本：开一个线程来模拟异步定时器
        std::thread([handle, this]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kMilliSeconds)
            );
            // 定时器到期后，把协程放回就绪队列
            Scheduler::instance().schedule(handle);
        }).detach();
    }

    void await_resume() noexcept {}
};

/// 协程函数——交替执行
ScheduledTask producer()
{
    for (int i = 0; i < 3; ++i) {
        std::printf("  [producer] 生产第 %d 个消息\n", i + 1);
        co_await YieldAwaiter{}; // 让出执行权
    }
    std::puts("  [producer] 完成！");
}

ScheduledTask consumer()
{
    for (int i = 0; i < 3; ++i) {
        std::printf("  [consumer] 消费第 %d 个消息\n", i + 1);
        co_await YieldAwaiter{}; // 让出执行权
    }
    std::puts("  [consumer] 完成！");
}

int main()
{
    auto& sched = Scheduler::instance();

    // 创建两个协程（此时都不会执行，因为 initial_suspend 返回 suspend_always）
    auto prod = producer();
    auto cons = consumer();

    // 把它们都放进就绪队列
    sched.schedule(prod.handle);
    sched.schedule(cons.handle);

    std::puts("=== 调度器开始运行 ===");

    // 启动调度循环
    // 两个协程会交替执行：
    // [producer] 生产第 1 个消息 → yield
    // [consumer] 消费第 1 个消息 → yield
    // [producer] 生产第 2 个消息 → yield
    // [consumer] 消费第 2 个消息 → yield
    // [producer] 生产第 3 个消息 → yield
    // [consumer] 消费第 3 个消息 → yield
    // [producer] 完成！
    // [consumer] 完成！
    sched.run();

    std::puts("=== 调度器运行结束 ===");

    // 清理
    prod.handle.destroy();
    cons.handle.destroy();

    return 0;
}
```

Although this scheduler is rudimentary, it demonstrates the core model of coroutine scheduling. `YieldAwaiter` shows the most basic cooperative scheduling: the coroutine voluntarily yields execution, puts itself back in the ready queue, and lets other coroutines run. `AsyncSleepAwaiter` shows the basic pattern of an async timer: suspend the coroutine, set a timer (simulated here with a thread), and when the timer expires, put the coroutine back in the ready queue.

The real challenges come later—when we need to combine this scheduler with I/O multiplexing (epoll), things will become more complex, but the basic model remains unchanged: **the awaiter's `await_suspend` is responsible for submitting the coroutine handle to the scheduler, and the scheduler `resume()` the coroutine at the appropriate time**.

## Where We Are

In this article, we broke down the two major customization extension points of C++20 coroutines. `promise_type` controls the macro lifecycle of the coroutine—how to create the return object, whether to suspend at startup, what to do at the end, and how to handle return values and exceptions. The awaiter/awaitable protocol controls the micro suspension and resumption of the coroutine—`await_ready` asks "are you ready," `await_suspend` performs operations upon suspension, and `await_resume` retrieves the result upon resumption. The three return forms of `await_suspend` (void / bool / coroutine_handle) provide progressive flexibility from simple suspension to symmetric transfer. Finally, we saw that `await_suspend` is the key integration point for schedulers—it submits the coroutine handle to the scheduler, letting the scheduler decide when to resume the coroutine.

But so far, our scheduler is still just a toy with a "ready queue + sequential execution." Real async I/O needs to connect with the OS's I/O multiplexing mechanisms. What we are going to do in the next article is: combine coroutines with epoll (Linux's I/O multiplexing) to build an event loop capable of handling real network I/O. That is where coroutines truly shine.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — The authoritative reference for C++20 coroutines, containing the complete language specification
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — A hands-on article on building a coroutine async framework from scratch
- [My Tutorial and Take on C++20 Coroutines — David Mazieres (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html) — A coroutine tutorial by a Stanford professor, with deep and practical explanations
- [C++ Coroutines: Defining the co_await operator — Raymond Chen (Microsoft)](https://devblogs.microsoft.com/oldnewthing/20191218-00/?p=103221) — Explains the member function and free function overloading of `operator co_await`, and the overload resolution rules
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/) — A practical guide, including a reminder about the bool semantic differences between `await_ready` and `await_suspend`
- [C++20 Coroutines — Complete Guide — Simon Toth (ITNEXT)](https://itnext.io/c-20-coroutines-complete-guide-7c3fc08db89d) — A comprehensive guide covering the entire coroutine mechanism
