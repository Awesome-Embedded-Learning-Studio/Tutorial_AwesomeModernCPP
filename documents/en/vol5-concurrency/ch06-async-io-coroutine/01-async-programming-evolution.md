---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Trace the evolution of asynchronous programming paradigms—callbacks, future chains, coroutines—and understand each model's motivation, pain points, and concrete form in C++
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Thread Pool Design
- promise and packaged_task
reading_time_minutes: 21
related:
- C++20 Coroutine Fundamentals
- Asynchronous I/O and Event Loops
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- 基础
title: 'The Evolution of Asynchronous Programming: From Callback Hell to Coroutines'
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/01-async-programming-evolution.md
  source_hash: 98889fec015dcc3e3ed8741e22cc6501a6cbae4652d183ef2bfb6e87457367ce
  translated_at: '2026-09-26T09:03:07+00:00'
  engine: anthropic
  token_count: 14000
---
# The Evolution of Asynchronous Programming: From Callback Hell to Coroutines

> 📖 **Required reading**: This article puts C++20 coroutines to work. If you haven't yet met the low-level machinery—`co_await`/`co_return`, `promise_type`—you can start with [Volume 4: Coroutine Fundamentals](../../vol4-advanced/01-coroutine-basics.md), which builds up the "skeleton" of a coroutine from scratch.

Honestly, writing this article stirs up a few feelings. Throughout the earlier chapters we dealt with threads, locks, and atomics—tools that give us precise control, at the price of having to manage everything ourselves. Thread creation and destruction, synchronization design, hauling results from worker threads back to the main thread, figuring out how exceptions travel back: every concurrent task means repeating the whole ritual. In ch05 we streamlined some of that work with `std::async` and `std::future`, but you quickly discover that once you need to chain several asynchronous operations—read a file first, then parse the data, finally write the result back—managing a chain of futures gets genuinely clumsy.

That is the core problem asynchronous programming tries to solve: **how to organize and compose multiple asynchronous operations elegantly**. The problem isn't unique to C++; nearly every language has been through the same evolution—from callbacks, to future/promise chains, and then to coroutines. In this article we'll trace that arc from beginning to end: what motivates each model, what problems it solves, what new problems it drags in—and, finally, why so many people consider C++20 coroutines "the right way to do asynchronous programming".

## Environment

Before we start, let's pin down the environment. All code in this article uses the pure standard library, with no platform dependencies—it runs on Linux, macOS, and Windows. For compilers, the callback and future sections only need C++11, but the coroutine examples need C++20—you want one of GCC 12+, Clang 15+, or MSVC 19.34+, and adding `-std=c++20 -Wall -Wextra` to your compile options is enough. Honestly, compiler support for C++20 coroutines has been quite mature since 2024; every version just listed compiles the complete set of coroutine language features correctly. One caveat, though: the standard library's `<generator>` was introduced only in C++23 and is not yet fully supported by every implementation, so the code in this article uses a hand-written generator type and does not depend on the standard library header.

## A Scenario: 1000 Concurrent Connections

Let's begin with a concrete scenario. Suppose you're writing a network server that needs to handle 1000 client connections simultaneously. Each connection's lifecycle looks roughly like: accept connection → read request → process request → send response → close connection. Along the way, reads and writes are I/O operations, and I/O is slow—a single network read might wait a few milliseconds, or even hundreds of milliseconds.

The most intuitive approach is "one thread per connection": whenever a new connection comes in, we spin up a new thread dedicated to handling it. It's simple to write, but the problem is glaring—1000 connections mean 1000 threads. Every thread carries its own stack (8MB by default on Linux), so stacks alone eat nearly 8GB of memory. And the operating system pays dearly to schedule 1000 threads—context switches, cache invalidation, lock contention all burn a lot of CPU time. The sharper issue: most of those 1000 threads aren't computing most of the time—they're waiting for I/O—waiting for data to arrive on the network card, waiting for the TCP buffer to free up space. While a thread waits on I/O, the memory and scheduling resources it occupies are entirely wasted.

That is the fundamental problem of synchronous blocking I/O: **a thread sits on its resources doing nothing while waiting for I/O, and you can't take those resources and put them to work on something else**.

