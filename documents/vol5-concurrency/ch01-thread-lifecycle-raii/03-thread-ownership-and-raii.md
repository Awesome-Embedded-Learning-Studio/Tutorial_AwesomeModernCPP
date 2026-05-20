---
title: "线程所有权与 RAII"
description: "用 RAII 包装 std::thread，实现异常安全的 joining_thread guard 与作用域退出清理"
chapter: 1
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - RAII
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "线程参数与生命周期"
related:
  - "mutex 与 RAII 锁"
  - "jthread 与停止令牌"
---

# 线程所有权与 RAII

上一篇我们搞清楚了 `std::thread` 的参数传递和生命周期管理，知道了一个 `std::thread` 对象在销毁之前必须被 `join()` 或 `detach()`，否则程序会直接 `std::terminate()`。但说实话，每次手动调 `join()` 是一件很烦的事情——不是因为它难，而是因为它太容易忘了。特别是在有异常抛出的代码路径里，你可能在一个函数中间就跳出去了，后面的 `join()` 根本执行不到。更糟糕的是，如果你的函数有多个 `return` 路径，每一条都得记得 `join()`，漏一个就是定时炸弹。

这一篇我们要做的事情很简单：用 RAII 把 `std::thread` 包起来，让资源管理变成自动的。我们会从 `std::thread` 的 move 语义聊起，搞清楚"线程所有权"到底意味着什么，然后一步步实现 `thread_guard` 和 `joining_thread`——后者其实就是 C++20 `std::jthread` 的前身。最后我们会讨论异常安全、容器中的线程管理，以及一个很实用的练习。

## std::thread 是 move-only 的

先来搞清楚一个基本事实：`std::thread` 是不可复制的。你不能把一个线程对象赋值给另一个，也不能通过值传递来转移它。原因很简单——一个操作系统线程在同一时刻只能被一个 `std::thread` 对象管理。如果允许复制，就会出现两个对象试图 `join()` 同一个底层线程的情况，这在语义上是无法定义的。

所以 `std::thread` 只支持 move 语义。当你把一个 `std::thread` 对象 move 给另一个对象时，底层线程的所有权就从源对象转移到了目标对象，源对象变成"空"的（`joinable() == false`）。这个过程可以用一个最简单的例子来验证：

```cpp
#include <thread>
#include <iostream>

void worker()
{
    std::cout << "Worker thread running\n";
}

int main()
{
    std::thread t1(worker);
    std::cout << "t1 joinable: " << t1.joinable() << "\n";  // true

    std::thread t2 = std::move(t1);  // 所有权转移
    std::cout << "t1 joinable after move: " << t1.joinable() << "\n";  // false
    std::cout << "t2 joinable after move: " << t2.joinable() << "\n";  // true

    t2.join();  // 现在是 t2 负责管理线程
    return 0;
}
```

你会发现，move 之后 `t1` 就不再管理任何线程了——它变成一个"空壳"。所有关于这个线程的操作（`join()`、`detach()`）都必须通过 `t2` 来进行。这个 move-only 的设计确保了任何时刻都只有一个对象拥有对底层线程的控制权，从根本上杜绝了"两个对象 join 同一个线程"的混乱局面。

这种"唯一所有者"的语义跟 `std::unique_ptr` 非常相似——`unique_ptr` 也是不可复制、只可移动的，move 之后源指针变成 `nullptr`。实际上，C++ 标准库里有不少资源管理类型都采用了这种模式：`std::fstream`、`std::unique_lock`、`std::future`，它们都是 move-only 的。这不是巧合，而是 RAII 设计哲学的直接体现——资源的生命周期由一个唯一的所有者来管理，所有者被销毁时自动释放资源。

### 函数返回 std::thread

move 语义的一个很实用的场景是从函数中返回 `std::thread` 对象。因为在 C++ 中，函数返回值是被优化过的（RVO/NRVO），即使 `std::thread` 不可复制，返回一个 `std::thread` 也是完全合法的：

```cpp
#include <thread>
#include <iostream>

void background_task(int id)
{
    std::cout << "Background task " << id << " running\n";
}

std::thread make_worker(int id)
{
    return std::thread(background_task, id);
    // 或者更明确地写：
    // std::thread t(background_task, id);
    // return t;  // 隐式 move 或 NRVO
}

int main()
{
    std::thread t = make_worker(42);
    t.join();
    return 0;
}
```

