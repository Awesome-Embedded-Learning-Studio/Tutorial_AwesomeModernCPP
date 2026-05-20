---
title: 'Async Programming Evolution: From Callback Hell to Coroutines'
description: Tracing the evolution of the asynchronous programming paradigm—callbacks,
  future chains, and coroutines—and understanding the motivation, pain points, and
  implementation forms of each model in C++.
chapter: 6
order: 1
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- 基础
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 线程池设计
- promise 与 packaged_task
related:
- C++20 协程基础
- 异步 I/O 与事件循环
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/01-async-programming-evolution.md
  source_hash: d0ffebacd5f4e338cd302c5f19546db80e39dbd6e5e1192ad00342c148a035c6
  translated_at: '2026-05-20T04:44:37.332058+00:00'
  engine: anthropic
  token_count: 3709
---
# Async Programming Evolution: From Callback Hell to Coroutines

Honestly, reaching this point in the series brings a sense of reflection. In previous chapters, we have been working closely with threads, locks, and atomic operations. These tools give us precise control—but the cost is that you must manage everything yourself. Thread creation and destruction, synchronization mechanism design, moving results back to the main thread, propagating exceptions—every concurrent task repeats this entire workflow. In ch05, we used `std::async` and `std::future` to simplify some of this work, but you will quickly discover a problem: when you need to chain multiple async operations—read a file, parse the data, then write back the result—managing future chains becomes incredibly clumsy.

This is the core problem async programming aims to solve: **how to elegantly organize and compose multiple async operations**. This is not a problem unique to C++; almost every language goes through the same evolution—from callbacks to future/promise chains, and finally to coroutines. In this chapter, we will trace this evolution from start to finish, examining the motivation behind each model, what problems it solves, what new problems it introduces, and ultimately why C++20 coroutines are widely considered "the right way to do async programming."

## Environment

Before we dive in, let us clarify the setup. All code in this chapter uses the pure standard library with no platform dependencies, so it runs on Linux, macOS, and Windows. On the compiler side, the callback and future sections only require C++11, but the coroutine examples need C++20 support—you will need GCC 12+, Clang 15+, or MSVC 19.34+, with the `-std=c++20 -Wall -Wextra` compiler flag added. Honestly, compiler support for C++20 coroutines has been quite mature since 2024, and the versions mentioned above can correctly compile the full set of coroutine language features. However, note one thing: the standard library's `<generator>` was only introduced in C++23 and is not yet fully supported across all implementations. Therefore, in this chapter's code, we use a hand-written generator type and do not rely on standard library headers.

## A Scenario: 1000 Concurrent Connections

Let us start with a concrete scenario. Suppose you are writing a network server that needs to handle 1000 client connections simultaneously. The lifecycle of each connection roughly follows: accept connection → read request → process request → send response → close connection. Throughout this process, reading and writing are I/O operations, and I/O operations are slow—a single network read might take a few milliseconds or even hundreds of milliseconds.

The most intuitive approach is "one thread per connection": whenever a new connection comes in, we spawn a new thread dedicated to handling it. This approach is simple to write, but the problems are obvious—1000 connections mean 1000 threads. Each thread has its own stack (8MB by default on Linux), so just the stack space alone would consume nearly 8GB of memory. Furthermore, the operating system's overhead for scheduling 1000 threads is not trivial—context switches, cache invalidation, and lock contention all consume significant CPU time. More critically, these 1000 threads spend most of their time not computing, but waiting for I/O—waiting for data to arrive on the network card, waiting for the TCP buffer to free up space. While a thread is waiting for I/O, the memory and scheduling resources it occupies are entirely wasted.

This is the fundamental problem with synchronous blocking I/O: **threads waste resources while waiting for I/O, and you cannot repurpose those resources to do other work**.