The core idea of asynchronous programming is: don't let the thread wait there foolishly. When you hit an I/O operation, go do something else first, and come back to continue once the I/O completes. But "go do something else, come back later" is easy to say—how do you organize it at the code level? That's the question the next three models—callbacks, future chains, and coroutines—each answer in their own way.

## The Callback Model: The Most Primitive Asynchrony

Let's start with the most intuitive option, the callback model. The idea is blunt: when you kick off an asynchronous operation, you also pass in a function (the callback), telling the system "when the operation finishes, call this function for me".

Let's feel it out with a simplified example. Suppose we want to implement the flow "asynchronously read a file, then asynchronously process the data, then asynchronously write the result back". To avoid pulling in a real asynchronous I/O library, we simulate the asynchronous operations with `std::thread`:

```cpp
#include <functional>
#include <iostream>
#include <string>
#include <thread>

// Simulate asynchronously reading a file
void async_read_file(const std::string& path,
                     std::function<void(std::string)> on_complete)
{
    std::thread([path, on_complete] {
        // Simulate I/O latency
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string content = "file content from " + path;
        on_complete(content);
    }).detach();
}

// Simulate asynchronously processing the data
void async_process(const std::string& input,
                   std::function<void(std::string)> on_complete)
{
    std::thread([input, on_complete] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::string result = "processed(" + input + ")";
        on_complete(result);
    }).detach();
}

// Simulate asynchronously writing the result back
void async_write(const std::string& data,
                 std::function<void(bool)> on_complete)
{
    std::thread([data, on_complete] {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "  [write] 写入: " << data << "\n";
        on_complete(true);
    }).detach();
}

int main()
{
    std::cout << "开始异步处理流程...\n";

    async_read_file("data.txt", [](std::string content) {
        std::cout << "  [read] 读到: " << content << "\n";

        async_process(content, [](std::string processed) {
            std::cout << "  [process] 结果: " << processed << "\n";

            async_write(processed, [](bool success) {
                std::cout << "  [write] 写入"
                          << (success ? "成功" : "失败") << "\n";
                std::cout << "全部完成!\n";
            });
        });
    });

    // Wait for the asynchronous operations to finish (demo only; never do this in production)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

Do you see the problem? Three levels of nested lambdas—this is the infamous **callback hell**. Every extra asynchronous step adds one more level of indentation; with 5 or 10 async steps, readability collapses and the indentation marches right off the edge of your screen. And nesting hurts more than readability: the deeper problems are fragmented control flow, scattered error handling, and hairy lifetime management—those are the callback model's real pain points.

> ⚠️ This code uses `detach()` to keep the demo simple. In production code, you should manage thread lifetimes with a thread pool or `join()`, not let threads slip out of supervision.

The callback model's pain goes far beyond "the indentation is too deep". Start with fragmented control flow: what used to be one linear flow—read, process, write—is split into three independent functions, each aware only of its own slice of the logic. You can't see the order of the whole flow at a glance, because the order hides inside nested callback registrations. When you need to understand "how does the whole flow run", you start from the outermost callback and hop inward level by level—a completely different cognitive mode from reading ordinary sequential code.

Then comes error handling. Every step can fail, and the callback model has no unified error-handling mechanism. Typically you check the previous step's result inside every callback, then decide whether to continue or report an error. With 5 steps, you write 5 pieces of error-handling code, and that error-handling logic is itself nested and fragmented. There is no centralized `try/catch`-style mechanism; each callback fights its own battle.

The nastiest part is actually lifetime management. A callback is a closure that captures references to variables in the enclosing scope. What if those variables are already dead by the time the callback is invoked asynchronously? Dangling references, use-after-free—bugs like these show up especially easily in the callback model. You also have to worry about whether the callback got invoked multiple times, or never at all, and how exceptions escape from inside a callback—problems that simply don't exist in synchronous code, yet the callback model forces you to handle each one.

Put plainly, the callback model expresses "what happens next" with function pointers, and a function pointer is a low-level primitive—it offers no composition, no error propagation, no resource management. That's why every language has gone looking beyond callbacks for something better.

## Future/Promise Chains: A Step Up from Callbacks

With the callback model's pain points in clear view, let's look at the second option, the future/promise model. It is the first layer of improvement over callbacks, and the core idea is: an asynchronous operation returns a `future<T>`—a voucher for "a value that will exist at some point in the future". You can block on `get()` waiting for the result, or, one way or another, register a continuation that runs once the value is ready.

C++11 introduced `std::future` and `std::promise`, but the standard library's `std::future` has one big limitation: **it doesn't support `.then()`—that is, you can't register a continuation directly on a future**. To do "read a file asynchronously, then process the data", you have to orchestrate by hand:

```cpp
#include <future>
#include <iostream>
#include <string>
#include <thread>

