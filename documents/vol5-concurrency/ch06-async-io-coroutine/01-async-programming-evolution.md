---
title: "异步编程演进：从回调地狱到协程"
description: "梳理异步编程范式的演进脉络——回调、future 链、协程，理解每种模型的动机、痛点与 C++ 中的实现形态"
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
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "线程池设计"
  - "promise 与 packaged_task"
related:
  - "C++20 协程基础"
  - "异步 I/O 与事件循环"
---

# 异步编程演进：从回调地狱到协程

> 📖 **前置阅读**：这一篇会用到 C++20 协程。如果你还没接触过 `co_await`/`co_return`、`promise_type` 这些底层机制，可以先翻 [卷四·协程基础](../../vol4-advanced/01-coroutine-basics.md)——那里从零拆解了协程的"骨架"是怎么搭起来的。

说实话，写到这一篇的时候笔者是有点感慨的。我们在前面的章节里一直在跟线程、锁、原子操作打交道，这些工具给了我们精确的控制力——代价是你得自己管所有事情。线程的创建和销毁、同步机制的设计、结果从子线程搬回主线程、异常怎么传回来，每次写一个并发任务都要重复这套流程。ch05 里我们用 `std::async` 和 `std::future` 简化了一些工作，但你很快就会发现：当你需要把多个异步操作串联起来——先读文件，再解析数据，最后写回结果——future 链的管理就变得非常笨拙。

这就是异步编程要解决的核心问题：**如何优雅地组织和组合多个异步操作**。这个问题不是 C++ 独有的，几乎所有语言都在经历同样的演进——从回调（callback）到 future/promise 链，再到协程（coroutine）。这一篇我们要把这个演进脉络从头到尾理清楚，看清楚每种模型的动机是什么、解决了什么问题、又引入了什么新问题，最后理解为什么 C++20 协程被很多人认为是"异步编程的正确打开方式"。

## 环境

在动手之前，我们先把环境说清楚。本篇所有代码使用纯标准库，没有平台依赖，在 Linux、macOS、Windows 上都能跑。编译器方面，回调部分和 future 部分只需要 C++11 就够了，但协程示例需要 C++20 支持——你需要 GCC 12+、Clang 15+ 或 MSVC 19.34+ 中的一个，编译选项加上 `-std=c++20 -Wall -Wextra` 即可。说实话，C++20 协程的编译器支持在 2024 年之后已经相当成熟了，上面提到的版本都能正确编译完整的协程语言特性。不过要注意一点：标准库的 `<generator>` 是 C++23 才引入的，目前并非所有实现都完整支持，所以本篇代码里我们使用手写的 generator 类型，不依赖标准库的头文件。

## 一个场景：1000 个并发连接

我们先从一个具体的场景开始。假设你要写一个网络服务器，它需要同时处理 1000 个客户端连接。每个连接的生命周期大致是：接受连接 → 读取请求 → 处理请求 → 发送响应 → 关闭连接。在整个过程中，读和写都是 I/O 操作，而 I/O 操作是慢的——一次网络读取可能要等几毫秒甚至几百毫秒。

最直觉的做法是"一个连接一个线程"：每当有新连接进来，我们就开一个新线程专门处理它。这个方案写起来简单，但问题也很明显——1000 个连接就意味着 1000 个线程。每个线程有自己的栈（Linux 默认 8MB），光栈空间就要吃掉接近 8GB 内存。而且操作系统调度 1000 个线程的开销也不小——上下文切换、缓存失效、锁竞争，这些都会吃掉大量 CPU 时间。更关键的是，这 1000 个线程大部分时间不是在计算，而是在等 I/O——等网卡上的数据到达、等 TCP 缓冲区腾出空间。线程在等 I/O 的时候，它占用的内存和调度资源全都是浪费的。

这就是同步阻塞 I/O 的根本问题：**线程在等待 I/O 时白白占用资源，而你又不能拿这些资源去干别的事情**。

异步编程的核心思路是：不要让线程傻等。当遇到一个 I/O 操作时，先去干别的事情，等 I/O 完成了再回来继续处理。但"先去干别的事情、等会再回来"这件事，说起来简单，在代码层面怎么组织？这就是接下来我们要探讨的三种模型——回调、future 链、协程——各自给出的答案。

## 回调模型：最原始的异步

我们先从最直觉的方案开始——回调模型。它的思路很直白：当你发起一个异步操作时，同时传进去一个函数（回调），告诉系统"操作完成后帮我调用这个函数"。

