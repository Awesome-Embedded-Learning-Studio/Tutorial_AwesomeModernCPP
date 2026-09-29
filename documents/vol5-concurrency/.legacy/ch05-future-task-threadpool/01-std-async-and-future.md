---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: 理解 std::async 的 launch policy、future.get 的阻塞语义与 deferred 陷阱
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 线程安全队列
reading_time_minutes: 24
related:
- promise 与 packaged_task
- 线程池设计
tags:
- host
- cpp-modern
- intermediate
- 异步编程
title: std::async 与 future
---
# std::async 与 future

写到这一篇，说实话笔者是松了一口气的。前面几章我们一直在跟 `std::thread`、`std::mutex`、`std::atomic` 这些底层原语打交道，直接操控线程的创建、同步、甚至内存序。这玩意儿写多了确实累——你得自己管线程生命周期，自己设计同步机制，自己把结果从子线程搬回主线程，还得操心异常怎么传回来、线程崩了怎么办。每次写一个并发任务都要重复这套流程，写着写着你就会想：有没有一种方式，让我只管说"帮我异步跑一个任务，把结果拿回来"，其他的你别烦我？

C++11 确实提供了这么一套更高层的抽象，核心就是 `std::async` 和 `std::future`。这一篇我们要把 `std::async` 的启动策略彻底搞清楚，把 `std::future` 的阻塞语义和一次性消耗模型吃透，尤其是那个经典的 deferred 陷阱——如果你不清楚默认策略的行为，写出来的代码可能在本地跑得好好的，上线后在特定负载下就莫名其妙地串行化了。这个坑笔者自己也踩过，所以我们一步一步把它拆开来看。

## std::async：启动一个异步任务

我们现在要做的是，先从最基础的用法入手，把 `std::async` 的基本形态摸清楚，然后再逐步深入策略和行为细节。

`std::async` 是一个函数模板，它接受一个可调用对象和一组参数，返回一个 `std::future`——这个 future 就是你在未来某个时刻拿回任务返回值的"凭证"。它有两个重载：一个接受启动策略（launch policy），另一个使用默认策略。先别管策略，我们先跑起来看看：

```cpp
#include <future>
#include <iostream>
#include <chrono>

int heavy_computation(int x)
{
    // 模拟耗时计算
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return x * x;
}

int main()
{
    // 异步启动任务
    std::future<int> result = std::async(std::launch::async, heavy_computation, 42);

    std::cout << "任务已提交，主线程继续干活...\n";

    // 在这里主线程可以做其他事情

    int value = result.get();  // 阻塞等待结果
    std::cout << "计算结果: " << value << "\n";
    return 0;
}
```

`std::async` 的第一个参数是启动策略，第二个参数是要执行的可调用对象，后面的参数会以完美转发的方式传给这个可调用对象。返回值是一个 `std::future<int>`——模板参数就是任务的返回类型。如果任务返回 `void`，那你就拿到一个 `std::future<void>`。

上面的代码里，`std::launch::async` 是一个枚举值，意思是"立刻在新线程上启动这个任务"。当你拿到 future 之后，主线程不会被阻塞，该干嘛干嘛，直到你调用 `result.get()` 时才会等待任务完成。

## 两种启动策略

很好，基本用法跑通了。接下来问题来了——`std::async` 的策略到底是怎么回事？前面我们一直显式传了 `std::launch::async`，但如果不传呢？这里就藏着我们今天要拆的第一个坑。

`std::async` 支持两种启动策略，通过 `std::launch` 枚举来指定。`std::launch::async` 要求运行时在调用 `std::async` 的时候就创建一个新线程（或者从内部线程池取一个），立刻执行任务。如果系统暂时没有资源创建线程，标准要求实现要么创建线程执行，要么抛出 `std::system_error`——这是一个你需要留意的错误条件。而 `std::launch::deferred` 则完全不同——它不创建任何新线程，任务会被延迟到你在 future 上调用 `get()` 或 `wait()` 时才执行，而且在调用线程上同步执行。也就是说，如果你在主线程上调了 `get()`，任务就直接在主线程上跑了，跟普通的函数调用没有本质区别，只是多了一层包装。

这两种策略可以按位或组合。`std::launch::async | std::launch::deferred` 就是默认策略——当你不传第一个参数时，`std::async` 使用的就是这个组合。这意味着实现有权自行选择到底是异步还是延迟，标准把决定权交给了标准库的实现者。