// Simulate asynchronously reading a file
std::future<std::string> async_read_file(const std::string& path)
{
    return std::async(std::launch::async, [&path] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return "file content from " + path;
    });
}

// Simulate asynchronously processing the data
std::future<std::string> async_process(const std::string& input)
{
    return std::async(std::launch::async, [&input] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return "processed(" + input + ")";
    });
}

// Simulate asynchronously writing
std::future<bool> async_write(const std::string& data)
{
    return std::async(std::launch::async, [&data] {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "  [write] 写入: " << data << "\n";
        return true;
    });
}

int main()
{
    std::cout << "开始 future 链式处理...\n";

    // Step 1: read the file asynchronously
    std::future<std::string> f1 = async_read_file("data.txt");

    // Orchestrate the chain by hand—wait for f1 to finish, then start the next step
    // Note: the standard std::future has no .then(); we can only chain manually
    std::string content = f1.get();
    std::cout << "  [read] 读到: " << content << "\n";

    // Step 2: process the data
    std::future<std::string> f2 = async_process(content);
    std::string processed = f2.get();
    std::cout << "  [process] 结果: " << processed << "\n";

    // Step 3: write the result
    std::future<bool> f3 = async_write(processed);
    bool success = f3.get();
    std::cout << "  [write] 写入" << (success ? "成功" : "失败") << "\n";
    std::cout << "全部完成!\n";

    return 0;
}
```

You'll notice the nesting is gone—each asynchronous step is linear: `get()` the previous step's result, then launch the next step. Compared with the callback model, the future chain is a clear readability win: the control flow turns from a "pyramid of nested callbacks" into a "flat linear sequence".

But the problem is just as obvious: **the main thread blocks at every step**. `f1.get()` blocks until the read finishes, `f2.get()` blocks until processing finishes—how is that different from synchronous code? If you want the real thing—"main thread never blocks, asynchronous steps chain automatically"—you need `.then()`, which registers a function to be invoked automatically when the future's value is ready and returns a new future, forming a chain.

`std::future::then()` first appeared in C++'s Concurrency TS (Technical Specifications) as part of `std::experimental::future`, and Boost.Asio's `boost::future` also implements full `.then()` support. But the Concurrency TS was ultimately never merged into the C++ International Standard—as of C++23, the standard `std::future` still has no `.then()`. The C++ committee's position: rather than patching `std::future`, push the Sender/Receiver model (proposal P2300, i.e. `std::execution`, formally merged into the C++26 working draft at the St. Louis meeting in June 2024). So in standard C++, although `std::execution` is on its way, chaining the current `std::future` remains a clumsy affair.

> ⚠️ If you need future chaining, you can look at Boost.Asio's `boost::future::then()`, or use third-party libraries such as `thousandeyes-futures`. But the standard C++ `std::future` doesn't have this capability for now.

Future/promise chains really are progress over callbacks, but they bring problems of their own. A future involves heap allocation—every future carries an internal shared state, used to pass the value and the exception between the writing end (promise/async) and the reading end (future). That shared state is typically heap-allocated, so chaining multiple futures means multiple heap allocations. Exception propagation isn't very intuitive either—if a step in the chain throws, the exception is captured and stored in the future's shared state, and it is only rethrown when you call `get()`. That means you must check for exceptions at every step, or the rest of the chain may start while it is in an exceptional state.

## Coroutines: Writing Asynchronous Code Like Synchronous Code

Callbacks are too fragmented, future chains too clumsy—so is there a way to make asynchronous code **read exactly like synchronous code**, while executing asynchronously? That is, code that looks like one linear flow—read the file, process, write back—with no callbacks, no nesting, no manual orchestration, yet asynchronous execution underneath?

That is the core selling point of C++20 coroutines. Let's look at the code first, then explain what it does. The code below implements the same "read → process → write back" flow as before, but in coroutine style.

Don't be intimidated by the amount of code—we'll take it apart from the top. The first block is the `Task` struct, which defines the coroutine's return type. A C++20 coroutine requires the return type to contain a nested type named `promise_type`; the compiler uses that type to customize the coroutine's various behavioral policies. Inside `promise_type` you see several fixed-name functions: `get_return_object()` creates the Task object returned to the caller; `initial_suspend()` decides whether the coroutine suspends right at the start (returning `suspend_never` here means the coroutine starts executing immediately); `final_suspend()` decides what happens after the coroutine ends (returning `suspend_always` means the coroutine stays suspended after finishing, waiting for external destruction); `return_void()` handles `co_return` or the function ending normally; `unhandled_exception()` handles uncaught exceptions. These functions form the basic skeleton of a coroutine's lifecycle.

Next come the three awaitable types—`AsyncRead`, `AsyncProcess`, `AsyncWrite`. Each implements three key functions: `await_ready()` returning `false` means "the operation isn't done yet, we must suspend"; `await_suspend()` is called when the coroutine suspends—here we launch a new thread to simulate asynchronous I/O, and when the thread finishes it calls `h.resume()` to resume the coroutine; `await_resume()` is called when the coroutine resumes, and its return value becomes the result of the `co_await` expression. You'll notice that each awaitable is effectively a "descriptor of an asynchronous operation"—it tells the coroutine when the operation is ready, what to do on suspension, and what result to hand back on resumption.

Finally, the `process_file` coroutine function. Look at this code—ignore the `co_await` keyword and it is indistinguishable from an ordinary synchronous function. A linear flow, one step after another, no callbacks, no nesting, no blocking `get()`. But its execution is asynchronous: at each `co_await`, the coroutine suspends, control returns to the caller, and the underlying thread can go do something else; when the asynchronous operation completes, the coroutine resumes from the suspension point and carries on.

```cpp
#include <coroutine>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// ---- A minimal coroutine task type ----

