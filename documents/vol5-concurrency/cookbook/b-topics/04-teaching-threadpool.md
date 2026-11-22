---
title: "教学级线程池：五步演进"
description: "从 worker 加任务队列加 condition_variable 的最小骨架出发，五步演进到 submit 返回 future、异常跨线程传播、优雅关闭，再到 C++20 jthread 与 stop_token 的改造与两个 stop_source 的陷阱。"
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - advanced
  - 异步编程
  - mutex
difficulty: advanced
platform: host
cpp_standard: [17, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 教学级线程池：五步演进

线程池在卷五被明确划出必修线——生产级线程池及其完整调度、资源控制与维护要求不进必修。整篇平移住进选读区，咱们把它当教学级的完整实现来讲，五步演进，每步只加一个能力。

读它之前，您需要第四单元的异步基础设施：[《用 std::async 执行计算》](../../chapter04-organizing-tasks/01-launch-with-async)的 future 用法、[《自行传递结果：std::promise 与 std::future》](../../chapter04-organizing-tasks/03-promise-and-future)的结果传递，`std::packaged_task` 的专篇（承同一节素材）见 [《把计算包装成可存储分发的任务》](../../chapter04-organizing-tasks/04-packaged-task)。第三单元的条件变量（[《等一个条件：从轮询到条件变量》](../../chapter03-wait-and-coordinate/01-wait-for-a-condition)），第一单元的 [jthread 与停止协议](../../chapter01-start-concurrency/06-jthread-and-stopping)，也都要在手边。

平移来路：机制讲解、代码与练习原样保留，我改动的只有编号、frontmatter、指向正文的交叉引用，外加判死词表清理。素材的环境说明与“测试通过”声明原样保留在文中，本轮我没有重新编译验证。

第四单元咱们把 `std::async`、`std::future`、`std::promise` 这套异步基础设施逐个拆解了一遍。咱们手里跑得通的队列只有一个 worker 线程，提交 4 个任务只能排着队一个一个跑，完全没有并行可言，跟直接在主线程调用也没什么本质区别。

现在咱们要做的，是把单 worker 的队列扩展成真正的线程池：一组提前建好的工作线程，共享一个任务队列，并发地取出任务执行。线程池是生产环境中最常用的并发模式之一，它避免了频繁创建销毁线程的系统开销，让您可以控制并发度（线程数），而且配合 `packaged_task` / `future`，可以把结果和异常干净地传回提交者。

本篇咱们从头搭一个功能完整的线程池，每一步都在前一步的基础上增加一个能力。咱们会经过这么几个阶段：搭一个只有 `enqueue()` 的最小骨架，让多 worker 跑起来。然后加上 `submit()` 返回 `future`，让调用者能拿到结果。接着处理异常在跨线程间的传播问题。再设计优雅关闭序列，停止接受新任务、排空队列、然后 join 所有 worker。最后看看 C++20 的 `jthread` + `stop_token` 能怎么简化关闭逻辑。

> **环境说明**：本篇所有代码基于 C++17，gcc 12+、clang 15+、MSVC 19.34+，咱们在 x86-64 Linux 和 macOS 上测试通过。最后一步的 C++20 改造，需要支持 `<stop_token>` 的编译器，gcc 10+、clang 17+（libc++ 部分支持，Clang 20 完整支持）、MSVC 19.28+。

## 最小可行的线程池

别急着搞什么 submit 返回 future、异常传播这些花活，咱们把最核心的骨架搭起来。一个能跑的线程池，结构其实非常经典：N 个 worker 线程共享一个任务队列，队列用 `std::mutex` 保护，用 `std::condition_variable` 通知 worker 有新任务进来。就这么简单。

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            w.join();
        }
    }

    void enqueue(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_loop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
};
```

这个结构几乎是所有 C++ 线程池的原型。咱们把它的核心组成逐个看一遍，搞清楚每个零件在干什么。

`workers_` 是一组提前建好的 `std::thread` 对象，构造函数里循环创建，每个线程执行同一个 `worker_loop()`。线程的数量，您可以交给 `std::thread::hardware_concurrency()` 决定，也可以按任务特性手动指定。任务吃 CPU，线程数跟核心数差不多就行，多了反而会因为上下文切换拖慢速度。任务多在等 I/O，您可以适当多一些，线程等 I/O 的时候，CPU 空出来正好给别的线程用。

`tasks_` 是一个 `std::queue<std::function<void()>>`——所有任务被类型擦除成 `std::function<void()>` 后塞进这个队列。不论您提交的是一个返回 `int` 的函数、一个返回 `std::string` 的 lambda 还是一个什么都不返回的函数对象，到了队列里都是 `void()` 签名。至于怎么把不同签名的可调用对象统一成 `void()` 并保留返回值，这是咱们下一步要解决的问题。

`mutex_` 和 `cv_` 是线程池同步的核心，咱们分开认：`mutex_` 保护 `tasks_` 队列和 `stop_` 标志，确保只有一个线程在同一时刻操作队列。`cv_` 用于通知 worker：有新任务来了（`notify_one`）或者该停了（`notify_all`）。

`stop_` 标志控制关闭序列。当析构函数设置 `stop_ = true` 并 `notify_all()` 时，所有 worker 被唤醒。咱们注意 worker 的退出条件：它不是“`stop_` 为 true 就立刻退出”，真正的退出条件是“stop_ 为 true **且**队列为空”，这保证了已提交但还没执行的任务不会被丢弃。

咱们用一段简单的测试代码验证它能跑：

```cpp
#include <iostream>
#include <chrono>