听起来好像挺灵活的，但问题恰恰出在这个"实现自行选择"上。Scott Meyers 在 *Effective Modern C++* 的 Item 36 里专门讲了这个坑：默认策略下的 `std::async` 可能选择 deferred，这意味着你的任务可能根本没有在另一个线程上运行。更糟糕的是，`std::future` 的 `wait_for()` 函数在面对 deferred 任务时返回 `std::future_status::deferred` 而不是 `timeout`——如果你写了一个轮询循环用 `wait_for()` 来检查任务是否完成，碰到 deferred 任务这个循环就会永远等下去。

我们来看一个能直观展示两者差异的例子：

```cpp
#include <future>
#include <iostream>
#include <chrono>
#include <thread>

int compute(int x)
{
    std::cout << "  [compute] 在线程 "
              << std::this_thread::get_id() << " 上执行\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return x * 2;
}

void test_launch_policy()
{
    auto main_id = std::this_thread::get_id();
    std::cout << "主线程 ID: " << main_id << "\n\n";

    // 策略一：async —— 强制在新线程上执行
    std::cout << "--- std::launch::async ---\n";
    auto f1 = std::async(std::launch::async, compute, 10);
    std::cout << "  [main] future 已创建，任务已在新线程启动\n";
    std::cout << "  [main] 结果: " << f1.get() << "\n\n";

    // 策略二：deferred —— 延迟到 get() 时在调用线程执行
    std::cout << "--- std::launch::deferred ---\n";
    auto f2 = std::async(std::launch::deferred, compute, 20);
    std::cout << "  [main] future 已创建，任务尚未启动\n";
    std::cout << "  [main] 现在调用 get()...\n";
    std::cout << "  [main] 结果: " << f2.get() << "\n";
}

int main()
{
    test_launch_policy();
    return 0;
}
```

运行这段代码，你会看到 async 模式下 compute 打印的线程 ID 与主线程不同，而 deferred 模式下两者的线程 ID 是一样的——因为 deferred 任务就是在调用 `get()` 的线程上同步执行的。

## std::future\<T\>：获取异步结果

`std::future<T>` 是 C++ 标准库提供的"一次性结果容器"。你可以把它理解为一个只读的单次管道：一端（`std::async`、`std::promise` 或 `std::packaged_task`）负责往里面塞值，另一端（你手里的 `std::future`）负责把值取出来。这个管道的设计哲学非常明确——值只能被取走一次，取完管道就报废了。

我们回头来看看 future 提供的核心操作。`get()` 是你用得最多的——它会阻塞当前线程直到结果就绪，然后返回结果值；如果任务抛了异常，`get()` 会重新抛出那个异常（异常传播机制后面专门讲）。但这里有一个关键约束：`get()` 只能调用一次，调用之后 future 就失效了，共享状态被释放，之后再对它做任何操作都是未定义行为（通常抛 `std::future_error`）。

如果你只是想等任务完成，不急着拿值，那就用 `wait()`——纯阻塞等待，不返回结果，但调用结束后结果一定就绪了。更常用的场景是带超时的等待：`wait_for()` 接受一个时间段（比如 500ms），`wait_until()` 接受一个绝对时间点，两者都返回 `std::future_status` 枚举——`ready` 表示结果已就绪，`timeout` 表示等了这么久还没好，`deferred` 表示任务压根没启动（还记得 deferred 策略吗？就是它）。对于 deferred 任务，`wait_for()` 和 `wait_until()` 会立刻返回 `deferred` 状态，不会真正等待，这个行为我们后面会看到它有多坑。

还有一个辅助函数 `valid()`，用来检查这个 future 是否还关联着共享状态。默认构造的 `std::future` 的 `valid()` 返回 `false`，调用了 `get()` 之后也返回 `false`——如果你不确定一个 future 还能不能用，先调一下 `valid()` 是好习惯。

我们用一个综合示例把这些操作串起来：

```cpp
#include <future>
#include <iostream>
#include <chrono>

int slow_task()
{
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 42;
}

int main()
{
    std::future<int> f = std::async(std::launch::async, slow_task);

    std::cout << "valid() = " << std::boolalpha << f.valid() << "\n";

    // 用 wait_for 轮询（演示用，实际中不推荐这种模式）
    while (true) {
        auto status = f.wait_for(std::chrono::milliseconds(500));
        if (status == std::future_status::ready) {
            std::cout << "任务就绪!\n";
            break;
        } else if (status == std::future_status::timeout) {
            std::cout << "还在跑...\n";
        } else if (status == std::future_status::deferred) {
            std::cout << "任务被延迟了，不会自动执行\n";
            break;
        }
    }

    if (f.valid()) {
        int result = f.get();
        std::cout << "结果: " << result << "\n";
        std::cout << "get() 后 valid() = " << f.valid() << "\n";
    }
    return 0;
}
```

