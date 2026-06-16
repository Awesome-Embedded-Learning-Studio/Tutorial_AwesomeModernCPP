---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Tracing the evolution of asynchronous programming paradigms—callbacks,
  future chains, and coroutines—to understand the motivation, pain points, and C++
  implementation forms of each model.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 线程池设计
- promise 与 packaged_task
reading_time_minutes: 21
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
  source: documents/vol5-concurrency/ch06-async-io-coroutine/01-async-programming-evolution.md
  source_hash: 98889fec015dcc3e3ed8741e22cc6501a6cbae4652d183ef2bfb6e87457367ce
  translated_at: '2026-06-16T04:06:04.415973+00:00'
  engine: anthropic
  token_count: 3744
---
# Evolution of Asynchronous Programming: From Callback Hell to Coroutines

> 📖 **Prerequisites**: This article utilizes C++20 coroutines. If you haven't yet encountered the underlying mechanisms of `co_await`, `co_yield`, and `co_return`, you might want to review [Volume 4: Coroutine Basics](../../vol4-advanced/01-coroutine-basics.md) first—it breaks down the "skeleton" of coroutines from scratch.

To be honest, I feel a bit emotional writing this. In previous chapters, we've been dealing with threads, locks, and atomic operations. These tools give us precise control—but the cost is that you have to manage everything yourself. Thread creation and destruction, synchronization mechanism design, moving results from worker threads back to the main thread, and how to propagate exceptions—every time you write a concurrent task, you repeat this process. In Chapter 5, we used `std::async` and `std::future` to simplify some of the work, but you will soon discover: when you need to chain multiple asynchronous operations—read a file first, then parse the data, and finally write back the result—managing future chains becomes very clumsy.

This is the core problem that asynchronous programming aims to solve: **how to elegantly organize and compose multiple asynchronous operations**. This problem is not unique to C++; almost every language has undergone the same evolution—from callbacks to future/promise chains, and finally to coroutines. In this article, we will clarify this evolutionary path from beginning to end, seeing the motivation behind each model, what problems it solved, what new problems it introduced, and finally understanding why C++20 coroutines are considered by many to be "the right way to do asynchronous programming."

## Environment

Before we get our hands dirty, let's clarify the environment. All code in this article uses the pure standard library with no platform dependencies, so it runs on Linux, macOS, and Windows. Regarding compilers, the callback and future sections only require C++11, but the coroutine examples need C++20 support—you need GCC 12+, Clang 15+, or MSVC 19.34+. Just add the `-fcoroutines-ts` or `/await` compiler flag. To be honest, compiler support for C++20 coroutines has been quite mature since 2024; the versions mentioned above can correctly compile the full set of coroutine language features. However, note one thing: the standard library's `std::generator` was introduced in C++23, and not all implementations fully support it yet. Therefore, in the code here, we use a hand-written generator type and do not rely on standard library headers.

## A Scenario: 1000 Concurrent Connections

Let's start with a concrete scenario. Suppose you are writing a network server that needs to handle 1000 client connections simultaneously. The lifecycle of each connection is roughly: accept connection → read request → process request → send response → close connection. Throughout this process, reading and writing are I/O operations, and I/O is slow—a single network read might wait for a few milliseconds or even hundreds of milliseconds.

The most intuitive approach is "one thread per connection": whenever a new connection comes in, we spin up a new thread dedicated to handling it. This scheme is simple to write, but the problems are obvious—1000 connections mean 1000 threads. Each thread has its own stack (8MB by default on Linux), so just the stack space will consume nearly 8GB of memory. Moreover, the operating system overhead for scheduling 1000 threads is not small—context switches, cache invalidation, lock contention—these all eat up a lot of CPU time. More critically, these 1000 threads spend most of their time not computing, but waiting for I/O—waiting for data to arrive on the network card, waiting for the TCP buffer to free up space. When a thread is waiting for I/O, the memory and scheduling resources it occupies are completely wasted.

This is the fundamental problem with synchronous blocking I/O: **the thread occupies resources for nothing while waiting for I/O, and you can't use those resources to do anything else**.

The core idea of asynchronous programming is: don't let the thread wait foolishly. When you encounter an I/O operation, go do something else first, and come back to continue processing when the I/O is complete. But "go do something else first, come back later"—how do we organize this at the code level? This is the answer that the three models we will discuss next—callbacks, future chains, and coroutines—each provide.