我们先用一个简化的例子来感受一下。假设我们要实现"先异步读取文件内容，然后异步处理数据，最后异步写回结果"这个流程。为了不引入真正的异步 I/O 库，我们用 `std::thread` 来模拟异步操作：

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

你看到问题了吗？三层嵌套的 lambda——这就是所谓的**回调地狱（callback hell）**。每多一步异步操作，缩进就深一层，如果你有 5 步、10 步异步操作，代码的可读性会急剧下降，右边的缩进能跑到屏幕外面去。而且嵌套不仅影响可读性，更深层的问题在于控制流的碎片化、错误处理的分散以及生命周期管理的复杂——这些才是回调模型真正的痛点。

> ⚠️ 这段代码用了 `detach()` 来简化演示。在生产代码中，你应该用线程池或 `join()` 来管理线程生命周期，而不是让线程脱离管控。

回调模型的痛点远不止"缩进太深"这么简单。我们先说控制流碎片化的问题——原本一个线性流程，读、处理、写，被拆成了三个独立的函数，每个函数只知道自己的那一段逻辑。你无法一眼看出整个流程的顺序，因为顺序隐藏在嵌套的回调注册里。当你需要理解"整个流程是怎么跑的"时，你得从最外层的回调开始，一层一层往里跳——这跟阅读普通顺序代码的认知模式完全不同。

紧接着是错误处理的问题。每一步操作都可能失败，而回调模型没有统一的错误处理机制。你通常需要在每个回调里检查上一步的结果，然后决定是继续还是报错。如果有 5 步操作，你就要写 5 段错误处理代码，而且这些错误处理逻辑也是嵌套的、碎片化的。没有 `try/catch` 那种集中式的错误处理手段，你只能在每个回调里各自为战。

最棘手的其实是生命周期管理。回调是一个闭包，它捕获了外层作用域的变量引用。如果回调被异步调用时，那些变量已经失效了呢？悬垂引用、use-after-free，这些 bug 在回调模型里特别容易出现。你还得操心回调是不是被调了多次、是不是根本没被调，以及异常怎么从回调里传出来——这些问题在同步代码里根本不存在，但在回调模型里你必须逐个处理。

说白了，回调模型用"函数指针"来表达"接下来做什么"，但函数指针是一个低层原语——它没有组合性、没有错误传播、没有资源管理。这就是为什么所有语言都在往回调之外寻找更好的方案。

## Future/Promise 链：比回调好一点

回调的痛点看清楚了，接下来我们看第二种方案——future/promise 模型。它是对回调的第一层改进，核心思想是：异步操作返回一个 `future<T>`——一个代表"未来某个时刻会有的值"的凭证。你可以通过 `get()` 阻塞等待结果，或者用某种方式注册"值就绪后执行"的后续操作。

C++11 引入了 `std::future` 和 `std::promise`，但标准库的 `std::future` 有一个很大的局限：**它不支持 `.then()` ——也就是说，你不能直接在一个 future 上注册后续操作**。如果你想实现"先异步读文件，然后处理数据"，你得手动编排：

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

你会发现，这段代码的嵌套消失了——每个异步步骤都是线性的，先 `get()` 拿到上一步的结果，再启动下一步。相比回调模型，future 链在可读性上有明显提升：控制流从"嵌套的回调金字塔"变成了"扁平的线性序列"。

但问题也很明显：**主线程在每一步都阻塞了**。`f1.get()` 会阻塞到读文件完成，`f2.get()` 会阻塞到处理完成——这跟同步代码有什么区别？如果你想真正实现"主线程不阻塞、异步步骤自动串联"的效果，你需要 `.then()` 这个东西——在 future 的值就绪后自动调用注册的函数，返回一个新的 future，形成链式调用。

`std::future::then()` 最早出现在 C++ 的 Concurrency TS（技术规范）中，作为 `std::experimental::future` 的一部分，Boost.Asio 的 `boost::future` 也实现了完整的 `.then()` 支持。但 Concurrency TS 最终没有被合并到 C++ 国际标准中——到 C++23 为止，标准 `std::future` 依然没有 `.then()`。C++ 委员会的态度是：与其在 `std::future` 上打补丁，不如推 Sender/Receiver 模型（P2300 提案，即 `std::execution`，已在 2024 年 7 月的 St. Louis 会议上正式并入 C++26 工作草案）。所以在标准 C++ 中，虽然 `std::execution` 即将到来，但目前的 `std::future` 链式组合仍然是一件笨拙的事情。

