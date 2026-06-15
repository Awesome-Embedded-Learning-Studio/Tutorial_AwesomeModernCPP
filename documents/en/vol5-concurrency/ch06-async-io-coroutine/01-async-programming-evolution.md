---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Tracing the evolution of asynchronous programming paradigms—callbacks,
  future chains, and coroutines—to understand the motivation, pain points, and implementation
  forms of each model in C++.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 线程池设计
- promise 与 packaged_task
reading_time_minutes: 18
related:
- C++20 协程基础
- 异步 I/O 与事件循环
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- 基础
title: 'Asynchronous Programming Evolution: From Callback Hell to Coroutines'
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch06-async-io-coroutine/01-async-programming-evolution.md
  source_hash: 69bdb786dac2ba9a89659ceb3dbc19b6a9c686db20dd2b1800f19ad72d3bf599
  token_count: 3751
  translated_at: '2026-06-13T11:51:48.247704+00:00'
---
# Evolution of Asynchronous Programming: From Callback Hell to Coroutines

> 📖 **Prerequisites**: This article uses C++20 coroutines. If you haven't yet encountered the underlying mechanisms of `co_await`, `co_yield`, and `co_return`, you might want to review [Volume 4 · Coroutine Basics](../../vol4-advanced/01-coroutine-basics.md) first—it breaks down how the "skeleton" of a coroutine is constructed from scratch.

To be honest, writing this piece brings up some mixed feelings. In previous chapters, we dealt extensively with threads, locks, and atomic operations. These tools give us precise control—but the cost is that you have to manage everything yourself. Thread creation and destruction, synchronization mechanism design, moving results from worker threads back to the main thread, and exception propagation—every time you write a concurrent task, you repeat this entire process. In Chapter 5, we used `std::async` and `std::future` to simplify some of this work, but you quickly discover a limitation: when you need to chain multiple asynchronous operations—read a file, parse data, write back results—managing `future` chains becomes very clumsy.

This is the core problem that asynchronous programming aims to solve: **how to elegantly organize and compose multiple asynchronous operations**. This problem isn't unique to C++; almost every language has undergone the same evolution—from callbacks to future/promise chains, and finally to coroutines. In this article, we will trace this evolution from start to finish, examining the motivation behind each model, the problems they solve, the new issues they introduce, and finally, why C++20 coroutines are widely considered "the right way to do asynchronous programming."

## Environment

Before we get our hands dirty, let's clarify the environment. All code in this article uses the pure standard library with no platform dependencies, so it runs on Linux, macOS, and Windows. Regarding compilers, the callback and `future` sections only require C++11, but the coroutine examples need C++20 support—you will need GCC 12+, Clang 15+, or MSVC 19.34+. Simply add the `-std=c++20` compiler flag. To be honest, compiler support for C++20 coroutines has been quite mature since 2024, and the versions mentioned above can correctly compile the full set of coroutine language features. However, note that `std::generator` was introduced in C++23, and not all implementations fully support it yet. Therefore, the code in this article uses a hand-written `generator` type and does not rely on standard library headers.

## A Scenario: 1000 Concurrent Connections

Let's start with a concrete scenario. Suppose you are writing a network server that needs to handle 1000 client connections simultaneously. The lifecycle of each connection is roughly: accept connection → read request → process request → send response → close connection. Throughout this process, reading and writing are I/O operations, and I/O is slow—a single network read might take a few milliseconds or even hundreds of milliseconds.

The most intuitive approach is "one connection, one thread": whenever a new connection arrives, we spawn a new thread dedicated to handling it. This scheme is simple to write, but the problems are obvious—1000 connections mean 1000 threads. Each thread has its own stack (8MB by default on Linux), so just the stack space would consume nearly 8GB of RAM. Furthermore, the overhead of the OS scheduling 1000 threads is not negligible—context switches, cache invalidation, and lock contention all consume significant CPU time. More critically, these 1000 threads spend most of their time not computing, but waiting for I/O—waiting for data to arrive on the network card or for the TCP buffer to free up space. While a thread waits for I/O, the memory and scheduling resources it occupies are completely wasted.