这段代码会每 500ms 检查一次任务状态，等任务完成后调用 `get()` 取值。调用 `get()` 之后 `valid()` 变成 `false`，说明共享状态已经释放了。

## 一次性消耗语义

`std::future` 的设计哲学是"一次性消耗"——共享状态里的值只能被取走一次。这个设计体现在几个层面，我们一个一个拆。

从 `get()` 的返回语义说起。`get()` 执行的是移动语义：对于 `std::future<int>`，`get()` 返回的是 `int` 的值拷贝（因为 int 的移动就是拷贝，无所谓），但对于 `std::future<std::string>`，`get()` 返回的 `std::string` 是从共享状态中移动出来的，值被取走后再调用 `get()` 就是未定义行为。值得注意的是，标准库对 `std::future<T&>`（引用类型）和 `std::future<void>` 有单独的特化，它们的 `get()` 行为略有不同——前者返回引用，后者只做同步等待不返回任何东西。

从 future 对象本身的属性来看，`std::future` 是只移动的（move-only）。你不能拷贝一个 `std::future`，只能移动它——移动之后原来的 future 的 `valid()` 变成 `false`，新 future 接管了共享状态。这个设计确保了任何时刻只有一个 future 能访问共享状态，从根本上杜绝了多人抢同一个结果的竞争条件。而且没有任何机制可以"重置"一个已经消耗掉的 future，如果你需要多次读取同一个结果，应该用 `std::shared_future`——下一篇我们会讲到。

```cpp
#include <future>
#include <iostream>
#include <string>

std::string generate_report()
{
    return "这是一份详细的分析报告";
}

int main()
{
    std::future<std::string> f = std::async(std::launch::async, generate_report);

    // 第一次 get() —— 正常
    std::string report = f.get();
    std::cout << "报告: " << report << "\n";

    // 第二次 get() —— 未定义行为！valid() 已经是 false
    // std::string report2 = f.get();  // 千万别这么干

    std::cout << "get() 后 valid() = " << std::boolalpha << f.valid() << "\n";
    return 0;
}
```

这个一次性语义不是缺陷而是设计选择。`std::future` 的目标是轻量级的一次性结果传递，而不是一个可反复读取的结果容器。如果你需要"广播"一个结果给多个消费者，C++ 提供了 `std::shared_future` 来满足这个需求——代价是额外的引用计数开销。

## deferred 策略的陷阱

前面我们已经提到了 deferred 策略的基本行为：任务不会异步执行，而是延迟到你调用 `wait()` 或 `get()` 时在当前线程上同步执行。但这个行为在实际工程中引发的 bug 远比你想的多——事情到这里还没完，真正的坑在后面。

> **踩坑预警**：默认策略下的 `std::async` 是笔者踩过的最隐蔽的并发坑之一。本地测试一切正常，上了生产环境才发现任务全是串行的——因为标准库实现选择了 deferred 策略（默认策略下实现有权自行选择 async 或 deferred，标准并未规定选择的条件）。

最大的陷阱来自默认策略。当你写 `std::async(f, args...)` 而不指定策略时，使用的是 `std::launch::async | std::launch::deferred`，这意味着标准库实现可以自行选择。在某些实现上（尤其是在高负载时），标准库可能大量选择 deferred 策略。于是你以为你在做并行计算，实际上所有任务都在主线程上串行执行了——而且你的测试永远无法覆盖"标准库突然切换策略"这种场景。

一个特别危险的场景是"fire-and-forget"模式——你启动了多个 async 任务，没有立即调用 `get()`，期望它们在后台并行跑完。我们来看看这段代码：

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <chrono>

int work(int id)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "任务 " << id << " 完成\n";
    return id * 10;
}