int main()
{
    ThreadPool pool(4);

    for (int i = 0; i < 8; ++i) {
        pool.enqueue([i] {
            std::cout << "任务 " << i << " 在线程 "
                      << std::this_thread::get_id() << " 上执行\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    }

    // 析构时等待所有任务完成
    return 0;
}
```

您会看到 8 个任务被分配到 4 个线程上执行，前 4 个几乎同时开始，后 4 个在前一批完成后接着跑。

很好，到这里骨架已经立起来了。但这个版本有个明显的缺陷：`enqueue()` 不返回任何东西。您提交了一个任务，任务执行完了，但您拿不到结果——这就很尴尬了。如果任务抛异常了，更麻烦：异常会被 `std::function<void()>` 的调用吞掉，具体行为取决于实现，通常是调用 `std::terminate` 直接把程序干掉。接下来咱们解决这个问题。

## 让 submit() 返回 future

用 `packaged_task` + `shared_ptr` 的方式返回 future，思路在第四单元讲 promise 与 future 时已经铺过（[《自行传递结果》](../../chapter04-organizing-tasks/03-promise-and-future)），咱们这里接着用。线程池需要同样的模式，只不过现在有多个 worker 同时从队列取任务——但没关系，`packaged_task` 本身是线程安全的（对共享状态的设置只发生一次），只要咱们不在多个线程里同时调用同一个 `packaged_task` 就行。

咱们给自己定的目标，是提供一个 `submit()` 模板函数：它接受任意可调用对象和参数，返回一个 `std::future<R>`，其中 `R` 是可调用对象的返回类型。调用者可以拿这个 future 来 `get()` 结果，或者在异常情况下拿到异常。

```cpp
template <typename F, typename... Args>
auto submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> fut = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("线程池已停止，无法提交新任务");
        }
        tasks_.push([task]() { (*task)(); });
    }
    cv_.notify_one();

    return fut;
}
```

这段代码有几个要点值得咱们仔细说一说，因为每一个都是吃过亏之后才意识到的重要细节。

咱们一个个看。`std::invoke_result_t<F, Args...>` 是 C++17 提供的类型萃取，用来推导 `F(Args...)` 的返回类型。它比 C++11 的 `std::result_of` 更通用，能正确处理成员函数指针、带引用修饰符的函数对象等情况。`ReturnType` 就是任务的返回类型，它决定了 `packaged_task` 的签名和 `future` 的模板参数。

`std::make_shared<std::packaged_task<ReturnType()>>` 把可调用对象和参数绑定在一起，包装成一个签名为 `ReturnType()` 的 `packaged_task`。这里用 `std::bind` 把参数提前绑定好——因为队列里存的是 `std::function<void()>`，不接受参数，所以咱们需要把参数绑定到可调用对象上，形成一个无参的可调用实体。

然后咱们把 `packaged_task` 包在 `shared_ptr` 里。这一步非常要紧，也是很多新手容易卡住的地方——因为 `std::function<void()>` 要求可调用对象是可拷贝的，而 `std::packaged_task` 是 move-only 的，不能直接塞进 `std::function`。通过 `shared_ptr` 包装后，lambda 捕获的是一个 `shared_ptr`（可拷贝），而 `packaged_task` 本身只有一个实例被 `shared_ptr` 管理。这个技巧在线程池实现中几乎是标配，您几乎在每一个正经的 C++ 线程池实现里都能看到它。

咱们看入队的一行：`tasks_.push([task]() { (*task)(); })` 把一个 lambda 推入队列。这个 lambda 捕获了 `shared_ptr<packaged_task<R()>>`，调用时解引用并执行 `packaged_task`。`packaged_task` 被调用后，内部的 promise 会自动设置返回值或存储异常，调用者手里的 future 就就绪了。

还有一个细节需要注意：咱们在推入任务之前检查了 `stop_`。如果线程池已经进入关闭状态，就不应该再接受新任务，直接抛异常。这避免了关闭过程中提交任务导致的不确定行为——想想看，您肯定不想自己的任务被推入队列后才发现 worker 线程已经全退了，结果任务永远得不到执行。

咱们来看一个完整的 submit 用法：

```cpp
#include <iostream>
#include <string>