## Callback Model: The Most Primitive Asynchrony

Let's start with the most intuitive solution—the callback model. The idea is straightforward: when you initiate an asynchronous operation, you also pass in a function (a callback), telling the system "call this function for me when the operation is complete."

Let's use a simplified example to get a feel for it. Suppose we want to implement the process of "asynchronously read file content, then asynchronously process the data, and finally asynchronously write the result back." To avoid introducing a real asynchronous I/O library, we use `std::thread` to simulate asynchronous operations:

```cpp
// 01_callback_model.cpp
#include <iostream>
#include <thread>
#include <functional>

// Simulate an async read operation
template <typename F>
void async_read(F&& callback) {
    std::thread([cb = std::forward<F>(callback)]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate I/O wait
        cb("File content"); // Call the callback with the result
    }).detach();
}

// Simulate an async process operation
template <typename F>
void async_process(const std::string& input, F&& callback) {
    std::thread([input, cb = std::forward<F>(callback)]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Simulate computation
        cb(input + " [Processed]"); // Call the callback with the result
    }).detach();
}

// Simulate an async write operation
template <typename F>
void async_write(const std::string& data, F&& callback) {
    std::thread([data, cb = std::forward<F>(callback)]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(80)); // Simulate I/O wait
        cb(true); // Call the callback indicating success
    }).detach();
}

int main() {
    // Start the callback chain
    async_read([](const std::string& content) {
        std::cout << "Read: " << content << std::endl;

        async_process(content, [](const std::string& processed) {
            std::cout << "Processed: " << processed << std::endl;

            async_write(processed, [](bool success) {
                if (success) {
                    std::cout << "Write success!" << std::endl;
                }
            });
        });
    });

    std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait for all async ops to finish
    return 0;
}
```

Do you see the problem? Three levels of nested lambdas—this is the so-called **callback hell**. With every additional asynchronous step, the indentation goes deeper. If you have 5 or 10 asynchronous steps, code readability drops drastically, and the indentation on the right runs off the screen. Moreover, nesting doesn't just affect readability; the deeper problems lie in the fragmentation of control flow, scattered error handling, and complex lifecycle management—these are the real pain points of the callback model.

> ⚠️ This code uses `.detach()` to simplify the demonstration. In production code, you should use a thread pool or `std::jthread` to manage the thread lifecycle, rather than letting threads run uncontrolled.

The pain points of the callback model go far beyond "indentation too deep." First, let's talk about control flow fragmentation—what was originally a linear process (read, process, write) is split into three independent functions, each knowing only its own logic. You cannot see the order of the whole process at a glance because the order is hidden in the nested callback registration. When you need to understand "how the whole process runs," you have to start from the outermost callback and jump in layer by layer—this is completely different from the cognitive model of reading normal sequential code.

Next is the error handling problem. Every step can fail, and the callback model lacks a unified error handling mechanism. You usually need to check the result of the previous step in each callback and then decide whether to continue or report an error. If there are 5 steps, you write 5 pieces of error handling code, and these error handling logics are also nested and fragmented. Without a centralized error handling mechanism like `try-catch`, you can only fight on your own in each callback.

The trickiest part is actually lifecycle management. A callback is a closure that captures references to variables in the outer scope. What if those variables have gone out of scope when the callback is invoked asynchronously? Dangling references, use-after-free—these bugs are particularly prone to occur in the callback model. You also have to worry about whether the callback is called multiple times, or not called at all, and how to propagate exceptions from the callback—these problems don't exist in synchronous code at all, but in the callback model you must deal with them one by one.

Basically, the callback model uses "function pointers" to express "what to do next," but a function pointer is a low-level primitive—it has no composability, no error propagation, and no resource management. This is why all languages are looking for better solutions beyond callbacks.

## Future/Promise Chain: A Bit Better Than Callbacks

Now that we've seen the pain points of callbacks, let's look at the second solution—the future/promise model. It is the first layer of improvement over callbacks. The core idea is: an asynchronous operation returns a `std::future`—a voucher representing "a value that will be available at some point in the future." You can use `.get()` to block and wait for the result, or use some method to register a follow-up operation to execute "when the value is ready."