这里 `make_worker` 返回的 `std::thread` 对象通过 move（或者 NRVO 优化直接在调用方的栈上构造）传递给 `main` 中的 `t`，线程的所有权从函数内部转移到了调用方。这种模式在创建线程池、任务调度器等场景中非常常见——工厂函数负责创建线程，调用方负责管理它的生命周期。

## 线程所有权语义：谁负责 join/detach

上一篇文章我们说过，`std::thread` 的析构函数会调用 `std::terminate()`——如果线程仍然是 `joinable()` 的话。这个设计是有意为之的：标准委员会认为，如果一个线程对象在销毁时既没有被 join 也没有被 detach，那几乎可以确定是程序员的错误（忘了或者逻辑漏洞），静默地 join 可能导致难以调试的挂起，静默地 detach 可能导致对已销毁变量的访问。所以标准选择了最"刺耳"的做法——直接终止程序，逼你面对这个问题。

但这带来一个很现实的问题：在复杂的代码路径中，你怎么保证每一条路径都正确地处理了线程？考虑下面这个函数：

```cpp
void process_with_thread()
{
    std::thread t([]() {
        // 一些后台工作...
    });

    do_something();        // 如果这里抛异常呢？
    do_something_else();   // 如果这里抛异常呢？

    t.join();              // 只有执行到这里才会 join
}
```

如果 `do_something()` 抛出了异常，`t.join()` 就永远不会被执行。异常沿着调用栈往上传播，`t` 的析构函数被调用，发现线程还是 `joinable()` 的，于是 `std::terminate()` 告终。程序崩了，你可能还一头雾水。

你可能会想：加个 `try-catch` 不就行了？确实可以，但代码会变得很丑，而且每个用到 `std::thread` 的地方都得这么搞。真正的解决方案是让资源管理自动化——这正是 RAII 擅长的。

## thread_guard：析构函数中自动 join

Anthony Williams 在《C++ Concurrency in Action》中给出了一个经典的 RAII 包装器——`thread_guard`。它的思路很直白：在构造时接收一个 `std::thread` 的引用，在析构时确保线程被 join。这样无论函数怎么退出（正常返回、异常抛出、early return），线程都会被正确地清理。

我们先来实现一个基础版本：

```cpp
#include <thread>

class ThreadGuard {
public:
    enum class Action { kJoin, kDetach };

    explicit ThreadGuard(std::thread& t, Action action = Action::kJoin)
        : thread_(t), action_(action)
    {}

    ~ThreadGuard()
    {
        if (thread_.joinable()) {
            if (action_ == Action::kJoin) {
                thread_.join();
            }
            else {
                thread_.detach();
            }
        }
    }

    // 禁止复制和移动——guard 不应该被转移
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

private:
    std::thread& thread_;  // 注意：持有引用，不是拥有线程
    Action action_;
};
```

使用起来是这样的：

```cpp
#include <iostream>

void background_work()
{
    std::cout << "Working in background...\n";
}

void process()
{
    std::thread t(background_work);
    ThreadGuard guard(t);  // guard 绑定到 t

    // 现在无论这里发生什么，guard 的析构函数都会 join t
    do_something();        // 即使这里抛异常
    do_something_else();   // 即使这里也抛异常

    // 不需要手动 t.join()——guard 会处理的
}
```

这个设计有一个不太优雅的地方：`ThreadGuard` 持有的是 `std::thread` 的引用，这意味着 `std::thread` 对象必须在外部存在，而且它的生命周期必须比 `ThreadGuard` 长。如果反过来——guard 先析构了，那没问题，guard 会 join 线程；但如果 `std::thread` 对象先析构了（比如它是在更内层的作用域里创建的），guard 的析构函数里就会访问一个已经不存在的对象——悬垂引用，UB。

还有一个问题是 `ThreadGuard` 在 join 之后，原始的 `std::thread` 对象仍然存在，但已经是 `joinable() == false` 的了。这种"guard 和 thread 分离"的状态在复杂代码中可能造成困惑——到底谁拥有这个线程？谁负责它的生命周期？

## joining_thread：接管所有权的 RAII wrapper

一个更干净的设计是让 wrapper 直接**拥有** `std::thread`——不是持有引用，而是把线程对象 move 进来。这样所有权就完全清晰了：wrapper 拥有线程，wrapper 析构时自动 join。这个思路的实现就是 `joining_thread`，它本质上就是 C++20 `std::jthread` 在 C++11 中就能写出的版本：