> ⚠️ 如果你需要 future 链式组合，可以参考 Boost.Asio 的 `boost::future::then()`，或者使用第三方库如 `thousandeyes-futures`。但标准 C++ 中的 `std::future` 暂时没有这个能力。

Future/Promise 链相比回调确实有进步，但它也引入了自己的问题。future 本身涉及堆分配——每个 future 内部都有一个共享状态（shared state），用于在写端（promise/async）和读端（future）之间传递值和异常。这个共享状态通常是堆分配的，当你链接多个 future 时，就会有多次堆分配。异常的传播也不是很直观——如果链中的某一步抛了异常，异常会被捕获并存储在 future 的共享状态里，直到你调用 `get()` 时才重新抛出。这意味着你必须在每一步都检查异常，否则链的后续步骤可能会在异常状态下启动。

## 协程：异步代码写得像同步代码一样

回调太碎片，future 链又太笨拙，那有没有一种方式，让异步代码的**写法跟同步代码一模一样**，但执行是异步的？也就是说，代码看起来就是一个线性的流程：读文件、处理、写回，没有任何回调、没有任何嵌套、没有任何手动编排，但底层自动实现了异步执行？

这就是 C++20 协程的核心卖点。我们先直接看代码，然后再解释它做了什么。下面这段代码实现了和前面一样的"读取 → 处理 → 写回"流程，但使用协程风格。

先别被代码量吓到——我们从头开始拆。第一块是 `Task` 结构体，它定义了协程的返回类型。C++20 协程要求返回类型内部必须包含一个叫 `promise_type` 的嵌套类型，编译器通过这个类型来定制协程的各种行为策略。你看到 `promise_type` 里面有几个固定名字的函数：`get_return_object()` 创建返回给调用者的 Task 对象，`initial_suspend()` 决定协程是否在开始时就挂起（这里返回 `suspend_never`，意味着协程立即开始执行），`final_suspend()` 决定协程结束后的行为（返回 `suspend_always`，意味着协程结束后挂在那里等外部销毁），`return_void()` 处理 `co_return` 或函数正常结束的情况，`unhandled_exception()` 处理未捕获的异常。这几个函数构成了协程生命周期的基础骨架。

接下来是三个 awaitable 类型——`AsyncRead`、`AsyncProcess`、`AsyncWrite`。它们各自实现了三个关键函数：`await_ready()` 返回 `false` 表示"操作还没完成，需要挂起"；`await_suspend()` 在协程挂起时被调用，在这里我们启动一个新线程来模拟异步 I/O，线程完成后调用 `h.resume()` 恢复协程；`await_resume()` 在协程恢复时被调用，它的返回值就是 `co_await` 表达式的结果。你会发现，每个 awaitable 实际上就是一个"异步操作的描述器"——它告诉协程"操作什么时候就绪"、"挂起时该干什么"、"恢复时给你什么结果"。

最后是 `process_file` 这个协程函数。你看这段代码——如果不看 `co_await` 关键字，它看起来跟一个普通的同步函数没有任何区别。线性流程，一步一步往下走，没有回调，没有嵌套，没有 `get()` 阻塞。但它的执行是异步的：每当遇到 `co_await`，协程就挂起（suspend），控制权交还给调用者，底层的线程可以去干别的事情；当异步操作完成后，协程从挂起点恢复（resume），继续往下执行。

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

这就是协程的魔法：**异步代码写起来像同步代码一样直白，但执行模型是完全异步的**。

回头看看，C++20 为协程一共引入了三个关键字。`co_await expr` 挂起当前协程，等待 `expr` 代表的异步操作完成，操作的结果作为 `co_await` 表达式的返回值——这是我们用得最多的，上面的示例里每一处异步操作都是通过它来挂起和恢复的。`co_yield expr` 产出一个值并挂起协程——这是生成器（generator）的基础，我们后面会看到。`co_return expr` 返回一个值并结束协程。只要函数体内出现了这三个关键字中的任何一个，编译器就会把它当作协程来处理——不需要特殊的函数声明或修饰符，这个设计确实很优雅。

> ⚠️ 协程的返回类型有严格要求：必须包含一个嵌套的 `promise_type` 类型。编译器通过这个 `promise_type` 来定制协程的各种行为。这部分机制我们会在下一篇深入拆解。

这个示例里我们手写了 `Task`、`AsyncRead`、`AsyncProcess`、`AsyncWrite` 这些辅助类型，看起来代码量不小。但在实际项目中，这些基础设施通常由框架提供（比如 Boost.Asio 的 `awaitable`、cppcoro 的 `task`），你只需要写 `process_file` 里面那段线性逻辑就行了。C++20 协程提供的是语言层面的机制，库负责提供易用的包装——这是一个"语言特性 + 库支持"的组合设计。

