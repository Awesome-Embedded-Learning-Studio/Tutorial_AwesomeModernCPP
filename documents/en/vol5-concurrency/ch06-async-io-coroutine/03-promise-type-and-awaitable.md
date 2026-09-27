---
chapter: 6
cpp_standard:
- 20
description: 'Master the two customization extension points of C++20 coroutines: promise_type controls coroutine behavior, and awaitable controls suspension and resumption'
difficulty: advanced
order: 3
platform: host
prerequisites:
- C++20 Coroutine Fundamentals
reading_time_minutes: 28
related:
- Asynchronous I/O and Event Loops
- 'Coroutine Echo Server in Practice'
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
  translated_at: '2026-09-26T09:12:30+00:00'
  engine: anthropic
  token_count: 8000
---
# promise_type and awaitable

In the previous article we saw the basic syntax of C++20 coroutines—what `co_await`, `co_yield`, and `co_return` look like, and what kind of state machine the compiler generates for us. But honestly, just knowing how to use those keywords is the surface layer. The real power of C++20 coroutines—or rather, the real "trap"—is that it hands almost every behavioral decision over to two customization points: `promise_type` and `awaitable` (more precisely, the awaiter). This turns coroutines into a "framework" rather than a "feature": the language standard only specifies which methods the compiler calls and when; how those methods are implemented is entirely up to you.