int main()
{
    std::vector<std::future<int>> futures;

    // 启动 4 个"异步"任务（使用默认策略）
    for (int i = 0; i < 4; ++i) {
        futures.push_back(std::async(work, i));  // 默认策略：async | deferred
    }

    // 依次收集结果
    for (auto& f : futures) {
        std::cout << "结果: " << f.get() << "\n";
    }
    return 0;
}
```

如果实现选择了 deferred 策略，这 4 个任务会串行地在主线程上执行，总耗时 4 秒而不是预期的 1 秒。更隐蔽的是，即使实现通常选择 async，在某些特殊条件下（比如线程资源紧张）它也可能切换到 deferred——你的测试永远无法覆盖这种情况，这就很烦了。

紧接着还有第二个陷阱，跟 `wait_for()` 有关。如果你用 `wait_for()` 写了一个超时循环来轮询 deferred 任务，循环会立刻返回 `deferred` 状态而不是 `timeout`。如果你没有处理 `deferred` 这个分支（说实话，很多人确实会忽略它），循环就变成了死循环：

```cpp
// ⚠️ 危险！如果没有处理 deferred 状态，可能永远循环下去
while (f.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
    // 如果任务是 deferred 的，这个循环永远不会退出！
    // 因为 wait_for 对 deferred 任务立刻返回 std::future_status::deferred
}
```

千万别以为这只是教科书上的极端例子——笔者在真实项目中见过这种死循环，而且它只在特定负载下才触发，排查起来真的血压拉满。正确的做法是先检查 `wait_for` 的返回值，如果是 `deferred` 就直接调用 `get()` 或者采取其他策略：

```cpp
auto status = f.wait_for(std::chrono::milliseconds(100));
if (status == std::future_status::deferred) {
    // 任务被延迟了，直接在当前线程执行
    result = f.get();
} else if (status == std::future_status::ready) {
    result = f.get();
} else {
    // timeout —— 继续等待或做其他事情
}
```

所以笔者的建议很简单：**如果你确实需要异步执行，就显式指定 `std::launch::async`**。默认策略看起来很灵活——"让实现帮你选"嘛，多优雅——但这种灵活性在实际项目中几乎全是坑。Scott Meyers 在 *Effective Modern C++* 的 Item 36 里也建议：如果你想确保任务是真正异步执行的，永远显式传 `std::launch::async`。把这句话贴在显示器边上都不为过。

## 异常传播

到目前为止我们一直在处理正常返回值的场景，但实际工程中任务抛异常是常有的事。`std::async` 的一个很大优点是它会自动捕获任务中抛出的异常，并通过 `std::future` 传播到调用方——你不需要手动设计错误码或者其他错误传递机制。

这个机制的工作原理是这样的：如果任务函数抛出异常，异常会被捕获并存储在 `std::future` 的共享状态中；当你调用 `get()` 时，存储的异常会被重新抛出。这意味着你可以在主线程中用 try-catch 来处理子线程的异常，跟处理普通函数调用抛出的异常没什么区别。

```cpp
#include <future>
#include <iostream>
#include <stdexcept>

int risky_computation(int x)
{
    if (x < 0) {
        throw std::invalid_argument("参数不能为负数");
    }
    return x * x;
}

int main()
{
    auto f1 = std::async(std::launch::async, risky_computation, -5);

    try {
        int result = f1.get();  // 会抛出 std::invalid_argument
        std::cout << "结果: " << result << "\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    // 正常情况
    auto f2 = std::async(std::launch::async, risky_computation, 5);
    try {
        int result = f2.get();
        std::cout << "正常结果: " << result << "\n";  // 输出 25
    } catch (const std::invalid_argument& e) {
        std::cout << "不会执行到这里\n";
    }
    return 0;
}
```

这个异常传播机制对于 deferred 策略同样有效——只不过 deferred 策略下异常是在 `get()` 调用时同步抛出的，跟普通的函数调用抛异常没有区别。

这里有一个细节需要注意——如果你从不调用 `get()`，异常就被默默吞掉了。更准确地说，如果 `std::future` 析构时任务还没有完成（对于 async 策略），析构函数会阻塞等待任务完成。如果任务抛了异常且你从没调过 `get()`，异常随共享状态一起被释放——不会传播，不会终止程序，就是丢了。这是一种静默的错误，非常危险。所以，**从 `std::async` 返回的 future 一定要调用 `get()`**，哪怕你不需要返回值，哪怕你只是想确认一下任务没有抛异常。

## std::async 返回的 future 的析构行为

你可能注意到了，前面的例子里我们都老老实实地保存了 future 对象，最后才调用 `get()`。但如果你随手写了一行 `std::async(std::launch::async, some_task);`，没保存返回值呢？这里要特别提一下 `std::async` 返回的 `std::future` 的析构行为，因为它跟普通的 `std::future` 不一样。

当你通过其他方式（比如 `std::promise`）获得 `std::future` 时，future 析构只是释放共享状态的引用——如果 promise 还没设值，future 就这么析构了，什么都不会等。

但 `std::async` 返回的 future 是特殊的：如果任务是通过 `std::launch::async` 启动的，且这是最后一个引用该共享状态的 future，那么析构函数会阻塞，直到任务完成。这是标准明确要求的行为（[futures.async]），目的是防止任务还在跑的时候你把 future 扔了导致任务变成孤儿线程。

这意味着以下代码实际上是串行的：

```cpp
#include <future>
#include <iostream>
#include <chrono>

void task(int id)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "任务 " << id << " 完成\n";
}