The core idea behind async programming is: do not let threads sit idle. When encountering an I/O operation, go do something else first, and come back to continue processing once the I/O completes. But "go do something else and come back later" is easy to say—how do we organize this at the code level? This is the question that the three models we are about to explore—callbacks, future chains, and coroutines—each answer in their own way.

## The Callback Model: The Most Primitive Form of Async

Let us start with the most intuitive approach—the callback model. The idea is straightforward: when you initiate an async operation, you also pass in a function (the callback), telling the system "call this function for me when the operation is done."

Let us first get a feel for this with a simplified example. Suppose we want to implement the following flow: "asynchronously read file contents, then asynchronously process the data, and finally asynchronously write back the result." To avoid introducing a real async I/O library, we use `std::thread` to simulate async operations:

```cpp
#include <functional>
#include <iostream>
#include <string>
#include <thread>

// 模拟异步读取文件内容
void async_read_file(const std::string& path,
                     std::function<void(std::string)> on_complete)
{
    std::thread([path, on_complete] {
        // 模拟 I/O 延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string content = "file content from " + path;
        on_complete(content);
    }).detach();
}

// 模拟异步处理数据
void async_process(const std::string& input,
                   std::function<void(std::string)> on_complete)
{
    std::thread([input, on_complete] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::string result = "processed(" + input + ")";
        on_complete(result);
    }).detach();
}

// 模拟异步写回结果
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

    // 等待异步操作完成（仅用于演示，生产代码别这么干）
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

Do you see the problem? Three levels of nested lambdas—this is the so-called **callback hell**. Every additional async step adds another level of indentation. If you have five or ten async operations, the code's readability drops drastically, and the indentation on the right runs right off the screen. Moreover, nesting doesn't just affect readability; the deeper issues lie in fragmented control flow, scattered error handling, and complex lifetime management—these are the real pain points of the callback model.

> ⚠️ This code uses `detach()` to simplify the demonstration. In production code, you should use a thread pool or `join()` to manage thread lifetimes, rather than letting threads run unmanaged.

The pain points of the callback model go far beyond "too much indentation." Let us first discuss the issue of fragmented control flow—what was originally a linear process (read, process, write) gets torn into three independent functions, where each function only knows its own piece of logic. You cannot see the order of the entire flow at a glance, because the order is hidden within the nested callback registrations. When you need to understand "how does the entire flow run," you have to start from the outermost callback and jump inward layer by layer—this is completely different from the cognitive model of reading normal sequential code.

Next is the error handling problem. Every step can fail, and the callback model has no unified error handling mechanism. You typically need to check the previous step's result in each callback, then decide whether to continue or report an error. If you have five steps, you write five pieces of error handling code, and these error handling logic blocks are also nested and fragmented. Without a centralized error handling mechanism like `try/catch`, you can only fend for yourself in each callback.

The trickiest part is actually lifetime management. A callback is a closure that captures variable references from the outer scope. What if those variables have already been destroyed by the time the callback is invoked asynchronously? Dangling references, use-after-free—these bugs are especially prone to appearing in the callback model. You also have to worry about whether a callback gets called multiple times, whether it never gets called at all, and how to propagate exceptions out of a callback—these problems simply do not exist in synchronous code, but in the callback model, you must handle them one by one.

Frankly, the callback model uses "function pointers" to express "what to do next," but a function pointer is a low-level primitive—it has no composability, no error propagation, and no resource management. This is why all languages are looking beyond callbacks for better solutions.

## Future/Promise Chains: A Step Up from Callbacks

Now that we have seen the pain points of callbacks, let us look at the second approach—the future/promise model. It is the first layer of improvement over callbacks, with the core idea being: an async operation returns a `future<T>`—a token representing "a value that will be available at some point in the future." You can block and wait for the result via `get()`, or register a follow-up operation to execute "when the value is ready" through some mechanism.

C++11 introduced `std::future` and `std::promise`, but the standard library's `std::future` has a major limitation: **it does not support `.then()` —meaning you cannot directly register a follow-up operation on a future**. If you want to implement "asynchronously read a file, then process the data," you have to manually orchestrate it:

```cpp
#include <future>
#include <iostream>
#include <string>
#include <thread>