int compute(int x)
{
    return x * x;
}

int main()
{
    ThreadPool pool(4);

    auto f1 = pool.submit(compute, 5);
    auto f2 = pool.submit(compute, 10);
    auto f3 = pool.submit([]() -> std::string {
        return "hello from thread pool";
    });

    std::cout << "f1: " << f1.get() << "\n";  // 25
    std::cout << "f2: " << f2.get() << "\n";  // 100
    std::cout << "f3: " << f3.get() << "\n";  // hello from thread pool
    return 0;
}
```

三个任务被提交到池中，由不同的 worker 线程并行执行。您会看到，`submit()` 返回的 future 类型由编译器自动推导——`f1` 和 `f2` 是 `std::future<int>`，`f3` 是 `std::future<std::string>`。

## 让异常跨线程传播

异步编程中的异常处理是一个很容易出错的地方，我自己在这里翻过车不止一次。如果您的任务在 worker 线程里抛了异常，但您没有正确处理，异常就会丢失——worker 线程不会崩（因为异常被 `std::function` 的调用机制捕获了），但您也永远拿不到结果，程序的行为就变成了诡异的“默默失败”。这样的 bug 比直接崩溃还难排查，至少崩溃了您还能看到堆栈信息。

好在 `packaged_task` 已经帮咱们处理了这件事。当被封装的函数抛出异常时，`packaged_task` 会在内部用 `std::current_exception()` 捕获异常并存储到共享状态中。调用者通过 `future.get()` 取结果时，如果共享状态中存的是异常，`get()` 会重新抛出这个异常。整个过程对调用者来说是透明的，您只需要在 `get()` 的地方 try-catch 就行了。

咱们用一个例子来验证：

```cpp
#include <iostream>
#include <stdexcept>

int risky_task(int x)
{
    if (x < 0) {
        throw std::invalid_argument("参数不能为负数");
    }
    return x * x;
}

