---
title: "把计算包装成可存储分发的任务"
description: "packaged_task 把可调用对象连同供值端包成一件：模板参数是函数签名，调用即执行，值与异常由它自动装进共享状态，只移动让任务能存能传，单工作线程的 SimpleTaskQueue 把提交与取结果连成一条线。"
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 把计算包装成可存储分发的任务

咱们这一篇只干一件事：把一段普通的函数，包成一件能存进容器、能移动给别人、能放进队列里排队的东西。干活的工具叫 `std::packaged_task`，C++11 进的标准（cppreference 的标注，现行草案收在 [futures.task] 小节）。它把可调用对象连同供值端包成一件：您调用它，它执行任务，返回值或者异常自动装进共享状态。任务从"等着被调用的一段函数"变成一个对象，谁持有它，谁就攥着触发执行与装值的权力。往后选读区那座线程池，提交接口底下垫的就是它。

## 把函数包成任务

咱们从最小的例子看起。`add` 是一个普通的函数，包进 packaged_task，拿到 future，再把它移动给一条线程去执行：

```cpp
#include <future>
#include <iostream>
#include <thread>

int add(int a, int b)
{
    return a + b;
}

int main()
{
    // 创建 packaged_task，封装一个可调用对象
    std::packaged_task<int(int, int)> task(add);

    // 获取关联的 future
    std::future<int> fut = task.get_future();

    // 在另一个线程上执行 task
    std::thread t(std::move(task), 10, 20);

    // 在主线程获取结果
    int result = fut.get();
    std::cout << "10 + 20 = " << result << "\n";

    t.join();
    return 0;
}
```

咱们对着代码认它的形状。模板参数是一个函数签名，`int(int, int)` 的意思是接受两个 `int`、返回 `int`，您包进去的可调用对象得跟这个签名对得上。`get_future()` 把内部供值端关联的 future 交到您手里，额度跟 4.3 的 promise 一样只有一回，第二回抛 `future_error`，错误条件是 `future_already_retrieved`（[futures.task.members]/21.1），这一款笔者拿探针在本机验过，libstdc++ 抛的就是它。

触发执行的入口就是函数调用运算符本身，写 `task(10, 20)`，任务立刻跑起来。它没有叫 `run` 或者 `execute` 的成员。标准写的效果是：调用存进去的那个任务，正常返回就把返回值存进共享状态，抛了异常就把异常存进去，状态随之就绪，所有等着它的线程被唤醒（[futures.task.members]/22）。上面例子里执行发生在 `t` 那条线程，是因为咱们把 task 移动给了线程构造函数，任务本身在哪里跑，取决于谁调用了它。"任务"与"线程"从这里分开，后面搭队列靠的就是这一步。

### 只移动：为什么不给拷贝

拷贝构造和拷贝赋值在类定义里都标着 `= delete`，标准在旁边写明 no copy（[futures.task.general]）。您想复制一份，编译器当场拒绝，想把它交给别的线程、塞进容器，走的都是 `std::move`。这个脾气您在 4.1 的 future、4.3 的 promise 身上都见过，packaged_task 是第三个。

道理咱们能自己推出来。一份 packaged_task 底下连着一段共享状态，装值的额度只有一回。要是允许拷贝，拷出来的几份指着同一段共享状态，各调一回，第二回装值就收到 `future_error`，错误条件 `promise_already_satisfied`（/24.1），状态已经就绪，装不进第二份结果。标准干脆不给这条路：一份任务一个主人，权力随移动交接，谁调用了它，装值的额度就归谁。

## 异常自动装：调用不抛，get 才抛

4.3 里异常的传递要走三步，`catch (...)` 接住，`std::current_exception()` 装成指针，`set_exception` 存进状态。这三步 packaged_task 全包了：