```cpp
#include <thread>
#include <utility>

class JoiningThread {
public:
    JoiningThread() noexcept = default;

    // 接受任意可调用对象和参数，直接构造线程
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...)
    {}

    // 从 std::thread move 构造——接管所有权
    explicit JoiningThread(std::thread t) noexcept
        : thread_(std::move(t))
    {}

    // 支持从另一个 JoiningThread move
    JoiningThread(JoiningThread&& other) noexcept
        : thread_(std::move(other.thread_))
    {}

    JoiningThread& operator=(JoiningThread&& other) noexcept
    {
        if (this != &other) {
            // 先处理当前持有的线程
            if (joinable()) {
                join();
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    // 也可以从一个新的 std::thread 赋值
    JoiningThread& operator=(std::thread other) noexcept
    {
        if (joinable()) {
            join();
        }
        thread_ = std::move(other);
        return *this;
    }

    ~JoiningThread()
    {
        if (joinable()) {
            join();
        }
    }

    void join()
    {
        thread_.join();
    }

    void detach()
    {
        thread_.detach();
    }

    bool joinable() const noexcept
    {
        return thread_.joinable();
    }

    // 获取底层 std::thread（用于 native_handle 等）
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

    // 禁止复制
    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

private:
    std::thread thread_;
};
```

你会发现这个类跟 `std::thread` 的接口几乎一模一样，唯一多出来的就是析构函数中的自动 `join()`。这正是 RAII 的精髓——不改变接口的用法，只在资源清理环节加上自动化。使用方式跟裸 `std::thread` 几乎一样：

```cpp
#include <iostream>

void task(int id)
{
    std::cout << "Task " << id << " running\n";
}

int main()
{
    JoiningThread t1(task, 1);  // 自动 join
    JoiningThread t2([]() {
        std::cout << "Lambda task running\n";
    });

    // 从 std::thread 构造
    JoiningThread t3(std::thread(task, 3));

    // 不需要手动 join——析构时自动完成
    return 0;
}
```

move 赋值运算符里有一个细节值得注意：在接收新线程之前，必须先处理当前持有的线程。如果当前线程还是 `joinable()` 的，必须先 join 它，否则它就变成了无主线程——析构时没人处理它，程序会 `terminate()`。这个"先清理旧的再接手新的"的模式在 RAII 类中很常见，`std::unique_ptr` 的赋值运算符也是这样做的（先 delete 旧的指针，再接管新的）。

### C++20 的 std::jthread

C++20 标准终于引入了 `std::jthread`，它的行为跟我们的 `JoiningThread` 非常相似——析构时自动 join。但 `std::jthread` 还多了一个重要的功能：**协作式取消（cooperative cancellation）**，它内部持有一个 `std::stop_source`，可以通过 `request_stop()` 请求线程停止执行。这个功能我们在后面的"jthread 与停止令牌"章节会详细展开。

如果你已经在使用 C++20，直接用 `std::jthread` 就好。如果你还在 C++11/14/17 上，上面的 `JoiningThread` 就是一个完全可行的替代方案。两者的核心思想是一样的：用 RAII 把线程的生命周期管理自动化，让编译器来保证资源不泄漏。

## 异常安全：join() 抛出异常时会发生什么

现在我们有了自动 join 的 RAII wrapper，看起来问题都解决了。但真正的坑在后面——`join()` 本身是可能抛出异常的。

什么情况下 `join()` 会抛异常？最直接的例子是底层的 `pthread_join` 调用失败了——虽然在正常的程序中这几乎不会发生，但标准并没有保证 `join()` 是 `noexcept` 的。如果你的程序在 `JoiningThread` 的析构函数中调用了 `join()`，而 `join()` 抛出了异常，那么会发生什么？

答案是：析构函数中抛出的异常会触发 `std::terminate()`。C++ 规定，如果析构函数正在执行（无论是正常销毁还是栈展开期间），又有新的异常抛出且未被捕获，程序就会终止。所以如果你的 `JoiningThread` 在析构时 `join()` 抛了异常，程序照样会崩。