int main()
{
    ThreadPool pool(2);

    // 正常路径
    auto f1 = pool.submit(risky_task, 5);
    try {
        std::cout << "结果: " << f1.get() << "\n";  // 25
    } catch (const std::exception& e) {
        std::cout << "异常（不该走到这里）: " << e.what() << "\n";
    }

    // 异常路径
    auto f2 = pool.submit(risky_task, -3);
    try {
        std::cout << "结果: " << f2.get() << "\n";  // 不会执行到
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";  // 参数不能为负数
    }

    return 0;
}
```

异常从 worker 线程穿越到主线程，类型信息完好无损。您不需要设计错误码体系，不需要把异常信息序列化成字符串，也不需要全局的错误处理回调，`packaged_task` + `future` 的组合把跨线程异常传播封装得干干净净。这一点真的值得感慨一下：C++ 的异常机制本来是栈展开（stack unwinding）导向的，天然适合同步调用。跨线程传播异常本来是件很麻烦的事情，但 `packaged_task` 在内部帮咱们把 `std::current_exception()` 捕获并存储，调用者那边 `future.get()` 时再重新抛出，整个过程对调用者来说跟处理同步异常一模一样。

不过这里有一个真正的陷阱——如果您提交了任务但从没调用 `future.get()`，异常就被默默吞掉了。这跟 `std::async` 返回的 future 不同，`std::async` 的 future 析构时会阻塞等待任务完成，而 `packaged_task` 关联的 future 析构时只是释放共享状态的引用，不会等。所以，**从线程池 submit() 拿到的 future，要么调 `get()`，要么至少调 `wait()` 确认任务完成了**，别把异常给丢了。

## 优雅关闭的时序

关闭一个线程池听起来简单——让 worker 线程退出就行了嘛。但事情到这里还没完，真正的麻烦在关闭的时序上。关闭的时候队列里可能还有没执行的任务，正在执行的任务可能还没跑完。如果您粗暴地把 worker 全杀掉（比如直接 detach 或者 terminate），已提交的任务就被丢了，正在执行的任务可能留下半成品状态，想想一个正在写文件的线程被干掉了，您就能理解这有多灾难。

一个“优雅”的关闭序列，咱们分三步走：停止接受新任务（`submit()` 抛异常或返回错误）。然后，让 worker 线程把队列里剩余的任务全部执行完。最后，所有 worker 线程正常退出，析构函数 join 它们。

咱们回到 `worker_loop()` 中的退出条件：

```cpp
cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
if (stop_ && tasks_.empty()) {
    return;
}
```

这个条件的含义是：worker 被唤醒后，如果 `stop_` 为 true 且队列为空，就退出。如果 `stop_` 为 true 但队列里还有任务，worker 会继续取出并执行剩余任务，直到队列清空才退出。这就是“排空队列”的语义——咱们不丢任务，只是不再接受新任务了。

咱们回头看析构函数的关闭序列：

```cpp
~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        w.join();
    }
}
```

这里有几个时序上的要点需要咱们说清楚。

设置 `stop_` 必须在持有锁的情况下进行。虽然 `stop_` 的读写只在获取锁后发生，理论上不需要 atomic，但把修改放在锁的保护范围内能让代码的意图更清晰，“修改共享状态时必须持锁”是并发编程的基本纪律，咱们不必在这里省这个锁。

`notify_all()` 在释放锁之后调用。这并不是强制的，标准允许您在持锁期间 notify。但释放锁后 notify 是一个常见的优化，如果 worker 线程被唤醒后需要获取同一把锁（它们确实需要），那么在释放锁之后再唤醒，可以避免“唤醒、抢锁失败、又阻塞”的无用上下文切换。

`join()` 必须在 `notify_all()` 之后。如果您 join 在前、notify 在后，worker 永远收不到停止信号，`join()` 就永远阻塞——这是个死锁。顺序一定要是：通知在前、等待在后。

这套关闭机制有一个隐含的保证：析构函数返回时，所有已提交的任务一定已经执行完毕。因为 `join()` 会阻塞到 worker 线程退出，而 worker 线程退出时队列一定已经空了。这对资源清理来说非常要紧，您不会在析构之后还有后台线程在访问已经销毁的对象。

## C++20 改造：jthread + stop_token

到这一步为止，咱们一直在用 `std::thread` 加手动 `stop_` 标志，加手动 `notify_all()`，再加手动 `join()` 来管理线程池。这套组合能工作，但写起来确实啰嗦，每次都要记得设置标志、通知、join，少一步就是死锁或者资源泄漏。C++20 引入了 `std::jthread`、`std::stop_token`、`std::stop_source`，加上 `std::condition_variable_any` 对 `stop_token` 的支持，可以大幅简化关闭逻辑。

有一个重要的细节咱们要交代，这也是很多教程会搞错的地方。`std::condition_variable`（不是 `_any`）**没有** C++20 stop_token 重载。stop_token 的等待集成只在 `std::condition_variable_any` 上提供。原因是 `std::condition_variable` 只支持 `std::unique_lock<std::mutex>` 一种锁类型，而 `std::condition_variable_any` 是一个模板类，支持任意满足 BasicLockable 要求的锁类型，templated 的设计让 stop_token 集成更自然。如果您在代码里用 `std::condition_variable` 去调 `wait(lock, stop_token, predicate)`，编译器会直接报错——别问我怎么知道的。

用 jthread + stop_token 改造后的线程池，咱们一起看它长什么样：

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <stop_token>
#include <mutex>
#include <condition_variable>  // condition_variable_any 也在这个头文件
#include <functional>
#include <future>
#include <memory>
#include <type_traits>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) {
                worker_loop(st);
            });
        }
    }

    ~ThreadPool()
    {
        // 请求所有 jthread 停止
        for (auto& w : workers_) {
            w.request_stop();
        }
        cv_any_.notify_all();
        // jthread 析构时自动 join，不需要手动 join
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested()) {
                throw std::runtime_error("线程池已停止，无法提交新任务");
            }
            tasks_.push([task]() { (*task)(); });
        }
        cv_any_.notify_one();

        return fut;
    }

private:
    bool stop_requested() const
    {
        // 如果任意 jthread 已经被请求停止，就认为池在关闭中
        return !workers_.empty() && workers_[0].get_stop_source().stop_requested();
    }

    void worker_loop(std::stop_token st)
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // 使用 condition_variable_any 的 stop_token 重载
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    // stop 被请求了，检查队列是否还有任务
                    if (tasks_.empty()) {
                        return;
                    }
                    // 还有剩余任务，继续执行
                }
                if (tasks_.empty()) {
                    continue;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable_any cv_any_;
};
```

接下来咱们来看看这个版本跟前面版本的关键区别。