```cpp
#include <future>
#include <iostream>
#include <stdexcept>

int risky_func(int x)
{
    if (x == 0) {
        throw std::runtime_error("除零错误");
    }
    return 100 / x;
}

int main()
{
    std::packaged_task<int(int)> task(risky_func);
    std::future<int> fut = task.get_future();

    // 在当前线程调用 task（也可以在另一个线程）
    task(0);  // 传入 0，触发异常

    try {
        int result = fut.get();  // 重新抛出异常
        std::cout << "结果: " << result << "\n";
    } catch (const std::runtime_error& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }
    return 0;
}
```

```text
捕获到异常: 除零错误
```

咱们把控制流跟着代码走一遍。`task(0)` 这一行调用的是 packaged_task 的调用运算符，`risky_func` 在里面抛出的 `runtime_error` 没有从这一行动身，按标准的效果条款，异常与返回值走同一条路，存进了共享状态（/22）。真正抛异常的位置在 `fut.get()`：取的一头等到就绪，发现状态里存的是异常，就在您的线程里把它重抛，抛的是存进去的那个本身（[futures.unique.future]/19，4.1 核对过的条款）。`main` 里的 `try` 加 `catch` 接住它，类型与消息跟 `risky_func` 里抛的一样。

这个安排把执行与善后分到了两处。执行的一方只管调用，异常有没有人接它不管。取的一方只管 `get`，值与异常从同一个口子出来。上面例子里调用与取值同在 `main`，您把这行 `task(0)` 挪到任何一条线程上，链路照样通。4.3 里您亲手写的那两行，到这里是 packaged_task 内部替您做的。

## 搭一条单工作线程的任务队列

包装成对象的价值，要等任务进了队列才看得足。咱们这一节搭一个最小的任务队列，只有一条工作线程，提交、执行、传结果三件事一件不缺。整个类咱们原样摆出来，您跟着注释读：

```cpp
#include <future>
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class SimpleTaskQueue
{
public:
    using TaskType = std::function<void()>;

    SimpleTaskQueue()
    {
        worker_ = std::thread([this]() { worker_loop(); });
    }

    ~SimpleTaskQueue()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_one();
        worker_.join();
    }

    // 提交一个 packaged_task，返回对应的 future
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
            queue_.push([task]() { (*task)(); });
        }
        cv_.notify_one();

        return fut;
    }

private:
    void worker_loop()
    {
        while (true) {
            TaskType task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return done_ || !queue_.empty(); });
                if (done_ && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
        }
    }

    std::thread worker_;
    std::queue<TaskType> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{false};
};
```

类不长，咱们把两头分开看。`submit` 是提交的一头：您递进来一个可调用对象和一组参数，它用 `std::bind` 把函数与参数绑成一个无参调用，装进 `packaged_task<ReturnType()>`，外面套一层 `shared_ptr`，包成 `std::function<void()>` 推进队列，`get_future` 拿到的 future 原样递回给您。套 `shared_ptr` 有一层原因：`std::function` 要求装进去的东西能拷贝，packaged_task 偏偏只移动，装不进去，于是拷贝发生在指针上，执行的时候经 `(*task)()` 解引用回到任务本身。`worker_loop` 是执行的一头：工作线程挂在条件变量上，队列不空或者收到收工通知才醒，醒来取出一个任务执行，内部的供值端自动装值装异常。

整条链连起来念：您提交，任务入队，工作线程取出执行，供值端自动装进结果，您用 future 等到它。4.3 里两对 promise 加 future 的手工链，到这里缩成一句 `submit`。

咱们拿它跑三个任务，两个函数调用加一个 lambda：

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

int heavy_compute(int x)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return x * x;
}