C++11 introduced `std::future` and `std::promise`, but the standard library's `std::future` has a major limitation: **it does not support continuations (`.then()`). In other words, you cannot directly register a follow-up operation on a future**. If you want to implement "async read file, then process data," you have to orchestrate it manually:

```cpp
// 02_future_chain.cpp
#include <iostream>
#include <future>
#include <thread>
#include <chrono>

// Simulate async read
std::future<std::string> async_read() {
    return std::async(std::launch::async, []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return std::string("File content");
    });
}

// Simulate async process
std::future<std::string> async_process(const std::string& input) {
    return std::async(std::launch::async, [input]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return input + " [Processed]";
    });
}

// Simulate async write
std::future<bool> async_write(const std::string& data) {
    return std::async(std::launch::async, [data]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return true;
    });
}

int main() {
    // Manual orchestration
    auto f1 = async_read();
    auto content = f1.get(); // Block here

    auto f2 = async_process(content);
    auto processed = f2.get(); // Block here

    auto f3 = async_write(processed);
    bool success = f3.get(); // Block here

    std::cout << "Result: " << success << std::endl;
    return 0;
}
```

You will notice that the nesting in this code is gone—each asynchronous step is linear: first `.get()` the result of the previous step, then start the next step. Compared to the callback model, the future chain has significantly improved readability: the control flow has changed from a "nested callback pyramid" to a "flat linear sequence."

But the problem is also obvious: **the main thread blocks at every step**. `f1.get()` blocks until the file is read, `f2.get()` blocks until processing is complete—what's the difference between this and synchronous code? If you want to truly achieve "main thread doesn't block, async steps chain automatically," you need `.then()`—automatically calling the registered function when the future's value is ready, returning a new future, forming a chain call.

`.then()` first appeared in C++'s Concurrency TS (Technical Specification) as part of `std::future` extensions. Boost.Asio's `boost::asio::awaitable` also implements complete `.then()` support. However, Concurrency TS was ultimately not merged into the C++ international standard—as of C++23, the standard `std::future` still lacks `.then()`. The C++ Committee's attitude is: rather than patching `std::future`, it's better to push the Sender/Receiver model (Proposal P2300, i.e., `std::execution`, which was officially merged into the C++26 working draft at the St. Louis meeting in July 2024). So in standard C++, although `std::execution` is just around the corner, chaining `std::future` currently remains a clumsy task.

> ⚠️ If you need future chaining, you can refer to Boost.Asio's `boost::asio::awaitable`, or use third-party libraries like `folly::Future`. But standard C++'s `std::future` temporarily lacks this capability.

The Future/Promise chain is indeed an improvement over callbacks, but it also introduces its own problems. A future itself involves heap allocation—every future has a shared state internally, used to pass values and exceptions between the write end (promise/async) and the read end (future). This shared state is usually heap-allocated, so when you link multiple futures, you have multiple heap allocations. Exception propagation is also not very intuitive—if a step in the chain throws an exception, the exception is caught and stored in the future's shared state, only to be re-thrown when you call `.get()`. This means you must check for exceptions at every step, otherwise subsequent steps in the chain might start in an exceptional state.

## Coroutines: Writing Asynchronous Code Like Synchronous Code

Callbacks are too fragmented, and future chains are too clumsy. Is there a way to make asynchronous code **look and write exactly like synchronous code**, but execute asynchronously? That is, the code looks like a linear process: read file, process, write back, with no callbacks, no nesting, no manual orchestration, but the underlying execution is automatically asynchronous?

This is the core selling point of C++20 coroutines. Let's look at the code directly first, then explain what it does. The following code implements the same "read → process → write back" process as before, but using the coroutine style.

Don't be intimidated by the amount of code—we'll break it down from the start. The first block is the `Task` struct, which defines the return type of the coroutine. C++20 coroutines require that the return type must contain a nested type named `promise_type`. The compiler customizes various behavioral policies of the coroutine through this type. You see several fixed-name functions inside `promise_type`: `get_return_object` creates the `Task` object returned to the caller; `initial_suspend` determines whether the coroutine suspends at the very beginning (here it returns `std::never_suspend`, meaning the coroutine starts executing immediately); `final_suspend` determines the behavior after the coroutine ends (returns `std::suspend_always`, meaning the coroutine hangs there after completion, waiting for external destruction); `return_value` handles the `co_return` case or normal function termination; `unhandled_exception` handles uncaught exceptions. These functions constitute the basic skeleton of the coroutine lifecycle.