头一处变化是 worker 线程改成了 `std::jthread`。`jthread` 的构造函数接受一个以 `std::stop_token` 为首参数的可调用对象，自动创建一个内部的 `std::stop_source` 并把对应的 `stop_token` 传给您的函数。您不需要自己维护 `stop_` 标志了——这个标志的生命周期管理由 `jthread` 内部处理。

第二处，咱们把条件等待换成 `std::condition_variable_any` 的 stop_token 重载。这个重载的签名是 `wait(lock, stop_token, predicate)`，它的行为是：predicate 为 true 就立刻返回 true。stop 被请求了也立刻返回，但返回值是 predicate 的当前值（通常是 false）。这取代了手动检查 `stop_` 标志的逻辑——当 `request_stop()` 被调用时，`cv_any_.wait()` 会被自动唤醒，不再需要在析构函数里手动 `notify_all()`。

第三处是析构函数更简洁了。`jthread` 析构时会自动调用 `request_stop()` 然后 `join()`，所以您甚至不需要显式写析构函数——不过咱们还是保留了显式的析构，因为需要在停止之前用 `notify_all()` 唤醒可能正在等待的 worker。

但这个版本也有一个不太优雅的地方，`stop_requested()` 的实现依赖于检查 `workers_[0]` 的 stop_source。这在 workers_ 为空时会出问题（虽然构造函数保证了至少有一个 worker，但依赖这样的隐含假设总是让人不太舒服）。一个更干净的做法是让线程池自己持有一个 `std::stop_source`，然后把它关联的 `stop_token` 传给每个 worker。代码会稍微复杂一些，但语义更清晰。咱们来看这个改进版：

```cpp
class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
        : stop_source_()
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            auto st = stop_source_.get_token();
            workers_.push_back(std::jthread([this, st] {
                worker_loop(st);
            }));
        }
    }

    ~ThreadPool()
    {
        stop_source_.request_stop();
        cv_any_.notify_all();
        // jthread 在 vector 析构时自动 join
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_source_.stop_requested()) {
                throw std::runtime_error("线程池已停止，无法提交新任务");
            }
            tasks_.push([task]() { (*task)(); });
        }
        cv_any_.notify_one();

        return fut;
    }

private:
    void worker_loop(std::stop_token st)
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    // stop 被请求了
                    if (tasks_.empty()) {
                        return;
                    }
                    // 还有剩余任务，继续执行完后退出
                }
                if (tasks_.empty()) {
                    continue;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable_any cv_any_;
    std::stop_source stop_source_;
};
```

这个版本用线程池自己持有的 `stop_source_` 来管理停止状态，咱们理一遍：`submit()` 里检查 `stop_source_.stop_requested()` 来判断是否还在运行，析构函数调用 `stop_source_.request_stop()` 来触发关闭。每个 worker 线程通过 `stop_source_.get_token()` 拿到同一个 stop_token——当 `request_stop()` 被调用时，所有持有这个 token 的等待操作都会被唤醒。

注意这里有一个微妙的地方：咱们把 `stop_token` 通过 lambda 捕获传给 worker 线程，而不是依赖 `jthread` 自动传参的机制。这是因为 `jthread` 自动创建的 `stop_token` 跟每个 `jthread` 自己的 `stop_source` 关联，调用某个 `jthread` 的 `request_stop()` 只会取消那个线程。而咱们想要的是：调用一次 `request_stop()` 取消所有 worker。所以咱们需要一个共享的 `stop_source`，把它的 `stop_token` 分发给所有 worker。

这个版本在语义上是干净的，但有一个需要您注意的架构问题，`jthread` 自带的 `stop_source` 和咱们手动创建的 `stop_source_`，是两个独立的停止源。`jthread` 析构时调用的是自带的 `stop_source` 的 `request_stop()`，而咱们的 worker_loop 监听的是咱们手动创建的那个。这意味着，`jthread` 自身的停止机制，实际上跟咱们的 worker 线程是脱节的，您调用 `workers_[i].request_stop()`，不会唤醒那个 worker，因为 worker_loop 监听的不是 `jthread` 的 stop_token。

这也意味着咱们的显式析构函数是必须的，不是可选的。如果咱们依赖默认析构，成员按声明的逆序销毁：`stop_source_` 和 `cv_any_` 会在 `workers_` 之前被销毁，而 `workers_` 中的 `jthread` 析构时调用的 `request_stop()` 又打不到咱们的 worker_loop——结果就是 `join()` 永远阻塞，死锁。显式析构函数调用 `stop_source_.request_stop()` + `cv_any_.notify_all()` 在前，确保 worker 线程退出，然后 `jthread` 析构时的 `join()` 才能顺利返回。