int main()
{
    SimpleTaskQueue queue;

    auto f1 = queue.submit(heavy_compute, 5);
    auto f2 = queue.submit(heavy_compute, 10);
    auto f3 = queue.submit([]() {
        return std::string("hello from task queue");
    });

    std::cout << "f1: " << f1.get() << "\n";
    std::cout << "f2: " << f2.get() << "\n";
    std::cout << "f3: " << f3.get() << "\n";
    return 0;
}
```

```text
f1: 25
f2: 100
f3: hello from task queue
```

三行输出都在，时序咱们对着单工作线程的纪律读。`heavy_compute(5)` 头一个进队也头一个执行，`f1` 的 `get` 在一秒上下等到 25。`heavy_compute(10)` 排第二个，又是一秒，`f2` 拿到 100 的时候距离提交已经过去约两秒。`f3` 的 lambda 本身不费时间，可它排在队尾，轮到它执行是两秒以后的事，`get` 一调就回。谁先提交谁先跑，这是单条工作线程的纪律。并行是线程池的活，单线程队列给的是骨架：提交与执行分离，结果与异常沿 future 传回提交的一方。

### 返回类型怎么跟上：invoke_result_t 与尾置返回

`submit` 的返回类型写在参数表的后面，`auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>`，这是尾置返回的写法。返回类型里的 `invoke_result_t<F, Args...>` 要引用 F 和 Args 这两个参数，前置返回类型却写在函数名前面，那时它们还没进场，够不着。挪到参数表后面，编译器先见到 F 与 Args，再推返回类型，顺序就顺了。

`std::invoke_result_t<F, Args...>` 是类型萃取，替您问编译器一件事：拿 F 配上这组参数去调用，返回什么类型，它就给什么。这个工具 C++17 才进场。您的编译器停在 C++11 或 C++14，同一件事当时的写法是 `std::result_of_t<F(Args...)>`。`result_of` 在 C++17 被废弃，C++20 被移除，新代码按本篇的写法走 `invoke_result_t` 就好，本篇的队列代码也是用它编的。

## 超时等待：等待能放弃，任务取消不了

future 带计时的等待有两个，`wait_for` 是常用的一个：等到就绪或者时限用完就回来，返回一个 `std::future_status`。咱们给一个要跑三秒的任务设两秒的期限：

```cpp
#include <future>
#include <iostream>
#include <chrono>
#include <thread>

int slow_computation()
{
    // 模拟一个耗时 3 秒的计算
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 42;
}