struct Task
{
    struct promise_type
    {
        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(
                *this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
};

// ---- Awaitables that simulate asynchronous operations ----

struct AsyncRead
{
    std::string path;
    std::string result;

    bool await_ready() { return false; }   // always suspend

    void await_suspend(std::coroutine_handle<> h)
    {
        // Simulate asynchronous I/O on a new thread
        std::thread([this, h] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            result = "file content from " + path;
            h.resume();     // I/O done; resume the coroutine
        }).detach();
    }

    std::string await_resume() { return std::move(result); }
};

struct AsyncProcess
{
    std::string input;
    std::string result;

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        std::thread([this, h] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            result = "processed(" + input + ")";
            h.resume();
        }).detach();
    }

    std::string await_resume() { return std::move(result); }
};

struct AsyncWrite
{
    std::string data;

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        std::thread([this, h] {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            std::cout << "  [write] 写入: " << data << "\n";
            h.resume();
        }).detach();
    }

    bool await_resume() { return true; }
};

// ---- The coroutine function: looks exactly like synchronous code ----

Task process_file(const std::string& path)
{
    std::cout << "开始处理 " << path << "...\n";

    // co_await: suspend the coroutine, wait for the asynchronous operation to finish
    std::string content = co_await AsyncRead{path};
    std::cout << "  [read] 读到: " << content << "\n";

    std::string processed = co_await AsyncProcess{content};
    std::cout << "  [process] 结果: " << processed << "\n";

    bool success = co_await AsyncWrite{processed};
    std::cout << "  [write] 写入" << (success ? "成功" : "失败") << "\n";

    std::cout << "全部完成!\n";
}