// 模拟异步读取文件
std::future<std::string> async_read_file(const std::string& path)
{
    return std::async(std::launch::async, [&path] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return "file content from " + path;
    });
}

// 模拟异步处理数据
std::future<std::string> async_process(const std::string& input)
{
    return std::async(std::launch::async, [&input] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return "processed(" + input + ")";
    });
}

// 模拟异步写入
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

    // 第一步：异步读取文件
    std::future<std::string> f1 = async_read_file("data.txt");

    // 手动编排链式调用——等待 f1 完成，然后启动下一步
    // 注意：标准 std::future 没有 .then()，只能手动串联
    std::string content = f1.get();
    std::cout << "  [read] 读到: " << content << "\n";

    // 第二步：处理数据
    std::future<std::string> f2 = async_process(content);
    std::string processed = f2.get();
    std::cout << "  [process] 结果: " << processed << "\n";

    // 第三步：写入结果
    std::future<bool> f3 = async_write(processed);
    bool success = f3.get();
    std::cout << "  [write] 写入" << (success ? "成功" : "失败") << "\n";
    std::cout << "全部完成!\n";

    return 0;
}
```

You will notice that the nesting in this code is gone—each async step is linear: first `get()` to get the previous step's result, then start the next step. Compared to the callback model, future chains offer a clear improvement in readability: the control flow changes from a "nested callback pyramid" to a "flat linear sequence."

But the problem is also obvious: **the main thread blocks at every step**. `f1.get()` blocks until the file read completes, and `f2.get()` blocks until the processing finishes—how is this any different from synchronous code? If you want to truly achieve the effect of "the main thread doesn't block, and async steps are automatically chained," you need `.then()`—automatically invoking the registered function after the future's value becomes ready, returning a new future and forming a chain.

`std::future::then()` first appeared in C++'s Concurrency TS (Technical Specification) as part of `std::experimental::future`, and Boost.Asio's `boost::future` also implements full `.then()` support. However, the Concurrency TS was ultimately not merged into the international C++ standard—as of C++23, the standard `std::future` still lacks `.then()`. The C++ committee's stance is: rather than patching `std::future`, it is better to push the Sender/Receiver model (proposal P2300, i.e., `std::execution`, which was officially merged into the C++26 working draft at the St. Louis meeting in July 2024). So in standard C++, although `std::execution` is just around the corner, chaining with the current `std::future` remains a clumsy affair.

> ⚠️ If you need future chaining, you can refer to Boost.Asio's `boost::future::then()`, or use third-party libraries like `thousandeyes-futures`. But the standard C++ `std::future` temporarily lacks this capability.

Future/Promise chains are indeed an improvement over callbacks, but they introduce their own problems. A future itself involves heap allocation—each future internally has a shared state, used to pass values and exceptions between the write end (promise/async) and the read end (future). This shared state is typically heap-allocated, so when you chain multiple futures, you incur multiple heap allocations. Exception propagation is also not very intuitive—if a step in the chain throws an exception, the exception is caught and stored in the future's shared state, only to be re-thrown when you call `get()`. This means you must check for exceptions at every step, otherwise subsequent steps in the chain might start in an exceptional state.

## Coroutines: Writing Async Code Like Sync Code

Callbacks are too fragmented, and future chains are too clumsy—so is there a way to make async code **read exactly like synchronous code**, while executing asynchronously? In other words, the code looks like a linear flow: read file, process, write back, with no callbacks, no nesting, and no manual orchestration, but the underlying execution is automatically async?

This is the core selling point of C++20 coroutines. Let us look at the code first, and then explain what it does. The following code implements the same "read → process → write back" flow as before, but using the coroutine style.

Do not be intimidated by the amount of code—we will break it down from the start. The first piece is the `Task` struct, which defines the coroutine's return type. C++20 coroutines require the return type to internally contain a nested type called `promise_type`, which the compiler uses to customize various behavioral policies of the coroutine. You can see that `promise_type` contains several functions with fixed names: `get_return_object()` creates the Task object returned to the caller, `initial_suspend()` determines whether the coroutine suspends at the very beginning (here it returns `suspend_never`, meaning the coroutine starts executing immediately), `final_suspend()` determines the behavior after the coroutine finishes (returning `suspend_always` means the coroutine suspends there after finishing, waiting for external destruction), `return_void()` handles the case of `co_return` or normal function completion, and `unhandled_exception()` handles uncaught exceptions. These functions form the basic skeleton of the coroutine lifecycle.

Next are three awaitable types—`AsyncRead`, `AsyncProcess`, and `AsyncWrite`. Each implements three key functions: `await_ready()` returns `false` indicating "the operation is not yet complete, needs to suspend"; `await_suspend()` is called when the coroutine suspends, where we start a new thread to simulate async I/O, and the thread calls `h.resume()` to resume the coroutine upon completion; `await_resume()` is called when the coroutine resumes, and its return value becomes the result of the `co_await` expression. You will notice that each awaitable is essentially a "descriptor for an async operation"—it tells the coroutine "when the operation will be ready," "what to do when suspending," and "what result to give you when resuming."

Finally, there is the `process_file` coroutine function. Look at this code—if you ignore the `co_await` keyword, it looks no different from an ordinary synchronous function. A linear flow, stepping through one by one, no callbacks, no nesting, no `get()` blocking. But its execution is asynchronous: whenever it encounters `co_await`, the coroutine suspends, control returns to the caller, and the underlying thread can go do other things; when the async operation completes, the coroutine resumes from the suspension point and continues executing.

```cpp
#include <coroutine>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// ---- 一个最简的协程任务类型 ----

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

