---
chapter: 5
cpp_standard:
- 20
description: C++20 的自动 join 线程与协作式取消机制：stop_source、stop_token、stop_callback 的完整用法
difficulty: intermediate
order: 3
platform: host
prerequisites:
- promise 与 packaged_task
reading_time_minutes: 18
related:
- 线程所有权与 RAII
- 线程池设计
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- RAII守卫
- 进阶
title: jthread 与停止令牌
---
# jthread 与停止令牌

说实话，笔者在写前面几篇的时候，用 `std::thread` 用得挺心虚的。每次都要手动 `join()`，稍不留神就 `std::terminate()` 收场，想中途停掉一个线程还得自己搞 `std::atomic<bool>` 的标志位——这都 2026 年了，C++ 的线程管理居然还这么"原始"。上一篇我们用 `std::promise` 和 `std::packaged_task` 构建了手动控制异步任务的能力，但底层的线程工具一直没升级，所以这一篇我们就要把这块短板补上。

先别急，在进入正文之前，说一下环境：本篇所有代码基于 **C++20**，需要编译器支持 `<stop_token>` 头文件（GCC 10+、Clang 17+（libc++ 部分支持，20 完整支持）、MSVC 19.28+ 均可）。如果你的编译器还不够新，赶紧升级——这篇的东西没得降级替代。

C++20 终于给了我们 `std::jthread`，一个自动 join 的线程包装器，同时内建了一套协作式取消机制（cooperative cancellation）。这套机制的核心组件是三个类：`std::stop_source`（发出停止请求）、`std::stop_token`（检查停止请求）、`std::stop_callback`（注册停止回调）。它们不依赖 `std::jthread` 也能独立使用，但和 `std::jthread` 搭配起来最为方便。这一篇我们就把这套工具完整地过一遍。

## std::thread 的痛点：回顾

在动手学新东西之前，我们先回头看看 `std::thread` 到底有哪些让人头疼的问题。只有理解了痛点，才能明白 C++20 为什么要这样设计。

先看一个典型的问题场景。下面的代码乍一看没什么毛病——创建线程，干活，join，完事。

```cpp
#include <thread>

void worker();
void do_more_work();

void unsafe_example()
{
    std::thread t(worker);
    do_more_work();  // 如果这里抛异常...
    t.join();        // 这行不会执行
    // t 析构，线程仍然 joinable -> std::terminate()!
}
```

但如果 `do_more_work()` 抛出异常了呢？程序的控制流直接跳到栈展开，`t` 析构时发现线程还是 joinable 的，于是 `std::terminate()` 毫不客气地把整个进程干掉。没有错误信息，没有回旋余地，直接崩。你可能觉得"那我加个 try-catch 不就好了？"——可以，但每个使用 `std::thread` 的地方都要这么搞，遗漏了就是定时炸弹。

一个常见的修复方案是手写 RAII 包装器，在析构函数里自动 join。我们在 ch01 那篇其实就做过这件事。但每个项目都得自己写一份，而且析构时的 `join()` 是阻塞调用——如果线程在跑一个很长的任务，你析构 guard 的时候整个程序就卡在那了，还没有任何方式通知线程"该停了"。

这两个问题——忘了 join 就炸、没法通知线程停下来——就是 `std::jthread` 要一次性解决的。

## std::jthread：自动 join 的线程

好，现在来看 `std::jthread`。它的 `j` 代表 joining——名字就已经告诉你它的核心卖点了：析构时自动 join。用法和 `std::thread` 几乎一模一样，你几乎可以无脑替换：

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void worker()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "worker done\n";
}