int main()
{
    std::packaged_task<int()> task(slow_computation);
    std::future<int> fut = task.get_future();

    // 在独立线程执行
    std::thread t(std::move(task));

    // 设定 2 秒超时
    auto status = fut.wait_for(std::chrono::seconds(2));

    if (status == std::future_status::ready) {
        std::cout << "结果: " << fut.get() << "\n";
    } else if (status == std::future_status::timeout) {
        std::cout << "计算超时，放弃等待\n";
        // 注意：工作线程仍在运行，我们需要等待它结束
    } else {
        std::cout << "任务被延迟\n";
    }

    t.join();  // 确保线程正常结束
    return 0;
}
```

```text
计算超时，放弃等待
```

两秒一到，`wait_for` 带着 `timeout` 回来，咱们打印一行，放弃等待。放弃的是什么，得看清：放弃的只是等，任务本体还在那条线程上跑。C++ 的标准库没有线程取消这回事，线程跑起来，外面没有任何办法把它按停，`main` 走到 `t.join()` 还得等它自己跑完。笔者在本机跑了这段代码，超时行出现在两秒，`join` 又等了一秒才回来，程序全程三秒，一秒都省不掉。

真正的出路是让任务自己会听停。协作式取消的工具，第一单元的 [《jthread 与停止协议》](../chapter01-start-concurrency/06-jthread-and-stopping)已经备好：停止令牌递进任务，任务在合适的节点查一眼，查到就提前收工，`join` 才等得起。还有一条 4.2 讲过的边界得提醒：`wait_for` 碰上 deferred 的任务什么都不做，永远返回 `deferred`。本篇等的是真在跑的任务，没这个问题。哪天您把包装好的任务换成 deferred 发起，超时轮询立刻空转，那个场面 4.2 的陷阱二演过。

## 轮到您动手的时候

动笔之前，笔者把这一篇攒下的问题摆成一列，您对着自己的代码过一遍：

1. 模板参数的函数签名，跟您包进去的可调用对象对得上吗？参数个数、类型、返回值，哪一样不配，编译器都会指着 `packaged_task<R(ArgTypes...)>` 说话，报错顺着它读，别急着绕开。
2. `get_future` 的调用点数过了吗？额度一回，第二回是 `future_already_retrieved`，跟 4.3 的 promise 同款，探针验过。
3. 提交的任务会抛异常吗？`catch` 在谁手里，类型具体到能对上 `what()` 吗？异常从 `get` 出来，没人接，它就停在共享状态里随状态销毁，4.1 讲过的去向。
4. 一个任务要跑两回，packaged_task 做不到，`operator()` 第二回就是 `promise_already_satisfied`。标准给 `reset()` 备了一条路：换一段新的共享状态，旧的弃掉（/28）。您的任务要重跑，走的是哪条路？
5. 超时放弃等待之后，那条工作线程的去处安排了吗？`join` 会等到底，等不起，就得让任务自己会听停。

留一个小实验给您。把一个 packaged_task 建出来，`get_future` 把 future 拿好，然后不调用它，让它直接析构，再去 `get`。等待的一方会收到什么，您先猜。笔者交代本机跑出来的答案：`future_error`，错误条件 `broken_promise`。析构的效果条款写着弃掉共享状态（[futures.task.members]/14），没兑现就散场，等待的一方收到一句能定位的交代。4.2 的扩展节给 promise 演过弃掉，packaged_task 这边同款。

## 参考资源

素材交代。本篇的内容取自旧目录 ch05 的《promise 与 packaged_task》里 packaged_task 的部分，例子与文字全部重写，promise 与 shared_future 的两块已由 4.3 与 4.1 各归各位。重写时笔者把本篇的全部代码在本机重新编译跑过：GCC 16.2.1，`-std=c++20 -pthread`，另加 `-Wall -Wextra`，无警告。正文引的输出是单次读数，三段程序打的固定字符串逐字稳定，超时一节的两个时刻（两秒出超时行、`join` 再等一秒）跟机器走，别的环境读数跟着变。两支小探针，第二回 `get_future` 的款、任务没调用就析构的款，也是本机验的，`future_error` 的消息是 libstdc++ 的措辞，别的实现可以不同，错误条件的比对才是标准保的。额度、错误条件与 `reset` 的条款按现行草案 [futures.task] 核对，小节名跟正式标准一致，段号可能挪。版本对账：packaged_task 本体与头两段演示都是 C++11 的东西，队列一节 `submit` 的返回类型用了 C++17 的 `invoke_result_t`，C++11 或 C++14 的替代写法 `result_of_t` 在正文交代过，frontmatter 按主线标 11。

- [std::packaged_task（cppreference）][cppref-pt]，C++11 起，只移动，get_future 的一次额度、operator() 装值装异常、reset 换新共享状态。
- [std::promise（cppreference）][cppref-promise]，packaged_task 内部供值端用的机制，4.3 的主角。
- [std::future（cppreference）][cppref-future]，取的一头，get 等就绪并重抛存下的异常。
- [Class template packaged_task（现行草案 futures.task）][draft-task]，类定义与 no copy、get_future 与 operator() 的错误条件、析构弃掉共享状态、reset 的语义。
- [C++11 Concurrency Tutorial - Futures（Baptiste Wicht）][wicht]，旧文引过的入门教程，async 与 future 讲得细。

[cppref-pt]: https://en.cppreference.com/w/cpp/thread/packaged_task
[cppref-promise]: https://en.cppreference.com/w/cpp/thread/promise
[cppref-future]: https://en.cppreference.com/w/cpp/thread/future
[draft-task]: https://eel.is/c++draft/futures.task
[wicht]: https://baptiste-wicht.com/posts/2017/09/cpp11-concurrency-tutorial-futures.html