Next are three awaitable types—`AsyncRead`, `AsyncProcess`, `AsyncWrite`. Each implements three key functions: `await_ready` returns `false` to indicate "the operation is not finished yet, need to suspend"; `await_suspend` is called when the coroutine suspends—here we start a new thread to simulate async I/O, and the thread calls `coroutine_handle` to resume the coroutine when done; `await_resume` is called when the coroutine resumes, and its return value is the result of the `co_await` expression. You will find that each awaitable is actually a "descriptor for an asynchronous operation"—it tells the coroutine "when the operation is ready," "what to do when suspending," and "what result to give when resuming."

Finally, there is the `process_data` coroutine function. Look at this code—if you ignore the `co_await` keyword, it looks no different from a normal synchronous function. Linear flow, step by step, no callbacks, no nesting, no `.get()` blocking. But its execution is asynchronous: whenever it encounters `co_await`, the coroutine suspends, handing control back to the caller, and the underlying thread can go do other things; when the asynchronous operation completes, the coroutine resumes from the suspension point and continues executing.

```cpp
// 03_coroutine_model.cpp
#include <iostream>
#include <thread>
#include <coroutine>
#include <chrono>

// ------------------------------------------------------------
// 1. Coroutine Infrastructure: Task and Promise Type
// ------------------------------------------------------------

template <typename T>
struct Task {
    struct promise_type {
        T value;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() { return {}; }

        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T val) { value = val; }

        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}

    ~Task() {
        if (handle) handle.destroy();
    }

    // Prevent copying and moving for simplicity
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    T get() {
        if (!handle.done()) {
            // In a real framework, we would wait on an event here
            // For this simple example, we just spin (inefficient!)
            while (!handle.done()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return handle.promise().value;
    }
};

// ------------------------------------------------------------
// 2. Awaitable Types: Describing Async Operations
// ------------------------------------------------------------

struct AsyncRead {
    std::string operator co_await() {
        return {*this};
    }

    bool await_ready() { return false; } // Always suspend

    void await_suspend(std::coroutine_handle<> handle) {
        // Simulate async I/O in a separate thread
        std::thread([handle]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            handle.resume(); // Resume the coroutine
        }).detach();
    }

    std::string await_resume() { return "File content"; }
};

struct AsyncProcess {
    std::string input;

    std::string operator co_await() {
        return {*this};
    }

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([handle]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            handle.resume();
        }).detach();
    }

    std::string await_resume() { return input + " [Processed]"; }
};

struct AsyncWrite {
    std::string data;

    bool operator co_await() {
        return {*this};
    }

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([handle]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            handle.resume();
        }).detach();
    }

    bool await_resume() { return true; }
};

// ------------------------------------------------------------
// 3. The Coroutine: Linear Logic
// ------------------------------------------------------------

Task<bool> process_data() {
    // Step 1: Async Read
    std::string content = co_await AsyncRead{};
    std::cout << "Read: " << content << std::endl;

    // Step 2: Async Process
    std::string processed = co_await AsyncProcess{content};
    std::cout << "Processed: " << processed << std::endl;

    // Step 3: Async Write
    bool success = co_await AsyncWrite{processed};
    co_return success;
}

int main() {
    auto task = process_data();
    bool result = task.get(); // Wait for completion

    if (result) {
        std::cout << "Workflow finished successfully!" << std::endl;
    }

    return 0;
}
```

This is the magic of coroutines: **asynchronous code is written as straightforwardly as synchronous code, but the execution model is completely asynchronous**.

Looking back, C++20 introduced a total of three keywords for coroutines. `co_await` suspends the current coroutine, waiting for the asynchronous operation represented by the awaitable to complete, and the result of the operation is the return value of the `co_await` expression—this is what we use most often, and every asynchronous operation in the example above suspends and resumes through it. `co_yield` yields a value and suspends the coroutine—this is the foundation of generators, which we will see later. `co_return` returns a value and ends the coroutine. As long as any of these three keywords appears in the function body, the compiler treats it as a coroutine—no special function declaration or modifiers are needed. This design is indeed elegant.

> ⚠️ Coroutine return types have strict requirements: they must include a nested `promise_type`. The compiler customizes various behaviors of the coroutine through this `promise_type`. We will dissect this mechanism in depth in the next article.