int main()
{
    process_file("data.txt");

    // Wait for the asynchronous operations to finish (for the demo)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

That is the magic of coroutines: **asynchronous code as straightforward to write as synchronous code, with a fully asynchronous execution model underneath**.

Looking back, C++20 introduced exactly three keywords for coroutines. `co_await expr` suspends the current coroutine and waits for the asynchronous operation represented by `expr` to complete; the operation's result becomes the return value of the `co_await` expression—this is the one we use most, and every asynchronous step in the example above suspends and resumes through it. `co_yield expr` produces a value and suspends the coroutine—the foundation of generators, which we'll see later. `co_return expr` returns a value and ends the coroutine. The moment any one of these three keywords appears in a function body, the compiler treats it as a coroutine—no special function declaration or modifier needed. A genuinely elegant design.

> ⚠️ A coroutine's return type is strictly constrained: it must contain a nested `promise_type` type. The compiler customizes the coroutine's various behaviors through that `promise_type`. We'll dissect this machinery in depth in the next article.

In this example we hand-wrote the helper types `Task`, `AsyncRead`, `AsyncProcess`, and `AsyncWrite`, which looks like a lot of code. In real projects, though, this infrastructure is usually provided by a framework (for example Boost.Asio's `awaitable`, or cppcoro's `task`), and you only write the linear logic inside `process_file`. C++20 coroutines provide the language-level mechanism, and libraries provide the easy-to-use packaging—a combined "language feature + library support" design.

## Comparing the Three Models

Now let's put the three models side by side.

The callback model produces the most fragmented code—the linear flow is broken into nested callback functions, control flow is no longer a straight top-to-bottom line but hops along callback registrations, error handling must be done separately inside each callback, and there is no unified exception-propagation mechanism. On the other hand, callbacks themselves have almost no runtime overhead—a callback is essentially a function pointer plus the captured closure, so performance is the highest of the three. But debugging a callback chain is a nightmare: the call stack is broken. When the 5th-level callback misbehaves, your debugger sees only that one callback's stack frame; everything above it is lost.

Future/promise chains read far better than callbacks: through `.then()` (or manual `get()` chaining), the flow can be written as a linear chain of calls. Exceptions propagate automatically through the future's shared state—if some step throws, the exception travels down the chain to the final `get()` call. But performance-wise there is an overhead you can't ignore: every future involves one heap allocation (the shared state), so chaining 10 asynchronous operations means 10 heap allocations. Debugging difficulty is moderate—at least the call stack is continuous—but error messages from future chains are usually not very friendly: you see a `std::future_error`, not which step of the chain went wrong or how.

Coroutines read best of the three—the coroutine function looks identical to a synchronous function, control flow is linear, and the cognitive load of reading and understanding is the lowest. Error handling can use `try/catch`; exceptions propagate normally inside the coroutine, exactly matching synchronous behavior. Performance-wise, coroutine frames are usually heap-allocated, but the compiler can apply the "coroutine elision" optimization, embedding the frame into the caller's stack frame. Each suspension point involves only saving/restoring registers and coroutine state—far lighter than a thread context switch. The debugging experience is close to synchronous code—the call stack is complete, you can set a breakpoint on a `co_await` line, and when the coroutine resumes, the debugger stops there correctly.

But coroutines have a price of their own—the machinery of C++20 coroutines is genuinely intricate. `promise_type`, `coroutine_handle`, `awaitable`, `awaiter`: understanding how these concepts cooperate takes time. The compiler performs extensive transformations on coroutine functions, and when something goes wrong, the error messages can be deeply cryptic. The good news: once you understand the machinery, using it is completely natural—and that is exactly what the next article dissects.

## Where We Are

In this article we walked three stops along the evolution of asynchronous programming. The callback model expresses "what happens next" with function pointers—simple but fragmented, with readability and maintainability falling off a cliff once nesting gets deep. Future/promise chains replaced nested callbacks with "value container + chained composition", making control flow linear—but standard C++'s `std::future` lacks `.then()` support (the Concurrency TS `.then()` was never merged into the International Standard), chaining stays clumsy, and every future costs one heap allocation. Coroutines make asynchronous code as straightforward to write as synchronous code—C++20 provides coroutine support at the language level through the three keywords `co_await`/`co_yield`/`co_return`, with the underlying suspend/resume machinery implemented jointly by the compiler and promise_type.

But "looks simple" doesn't mean "simple underneath". The internals of C++20 coroutines are remarkably elaborate—the compiler transforms the coroutine function into a state machine, where every `co_await` is a state-transition point; `promise_type` customizes the coroutine's various behavioral policies; `coroutine_handle` is a non-owning handle to the coroutine frame, responsible for resuming and destroying it. The next article takes this machinery apart from the inside out: what transformation does the compiler actually perform on a coroutine function? What lives in a coroutine frame? How does `coroutine_handle` manage lifetimes? We'll also build a generator that can `co_yield` integers from scratch, tying all the concepts together.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); browse `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
- [Design and evolution of C++ future continuations — Ivan Krivyakov](https://ikriv.com/blog/?p=4916)
- [Concurrency TS (N4680) — ISO C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4680.pdf)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