您可能会想：那 vector 扩容时移动 `jthread` 会不会出问题？答案是不会——`jthread` 被移动后，原对象的 `joinable()` 变为 `false`，析构时直接跳过 `request_stop()` 和 `join()`。线程的执行权已经转移到了新的 `jthread` 对象上，不受影响。

事情到这里您会发现，C++20 的 stop_token 机制虽然好用，但跟线程池的交互并不像想象中那么简单，`jthread` 自动管理的 `stop_source` 和咱们手动创建的 `stop_source_` 各管各的，需要咱们在析构函数里手动协调两者的时序。

我的建议是：如果您的项目还在用 C++17 或更早版本，用 `std::thread` + 手动 `stop_` 标志的方式完全没问题，不要为了用新特性而引入不必要的复杂度。C++11 时代奠定的线程 + mutex + condition_variable 组合已经经过了十余年的实战检验，出 bug 的概率远低于您跟 C++20 新特性较劲的时候。如果您已经全面使用 C++20，并且 `jthread` 和 `stop_source` 在您的项目中已经被广泛使用，那么用它们来管理线程池的停止状态是合理的，但一定要注意上面提到的“两个 stop_source”问题。jthread 与 stop_token 的完整协议，第一单元的[《让工作有序结束》](../../chapter01-start-concurrency/06-jthread-and-stopping)是正篇。

咱们给出一个完整的、经过实战检验的 C++17 版本，它不依赖 C++20 的 `jthread` 和 `stop_token`，但结构清晰、功能完整：

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <stdexcept>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t num_threads)
    {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    // 禁止拷贝和移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> fut = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) {
                throw std::runtime_error("线程池已停止，无法提交新任务");
            }
            tasks_.push([task]() { (*task)(); });
        }
        cv_.notify_one();

        return fut;
    }

private:
    void worker_loop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_{false};
};
```

几个容易踩的错误咱们在这里也一并处理了。禁用拷贝和移动——线程池持有 `std::thread` 和 `std::mutex`，这两者都是不可拷贝的，而且线程池的生命周期管理不应该被移动打乱（想想移动后原来的线程池析构时 join 了已经不属于自己的线程，那场面一定很壮观）。析构函数里 join 之前检查 `joinable()`，虽然正常情况下线程一定是 joinable 的，但防御性编程总是好的，万一有人在您不知情的情况下 join 过了呢。

## Worker 线程的生命周期

线程池的 worker 线程，实际上在三种状态之间循环：空闲等待，执行任务，以及关闭退出。理解这个生命周期，对排查线程池相关的问题很重要，您遇到的大部分“任务不执行”、“线程池卡住”的 bug，都能从状态转换中找到线索。

构造函数里，每个 worker 线程被创建后立刻进入 `worker_loop()`。咱们顺着走一遍：此时队列是空的，worker 会在 `cv_.wait()` 上阻塞，进入空闲等待状态。这个阻塞是高效的，线程被操作系统挂起，不消耗 CPU 时间片，直到 `cv_.notify_one()` 或 `cv_.notify_all()` 把它唤醒。

当 `submit()` 推入一个任务并调用 `cv_.notify_one()` 时，一个（且只有一个）等待中的 worker 被唤醒。它从队列中取出任务，释放锁，然后在锁外执行任务。咱们强调一次，在锁外执行是非常要紧的设计决策，如果持锁执行任务，其他 worker 线程和 `submit()` 调用都会被阻塞，整个线程池就退化成了串行执行，多线程的意义就没了。任务执行完毕后，worker 回到循环顶部，重新获取锁，检查队列。如果队列空了，再次进入 `wait()` 阻塞。如果队列里还有任务，直接取出执行，不需要等待。这个“执行完一个任务后主动检查队列”的行为，避免了不必要的 notify 开销。

关闭的路径在析构函数中触发，咱们再走一遍：设置 `stop_ = true` 并调用 `cv_.notify_all()`。所有 worker 被唤醒，检查 `stop_ && tasks_.empty()`。如果队列为空，worker 正常退出循环，线程结束。如果队列还有任务，worker 继续执行，直到队列清空才退出。

您可能会问：如果一个 worker 正在执行一个耗时很长的任务，而此时析构函数被调用了，会发生什么？答案是——那个 worker 不会立刻响应停止请求。它会继续执行当前任务，直到任务完成后回到循环顶部，才会检查 `stop_` 标志。所以，**如果您的任务可能运行很长时间，线程池的析构可能会阻塞很长时间**。这不是 bug，这是优雅关闭的代价——您要么等它跑完，要么用更激进的方式（比如 `timed_wait` + detach 兜底），但 detach 的线程可能访问已销毁的对象，怎么算都不划算。

## 一个完整的实战示例

现在咱们把前面所有的能力串起来，写一个综合性的示例：并行计算一组数据的处理结果，处理函数可能抛异常，咱们需要正确处理正常结果和异常。这个例子模拟了生产环境中很常见的场景——批量处理一批数据，部分数据可能有问题导致处理失败，您需要知道哪些成功了、哪些失败了。

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <stdexcept>

// 模拟一个可能失败的处理函数
double process_data(int id, double value)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (value < 0) {
        throw std::runtime_error(
            "数据 " + std::to_string(id) + " 无效: 值为负数");
    }

    // 模拟计算
    return value * value + std::sqrt(value);
}

int main()
{
    ThreadPool pool(4);

    std::vector<double> inputs = {1.0, 4.0, -2.0, 9.0, 16.0, -5.0, 25.0, 36.0};
    std::vector<std::future<double>> futures;

    // 提交所有任务
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        futures.push_back(
            pool.submit(process_data, static_cast<int>(i), inputs[i]));
    }

    // 收集结果
    int success_count = 0;
    int fail_count = 0;

    for (std::size_t i = 0; i < futures.size(); ++i) {
        try {
            double result = futures[i].get();
            std::cout << "数据 " << i << " (" << inputs[i]
                      << ") -> 结果: " << result << "\n";
            ++success_count;
        } catch (const std::runtime_error& e) {
            std::cout << "数据 " << i << " (" << inputs[i]
                      << ") -> 失败: " << e.what() << "\n";
            ++fail_count;
        }
    }

    std::cout << "\n总计: " << success_count << " 成功, "
              << fail_count << " 失败\n";
    return 0;
}
```