## 三种模型的对比

现在我们把三种模型放在一起来看。

回调模型的代码是最"碎片化"的——线性流程被拆成嵌套的回调函数，控制流不再是从上到下的直线，而是跟着回调注册关系跳跃，错误处理需要在每个回调里单独处理，没有统一的异常传播机制。不过回调本身几乎没有运行时开销——它本质上就是一个函数指针加上捕获的闭包，性能是最高的。但调试回调链是一场噩梦：调用栈是断开的，当第 5 层回调出了问题，你的调试器只能看到那一个回调的栈帧，上面的调用关系全丢了。

Future/Promise 链在可读性上比回调好得多，通过 `.then()`（或者手动 `get()` 串联），流程可以写成线性的链式调用。异常通过 future 的共享状态自动传播——如果某一步抛了异常，它会沿着链传到最终的 `get()` 调用。但性能方面有一个不可忽视的开销：每个 future 都涉及一次堆分配（共享状态），当你链接 10 个异步操作时就是 10 次堆分配。调试难度适中——至少调用栈是连续的，但 future 链的错误信息通常不太友好，你看到的是一个 `std::future_error`，而不是链中具体哪一步出了什么问题。

协程在可读性上是三种模型里最好的——协程函数看起来跟同步函数一模一样，控制流是线性的，阅读和理解的认知负担最低。错误处理可以用 `try/catch`，异常会在协程内部正常传播，跟同步代码的行为完全一致。性能方面，协程帧通常是堆分配的，但编译器可以做"协程帧省略"（coroutine elision）优化，把帧嵌入调用者的栈帧。每个挂起点只涉及保存/恢复寄存器和协程状态，比线程上下文切换轻量得多。调试体验接近同步代码——调用栈是完整的，你可以在 `co_await` 那一行打断点，协程恢复执行时调试器会正确地停在那里。

但协程也有自己的代价——C++20 协程的机制相当复杂。`promise_type`、`coroutine_handle`、`awaitable`、`awaiter`，这些概念之间的协作关系需要你花时间理解。编译器对协程函数做了大量的变换，如果出了问题，错误信息可能非常晦涩。好消息是，一旦你理解了这套机制，使用起来是非常自然的——下一篇我们就要深入拆解这套机制。

## 我们的位置

这一篇我们沿着异步编程的演进脉络走了三站。回调模型用函数指针表达"接下来做什么"，简单但碎片化，嵌套深了之后可读性和可维护性急剧下降。Future/Promise 链用"值容器 + 链式组合"替代了嵌套回调，控制流变线性了，但标准 C++ 的 `std::future` 缺少 `.then()` 支持（Concurrency TS 的 `.then()` 始终未被合并到国际标准），链式组合仍然笨拙，而且每个 future 都有一次堆分配的开销。协程让异步代码写起来跟同步代码一样直白——C++20 通过 `co_await`/`co_yield`/`co_return` 三个关键字在语言层面提供了协程支持，底层的挂起/恢复机制由编译器和 promise_type 共同实现。

但"看起来简单"不代表"背后简单"。C++20 协程的内部机制是相当精巧的——编译器会把协程函数变换成一个状态机，每个 `co_await` 都是一个状态切换点；`promise_type` 定制了协程的各种行为策略；`coroutine_handle` 是协程帧的非拥有句柄，负责恢复和销毁。下一篇我们就要把这套机制从里到外拆清楚：编译器到底对协程函数做了什么变换？协程帧里存了什么？`coroutine_handle` 怎么管理生命周期？我们还会从零实现一个可以 `co_yield` 整数的 generator，把所有概念串起来。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch06-async-io-coroutine/`。

## 参考资源

- [Coroutines (C++20) — cppreference](https://en.cppreference.com/cpp/language/coroutines)
- [Understanding the Compiler Transform — Lewis Baker](https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform)
- [C++ Coroutines: Understanding operator co_await — Lewis Baker](https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await)
- [Coroutine changes for C++20 and beyond (WG21 P1745R0)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1745r0.pdf)
- [Design and evolution of C++ future continuations — Ivan Krivyakov](https://ikriv.com/blog/?p=4916)
- [Concurrency TS (N4680) — ISO C++](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/n4680.pdf)
- [My tutorial and take on C++20 coroutines — Dima Korolev (Stanford)](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)
- [Writing custom C++20 coroutine systems — Simon Tatham](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