// ---- 模拟异步操作的 awaitable ----

struct AsyncRead
{
    std::string path;
    std::string result;

    bool await_ready() { return false; }   // 总是挂起

    void await_suspend(std::coroutine_handle<> h)
    {
        // 在新线程上模拟异步 I/O
        std::thread([this, h] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            result = "file content from " + path;
            h.resume();     // I/O 完成，恢复协程
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

// ---- 协程函数：看起来跟同步代码一模一样 ----

Task process_file(const std::string& path)
{
    std::cout << "开始处理 " << path << "...\n";

    // co_await：挂起协程，等待异步操作完成
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

    // 等待异步操作完成（演示用）
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

This is the magic of coroutines: **async code is written as straightforwardly as sync code, but the execution model is fully asynchronous**.

Looking back, C++20 introduced a total of three keywords for coroutines. `co_await expr` suspends the current coroutine, waits for the async operation represented by `expr` to complete, and the operation's result becomes the return value of the `co_await` expression—this is the one we use the most, and every async operation in the example above suspends and resumes through it. `co_yield expr` yields a value and suspends the coroutine—this is the foundation of generators, which we will see later. `co_return expr` returns a value and ends the coroutine. As long as any of these three keywords appears in the function body, the compiler treats it as a coroutine—no special function declarations or modifiers are needed. This design is indeed elegant.

> ⚠️ Coroutine return types have strict requirements: they must contain a nested `promise_type` type. The compiler uses this `promise_type` to customize various coroutine behaviors. We will dive deep into this mechanism in the next chapter.

In this example, we hand-wrote `Task`, `AsyncRead`, `AsyncProcess`, and `AsyncWrite`—these helper types make the code look substantial. But in real projects, this infrastructure is typically provided by frameworks (such as Boost.Asio's `awaitable` or cppcoro's `task`), and you only need to write the linear logic inside `process_file`. C++20 coroutines provide a language-level mechanism, while libraries are responsible for providing easy-to-use wrappers—this is a combined design of "language feature + library support."

## Comparing the Three Models

Now let us look at all three models side by side.

The callback model produces the most "fragmented" code—the linear flow is torn into nested callback functions, and the control flow is no longer a straight top-to-bottom line but jumps around following callback registration relationships. Error handling must be dealt with individually in each callback, and there is no unified exception propagation mechanism. However, callbacks themselves have almost zero runtime overhead—they are essentially just a function pointer plus a captured closure, making them the highest in performance. But debugging a callback chain is a nightmare: the call stack is broken. When something goes wrong in the fifth level of callbacks, your debugger can only see that single callback's stack frame; all the calling relationships above are lost.

Future/Promise chains are much better than callbacks in terms of readability. Through `.then()` (or manual `get()` chaining), the flow can be written as a linear chain of calls. Exceptions automatically propagate through the future's shared state—if a step throws an exception, it travels along the chain to the final `get()` call. But performance-wise, there is an overhead that cannot be ignored: every future involves a heap allocation (for the shared state), so chaining ten async operations means ten heap allocations. Debugging difficulty is moderate—at least the call stack is continuous, but future chain error messages are usually not very friendly; you see a `std::future_error` rather than which specific step in the chain failed and why.

Coroutines offer the best readability of the three models—a coroutine function looks exactly like a synchronous function, the control flow is linear, and the cognitive burden of reading and understanding it is the lowest. For error handling, you can use `try/catch`, and exceptions propagate normally within the coroutine, behaving exactly like synchronous code. Performance-wise, coroutine frames are typically heap-allocated, but the compiler can perform "coroutine elision" optimizations, embedding the frame into the caller's stack frame. Each suspension point only involves saving/restoring registers and the coroutine state, which is much lighter than a thread context switch. The debugging experience is close to that of synchronous code—the call stack is complete, and you can set a breakpoint on the `co_await` line; when the coroutine resumes execution, the debugger will correctly stop there.

But coroutines also come with their own cost—the C++20 coroutine mechanism is quite complex. The collaborative relationships among `promise_type`, `coroutine_handle`, `awaitable`, and `awaiter` require time to understand. The compiler performs extensive transformations on coroutine functions, and if something goes wrong, the error messages can be very cryptic. The good news is that once you understand this mechanism, using it feels very natural—and in the next chapter, we will dive deep into breaking down this mechanism.

## Where We Are

In this chapter, we traveled three stops along the evolutionary path of async programming. The callback model uses function pointers to express "what to do next"—simple but fragmented, with readability and maintainability dropping drastically as nesting deepens. Future/Promise chains replace nested callbacks with "value containers + chain composition," making the control flow linear, but standard C++'s `std::future` lacks `.then()` support (the Concurrency TS's `.then()` was never merged into the international standard), making chain composition still clumsy, and every future incurs a heap allocation overhead. Coroutines make async code read as straightforwardly as sync code—C++20 provides coroutine support at the language level through three keywords: `co_await`/`co_yield`/`co_return`, and the underlying suspend/resume mechanism is jointly implemented by the compiler and the promise_type.

But "looking simple" does not mean "simple underneath." The internal mechanism of C++20 coroutines is quite intricate—the compiler transforms a coroutine function into a state machine, where each `co_await` is a state transition point; `promise_type` customizes the coroutine's various behavioral policies; and `coroutine_handle` is a non-owning handle to the coroutine frame, responsible for resuming and destroying it. In the next chapter, we will tear this mechanism apart inside and out: what transformations does the compiler actually perform on a coroutine function? What is stored inside the coroutine frame? How does `coroutine_handle` manage lifetimes? We will also implement an integer generator from scratch that supports `co_yield`, tying all the concepts together.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch06-async-io-coroutine/`.

## References

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
- [Design and evolution of C++ future continuations — Ivan Krivyakov](https://ikriv.com/blog/?p=4916)
- [Concurrency TS (N4680) — ISO C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4680.pdf)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