This is the fundamental problem with synchronous blocking I/O: **threads occupy resources while waiting for I/O, and you cannot use those resources to do anything else**.

The core idea of asynchronous programming is: don't let the thread wait stupidly. When you encounter an I/O operation, go do something else first, and come back to continue processing when the I/O is complete. But "go do something else and come back later" is easy to say, but how do we organize this at the code level? This is the question that the next three models—callbacks, future chains, and coroutines—each attempt to answer.

## Callback Model: The Most Primitive Asynchrony

We start with the most intuitive approach—the callback model. The idea is straightforward: when you initiate an asynchronous operation, you also pass in a function (a callback), telling the system "call this function when the operation is complete."

Let's use a simplified example to get a feel for it. Suppose we want to implement the flow: "asynchronously read file content, then asynchronously process the data, and finally asynchronously write the result back." To avoid introducing a real asynchronous I/O library, we use `std::thread` to simulate asynchronous operations:

```cpp
void process_file_callback() {
    std::thread([] {
        // Step 1: Async read
        std::string data = read_file();
        std::thread([data] {
            // Step 2: Async process
            std::string result = process(data);
            std::thread([result] {
                // Step 3: Async write
                write_file(result);
            }).detach();
        }).detach();
    }).detach();
}
```

Do you see the problem? Three levels of nested lambdas—this is so-called **callback hell**. With every additional asynchronous step, the indentation goes deeper. If you have 5 or 10 steps, readability drops precipitously, and the indentation runs off the screen. Furthermore, the nesting affects more than just readability; the deeper issues are the fragmentation of control flow, scattered error handling, and complex lifetime management—these are the real pain points of the callback model.

> ⚠️ This code uses `std::thread::detach` to simplify the demonstration. In production code, you should use a thread pool or `std::async` to manage the thread lifecycle, rather than letting threads run uncontrolled.

The pain points of the callback model go far beyond "indentation too deep." First, let's discuss the fragmentation of control flow—a process that was originally linear (read, process, write) is split into three independent functions, each knowing only its own piece of logic. You cannot see the order of the entire flow at a glance because the order is hidden in the nested callback registrations. When you need to understand "how the whole flow runs," you have to start from the outermost callback and jump in layer by layer—this is completely different from the cognitive model of reading normal sequential code.

Next is the error handling problem. Every step can fail, and the callback model lacks a unified error handling mechanism. You usually need to check the result of the previous step in each callback and decide whether to continue or report an error. If there are 5 steps, you write 5 pieces of error handling code, and these error handling logics are also nested and fragmented. Without a centralized error handling mechanism like `try-catch`, you can only fight on your own in each callback.

The trickiest part is actually lifetime management. A callback is a closure that captures references to variables in the outer scope. What if those variables are invalid when the callback is asynchronously invoked? Dangling references, use-after-free—these bugs are particularly prone to occur in the callback model. You also have to worry about whether the callback was called multiple times, or not called at all, and how to propagate exceptions out of the callback—these problems don't exist in synchronous code at all, but in the callback model, you must handle them one by one.

Basically, the callback model uses "function pointers" to express "what to do next," but a function pointer is a low-level primitive—it lacks composability, error propagation, and resource management. This is why all languages are looking for better solutions beyond callbacks.

## Future/Promise Chains: A Bit Better Than Callbacks

Now that we've seen the pain points of callbacks, let's look at the second approach—the future/promise model. It is the first layer of improvement over callbacks. The core idea is: an asynchronous operation returns a `std::future`—a voucher representing "a value that will exist at some point in the future." You can use `get()` to block waiting for the result, or use some method to register a follow-up operation to be executed "when the value is ready."