这段代码展示了线程池在实际场景中的典型用法：提交一组任务，然后逐个收集结果。您会发现整个使用体验跟同步代码非常接近，唯一的区别是任务在后台并行执行，而您通过 `future.get()` 拿到结果。异常通过 future 自动传播，调用者可以像处理同步异常一样处理异步异常。

## 实战中容易犯的错误

到这里咱们已经把线程池的核心功能都实现了，但实际使用中还有几个常见的错误值得单独说一说。这些错误我都亲自犯过，希望能帮您少走弯路。

头一个是 `std::bind` 和引用传递的问题。咱们 `submit()` 里用了 `std::bind` 来绑定参数，但 `std::bind` 默认是按值存储参数的——如果您的参数是大型对象，会被拷贝一份。如果您想传引用，需要用 `std::ref()` 或 `std::cref()` 包裹。更好的做法是直接用 lambda 替代 `std::bind`，lambda 的捕获列表让您可以精确控制每个参数是按值还是按引用传递，而且代码通常比 `std::bind` 更易读。如果您想用 lambda 替代 `std::bind`，submit 的实现可以简化成这样：

```cpp
template <typename F>
auto submit(F&& f) -> std::future<std::invoke_result_t<F>>
{
    using ReturnType = std::invoke_result_t<F>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::forward<F>(f));

    std::future<ReturnType> fut = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("线程池已停止，无法提交新任务");
        }
        tasks_.push([task]() { (*task)(); });
    }
    cv_.notify_one();

    return fut;
}
```

调用者可以在 lambda 里自己绑定参数和引用，咱们看一个例子：

```cpp
std::string large_data = "...";
auto fut = pool.submit([&large_data, x, y] {
    return process(large_data, x, y);
});
```

这样比 `std::bind` 灵活得多，而且生命周期关系在调用点一目了然，捕获了引用就意味着调用者必须保证 `large_data` 在任务执行完之前一直有效。这点在异步编程中没有商量余地，没有任何工具能帮您绕过。

第二个是 future 泄漏的问题。如果您 submit 了一个任务但从不调用 `get()` 或 `wait()`，您不会收到任何错误提示——任务可能在后台默默执行完了，也可能抛了异常然后异常被吞掉了，您还浑然不知。一个防御性的做法是在 submit 的文档里明确说明“每个 future 必须被消费”，或者在调试模式下追踪未消费的 future 数量。我在项目里吃过这个亏：一个后台任务的 future 被忽略了，任务里的异常就这么无声无息地消失了，排查了很久才定位到。