int main()
{
    // 注意：临时 future 对象在这条语句结束时就会析构
    std::async(std::launch::async, task, 1);  // 析构阻塞到任务完成
    std::async(std::launch::async, task, 2);  // 析构阻塞到任务完成
    std::async(std::launch::async, task, 3);  // 析构阻塞到任务完成
    // 总耗时 3 秒——完全是串行的！
    return 0;
}
```

每次 `std::async` 返回的临时 `std::future` 对象在语句结束时就会被析构，而析构会阻塞到任务完成。所以虽然你写了三行 `std::async`，实际执行是严格串行的。要真正并行，你需要把 future 存到容器里，等全部启动后再依次收集：

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <chrono>

void task(int id)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "任务 " << id << " 完成\n";
}

int main()
{
    std::vector<std::future<void>> futures;

    // 先全部启动
    for (int i = 1; i <= 3; ++i) {
        futures.push_back(std::async(std::launch::async, task, i));
    }

    // 再统一等待
    for (auto& f : futures) {
        f.get();  // 总耗时约 1 秒——三个任务并行执行
    }
    return 0;
}
```

这个析构行为是 `std::async` 的一个"特色"设计，经常让新手踩坑。你心里一定要有这根弦：`std::async` 返回的 future 的析构是会阻塞的——如果你随手忽略了返回值，你写的"并行"代码就变成了串行。

## std::future 与 std::thread 的比较：怎么选？

到这里我们可以把 `std::async`/`std::future` 和 `std::thread` 做一个对比了，顺便把选择策略也理清楚。

用 `std::thread` 执行异步任务时，你需要自己设计结果传递机制——比如用共享变量加 mutex、用全局变量加 atomic、或者用条件变量。异常处理也完全是你自己的事——子线程抛出的异常不会自动传回主线程，你得手动捕获并通过某种机制传递。线程管理也是手动操作：你必须在 `join()` 或 `detach()` 之间选一个，忘了就触发 `std::terminate`。

用 `std::async` 则省心很多：返回值通过 `std::future` 自动传递，异常自动传播，future 的析构会等待任务完成（不会出现孤儿线程）。代价是你失去了对线程的精细控制——你不能设置线程优先级、不能设置线程亲和性、不能给线程起名字，甚至不知道任务到底跑在哪个线程上。

所以选择逻辑其实很清晰。如果你要跑一个有明确输入输出的计算任务，任务之间相对独立，你需要异常传播，而且你不关心任务跑在哪个线程上——典型的比如并行的数据处理、并行的文件 I/O、或者把一个耗时计算从主线程卸载出去——用 `std::async`。`std::async` 适合的就是那种"丢出去一个任务，拿回来一个结果"的场景。但 `std::async` 不适合需要频繁创建销毁线程的场景——每次 `std::launch::async` 都可能创建一个新线程，系统开销不小。

如果你需要一个常驻后台的工作线程——后台监听线程、事件循环、或者需要设置线程属性（优先级、亲和性等）的情况——用 `std::thread`，但它需要你自己处理所有同步和错误传递，代码量明显更多。

如果你需要跑大量短任务，那就是线程池的主场了。线程池预先创建一组工作线程，任务被提交到队列中由工作线程取出执行。这避免了频繁创建销毁线程的开销，也让你可以控制并发度（最大线程数、任务队列大小等）。C++ 标准库目前没有提供线程池，所以你需要自己实现或者使用第三方库——我们会在后面的章节详细讲解线程池的设计与实现。

## 练习：使用 std::async 进行并行计算

### 练习 1：并行求和

给定一个包含 1000 万个随机整数的 `std::vector<int>`，用 `std::async` 将它分成 4 段并行求和，最后汇总结果。对比单线程版本和多线程版本的耗时。