The upside of this design philosophy is extreme flexibility—you can use coroutines to implement generators, asynchronous tasks, lazy evaluation, cooperative scheduling, even state machines. The downside is that the C++20 standard library provides almost no ready-made coroutine types (`std::generator` doesn't arrive until C++23), so you have no choice but to build the entire infrastructure yourself from scratch. What this article and the next one will do is take these two customization points apart thoroughly, so that by the time you finish reading, you can write a usable coroutine framework of your own.

## Environment Notes

All code in this article was tested and passes in the following environment:

- **Operating system**: Linux (WSL2, kernel 6.6+)
- **Compiler**: GCC 13+ or Clang 17+ (both already have quite complete C++20 coroutine support)
- **Compiler flags**: `-std=c++20 -fcoroutines` (GCC may need `-fcoroutines`; Clang usually supports coroutines by default)
- **Platform**: everything in this article is platform-independent pure C++20 and involves no OS-specific APIs (epoll only shows up in the next article)

## The Full Picture of promise_type

If you read the previous article, you should remember: whenever the compiler encounters a function containing `co_await`, `co_yield`, or `co_return`, it turns that function into a coroutine. And the coroutine's "behavior"—how its return value is constructed, whether it suspends at startup, what it does when it finishes—is controlled entirely by a nested type called `promise_type`.

This `promise_type` is nothing mysterious; it is simply a nested class of the coroutine's return type (or a type designated via `std::coroutine_traits`). The compiler constructs a `promise_type` object for you inside the coroutine's "coroutine frame", and then calls this object's methods at each milestone of the coroutine's lifecycle.

What we are going to do now is walk along the coroutine's lifecycle and take every hook of `promise_type` apart for a look.

### Lifecycle Overview

From being called to finally being destroyed, a coroutine goes through several broad phases. First, the compiler allocates a chunk of memory on the heap to hold the coroutine's state—local variables, suspension points, the promise object, and so on—this is the so-called "coroutine frame". You can customize the allocation strategy via `promise_type`'s `operator new`, but in most cases the default heap allocation is good enough. Once the coroutine frame is allocated, the compiler constructs a `promise_type` instance inside it, and immediately afterwards calls `get_return_object()`. The return value of that method is the object the coroutine function hands back to the caller—usually it grabs the coroutine's handle and wraps it into the return type.

Next, before the coroutine body executes its first statement, `initial_suspend()` is called. It returns an awaitable that decides whether the coroutine "starts executing immediately" or "suspends first". After that comes the stretch where your code actually runs, and along the way `co_await` (suspend), `co_yield` (produce a value and suspend), and `co_return` (return and finish) may happen. When `co_return` executes, it triggers `return_value()` or `return_void()`—the former if there is a return value, the latter if there isn't. After the coroutine body finishes executing (or exits via an exception), `final_suspend() noexcept` is called; it decides whether the coroutine suspends at the end. If `final_suspend` returns `suspend_never`, the coroutine frame is destroyed automatically; if it returns `suspend_always`, the coroutine frame stays suspended until someone manually calls `handle.destroy()`. If an uncaught exception is thrown during execution, `unhandled_exception()` is called, and then control jumps straight to `final_suspend`.

Here is the simplest possible `promise_type` implementation, containing all the required hooks:

```cpp
#include <coroutine>
#include <cstdio>

/// The simplest possible coroutine return type—it does nothing useful,
/// it just fully demonstrates every hook of promise_type
struct SimpleTask {
    struct promise_type {
        // ① Construct the object returned to the caller
        SimpleTask get_return_object()
        {
            // Wrap the coroutine handle into the return object
            return SimpleTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        // ② Before the coroutine starts—here we choose not to suspend, so execution begins immediately
        std::suspend_never initial_suspend() { return {}; }

        // ③ When the coroutine ends—suspend, to prevent the coroutine frame from being destroyed automatically
        //    Note: noexcept is mandatory
        std::suspend_always final_suspend() noexcept { return {}; }

        // ④ Called when co_return carries no return value
        void return_void() {}

        // ⑤ Exception handling
        void unhandled_exception()
        {
            // Simplest approach: rethrow directly
            // You could also store the exception and throw it later
            throw;
        }
    };

    // The coroutine handle—holds a reference to the coroutine frame
    std::coroutine_handle<promise_type> handle;
};

// A coroutine function that uses SimpleTask
SimpleTask hello_coroutine()
{
    std::puts("你好，协程世界！");
    co_return; // triggers return_void()
}

int main()
{
    auto task = hello_coroutine();   // the coroutine has already finished here (because initial_suspend returns suspend_never)
    task.handle.destroy();           // manual destruction is required (because final_suspend returns suspend_always)
    return 0;
}
```

You will notice that although this example is simple, it already covers all the core responsibilities of `promise_type`. Next, let's expand on each hook one by one.

### get_return_object(): Creating the Return Object

This hook is called immediately after the coroutine frame is allocated and the promise object constructed. Its return value is the object the coroutine function returns to the caller. There is one crucial detail here: when `get_return_object()` executes, the coroutine body has not started executing yet, but the coroutine frame already exists. So you can obtain the coroutine's handle via `std::coroutine_handle<promise_type>::from_promise(*this)`, stuff it into the return object, and the caller can then control the coroutine's execution through that handle.

This design is essentially a "handshake" between the coroutine and its caller: the coroutine says "I'm ready, here is my handle", and the caller, once it holds the handle, can either `resume()` immediately or stash it away and resume later.

### initial_suspend(): The Startup Suspension Decision

This hook decides whether the coroutine suspends before executing its first statement. It returns an awaitable object, and in practice there are just two common choices: `std::suspend_never` (don't suspend—execute the coroutine body immediately) and `std::suspend_always` (suspend and wait for the caller to `resume()` manually).

So when should you use `suspend_never`, and when `suspend_always`? It depends on your use case. If you want the coroutine to "start running the moment it is created" (fire-and-forget style), use `suspend_never`. If you want the coroutine to be lazy (lazy evaluation), started explicitly by the caller, use `suspend_always`. The latter is extremely common when implementing generators—you create a generator, and the coroutine body does not start running until you call `begin()` or `next()` for the first time.

### final_suspend() noexcept: The Critical Final Decision

This hook is probably the easiest place in the whole `promise_type` to get wrong.

`final_suspend` is called after the coroutine body finishes executing (either returning normally via `co_return`, or after `unhandled_exception` has dealt with the exception). It likewise returns an awaitable that decides whether the coroutine suspends at the end.

The key question is: why do most implementations choose to return `suspend_always`?

> ⚠️ **If you return `suspend_never`, the coroutine frame is destroyed immediately after `final_suspend` returns. This means any operation on the coroutine handle at that point is dangling—your program can crash at any moment.**
>
> Returning `suspend_always` keeps the coroutine suspended in its finished state, with the coroutine frame still valid, so the caller can safely inspect the coroutine's state, fetch the return value, and then manually call `handle.destroy()` to clean up. This is the safer "manual lifetime management" pattern.

Also, `noexcept` is not optional—the standard requires `final_suspend` to be `noexcept`. The reason is plain: if the awaitable operations of `final_suspend` threw an exception, the coroutine has already finished running—who should the exception be thrown to at that point? There is no sensible receiver, so the standard simply forbids this possibility at compile time.

### return_value() / return_void(): Handling co_return

When the coroutine executes `co_return expr;`, `promise_type::return_value(expr)` is called. If `co_return;` carries no return value (or the coroutine body ends with an implicit `co_return`), then `promise_type::return_void()` is called.

Note that choosing between `return_value` and `return_void` depends on your coroutine design: if your coroutine always returns a value via `co_return expr;`, define `return_value()`; if your coroutine exits via `co_return;` (or runs to the end of the function and returns implicitly), define `return_void()`. Technically you can define both—`co_return;` would call `return_void()`, `co_return expr;` would call `return_value(expr)`—but in practice, a well-designed coroutine type uses only one of them, to avoid confusing callers.

A typical `return_value` implementation stores the value in the promise object, to be fetched later through the handle:

```cpp
struct TaskWithValue {
    struct promise_type {
        int kResultValue; // stores the return value

        TaskWithValue get_return_object()
        {
            return TaskWithValue{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // called on co_return value;
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

`co_yield expr;` is actually equivalent to `co_await promise.yield_value(expr);`. In other words, the return value of `yield_value` must be an awaitable. The most common approach is to return `std::suspend_always`, meaning that after every yield the coroutine suspends and hands control back to the caller.

`yield_value` is at the heart of implementing generators. Each time the caller pulls a value from the generator, the generator runs to the next `co_yield`, produces the value, suspends, and waits for the next pull.

```cpp
#include <coroutine>
#include <cstdio>

// A simple integer generator
struct IntGenerator {
    struct promise_type {
        int kCurrentValue; // the current produced value

        IntGenerator get_return_object()
        {
            return IntGenerator{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield value; → produce the value and suspend
        std::suspend_always yield_value(int value)
        {
            kCurrentValue = value;
            return {}; // returns suspend_always: suspend the coroutine
        }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    // fetch the current value
    int current_value() const { return handle.promise().kCurrentValue; }

    // advance to the next value; returns false when the generator is done
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

// Use the generator to produce the Fibonacci sequence
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
    // Output: 0 1 1 2 3 5 8 13 21 34
    return 0;
}
```

Simple as it is, this generator demonstrates the core usage of `yield_value`: every `co_yield` produces a value and suspends, and the caller advances to the next value via `resume()`. This is exactly the machinery behind Python's `yield` keyword—except that in C++, you have to build the framework yourself.

### unhandled_exception(): The Last Line of Defense for Exceptions

If an exception is thrown inside the coroutine body and not caught, `unhandled_exception()` gets called. There are a few things you can do in this hook:

The simplest approach is to do nothing (the implicit call to `std::terminate()`), or just `throw;` to rethrow the exception out to the caller. But both approaches are rather blunt. A more refined approach stores `std::current_exception()` in the promise object, and calls `std::rethrow_exception()` later, when the caller fetches the result through the handle. That way exception propagation becomes "on demand" instead of "blow up immediately".

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
            // capture the exception and store it for later handling
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

Great—by this point we have walked through all the major hooks of `promise_type`. Looking back now, you will see that `promise_type` is essentially a "coroutine behavior controller": it controls how the coroutine starts, how it ends, and how return values and exceptions are handled. The `co_await` inside the coroutine body—the "suspend and resume" part—is governed by a separate mechanism, and that is the awaiter/awaitable protocol we are about to cover.

## The awaiter/awaitable Protocol

If `promise_type` controls the coroutine's "macro lifecycle", then the awaiter/awaitable controls the "micro suspend-and-resume". Every time you write `co_await expr;` inside a coroutine, the compiler runs a fixed protocol on `expr`: first it asks "are you ready yet", then "what should happen after suspending", and finally "what result do you give me upon resumption".

### How co_await expr Expands

Let's step through what exactly the compiler does when processing `co_await expr;`.

First, the compiler needs to obtain an awaiter object from `expr`, and this happens in two steps.

Step one is obtaining the awaitable. If `promise_type` defines an `await_transform` member function, the compiler first calls `promise.await_transform(expr)` to get an intermediate result, and this intermediate result is the awaitable. If there is no `await_transform`, then the original `expr` itself is the awaitable. (Note that the expressions produced by `initial_suspend`, `final_suspend`, and `yield_value` skip `await_transform` and are used directly as awaitables.)

Step two is obtaining the awaiter from the awaitable. The compiler performs overload resolution on `operator co_await`, with the member function `awaitable.operator co_await()` and the non-member function `operator co_await(awaitable)` participating as candidates together—not a sequential "look for members first, then ADL" lookup, but one unified overload resolution. If there happens to be exactly one best match, its return value is used as the awaiter; if no `operator co_await` can be found at all, then the awaitable itself is treated as the awaiter—provided it has the three methods `await_ready`, `await_suspend`, and `await_resume`; if the overload resolution is ambiguous (for example, both the member and the non-member match), the program is outright ill-formed and the compiler reports an error.

Once it has the awaiter, the compiler executes the following steps:

```cpp
if (!awaiter.await_ready()) {
    // Case A: suspension is needed
    // Save the current coroutine state, then:
    awaiter.await_suspend(handle);
    // The coroutine is now suspended; control returns to the caller/resumer
}
// Case B: no suspension needed (await_ready returned true), or upon resumption:
auto result = awaiter.await_resume();
// result is the value of the whole co_await expression
```

You will find that these three methods form a precise "query–suspend–resume" protocol:

**`await_ready()`** returns a `bool`. If it returns `true`, it means "no need to suspend, I am already ready", and execution jumps straight to `await_resume()`. If it returns `false`, it means "I'm not ready yet, we need to suspend". This method is a fast-path optimization—if you know the operation has already completed (a cached result, for example), returning `true` outright avoids the suspend/resume overhead.

**`await_suspend(handle)`** is called after suspension is confirmed to be necessary, and receives the current coroutine's `std::coroutine_handle`. This is the most flexible part of the whole protocol—it has three legal return types. When it returns `void`, the coroutine suspends unconditionally and control returns to the caller or resumer; when it returns `bool`, `true` means suspend and `false` means don't suspend (resume directly), giving you a chance to change your mind at the last moment; when it returns `std::coroutine_handle<>`, that is the so-called symmetric transfer—the coroutine suspends but does not return to the caller; instead, the coroutine corresponding to the returned handle is resumed directly. This mechanism is very important when coroutines call each other in chains, because it avoids stack overflow. Later we will devote a whole section to unpacking these three forms.

One more easily overlooked point: if `await_suspend` throws an exception, the coroutine is resumed automatically, and then the exception is immediately rethrown inside the coroutine body. In other words, the exception does not escape to the caller—it stays inside the coroutine, where you can catch it with `try/catch` in the body, or let it bubble up to `unhandled_exception()`.

> ⚠️ **The bool semantics of `await_ready()` and `await_suspend()` are inverted!** `await_ready()` returning `true` means "don't suspend"; `await_suspend()` returning `true` means "suspend". This design has tripped up many people on first contact. Here's a way to remember it: `await_ready` asks "are you ready?"—if you're ready, of course there's no need to suspend; `await_suspend` asks "should we suspend?"—and `true` means "yes, suspend".

**`await_resume()`** is called when the coroutine resumes execution (or immediately when `await_ready()` returns `true`). Its return value is the value of the entire `co_await` expression. If you don't need to return anything, just return `void`.

### An Asynchronous Timer Awaiter

Having said all this theory, next let's tie it together with a concrete example. We are going to implement a `SleepAwaiter`—an awaiter that puts the coroutine to "sleep" for a specified number of milliseconds.

Of course, truly asynchronous sleeping needs the cooperation of an event loop and timers, so here we first use a simplified synchronous version to show the complete structure of an awaiter:

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>

/// An async-sleep awaiter (synchronous blocking version, demo only)
struct SleepAwaiter {
    int kMilliSeconds; // how long to sleep

    explicit SleepAwaiter(int ms) : kMilliSeconds(ms) {}

    // ① Always needs to suspend—because we genuinely have to wait
    bool await_ready() const noexcept { return false; }

    // ② Do the sleeping upon suspension
    //    returning void = unconditional suspension, control returns to the caller
    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        // In a real event loop, this would be "register a timer and store the handle"
        // Here we simplify: sleep directly, then resume
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kMilliSeconds)
        );
        // Resume the coroutine as soon as the sleep ends
        handle.resume();
    }

    // ③ Nothing to return upon resumption
    void await_resume() const noexcept {}
};

/// We'd like co_await to accept a plain integer (milliseconds) directly.
/// Looks intuitive—but this version is actually problematic; see the analysis below.
SleepAwaiter operator co_await(int ms)
{
    return SleepAwaiter(ms);
}
```

Wait—there is a problem with the version above: `operator co_await(int ms)` is a free function, and ADL lookup needs to consider namespaces. For the built-in type `int`, ADL does nothing—`int` has no associated namespaces. So the more correct approach is to intercept it via `promise_type`'s `await_transform`:

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <thread>

/// An async-sleep awaiter
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

/// The coroutine task type
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

        // ④ await_transform: intercept the co_await expression
        //    When you write co_await 100;, the compiler calls this method
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

// Usage example
TimerTask countdown()
{
    for (int i = 5; i > 0; --i) {
        std::printf("倒计时: %d\n", i);
        co_await 1000; // wait 1 second (converted into a SleepAwaiter via await_transform)
    }
    std::puts("发射！");
}

int main()
{
    auto task = countdown(); // the coroutine starts executing immediately (initial_suspend returns suspend_never)
    // The coroutine has already finished, because SleepAwaiter resumed itself synchronously in await_suspend
    return 0;
}
```

In this example, `await_transform` plays the role of a "middleman"—it converts the `int` into a `SleepAwaiter`. This pattern is very common in real projects: inside `await_transform` you can do type checking, logging, cancellation checks, and so on.

### The Three Return Forms of await_suspend

Now here comes the question: why does `await_suspend` need three return forms? Isn't this just making things more complicated?

In fact, every form has its place. Let's take them apart one by one.

**Returning `void`** is the simplest—the coroutine suspends, and control returns to the caller or to whoever initiated the most recent `resume()`. This suits scenarios where suspension is "handed over entirely to external management"—storing the handle into a queue, for instance, and letting the event loop resume it later.

**Returning `bool`** gives you a chance to make a final decision between suspending and not suspending. For example, you do a check and find the I/O operation has actually already completed, so you return `false` to let the coroutine keep executing and avoid a pointless suspend/resume cost.

**Returning `std::coroutine_handle<>`** is the most powerful but also the most error-prone form. This is the so-called symmetric transfer. When your `await_suspend` returns a handle, the compiler suspends the current coroutine and then **immediately** resumes the coroutine corresponding to the returned handle—control does not go back to the caller. The standard's design intent is to let the compiler perform tail-call optimization so that the call stack depth doesn't grow—and the mainstream compilers (GCC, Clang, MSVC) really do this at higher optimization levels. Strictly speaking, though, tail-call optimization is a "quality of implementation" matter rather than a "standard guarantee": both GCC and Clang have had bugs where symmetric transfer still caused stack overflow (GCC #100897, LLVM #42853). In practice, this mechanism reliably avoids stack overflow, but don't rely on it under `-O0`.

Here's an example demonstrating symmetric transfer:

```cpp
#include <coroutine>
#include <cstdio>

/// A simple task type that supports chained execution
struct ChainTask {
    struct promise_type {
        // Stores the caller coroutine's handle, to resume it once we finish
        std::coroutine_handle<> kCaller;

        ChainTask get_return_object()
        {
            return ChainTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }

        // On completion, resume the caller via symmetric transfer
        std::coroutine_handle<> final_suspend() noexcept
        {
            return kCaller; // if kCaller is null, the behavior is undefined
        }

        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;

    /// When co_awaiting a ChainTask, suspend the current coroutine and start the awaited coroutine
    bool await_ready() noexcept { return false; }

    // symmetric transfer: suspend ourselves, resume the other side
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> caller) noexcept
    {
        // Store the caller so we can resume it once we finish
        handle.promise().kCaller = caller;
        // Return our own handle—the scheduler will resume this coroutine directly
        return handle;
    }

    void await_resume() noexcept {}
};
```

> ⚠️ **Symmetric transfer is the key mechanism for avoiding coroutine stack overflow.** If your coroutine A calls coroutine B, B calls C, C calls D... and every level goes "suspend A → resume B → suspend B → resume C", then without symmetric transfer the call stack grows deeper and deeper. Symmetric transfer gives the compiler the opportunity to avoid stack growth through tail-call optimization—which is crucial when coroutine chains are fairly long (deep recursive coroutine chains, for example). Note that tail-call optimization is a "quality of implementation" matter rather than a "standard guarantee"; at low optimization levels, stack overflow can still occur.

## operator co_await and ADL

Earlier we covered how the compiler gets an awaiter from an awaitable through overload resolution on `operator co_await`. Here is a problem you run into all the time in real-world engineering: you have a type from a third-party library and you cannot modify its source code—how do you add `operator co_await` to it?

The answer is to exploit ADL (Argument-Dependent Lookup). When overload resolution searches for `operator co_await` candidate functions, besides looking for member functions in the scope of the awaitable's class, it also searches for free functions in the namespaces associated with the awaitable's type via ADL. This gives us a back door for extending a type's await capability without modifying the original type. Here's a concrete example:

```cpp
namespace third_party {
    // A third-party type you cannot modify
    struct Future {
        // ... internal implementation
    };
}

// Add operator co_await inside the third_party namespace
// ADL will find this overload
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

// Now you can write:
third_party::Future fut;
auto result = co_await fut; // ADL finds operator co_await
```

That is the power of ADL—you don't need to modify the original type; you only need to provide a free-function `operator co_await` overload in its namespace. Of course, if you can modify the type itself, directly adding a member `operator co_await()` is simpler. One thing to watch out for, though: if the same type has both a member and a non-member `operator co_await`, and both match, overload resolution becomes ambiguous and the compiler reports an error outright. So don't provide both on the same type.

## From awaitable to Scheduler

So far, all of our awaiters have done something "immediate" inside `await_suspend`—either blocking synchronously or resuming right away. But in a real asynchronous framework, what `await_suspend` usually does is submit the coroutine handle to some scheduler (an event loop, a thread pool, and so on), and let the scheduler resume the coroutine at an appropriate time.

This is the bridge between awaitables and schedulers: **`await_suspend` is the key integration point for schedulers**. When the coroutine suspends, `await_suspend` holds the coroutine's handle, and it can store that handle anywhere—a queue, a timer list, the data field of an epoll event—and then let the scheduler come back and `resume()` it later.

Next, let's look at a minimal scheduler framework that shows how a complete "coroutines + scheduler" setup runs.

```cpp
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <deque>
#include <functional>

/// A minimal scheduler—maintains a ready queue and runs it in a loop
class Scheduler {
public:
    static Scheduler& instance()
    {
        static Scheduler kScheduler;
        return kScheduler;
    }

    /// Put a coroutine handle into the ready queue
    void schedule(std::coroutine_handle<> handle)
    {
        kReadyQueue.push_back(handle);
    }

    /// Run the scheduling loop until the queue is empty
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

/// A scheduler-friendly task type
struct ScheduledTask {
    struct promise_type {
        ScheduledTask get_return_object()
        {
            return ScheduledTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        // Lazy start: the coroutine doesn't run on creation; it waits for the scheduler to schedule it
        std::suspend_always initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }
    };

    std::coroutine_handle<promise_type> handle;
};

/// Yield a time slice—suspend and put ourselves back into the ready queue
struct YieldAwaiter {
    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // The core trick: put the current coroutine back into the ready queue so other coroutines run first
        Scheduler::instance().schedule(handle);
    }

    void await_resume() noexcept {}
};

/// Async sleep—suspend, then re-enqueue after the delay
/// (sleep simulates the timer here; a real implementation would use epoll + timerfd)
struct AsyncSleepAwaiter {
    int kMilliSeconds;

    explicit AsyncSleepAwaiter(int ms) : kMilliSeconds(ms) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // In a real scheduler you would register a timer here
        // Simplified version: spawn a thread to simulate an async timer
        std::thread([handle, this]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kMilliSeconds)
            );
            // Once the timer expires, put the coroutine back into the ready queue
            Scheduler::instance().schedule(handle);
        }).detach();
    }

    void await_resume() noexcept {}
};

/// Coroutine functions—alternating execution
ScheduledTask producer()
{
    for (int i = 0; i < 3; ++i) {
        std::printf("  [producer] 生产第 %d 个消息\n", i + 1);
        co_await YieldAwaiter{}; // yield execution
    }
    std::puts("  [producer] 完成！");
}

ScheduledTask consumer()
{
    for (int i = 0; i < 3; ++i) {
        std::printf("  [consumer] 消费第 %d 个消息\n", i + 1);
        co_await YieldAwaiter{}; // yield execution
    }
    std::puts("  [consumer] 完成！");
}

int main()
{
    auto& sched = Scheduler::instance();

    // Create two coroutines (neither runs at this point, because initial_suspend returns suspend_always)
    auto prod = producer();
    auto cons = consumer();

    // Put both into the ready queue
    sched.schedule(prod.handle);
    sched.schedule(cons.handle);

    std::puts("=== 调度器开始运行 ===");

    // Start the scheduling loop
    // The two coroutines will execute in turns:
    // [producer] produces message 1 → yield
    // [consumer] consumes message 1 → yield
    // [producer] produces message 2 → yield
    // [consumer] consumes message 2 → yield
    // [producer] produces message 3 → yield
    // [consumer] consumes message 3 → yield
    // [producer] done!
    // [consumer] done!
    sched.run();

    std::puts("=== 调度器运行结束 ===");

    // Clean up
    prod.handle.destroy();
    cons.handle.destroy();

    return 0;
}
```

Crude as it is, this scheduler shows the core model of coroutine scheduling. `YieldAwaiter` demonstrates the most basic cooperative scheduling: a coroutine voluntarily yields execution, puts itself back into the ready queue, and lets other coroutines run. `AsyncSleepAwaiter` shows the basic pattern of an asynchronous timer: suspend the coroutine, set a timer (simulated with a thread here), and when the timer expires, put the coroutine back into the ready queue.

The real traps are still ahead—when we combine this scheduler with I/O multiplexing (epoll), things get considerably more complicated, but the basic model never changes: **the awaiter's `await_suspend` is responsible for submitting the coroutine handle to the scheduler, and the scheduler `resume()`s the coroutine at the appropriate time**.

## Where We Are

In this article we took apart the two big customization extension points of C++20 coroutines. `promise_type` controls the coroutine's macro lifecycle—how to create the return object, whether to suspend at startup, what to do at the end, and how return values and exceptions are handled. The awaiter/awaitable protocol controls the coroutine's micro-level suspension and resumption—`await_ready` asks "are you ready yet", `await_suspend` does its work at suspension time, and `await_resume` fetches the result at resumption time. The three return forms of `await_suspend` (void / bool / coroutine_handle) provide a progressive ramp of flexibility from simple suspension up to symmetric transfer. Finally, we saw that `await_suspend` is the key integration point for schedulers—it submits the coroutine handle to the scheduler and lets the scheduler decide when to resume the coroutine.

But so far, our scheduler is still a toy of "ready queue + sequential execution". Real asynchronous I/O needs to connect to the operating system's I/O multiplexing machinery. What the next article will do is combine coroutines with epoll (Linux's I/O multiplexing) and build an event loop that can handle real network I/O. That is where coroutines truly shine.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines) — The authoritative reference for C++20 coroutines, including the complete language specification
- [C++20 Coroutines: Sketching a Minimal Async Framework — Jeremy Ong](https://jeremyong.com/cpp/2021/01/04/cpp20-coroutines-a-minimal-async-framework/) — A hands-on article on building a coroutine async framework from scratch
- [My Tutorial and Take on C++20 Coroutines — David Mazieres (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html) — A Stanford professor's coroutine tutorial, deep and practical
- [C++ Coroutines: Defining the co_await operator — Raymond Chen (Microsoft)](https://devblogs.microsoft.com/oldnewthing/20191218-00/?p=103221) — Explains the member and free-function overloads of `operator co_await` and the overload resolution rules
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/) — A practical guide, including a reminder about the differing bool semantics of `await_ready` and `await_suspend`
- [C++20 Coroutines — Complete Guide — Simon Toth (ITNEXT)](https://itnext.io/c-20-coroutines-complete-guide-7c3fc08db89d) — A comprehensive guide that covers the whole coroutine machinery