这不是一个愉快的现实。实际上，《C++ Concurrency in Action》第二版中也讨论了这个问题，最终的结论是：在析构函数中 join 线程是一个"合理但不够完美"的策略——如果 `join()` 失败了，你确实没有什么好的办法来处理，因为析构函数不应该抛异常。一个务实的做法是在析构函数中用 `try-catch` 把 `join()` 包起来，捕获异常后记录日志但不重新抛出：

```cpp
~JoiningThread()
{
    if (joinable()) {
        try {
            join();
        }
        catch (const std::system_error& e) {
            // join 失败了，记录日志但不抛出
            // 在实际项目中应该用正式的日志系统
            std::fprintf(stderr,
                         "JoiningThread: join() failed: %s\n", e.what());
        }
    }
}
```

这种做法不够优雅，但它是析构函数中唯一安全的异常处理方式——吞掉异常，记录下来，然后继续。如果你的场景对 `join()` 失败零容忍，那你可能需要换一种策略：不在析构函数中 join，而是要求调用方显式 join，如果忘了就让程序 terminate（跟裸 `std::thread` 一样）。这是在"安全性"和"可靠性"之间做取舍——自动 join 让忘记 join 的问题消失了，但引入了 `join()` 失败时的异常安全问题。

## 在容器中使用线程

`std::thread` 是 move-only 的，而 `std::vector` 从 C++11 开始就支持 move-only 类型了。所以 `std::vector<JoiningThread>` 是完全合法的，可以用来管理一组工作线程。这在实现线程池、并行处理等场景中非常实用。

来看一个具体的例子——并行处理一组数据：

```cpp
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>

// 将 range 并行地分配给多个线程处理
template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func func,
                       unsigned thread_count)
{
    std::size_t length = std::distance(first, last);
    if (length == 0) return;

    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }

    std::size_t block_size = length / thread_count;
    std::vector<JoiningThread> threads;
    threads.reserve(thread_count);

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back([block_start, block_end, &func]() {
            std::for_each(block_start, block_end, func);
        });
        block_start = block_end;
    }

    // 最后一个块由当前线程自己处理
    std::for_each(block_start, last, func);

    // 析构时所有 threads 自动 join
}
```

这里有几个值得注意的细节。首先是 `emplace_back`——因为 `JoiningThread` 的构造函数接受一个可调用对象，我们可以直接在 `vector` 中原地构造线程对象，不需要先构造再 move。然后是最后一个块的处理——我们让当前线程（调用方）自己处理最后一块数据，而不是开一个额外的线程。这是一个常见的优化：调用方线程本身也在工作，不需要闲着等所有工作线程完成。

当 `parallel_for_each` 函数返回时，`threads` 这个 `vector` 被销毁，每个 `JoiningThread` 的析构函数依次调用，所有线程都被 join。整个过程不需要手动管理任何线程的生命周期。

不过要注意，`std::vector` 扩容的时候会 move 元素到新的内存区域。对于 `JoiningThread` 来说这是安全的（因为我们定义了 move 构造函数），但如果你直接用裸 `std::thread` 存储，move 之后原对象变成空的，这也是安全的——只要你不忘记在新位置上 join。用 `reserve()` 预分配空间可以避免扩容带来的额外 move 操作。

## scope(guard) 模式应用于线程清理

`JoiningThread` 是一个通用的 RAII 线程包装器，适用于大多数场景。但有时候你可能想要更灵活的控制——比如在某些条件下 join，另一些条件下 detach，或者在线程结束前做一些清理工作。这时候可以用一个更通用的工具：scope guard。

scope guard 的核心思想是"在作用域退出时执行一段代码"，无论退出原因是正常返回、异常还是 `break`/`continue`。C++ 没有语言级别的 scope guard（不像 Go 有 `defer`，Rust 有 RAII 析构），但用 C++ 的析构函数可以很轻松地实现一个：

```cpp
#include <functional>
#include <utility>

class ScopeGuard {
public:
    template <typename Func>
    explicit ScopeGuard(Func&& func)
        : callback_(std::forward<Func>(func))
    {}

    ~ScopeGuard()
    {
        if (callback_) {
            callback_();
        }
    }

    void dismiss() noexcept
    {
        callback_ = nullptr;
    }

    ScopeGuard(ScopeGuard&& other) noexcept
        : callback_(std::move(other.callback_))
    {
        other.dismiss();
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    std::function<void()> callback_;
};
```

使用 scope guard 来管理线程的 join：