C++11 introduced `std::future` and `std::promise`, but the standard library's `std::future` has a major limitation: **it does not support continuations (`.then()`)**—that is, you cannot directly register a follow-up operation on a future. If you want to implement "read file asynchronously, then process data," you have to orchestrate it manually:

```cpp
void process_file_future_blocking() {
    // Step 1: Async read
    std::future<std::string> read_future = std::async([] { return read_file(); });
    std::string data = read_future.get(); // Block until read completes

    // Step 2: Async process
    std::future<std::string> process_future = std::async([data] { return process(data); });
    std::string result = process_future.get(); // Block until processing completes

    // Step 3: Async write
    std::future<void> write_future = std::async([result] { write_file(result); });
    write_future.get(); // Block until write completes
}
```

You will notice that the nesting in this code has disappeared—each asynchronous step is linear: first use `get()` to get the result of the previous step, then start the next step. Compared to the callback model, the future chain has significantly improved readability: the control flow has changed from a "nested callback pyramid" to a "flat linear sequence."

But the problem is also obvious: **the main thread blocks at every step**. `read_future.get()` blocks until the file is read, `process_future.get()` blocks until processing is complete—how is this different from synchronous code? If you want to truly achieve "non-blocking main thread, automatic chaining of asynchronous steps," you need continuations (`.then()`)—automatically calling the registered function when the future's value is ready, returning a new future, forming a chain call.

`.then()` first appeared in C++'s Concurrency TS (Technical Specification) as part of `std::future`, and Boost.Asio's `std::experimental::future` also implements complete continuation support. However, Concurrency TS was ultimately not merged into the C++ international standard—as of C++23, the standard `std::future` still lacks `.then()`. The C++ Committee's attitude is: rather than patching `std::future`, it's better to push the Sender/Receiver model (proposal P2300, i.e., `std::execution`, which was officially merged into the C++26 working draft at the St. Louis meeting in July 2024). So in standard C++, although `std::execution` is coming, chaining with the current `std::future` remains a clumsy task.

> ⚠️ If you need future chaining, you can refer to Boost.Asio's `awaitable` or use third-party libraries like `cppcoro`. But standard C++'s `std::future` currently lacks this capability.

Future/Promise chains are certainly an improvement over callbacks, but they introduce their own problems. Futures themselves involve heap allocation—every future has a shared state internally, used to pass values and exceptions between the write end (promise/async) and the read end (future). This shared state is usually heap-allocated, so when you link multiple futures, you have multiple heap allocations. Exception propagation is also not very intuitive—if a step in the chain throws an exception, the exception is caught and stored in the future's shared state, only to be re-thrown when you call `get()`. This means you must check for exceptions at every step, otherwise subsequent steps in the chain might start in an exceptional state.

## Coroutines: Writing Asynchronous Code Like Synchronous Code

Callbacks are too fragmented, and future chains are too clumsy. Is there a way to make asynchronous code **look exactly like synchronous code**, but execute asynchronously? That is, the code looks like a linear flow: read file, process, write back, with no callbacks, no nesting, no manual orchestration, but the underlying execution is automatically asynchronous?

This is the core selling point of C++20 coroutines. Let's look at the code first, then explain what it does. The following code implements the same "read → process → write back" flow as before, but using the coroutine style.

Don't be intimidated by the amount of code—we'll break it down from the beginning. The first block is the `Task` struct, which defines the return type of the coroutine. C++20 coroutines require that the return type internally contains a nested type named `promise_type`. The compiler customizes various behavior policies of the coroutine through this type. You see several fixed-name functions inside `promise_type`: `get_return_object` creates the `Task` object returned to the caller; `initial_suspend` determines whether the coroutine suspends at the very beginning (here it returns `std::suspend_never`, meaning the coroutine starts executing immediately); `final_suspend` determines the behavior after the coroutine ends (returns `std::suspend_always`, meaning the coroutine suspends there after completion, waiting for external destruction); `return_value` handles the `co_return` or normal function end; `unhandled_exception` handles uncaught exceptions. These functions constitute the basic skeleton of the coroutine lifecycle.