In this example, we hand-wrote `Task`, `AsyncRead`, `AsyncProcess`, `AsyncWrite`, and other auxiliary types, which looks like a lot of code. But in actual projects, this infrastructure is usually provided by frameworks (like Boost.Asio's `awaitable`, cppcoro's `task`), and you only need to write the linear logic inside the coroutine. C++20 coroutines provide a language-level mechanism, and the library is responsible for providing easy-to-use wrappers—this is a combined design of "language feature + library support."

## Comparison of the Three Models

Now let's look at the three models together.

The callback model code is the most "fragmented"—the linear process is split into nested callback functions. The control flow is no longer a straight line from top to bottom, but jumps following the callback registration relationship. Error handling needs to be handled separately in each callback, and there is no unified exception propagation mechanism. However, callbacks themselves have almost no runtime overhead—they are essentially just a function pointer plus a captured closure, so performance is the highest. But debugging a callback chain is a nightmare: the call stack is broken. When the 5th layer callback has a problem, your debugger can only see that one callback's stack frame; all the calling relationships above are lost.

The Future/Promise chain is much better than callbacks in terms of readability. Through `.then()` (or manual `.get()` chaining), the process can be written as a linear chain call. Exceptions propagate automatically through the future's shared state—if a step throws an exception, it travels along the chain to the final `.get()` call. But performance-wise, there is a non-negligible overhead: every future involves a heap allocation (shared state). When you chain 10 asynchronous operations, that's 10 heap allocations. Debugging difficulty is moderate—at least the call stack is continuous, but future chain error messages are usually not very friendly; you see a `broken_promise`, not what specific step in the chain went wrong.

Coroutines are the best of the three models in terms of readability—the coroutine function looks exactly like a synchronous function. The control flow is linear, and the cognitive load for reading and understanding is the lowest. Error handling can use `try-catch`, and exceptions propagate normally within the coroutine, behaving exactly like synchronous code. Performance-wise, coroutine frames are usually heap-allocated, but the compiler can perform "coroutine elision" optimization, embedding the frame into the caller's stack frame. Each suspension point only involves saving/restoring registers and coroutine state, which is much lighter than a thread context switch. The debugging experience is close to synchronous code—the call stack is complete, and you can set a breakpoint on the `co_await` line, and the debugger will correctly stop there when the coroutine resumes execution.

But coroutines also have their costs—the mechanism of C++20 coroutines is quite complex. `co_await`, `co_yield`, `promise_type`, `coroutine_handle`—the collaborative relationship between these concepts takes time to understand. The compiler performs a massive transformation on coroutine functions, and if something goes wrong, the error messages can be very obscure. The good news is that once you understand this mechanism, using it feels very natural—in the next article, we will dissect this mechanism in depth.

## Where We Are

In this article, we have traveled three stops along the evolutionary path of asynchronous programming. The callback model uses function pointers to express "what to do next"—simple but fragmented, and readability and maintainability drop drastically when nesting gets deep. The Future/Promise chain replaces nested callbacks with "value containers + chain composition." The control flow becomes linear, but standard C++'s `std::future` lacks `.then()` support (the Concurrency TS's `.then()` was never merged into the international standard), so chain composition remains clumsy, and every future involves a heap allocation overhead. Coroutines make asynchronous code write as straightforwardly as synchronous code—C++20 provides coroutine support at the language level through the three keywords `co_await`/`co_yield`/`co_return`, and the underlying suspend/resume mechanism is implemented jointly by the compiler and `promise_type`.

But "looking simple" doesn't mean "simple behind the scenes." The internal mechanism of C++20 coroutines is quite ingenious—the compiler transforms the coroutine function into a state machine, where each `co_await` is a state transition point; `promise_type` customizes the various behavioral policies of the coroutine; `coroutine_handle` is a non-owning handle to the coroutine frame, responsible for resumption and destruction. In the next article, we will dissect this mechanism inside out: What exactly does the compiler transform the coroutine function into? What is stored in the coroutine frame? How does `coroutine_handle` manage the lifecycle? We will also implement a generator that can `co_yield` integers from scratch, tying all the concepts together.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `vol5-async/01_async_evolution`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
- [Design and evolution of C++ future continuations — Ivan Krivyakov](https://ikriv.com/blog/?p=4916)
- [Concurrency TS (N4680) — ISO C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4680.pdf)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