int main()
{
    std::jthread t(worker);
    // 不需要手动 join —— t 析构时自动 join
    return 0;
}
```

你会发现，这段代码和用 `std::thread` 唯一的区别就是把 `std::thread` 换成了 `std::jthread`，然后删掉了那行 `t.join()`。但仅仅是自动 join 的话，那和我们手写的 RAII guard 没有本质区别——`std::jthread` 真正的杀手锏在析构行为里：在 join 之前，它会**先调用 `request_stop()`**，然后才 `join()`。伪代码大概是这样：

```cpp
// std::jthread 析构函数的逻辑（简化）
~jthread()
{
    if (joinable()) {
        request_stop();
        join();
    }
}
```

也就是说，`std::jthread` 不只是在析构时傻等线程结束，它会先礼貌地通知线程"该停了"，然后再等。线程函数如果能响应这个停止请求，就可以优雅退出，而不是让调用方在析构时无限期阻塞。这一点真的非常重要——如果你用过 Java 的 `Thread.interrupt()` 或者 Go 的 `context.Cancel()`，你会发现 C++20 这套设计的思路如出一辙：不强制杀死，而是协作退出。

> **踩坑预警**：如果你在 ch01 里已经手写了一个 `thread_guard` 或 `joining_thread` 之类的 RAII 包装器，请注意——那些手写的 guard 析构时只做 `join()`，不会做 `request_stop()`。如果你的线程函数内部有长时间阻塞的操作（比如 `sleep`、条件变量等待），手写的 guard 会让析构一直阻塞。`std::jthread` 的 `request_stop()` + `join()` 组合才是正确做法。

## 协作式取消：stop_source、stop_token、stop_callback

很好，现在我们已经知道 `std::jthread` 会自动 `request_stop()` 了。但"请求停止"到底是什么意思？线程怎么知道被请求了？这就是协作式取消要解决的问题。

核心思想其实很朴素：你不应该"杀死"一个线程——因为你不知道它处于什么状态，它可能正持有一把锁，可能刚写了一半数据——你应该"请求"它停止，然后由线程自己决定在合适的时机退出。你可以把它理解为一种信号机制：有人举了红旗说"请停止"，线程在每次循环开始时看一眼红旗，如果举起来了就优雅退出。这套机制由三个类组成，它们共享一个内部的停止状态（stop-state）。`std::stop_source` 是写端，负责发出停止请求；`std::stop_token` 是读端，负责查询停止状态；`std::stop_callback` 可以在停止请求发出时执行一段回调代码。

### std::stop_source 与 std::stop_token

我们先从写端和读端开始。`std::stop_source` 提供了 `request_stop()` 用于发出停止请求，`get_token()` 用于获取关联的 `std::stop_token`。`std::stop_token` 是只读的观察者，只有两个查询方法：`stop_requested()` 返回是否已经收到停止请求，`stop_possible()` 返回是否有关联的停止状态。一个 `stop_source` 可以派生出多个 `stop_token`——这一点后面会用到，它意味着你可以用同一个 `stop_source` 同时控制多个线程的停止。

```cpp
#include <stop_token>
#include <iostream>

int main()
{
    std::stop_source source;
    std::stop_token token = source.get_token();

    std::cout << source.stop_requested() << "\n";  // 0
    std::cout << token.stop_requested() << "\n";   // 0

    source.request_stop();

    std::cout << source.stop_requested() << "\n";  // 1
    std::cout << token.stop_requested() << "\n";   // 1
    // request_stop() 可以多次调用，只有第一次返回 true

    return 0;
}
```

这个例子展示了最基本的一对一关系：一个 `stop_source` 发出请求，它关联的 `stop_token` 立刻就能查到。值得注意的是 `request_stop()` 可以多次调用，只有第一次返回 `true`——后续调用是安全的但不会重复触发回调。

默认构造的 `std::stop_token` 没有关联任何停止状态，`stop_possible()` 返回 `false`。如果你确实不需要停止能力，可以用 `std::nostopstate` 构造一个空的 `std::stop_source`，这样它不会分配任何内部状态，省一点开销。

### std::jthread 如何传递 stop_token

接下来问题来了：`std::jthread` 内部那个 `stop_source` 是怎么和我们的线程函数沟通的？答案是——如果你的线程函数接受一个 `std::stop_token` 作为第一个参数，`std::jthread` 会自动把内部 token 传进去；如果函数不接受 `stop_token`，`std::jthread` 就退化成一个普通的自动 join 线程，没有任何取消能力。这个设计很聪明——向后兼容，你想用就用，不想用也完全不影响。

```cpp
#include <thread>
#include <stop_token>
#include <iostream>
#include <chrono>