```cpp
#include <thread>
#include <iostream>

void worker(int id)
{
    std::cout << "Worker " << id << " done\n";
}

void process()
{
    std::thread t(worker, 1);

    // 作用域退出时自动 join
    ScopeGuard join_guard([&t]() {
        if (t.joinable()) {
            t.join();
        }
    });

    // 一些可能抛异常的操作
    do_something();

    // 如果一切顺利，也可以手动 dismiss，然后自己 join
    // join_guard.dismiss();
    // t.join();
}
```

scope guard 比 `JoiningThread` 更灵活——你可以在 guard 的回调中做任何事情（join、detach、记录日志、更新状态等），不局限于 join。但它也更原始——没有类型安全保证，而且 `std::function` 的开销虽然很小但毕竟不是零。在一般的场景中，`JoiningThread` 是更好的选择；在需要更灵活控制的情况下，scope guard 是一个有价值的工具。

值得提一句的是，C++ 标准委员会曾多次讨论 scope guard 的标准化（P0052 等提案），但截至 C++23 并未正式纳入标准。目前最新的提案是 P3610（目标 C++29），计划在 `<scope>` 头文件中提供 `std::scope_exit`、`std::scope_fail`、`std::scope_success` 三个工具。在此之前，一些编译器以 `std::experimental::scope_exit` 的形式提供在 Library Fundamentals TS 中，也可以使用 Boost.ScopeExit 或自己实现（就像我们上面做的那样）。

## 小结

这一篇我们从 `std::thread` 的 move-only 特性出发，建立了"线程所有权"的概念——一个 `std::thread` 对象是底层操作系统线程的唯一所有者，所有权只能通过 move 转移，不能复制。这个设计跟 `std::unique_ptr` 一脉相承，确保了资源管理的清晰性。

然后我们用 RAII 模式解决了"忘记 join/detach"这个最常见的线程管理错误。`ThreadGuard` 是一个基础的实现（持有引用，析构时 join），`JoiningThread` 是一个更完善的实现（直接拥有线程，析构时自动 join）。后者本质上就是 C++20 `std::jthread` 在 C++11 中的手动实现。我们还讨论了 `join()` 可能抛出异常这个棘手的问题，以及在析构函数中的安全处理方式。

最后，我们看了 `std::vector<JoiningThread>` 在并行处理中的应用，以及更通用的 scope guard 模式。RAII 不只是一种编程技巧——它是 C++ 资源管理的核心哲学，当你开始用它来管理线程、锁、文件句柄这些资源时，你会发现代码变得更简洁、更安全、更不容易出 bug。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 练习

### 练习 1：实现可取消 join 的 JoiningThread

在上面的 `JoiningThread` 中加入一个 `cancel_join()` 方法——调用之后，析构函数不再自动 join 线程，而是 detach 它。考虑一下：`cancel_join()` 应该在什么条件下被调用？如果线程已经执行完毕但还没 join，`cancel_join()` 之后会发生什么？写一个测试用例验证你的实现。

```cpp
// 提示：你需要在类中加一个 bool 标志
class JoiningThread {
    // ...
    void cancel_join() noexcept
    {
        should_join_ = false;
    }

private:
    std::thread thread_;
    bool should_join_{true};
};
```

### 练习 2：用 JoiningThread 实现并行累加

实现一个函数 `parallel_accumulate`，它接受一个迭代器范围和一个初始值，把范围分成 N 块，每块用一个 `JoiningThread` 来累加，最后汇总所有部分和。注意处理最后一个块可能比其他块小的情况。跟 `std::accumulate` 对比一下结果是否一致。

### 练习 3：scope guard 与多线程清理

写一个程序，启动 3 个线程，每个线程执行一个模拟的长时间任务（比如 `std::this_thread::sleep_for`）。在函数的不同位置使用 `ScopeGuard` 来确保所有线程在函数退出时都被 join。然后在某个"可能失败"的检查点模拟一个异常，验证线程是否仍然被正确清理。

## 参考资源

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::jthread (C++20) — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams (Manning)](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition) — 本章 `thread_guard` 和 `joining_thread` 的设计灵感来源
- [P0052: Generic Scope Guard and RAII Wrapper for the C++ Standard Library](https://wg21.link/p0052)
- [RAII and the Rule of Zero — CppCon 2021](https://www.youtube.com/watch?v=7Qgd9B1KuMQ)