```cpp
#include <future>
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>

// 将 data[begin, end) 区间求和
long long partial_sum(const std::vector<int>& data, std::size_t begin, std::size_t end)
{
    return std::accumulate(data.begin() + begin, data.begin() + end, 0LL);
}

int main()
{
    constexpr std::size_t kDataSize = 10'000'000;
    constexpr int kNumTasks = 4;

    // 生成随机数据
    std::vector<int> data(kDataSize);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 100);
    for (auto& x : data) {
        x = dist(rng);
    }

    // 多线程版本
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<long long>> futures;
    std::size_t chunk = kDataSize / kNumTasks;

    for (int i = 0; i < kNumTasks; ++i) {
        std::size_t begin = i * chunk;
        std::size_t end = (i == kNumTasks - 1) ? kDataSize : (i + 1) * chunk;
        futures.push_back(
            std::async(std::launch::async, partial_sum,
                       std::cref(data), begin, end));
    }

    long long total = 0;
    for (auto& f : futures) {
        total += f.get();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       end_time - start)
                       .count();

    std::cout << "并行求和结果: " << total << "\n";
    std::cout << "耗时: " << elapsed << " us\n";

    // 单线程版本（用于验证）
    start = std::chrono::high_resolution_clock::now();
    long long single = std::accumulate(data.begin(), data.end(), 0LL);
    end_time = std::chrono::high_resolution_clock::now();
    elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                  end_time - start)
                  .count();

    std::cout << "单线程结果: " << single << "\n";
    std::cout << "耗时: " << elapsed << " us\n";
    std::cout << "结果一致: " << std::boolalpha << (total == single) << "\n";
    return 0;
}
```

注意这里用了 `std::cref(data)` 来传递数据的只读引用——因为 `std::async` 的参数默认是按值传递的，如果不加 `std::cref`，整个 vector 会被拷贝一份，既浪费内存又浪费时间。`std::cref` 是一个引用包装器，它让按值传递的参数在不拷贝的情况下传递引用。

### 练习 2：验证 deferred 陷阱

修改练习 1 的代码，分别使用 `std::launch::async`、`std::launch::deferred` 和默认策略运行，对比三者的耗时。观察 deferred 版本和单线程版本的耗时是否接近。

### 练习 3：异常传播验证

写一个 `std::async` 任务，让它抛出一个自定义异常。在主线程中用 try-catch 捕获并验证异常类型和消息内容是否一致。

## 小结

到这里，这篇我们就完整地走了一遍 `std::async` 和 `std::future` 的核心机制。`std::async` 提供了一种比 `std::thread` 更高级的异步任务启动方式，自动处理返回值传递和异常传播，确实省心不少。`std::future<T>` 是获取异步结果的标准通道，`get()`、`wait()`、`wait_for()` 这些操作虽然名字很直白，但背后的语义（尤其是 get 的一次性消耗和 deferred 状态下的 wait_for 行为）需要你牢记在心。

几个要点再强调一遍：默认启动策略（`async | deferred`）是一个需要警惕的陷阱，实现可能选择 deferred 策略导致任务串行执行；`wait_for()` 对 deferred 任务立刻返回 `deferred` 状态，轮询循环没处理这个分支就会变成死循环；`std::async` 返回的 future 的析构会阻塞到任务完成，随手忽略返回值就会让你的并行代码变成串行。如果你需要真正的异步执行，显式传 `std::launch::async`——这条规则贴在显示器边上都不为过。

下一篇我们来看 `std::promise` 和 `std::packaged_task`——它们是 `std::future` 的"另一端"，让你可以更灵活地控制值的设置和任务的封装。搞清楚了 future 这端的语义，再去理解 promise 那端就水到渠成了。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch05-future-task-threadpool/`。

## 参考资源

- [std::async — cppreference](https://en.cppreference.com/w/cpp/thread/async)
- [std::future — cppreference](https://en.cppreference.com/w/cpp/thread/future)
- [std::launch — cppreference](https://en.cppreference.com/w/cpp/thread/launch)
- [Effective Modern C++, Item 35, 36 — Scott Meyers](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [Async Tasks in C++11: Not Quite There Yet — Bartosz Milewski](https://bartoszmilewski.com/2011/10/10/async-tasks-in-c11-not-quite-there-yet/)
- [The Promises and Challenges of std::async — DZone](https://dzone.com/articles/the-promises-and-challenges-of-stdasync-task-based)