void cancellable_worker(std::stop_token token)
{
    while (!token.stop_requested()) {
        std::cout << "working...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "worker: stop requested, exiting\n";
}

int main()
{
    std::jthread t(cancellable_worker);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    t.request_stop();
    // t 析构时：先 request_stop()，再 join()
    return 0;
}
```

你会发现这段代码里我们根本没手动调 `join()`——`t` 析构时会自动先 `request_stop()` 再 `join()`。`request_stop()` 也是 `std::jthread` 的成员函数，底层调用它内部那个 `stop_source` 的 `request_stop()`。你也可以通过 `t.get_stop_source()` 拿到内部的 `stop_source` 来做更精细的控制，比如注册额外的回调或者把 token 传给别的组件。

### std::stop_callback：注册停止回调

光能检查停止标志还不够——有时候你希望在停止请求发出的瞬间执行一些清理操作，比如关闭文件句柄、释放网络连接、设置某个标志位。`std::stop_callback` 就是干这个的：构造函数接受一个 `std::stop_token` 和一个可调用对象，当关联的 `stop_source` 调用 `request_stop()` 时，回调就会被触发。

```cpp
#include <stop_token>
#include <iostream>
#include <thread>
#include <chrono>

void worker(std::stop_token token)
{
    int counter = 0;
    std::stop_callback cb(token, [&counter]() {
        std::cout << "stop callback fired! counter was: "
                  << counter << "\n";
    });

    while (!token.stop_requested()) {
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "worker exiting\n";
}

int main()
{
    std::jthread t(worker);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();
    return 0;
}
```

这段代码跑起来之后你会看到类似这样的输出：先是一秒的 `working...` 循环，然后 `request_stop()` 触发回调打印 `stop callback fired!`，最后工作线程检测到 `stop_requested()` 退出循环。

这里有几个细节需要留心。首先，回调是在调用 `request_stop()` 的线程上**同步执行**的，不是在工作线程上——所以千万别在回调里做耗时操作，否则会阻塞发出停止请求的那个线程。其次，如果你注册回调的时候停止请求已经发出了，回调会立即在注册线程上执行，不会错过。最后，`std::stop_callback` 的析构函数会自动取消注册，所以当 `worker` 函数结束时 `cb` 析构，不用担心悬空回调的问题。

## 协作式取消的实战模式

到这里我们已经把 API 层面的东西理清楚了。但 API 只是工具，真正重要的是怎么在实际场景里用好它们。接下来我们看三种常见的取消模式——从简单到复杂，每一种都有它适用的场景。

### 模式一：循环中轮询 stop_token

最简单的模式就是在循环条件里检查 `stop_token`。如果每次迭代时间短（毫秒级别），直接在 `while` 条件里检查就够了；但如果每次迭代要跑好几秒，你需要在迭代内部也插入检查点，否则停止请求来了还得等当前迭代跑完才能响应。来看代码：

```cpp
void polling_worker(std::stop_token token)
{
    int iteration = 0;
    while (!token.stop_requested()) {
        process_batch(iteration);
        ++iteration;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "processed " << iteration << " batches\n";
}
```

### 模式二：condition_variable + stop_token

纯粹的轮询模式有个问题——很多工作线程不是在循环里忙等，而是等在条件变量上。这时候 `stop_token` 的单纯轮询就不够用了，因为线程可能在 `cv.wait()` 上阻塞着，根本没机会检查停止标志。C++20 给 `std::condition_variable_any` 新增了接受 `std::stop_token` 的 `wait` 重载——当停止请求发出时，等待会自动被唤醒，`wait` 返回 `false` 表示是被停止信号唤醒的而非谓词满足。

> **踩坑预警**：注意是 `condition_variable_any` 而不是 `condition_variable`。标准委员会只给前者加了 `stop_token` 重载，后者不支持。如果你手头的代码已经在用 `condition_variable`，要么换成 `condition_variable_any`，要么用后面提到的 `stop_callback` 手动 `notify`。

```cpp
#include <thread>
#include <stop_token>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <iostream>
#include <chrono>

class TaskWorker
{
public:
    TaskWorker()
        : thread_([this](std::stop_token token) { run(token); })
    {}

    void submit(int task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(task);
        }
        cv_.notify_one();
    }

private:
    void run(std::stop_token token)
    {
        while (!token.stop_requested()) {
            int task = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // 返回 false 表示被停止请求唤醒
                if (!cv_.wait(lock, token,
                              [this] { return !tasks_.empty(); })) {
                    drain_queue();
                    break;
                }
                task = tasks_.front();
                tasks_.pop();
            }
            std::cout << "processing task: " << task << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    void drain_queue()
    {
        while (!tasks_.empty()) {
            int task = tasks_.front();
            tasks_.pop();
            std::cout << "draining task: " << task << "\n";
        }
    }

    std::mutex mutex_;
    std::queue<int> tasks_;
    std::condition_variable_any cv_;
    std::jthread thread_;
};
```

这段代码的逻辑其实很直白：工作线程在 `cv_.wait(lock, token, predicate)` 上等待，当有任务时取出执行，当收到停止请求时 `wait` 返回 `false`，线程 `drain_queue()` 把剩余任务处理完然后退出。`condition_variable_any` 内部就是用 `stop_callback` 帮你做了 `notify`——如果你必须用 `condition_variable`（不是 `_any`），那就得手动注册一个回调来 `notify_all()`，效果一样但代码会更啰嗦。

### 模式三：用 stop_source 控制一组线程

前面两种模式都是一对一——一个线程、一个停止信号。但实际工程中更常见的是一对多：你有好几个工作线程，希望一个按钮就能把所有线程一起停掉。这就用到了 `stop_source` 派生多个 `stop_token` 的能力。

```cpp
#include <stop_token>
#include <thread>
#include <iostream>
#include <chrono>

void data_processor(std::stop_token token, int id)
{
    while (!token.stop_requested()) {
        std::cout << "processor " << id << " working\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::cout << "processor " << id << " stopped\n";
}

int main()
{
    std::stop_source source;
    std::thread p1(data_processor, source.get_token(), 1);
    std::thread p2(data_processor, source.get_token(), 2);
    std::thread p3(data_processor, source.get_token(), 3);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    source.request_stop();  // 一次调用停止所有三个线程

    p1.join();
    p2.join();
    p3.join();
    return 0;
}
```

这里故意用了 `std::thread` 而不是 `std::jthread`，目的是展示 `stop_source` 和 `stop_token` 可以完全独立于 `std::jthread` 使用——你甚至可以在没有线程的场合用它来控制异步任务的取消。在实际项目中，用 `std::stop_source` 做一对多的停止控制比给每个线程单独设一个 `std::atomic<bool>` 要干净得多，也避免了手动管理多个标志位的同步问题。

## 将停止令牌集成到线程池

真正的坑在后面——前面三种模式都是独立的场景，但在一个真实的线程池里，你需要同时处理任务队列、条件变量、多个工作线程的停止，而且还要确保析构的时候不会死锁、不会丢任务。用 `stop_source` 和 `stop_token` 可以非常优雅地把这些事情统一管理起来。我们来看一个简化但完整的实现：

```cpp
#include <thread>
#include <stop_token>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <functional>
#include <vector>
#include <iostream>

class SimpleThreadPool
{
public:
    explicit SimpleThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back(
                [this, token = stop_source_.get_token()]() {
                    worker_loop(token);
                });
        }
    }

    ~SimpleThreadPool()
    {
        stop_source_.request_stop();
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    void submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_loop(std::stop_token token)
    {
        while (!token.stop_requested()) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!cv_.wait(lock, token,
                              [this] { return !tasks_.empty(); })) {
                    break;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
    std::condition_variable_any cv_;
    std::stop_source stop_source_;
    std::vector<std::jthread> workers_;
};
```

我们来拆解一下这段代码的设计思路。

首先看构造函数——我们用了一个独立的 `std::stop_source`（成员变量 `stop_source_`），而不是依赖 `std::jthread` 内部的那个。在 lambda 捕获列表中通过 `token = stop_source_.get_token()` 把同一个 token 传递给每个工作线程。这样做的原因是所有工作线程必须共享同一个停止信号——如果每个 `jthread` 用自己的 `stop_source`，你就得逐个调用 `request_stop()`，麻烦且容易遗漏。

接下来看析构函数——先调用 `stop_source_.request_stop()`，再 `cv_.notify_all()`，最后逐个 `join()`。你可能会问，既然 `request_stop()` 会触发 `condition_variable_any` 的 `wait` 返回，为什么还要额外 `notify_all()`？确实，理论上 `request_stop()` 就够了，但显式 `notify_all()` 是更明确的意图表达，也能确保不依赖特定实现的时序——万一 `request_stop()` 和 `wait` 之间有竞态呢？多写一行 `notify_all()` 换来确定性，值得。

最后说一个容易混淆的点：由于 lambda 没有接受 `std::stop_token` 参数，`std::jthread` 的内部 `stop_source` 在这里不会被使用。`jthread` 的析构仍然会做 `request_stop()` + `join()`，但它内部的 `request_stop()` 影响的是 `jthread` 自己的 `stop_source`，和我们传给 `worker_loop` 的那个 token 完全无关。真正控制工作线程退出的是我们在析构函数开头手动调用的 `stop_source_.request_stop()`。

## 我们的位置

这一篇我们从 `std::thread` 的痛点出发，一路走过了 `std::jthread` 的自动 join 语义、`stop_source`/`stop_token`/`stop_callback` 的协作式取消机制，最后把它们全部串起来用在线程池里。回头看，C++20 这套设计思路其实很简单——不要强制杀死线程，而是给它发个信号让它自己优雅退出。但在简单的设计背后，它解决了 `std::thread` 时代我们最头疼的两个问题：忘了 join 就炸，以及没法通知线程停下来。

下一篇我们要把这些工具整合起来，构建一个更完整的线程池——带任务优先级、动态线程数、工作窃取（work stealing）。有了 `jthread` 和停止令牌的基础，后面的事情会顺手很多。先正确性，再性能，这个原则一直没变。

## 练习

### 练习 1：带有停止令牌的可中断工作线程

实现一个 `InterruptibleWorker` 类，它内部运行一个工作线程，每隔 500ms 打印一次当前时间。要求使用 `std::jthread` 和 `std::stop_token`，线程在收到停止请求后打印 "shutting down" 然后退出，用 `std::stop_callback` 注册一个回调在停止时打印 "cleanup callback executed"。在 `main()` 中创建 worker，运行 3 秒后通过 `request_stop()` 停止它。提示：`std::stop_callback` 的回调在调用 `request_stop()` 的线程上执行，不要在回调里做耗时操作。

### 练习 2：改造线程池

基于上面 `SimpleThreadPool` 的代码，做以下改进：在析构时先清空队列中未执行的任务（打印被丢弃的任务编号），然后再停止工作线程；添加一个 `size()` 方法返回队列中当前等待的任务数量；用 `std::stop_callback` 替代手动 `notify_all` 的调用——在工作线程的循环开始前注册一个回调来通知条件变量。提示：思考 `std::stop_callback` 的生命周期——它需要在整个 `worker_loop` 期间保持有效。

### 练习 3：多 stop_source 的组合

假设你有两组工作线程，每组有自己的 `std::stop_source`。设计一个机制使得可以单独停止某一组、可以同时停止所有线程，并且停止请求是单向的。提示：可以为每组保留各自的 `std::stop_source`，再额外维护一个"全局"的 `std::stop_source`。工作线程需要同时检查两个 token——当任一 token 收到停止请求时退出。`std::stop_token` 本身没有"组合"操作，所以你可能需要在循环条件中检查 `token_a.stop_requested() || token_b.stop_requested()`。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch05-future-task-threadpool/`。

## 参考资源

- [std::jthread -- cppreference](https://en.cppreference.com/cpp/thread/jthread)
- [std::stop_token -- cppreference](https://en.cppreference.com/cpp/thread/stop_token)
- [std::stop_source -- cppreference](https://en.cppreference.com/cpp/thread/stop_source)
- [std::stop_callback -- cppreference](https://en.cppreference.com/cpp/thread/stop_callback)
- [std::condition_variable_any::wait -- cppreference](https://en.cppreference.com/cpp/thread/condition_variable_any/wait)
- [std::jthread and cooperative cancellation with stop token -- nextptr](https://www.nextptr.com/tutorial/ta1588653702/stdjthread-and-cooperative-cancellation-with-stop-token)
- [Cooperative Interruption of a Thread in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/cooperative-interruption-of-a-thread-in-c20/)
- [Better worker threads with C++23 cooperative thread interruption -- twdev.blog](https://twdev.blog/2023/06/stop_source/)
- [Interrupt Politely -- Herb Sutter](https://www.drdobbs.com/interrupt-politely/225700115)