Next are three awaitable types—`AsyncRead`, `AsyncProcess`, `AsyncWrite`. Each implements three key functions: `await_ready` returns `false` to indicate "the operation is not complete yet, needs to suspend"; `await_suspend` is called when the coroutine suspends—here we start a new thread to simulate asynchronous I/O, and the thread calls `coroutine_handle::resume` to resume the coroutine when done; `await_resume` is called when the coroutine resumes, and its return value becomes the result of the `co_await` expression. You will find that each awaitable is essentially a "descriptor for an asynchronous operation"—it tells the coroutine "when the operation is ready," "what to do when suspending," and "what result to give when resuming."

Finally, there is the `process_file_coroutine` coroutine function. Look at this code—if you ignore the `co_await` keyword, it looks no different from a normal synchronous function. Linear flow, step by step, no callbacks, no nesting, no `get()` blocking. But its execution is asynchronous: whenever it encounters `co_await`, the coroutine suspends, control is returned to the caller, and the underlying thread can go do other things; when the asynchronous operation completes, the coroutine resumes from the suspension point and continues executing.

```cpp
#include <coroutine>
#include <thread>
#include <string>
#include <iostream>

// Custom coroutine return type
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;
    Task(std::coroutine_handle<promise_type> handle) : h(handle) {}
    ~Task() { if (h && !h.done()) h.destroy(); }

    // Non-copyable
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
};

// Awaitable: Async Read
struct AsyncRead {
    std::string data;
    bool await_ready() { return false; } // Always suspend
    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([handle, this] {
            data = read_file(); // Simulate async I/O
            handle.resume();
        }).detach();
    }
    std::string await_resume() { return data; }
};

// Awaitable: Async Process
struct AsyncProcess {
    std::string input;
    std::string result;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([handle, this] {
            result = process(input);
            handle.resume();
        }).detach();
    }
    std::string await_resume() { return result; }
};

// Awaitable: Async Write
struct AsyncWrite {
    std::string content;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([handle, this] {
            write_file(content);
            handle.resume();
        }).detach();
    }
    void await_resume() {}
};

// Coroutine function
Task process_file_coroutine() {
    // Step 1: Async read
    std::string data = co_await AsyncRead{};
    // Step 2: Async process
    std::string result = co_await AsyncProcess{data};
    // Step 3: Async write
    co_await AsyncWrite{result};
}
```

This is the magic of coroutines: **asynchronous code is written as straightforwardly as synchronous code, but the execution model is fully asynchronous**.

Looking back, C++20 introduced three keywords for coroutines. `co_await` suspends the current coroutine, waiting for the asynchronous operation represented by the awaitable to complete, and the result of the operation becomes the return value of the `co_await` expression—this is what we use most often, and every asynchronous operation in the example above uses it to suspend and resume. `co_yield` yields a value and suspends the coroutine—this is the foundation of generators, which we will see later. `co_return` returns a value and ends the coroutine. As long as any of these three keywords appears in the function body, the compiler treats it as a coroutine—no special function declaration or modifiers are needed. This design is indeed elegant.

> ⚠️ Coroutine return types have strict requirements: they must contain a nested `promise_type`. The compiler customizes various coroutine behaviors through this `promise_type`. We will dissect this mechanism in depth in the next article.

In this example, we hand-wrote `Task`, `AsyncRead`, `AsyncProcess`, and `AsyncWrite` helper types, which looks like a lot of code. But in actual projects, this infrastructure is usually provided by frameworks (like Boost.Asio's `awaitable` or cppcoro's `task`), and you only need to write the linear logic inside the coroutine function. C++20 coroutines provide a language-level mechanism, and the library is responsible for providing easy-to-use wrappers—this is a "language feature + library support" design.