最后也是最阴险的一个：线程池的生命周期和任务所引用对象的生命周期不匹配。如果您的任务捕获了栈上变量的引用，而线程池的析构发生在栈变量销毁之后（比如线程池是全局的或静态的），您就面临悬垂引用的风险。这个问题的根源不在线程池本身，它落在异步编程“谁保证谁的生命周期”这个基本问题上，异步任务的执行时机是不确定的，您捕获的所有外部引用都必须在任务可能执行的时间范围内有效。没有什么好的解决方案，只能说在设计 API 的时候就思考这个问题，尽量用值捕获或者 `shared_ptr` 来延长生命周期。

## 练习

如果您想把本篇的内容真正内化，下面三个练习值得动手试试。它们分别从优先级调度、限时关闭、工作窃取三个方向扩展咱们的线程池，每一个都是生产环境中常见的需求。

### 练习 1：带优先级的线程池

请您给线程池的任务队列加上优先级支持。用 `std::priority_queue` 替代 `std::queue`，任务类型扩展为包含优先级和可调用对象的 pair。提交时允许指定优先级，worker 线程总是取出优先级最高的任务执行。

提示：`std::priority_queue` 默认是最大堆，您可以定义一个 `Task` 结构体，包含 `int priority` 和 `std::function<void()> func`，重载 `operator<`，让优先级数值大的任务排前面出队。

### 练习 2：限时关闭

请您给线程池的析构函数加上限时关闭逻辑：如果在一定时间（比如 5 秒）内还有 worker 没退出，就放弃等待并 detach 它们。注意 detach 的风险，被 detach 的线程可能访问已销毁的对象。思考一下如何安全地实现限时关闭，提示，可以让任务检查一个“池还活着吗”的标志。

### 练习 3：工作窃取

请您为线程池实现简单的工作窃取（work stealing）：每个 worker 有自己的本地任务队列，优先从本地队列取任务。当本地队列空了，尝试从其他 worker 的队列“窃取”任务。工作窃取可以减少线程间的竞争（因为大部分时候线程只操作自己的本地队列），是高性能线程池的常见优化。

## 小结

到这里，咱们从零开始搭了一个完整的线程池，覆盖了 C++ 线程池设计中几乎所有核心问题。

线程池的基本组成，是 worker 线程，任务队列，加一套同步原语（mutex + condition_variable）。worker 线程在构造时被创建，进入空闲等待状态，被 notify 后从队列取任务执行。关闭时设置停止标志，然后 notify_all 唤醒所有 worker，worker 把剩余任务执行完后退出——这套流程看起来简单，但时序上的细节（持锁 notify，stop 条件的语义，join 的顺序）每一处都值得咱们仔细思考。

`submit()` 接口通过 `packaged_task` + `shared_ptr` 实现类型擦除和 future 返回。`packaged_task` 把可调用对象和参数绑定在一起，自动处理返回值和异常的传播。`shared_ptr` 包装解决了 `packaged_task` 不可拷贝的问题。lambda 捕获 `shared_ptr` 实现了从 `packaged_task<R()>` 到 `std::function<void()>` 的类型擦除。这三者的组合是 C++ 线程池的“标准套路”，掌握了它您就能看懂绝大多数开源线程池的实现。

异常通过 `packaged_task` 的内部机制自动传播：任务抛异常时，异常被存储在共享状态中，调用者通过 `future.get()` 拿到异常。这让跨线程的异常处理变得跟同步代码一样自然——但前提是您记得调用 `get()`，不然异常就被默默吞掉了。

C++20 的 `jthread` 和 `stop_token` 可以简化线程池的关闭逻辑，但需要注意 `std::condition_variable` 不支持 `stop_token`——需要改用 `std::condition_variable_any`。另外，手动创建 `stop_source` 与 `jthread` 自带的 `stop_source` 可能存在不一致的问题，实际使用时需要仔细处理。如果您在 C++17 环境下，手动 stop 标志的方式完全够用，不必强上 C++20。

> 部分示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) 的 `code/volumn_codes/vol5/ch05-future-task-threadpool/`，其余以正文代码块为准，您可以对照着取用。

## 参考资源

- [std::packaged_task — cppreference](https://en.cppreference.com/w/cpp/thread/packaged_task)
- [std::condition_variable_any::wait — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait)
- [std::jthread — cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_token — cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Why does C++20 std::condition_variable not support std::stop_token? — Stack Overflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
- [Thread Pool C++ Implementation — Code Review Stack Exchange](https://codereview.stackexchange.com/questions/221617/thread-pool-c-implementation)

---

> **难度自评**：如果您对 packaged_task、future、condition_variable 的基本用法还不太熟悉，建议回顾第四单元的前三篇。线程池本质上是对这些组件的组合运用——理解了零件，组装就水到渠成了。