## Comparison of the Three Models

Now let's look at the three models together.

The callback model code is the most "fragmented"—the linear flow is split into nested callback functions, and the control flow is no longer a straight line from top to bottom but jumps according to callback registration relationships. Error handling needs to be handled separately in each callback, and there is no unified exception propagation mechanism. However, callbacks themselves have almost no runtime overhead—they are essentially just a function pointer plus a captured closure, so performance is the highest. But debugging a callback chain is a nightmare: the call stack is broken, and when the 5th layer callback has a problem, your debugger can only see that one callback's stack frame; the calling relationships above are all lost.

Future/Promise chains are much better than callbacks in terms of readability. Through `.then()` (or manual `get()` chaining), the flow can be written as a linear chain call. Exceptions propagate automatically through the future's shared state—if a step throws an exception, it travels along the chain to the final `get()` call. But performance-wise, there is a non-negligible overhead: every future involves a heap allocation (shared state), so when you link 10 asynchronous operations, that's 10 heap allocations. Debugging difficulty is moderate—at least the call stack is continuous, but future chain error messages are usually not very friendly; you see a `broken_promise`, not what specifically went wrong at which step in the chain.

Coroutines are the best of the three models in terms of readability—the coroutine function looks exactly like a synchronous function, the control flow is linear, and the cognitive burden of reading and understanding is the lowest. Error handling can use `try-catch`, and exceptions propagate normally within the coroutine, behaving exactly like synchronous code. Performance-wise, coroutine frames are usually heap-allocated, but the compiler can perform "coroutine elision" optimization to embed the frame into the caller's stack frame. Each suspension point only involves saving/restoring registers and coroutine state, which is much lighter than a thread context switch. The debugging experience is close to synchronous code—the call stack is complete, you can set a breakpoint on the `co_await` line, and the debugger will correctly stop there when the coroutine resumes execution.

But coroutines also have their costs—the C++20 coroutine mechanism is quite complex. `co_await`, `co_yield`, `co_return`, `promise_type`, the collaboration between these concepts requires time to understand. The compiler performs massive transformations on coroutine functions, and if something goes wrong, the error messages can be very obscure. The good news is that once you understand the mechanism, using it feels very natural—and in the next article, we will dissect this mechanism in depth.

## Where We Are

In this article, we walked through three stops along the evolutionary path of asynchronous programming. The callback model uses function pointers to express "what to do next"—simple but fragmented, and readability and maintainability drop precipitously as nesting deepens. Future/Promise chains replace nested callbacks with "value containers + chain composition," making the control flow linear, but standard C++'s `std::future` lacks `.then()` support (the Concurrency TS's `.then()` was never merged into the international standard), chain composition remains clumsy, and every future incurs a heap allocation overhead. Coroutines make asynchronous code read as straightforwardly as synchronous code—C++20 provides coroutine support at the language level through three keywords: `co_await`/`co_yield`/`co_return`, and the underlying suspend/resume mechanism is implemented jointly by the compiler and `promise_type`.

But "looking simple" doesn't mean "simple behind the scenes." The internal mechanism of C++20 coroutines is quite ingenious—the compiler transforms the coroutine function into a state machine, every `co_await` is a state transition point; `promise_type` customizes the coroutine's various behavior policies; `coroutine_handle` is a non-owning handle to the coroutine frame, responsible for resumption and destruction. In the next article, we will dissect this mechanism inside and out: What exactly does the compiler do to the coroutine function? What is stored in the coroutine frame? How does `coroutine_handle` manage the lifecycle? We will also implement a `generator` that can `co_yield` integers from scratch, tying all the concepts together.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `vol5-async`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
- [Design and evolution of C++ future continuations — Ivan Krivyakov](https://ikriv.com/blog/?p=4916)
- [Concurrency TS (N4680) — ISO C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4680.pdf)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
