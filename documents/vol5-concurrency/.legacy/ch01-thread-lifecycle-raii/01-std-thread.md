---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 掌握 C++ 线程的创建、join、detach、ID 与硬件并发查询，建立第一个多线程程序的直觉
difficulty: beginner
order: 1
platform: host
prerequisites:
- CPU cache 与 OS 线程
reading_time_minutes: 18
related:
- 线程参数与生命周期
- 线程所有权与 RAII
tags:
- host
- cpp-modern
- beginner
- 入门
title: std::thread 基础
---
# std::thread 基础

前一章我们聊了 CPU cache 的层次结构、MESI 协议、false sharing，也看了 Linux 的线程模型和 futex 机制——这些都是多线程程序运行的物理舞台。但光知道舞台长什么样还不够，我们得亲自上台演一演。这一篇就是我们的第一次登台：从 `std::thread` 的构造开始，搞清楚线程怎么创建、怎么等待、怎么"放手不管"，以及在操作过程中有哪些一不留神就会踩的坑。

`std::thread` 是 C++11 引入的标准线程类，定义在 `<thread>` 头文件中。它是 C++ 标准库对操作系统线程的直接封装——在 Linux 上，每个 `std::thread` 对象背后就是一个 pthread，而 pthread 又通过 `clone()` 系统调用映射到一个内核调度实体。我们在上一篇提到的 1:1 模型，在这里就是具体体现。

## 从三种方式构造 std::thread

`std::thread` 的构造函数接受一个**可调用对象**（callable）以及可选的参数列表。C++ 为我们提供了好几种表达"可调用"的方式，我们一个一个来看。

### 函数指针

最朴素的方式就是传一个普通函数指针：

```cpp
#include <thread>
#include <iostream>

void print_hello(int id)
{
    std::cout << "Hello from thread " << id << "\n";
}

int main()
{
    std::thread t(print_hello, 42);
    t.join();
    return 0;
}
```

`std::thread t(print_hello, 42)` 做了这么几件事：首先，它把 `print_hello`（函数指针）和 `42`（参数）打包存到内部存储中；然后，它调用底层的 `pthread_create`（或等价的系统调用）创建一个新的操作系统线程；最后，新线程在那个独立的执行上下文中，用保存的参数调用 `print_hello(42)`。注意，参数 `42` 是被**复制**到线程的内部存储中的——关于参数传递的细节，下一篇我们会专门展开。

### Lambda 表达式

在实际工程中，lambda 是创建线程最常用的方式，因为它可以在调用点直接定义线程要做的事情，不需要额外声明一个函数：

```cpp
#include <thread>
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> data = {1, 2, 3, 4, 5};
    int sum = 0;

    std::thread t([&data, &sum]() {
        for (int v : data) {
            sum += v;
        }
    });

    t.join();
    std::cout << "Sum = " << sum << "\n";
    return 0;
}
```

这段代码能正常工作，但如果你仔细看的话，`[&data, &sum]` 是按引用捕获的——这在单线程场景下完全没问题，但如果线程 detach 了或者生命周期超出了 `data` 和 `sum` 的作用域呢？这就是一个悬垂引用的温床。我们先记住这个"气味"，下一篇会系统性地拆解它。

### 函数对象（Functor）

第三种方式是传递一个重载了 `operator()` 的类实例：

```cpp
#include <thread>
#include <iostream>
#include <vector>

class Accumulator {
public:
    Accumulator(const std::vector<int>& data, int& result)
        : data_(data), result_(result)
    {}

    void operator()() const
    {
        int local_sum = 0;
        for (int v : data_) {
            local_sum += v;
        }
        result_ = local_sum;
    }

private:
    const std::vector<int>& data_;  // 注意：引用成员
    int& result_;                    // 引用成员
};

int main()
{
    std::vector<int> data = {1, 2, 3, 4, 5};
    int result = 0;

    // 注意：这里需要用花括号或 lambda 避免最令人头疼的解析问题
    // std::thread t(Accumulator(data, result));  // 编译错误！被解析为函数声明
    Accumulator acc(data, result);
    std::thread t(acc);  // OK：拷贝 acc 到线程中

    t.join();
    std::cout << "Result = " << result << "\n";
    return 0;
}
```

这里有一个经典的 C++ 陷阱——如果你直接写 `std::thread t(Accumulator(data, result));`，编译器会把它解析为一个名为 `t` 的函数声明（参数类型是指向 `Accumulator` 的指针），而不是一个线程对象的定义。这是所谓的"最令人头疼的解析（most vexing parse）"问题。解决方式有好几种：用额外的花括号 `std::thread t{Accumulator(data, result)};`，用 lambda `std::thread t([&](){ ... });`，或者像上面那样先构造一个具名对象再传入。

三种方式各有适用场景。函数指针适合简单、无状态的线程函数；lambda 适合在调用点定义局部逻辑，是日常开发中最常见的方式；functor 适合需要携带状态的复杂任务——但注意引用成员带来的生命周期风险。实际项目中，90% 以上的场景用 lambda 就够了。

## join() vs detach()：两种截然不同的策略

线程创建之后，我们必须在它的生命周期结束之前做一个决定：**join** 还是 **detach**。这个决定直接关系到程序的正确性。

### join：等待线程完成

`join()` 是阻塞调用——当前线程会停在那里，等到目标线程执行完毕才继续往下走。类比一下就是：你派了一个人去干活，你站在原地等他干完，然后你们一起继续。这是最常用的模式，也是最安全的模式。

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void slow_work()
{
    std::cout << "Worker: starting...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Worker: done.\n";
}

int main()
{
    std::cout << "Main: launching thread\n";
    std::thread t(slow_work);
    std::cout << "Main: waiting for thread...\n";
    t.join();
    std::cout << "Main: thread finished, continuing\n";
    return 0;
}
```

运行这段代码，你会看到输出严格按照 Main 启动 -> Worker 启动 -> Worker 完成 -> Main 继续的顺序发生。`join()` 保证了线程的执行结果在 `join` 返回时对调用线程可见——这是一个 happens-before 关系。

### detach：放手不管

`detach()` 做的事情恰好相反——它把线程从 `std::thread` 对象的管理中"剥离"出来。剥离之后，线程在后台独立运行（所谓的 daemon thread / 后台线程），`std::thread` 对象不再持有任何对它的引用。你也没法再 join 它了——`std::thread` 对象的 `joinable()` 会返回 `false`。

```cpp
#include <thread>
#include <iostream>
#include <chrono>

void background_task()
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Background task finished\n";
}

int main()
{
    std::thread t(background_task);
    t.detach();

    std::cout << "Main: detached thread, sleeping 1 second...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Main: exiting\n";
    return 0;
}
```

如果你跑这段代码，大概率看不到 "Background task finished" 这行输出——因为主线程只等了 1 秒就退出了，而 detach 的线程需要 2 秒。进程退出时，所有线程（包括 detach 的线程）都会被强制终止，没有任何清理机会。这就是 detach 最大的风险：**你对线程的执行时机完全失去了控制**。

那么什么时候该用 detach 呢？说实话，在大多数应用代码中，detach 不是一个好的选择。它适合的场景非常有限——比如一个后台日志线程，它的任务是把日志从内存缓冲区刷到磁盘，你不关心它什么时候结束，只要它最终会把数据写出去就行。但即便在这种场景下，用一个 `joinable` 的线程配合显式的 shutdown 信号通常是更稳妥的做法。

### 不 join 也不 detach 的后果：std::terminate

如果你在一个 `joinable` 的 `std::thread` 对象上既不调用 `join()` 也不调用 `detach()`，让它自然走到析构函数——你的程序会调用 `std::terminate()` 直接崩溃。这不是建议，是标准规定的硬性行为：

```cpp
#include <thread>
#include <iostream>

void some_work()
{
    std::cout << "Working...\n";
}

int main()
{
    std::thread t(some_work);
    // 没有 join() 也没有 detach()
    // t 析构时调用 std::terminate()
    return 0;  // terminate called without an active exception
}
```

C++ 标准这样设计是有原因的。如果析构函数默默地帮你 join，那析构可能阻塞——这是很多开发者不愿意接受的（析构函数应该快）。如果析构函数默默地帮你 detach，那线程可能在对象销毁后访问已经不存在的引用——这是未定义行为，比崩溃更糟糕。标准选择直接 `terminate`，是在强迫你**显式地做出决定**：你要么等它完成（join），要么放手不管（detach），但你不能假装这个问题不存在。

这个设计哲学贯穿了整个 C++ 并发 API：不做隐式的、可能令人惊讶的事情，把决定权交给程序员。代价是你必须记住在每一条代码路径上都处理线程的 join/detach，包括异常路径。一个常见的模式是使用 RAII 包装器——在构造时保存线程，在析构时自动 join——这个话题我们会在本章节后续的文章中展开。

## 线程的标识与查询

### get_id()：线程的身份证号

每个线程都有一个唯一的标识符，类型是 `std::thread::id`。你可以通过 `std::thread::get_id()` 获取某个线程对象的 ID，也可以通过 `std::this_thread::get_id()` 获取当前线程的 ID。`std::thread::id` 支持比较操作和输出到 `std::ostream`，方便调试和日志：

```cpp
#include <thread>
#include <iostream>

void worker()
{
    std::cout << "Worker thread ID: "
              << std::this_thread::get_id() << "\n";
}

int main()
{
    std::thread t(worker);
    std::cout << "Main thread ID: "
              << std::this_thread::get_id() << "\n";
    std::cout << "Worker's thread ID (from main): "
              << t.get_id() << "\n";
    t.join();

    // join 或 detach 后，get_id() 返回默认构造的 id
    std::cout << "After join, worker ID: "
              << t.get_id() << "\n";
    return 0;
}
```

几点需要注意：`std::thread::id` 的具体值是实现定义的——不同编译器、不同平台输出的格式可能不一样（GCC 通常输出一个数字，MSVC 可能输出一个十六进制地址），不要依赖它的具体格式做逻辑判断。`join()` 或 `detach()` 之后，`get_id()` 返回的是默认构造的 `std::thread::id{}`，表示"不关联任何线程"——这跟一个默认构造的 `std::thread` 对象的 `get_id()` 返回值一样。

`thread::id` 最实用的场景是作为 `std::hash` 的键，用来给线程分配资源（比如每个线程一个独立的内存池或日志缓冲区）。也可以用它来检测"当前线程是不是主线程"，实现简单的线程安全断言。

### native_handle()：触碰操作系统原生句柄

`std::thread` 是标准库的抽象，但有时候你需要直接操作底层的操作系统线程——比如设置线程优先级、CPU 亲和性、或者线程名。`native_handle()` 返回的是与平台相关的原生线程句柄：在 Linux 上是 `pthread_t`，在 Windows 上是 `HANDLE`。

```cpp
#include <thread>
#include <iostream>

// 注意：以下代码是 Linux 专用的
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

void set_high_priority(std::thread& t)
{
#ifndef _WIN32
    sched_param param;
    param.sched_priority = 10;  // 较高的优先级（具体值取决于调度策略）
    pthread_setschedparam(t.native_handle(), SCHED_RR, &param);
#endif
}

int main()
{
    std::thread t([]() {
        std::cout << "High priority thread running\n";
    });
    set_high_priority(t);
    t.join();
    return 0;
}
```

这段代码显然是不可移植的——它只会在支持 pthread 的平台上编译通过。在实际项目中，通常会把平台相关的代码用 `#ifdef` 隔离，或者抽象成一个平台层。`native_handle()` 给了你一条"逃生通道"，让你在标准库不够用的时候能直接跟操作系统打交道。

### hardware_concurrency()：我有多少个核心可用

`std::thread::hardware_concurrency()` 是一个静态成员函数，返回一个提示值，表示当前系统上真正可以并发执行的线程数量——在大多数情况下就是 CPU 的逻辑核心数（包括超线程）。

```cpp
#include <thread>
#include <iostream>

int main()
{
    unsigned int cores = std::thread::hardware_concurrency();
    std::cout << "Hardware concurrency: " << cores << "\n";
    return 0;
}
```

这个值是提示性质的，不是保证。如果信息不可用，函数返回 0。在 8 核 16 线程的 CPU 上，它通常返回 16。在容器环境中，它可能返回容器被分配的 CPU 核心数而不是物理机的总核心数。最常见的用法是根据它来决定线程池的大小或者任务分片的数量——但不要把它当作精确值，用之前最好检查一下返回值是不是 0。

## 线程函数中的异常

这里有一个非常重要的规则：**异常永远不应该逃逸线程函数**。如果一个异常从线程函数中逃逸出去（即线程函数抛出了异常但没有在线程内部被 catch），`std::terminate()` 会被调用，程序直接崩溃。

```cpp
#include <thread>
#include <iostream>
#include <stdexcept>

void unsafe_worker()
{
    throw std::runtime_error("Oops, something went wrong!");
    // 异常逃逸线程函数 -> std::terminate()
}

int main()
{
    try {
        std::thread t(unsafe_worker);
        t.join();  // 永远到不了这里
    } catch (const std::exception& e) {
        // 这个 catch 捕获不到线程里的异常！
        // 线程函数中的异常和主线程的 try-catch 是完全隔离的
        std::cout << "Caught: " << e.what() << "\n";
    }
    return 0;
}
```

这个行为其实很合理。每个线程有自己独立的调用栈，异常处理机制（栈展开、catch 匹配）只在当前线程的栈上工作。如果异常穿透了线程函数，那就意味着没有一个 catch 块能接住它——除了 `std::terminate`。主线程的 `try-catch` 跟子线程的异常处理是完全隔离的两个世界。

正确的做法是在线程函数内部处理所有可能的异常，或者把异常信息通过某种机制（`std::promise`/`std::future`、`std::exception_ptr`）传递回调用者。一个最简单的防御模式是这样的：

```cpp
#include <thread>
#include <iostream>
#include <stdexcept>
#include <functional>

void safe_worker(std::function<void()> task)
{
    try {
        task();
    } catch (const std::exception& e) {
        // 在线程内部处理异常，或者记录下来
        std::cerr << "Thread caught exception: "
                  << e.what() << "\n";
    } catch (...) {
        std::cerr << "Thread caught unknown exception\n";
    }
}

int main()
{
    std::thread t(safe_worker, []() {
        throw std::runtime_error("Oops!");
    });
    t.join();  // OK：异常在线程内部被捕获，程序不会 terminate
    std::cout << "Main continues normally\n";
    return 0;
}
```

在后面的章节中我们会介绍 `std::async` 和 `std::promise`/`std::future`，它们提供了更优雅的方式来把子线程的异常传递回主线程。但在直接使用 `std::thread` 的场景下，上面这种"在线程内部 catch-all"的模式是最基本的防御手段。

## 基本模式：派生线程，作用域退出时 join

有了上面的知识，我们可以总结出一个最基本的线程使用模式：为每个子任务派生一个线程，在当前作用域退出之前 join 所有线程。用代码来表达就是：

```cpp
#include <thread>
#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>

void process_range(const std::vector<int>& input,
                   std::vector<int>& output,
                   std::size_t start,
                   std::size_t end)
{
    for (std::size_t i = start; i < end; ++i) {
        // 模拟一个计算密集型操作
        output[i] = input[i] * input[i];
    }
}

int main()
{
    constexpr std::size_t kDataSize = 10'000'000;
    constexpr unsigned int kNumThreads = 4;

    std::vector<int> input(kDataSize);
    std::vector<int> output(kDataSize);

    // 初始化输入数据
    for (std::size_t i = 0; i < kDataSize; ++i) {
        input[i] = static_cast<int>(i);
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    std::size_t chunk_size = kDataSize / kNumThreads;

    // 派生线程
    for (unsigned int i = 0; i < kNumThreads; ++i) {
        std::size_t start = i * chunk_size;
        std::size_t end = (i == kNumThreads - 1)
                              ? kDataSize
                              : start + chunk_size;
        threads.emplace_back(process_range,
                             std::cref(input),
                             std::ref(output),
                             start,
                             end);
    }

    // 在作用域退出前 join 所有线程
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  end_time - start_time);

    std::cout << "Processed " << kDataSize << " elements in "
              << ms.count() << " ms using "
              << kNumThreads << " threads\n";
    return 0;
}
```

这段代码的执行流程很清晰：把数据分成 N 份，每份交给一个线程处理，然后主线程等所有工作线程完成。`threads.emplace_back(...)` 把线程对象直接构造在 vector 中，避免额外的移动。最后的 for 循环逐一 join，确保所有线程都执行完毕后才退出。

这里有一个值得注意的细节：`output` 被按引用传入每个线程（通过 `std::ref`），但不同线程写入的是 `output` 的不同区间——没有重叠，所以不会产生 data race。这种"分区并行"模式是多线程编程中最容易写出正确代码的方式之一：只要保证每个线程只碰自己的那份数据，就不需要任何同步机制。

但这个模式有一个问题——如果某个线程的 `process_range` 函数抛出了异常，`threads` 的析构函数会在栈展开时被调用，而我们前面说过，`joinable` 的线程析构会调用 `std::terminate`。要解决这个问题，我们需要用 RAII 把 join 的逻辑包装起来，确保即使发生异常也能正确 join。这个改进版本我们会在后续的"线程所有权与 RAII"一文中实现。

## 在线运行

在线体验 std::thread 的三种构造方式、线程 ID 查询和数据分区并行处理：

<OnlineCompilerDemo
  title="std::thread 基础"
  source-path="code/examples/vol5/09_std_thread.cpp"
  description="体验函数指针、lambda 和 functor 三种线程构造方式及数据分区并行"
  allow-run
/>

## 小结

这一篇我们完成了对 `std::thread` 基本接口的全面梳理。我们看到了三种构造线程的方式——函数指针、lambda 和 functor，它们的本质都是传入一个可调用对象和参数。`join()` 和 `detach()` 是两种截然不同的线程管理策略：join 是"等我干完再走"，detach 是"你先走，我自己收尾"。如果你什么都不做就让 `std::thread` 析构，标准会毫不留情地调用 `std::terminate`——这是 C++ 在用最严厉的方式提醒你：线程的生命周期必须显式管理。

我们还了解了线程的标识（`get_id()`）、原生句柄（`native_handle()`）和硬件并发查询（`hardware_concurrency()`），以及一个容易被忽视但至关重要的规则：异常不应该逃逸线程函数，否则会触发 `std::terminate`。

最后，我们建立了一个基本的并行处理模式：数据分区 + 多线程处理 + 逐一 join。这个模式在简单场景下很好用，但它没有处理异常安全和 RAII——这是我们接下来要解决的问题。

下一篇我们要进入一个更深的话题：线程参数的传递机制。我们会看到 `std::thread` 的 decay-copy 语义是怎么工作的，为什么 `std::ref` 是一把双刃剑，以及 detach 和引用捕获结合在一起会引发什么样的灾难。真正的坑在后面。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch01-thread-lifecycle-raii/`。

## 练习

### 练习 1：并行数组转换

给定一个 `std::vector<double>`，使用 `std::thread` 把它的每个元素做平方根运算。要求：

1. 用 `std::thread::hardware_concurrency()` 获取核心数，据此决定分几个线程
2. 每个线程处理数组的一段区间
3. 所有线程完成之后，打印前 10 个结果用于验证

提示：注意 `hardware_concurrency()` 可能返回 0 的情况，以及数组大小不能被线程数整除时的处理。

### 练习 2：验证 terminate 行为

编写一个程序，故意让一个 `joinable` 的 `std::thread` 析构而不调用 `join()` 或 `detach()`。运行程序，观察 `std::terminate` 被调用时的输出。然后在 `main` 函数中用 `try-catch` 包裹这段代码，看看你能不能"捕获"这个 terminate——答案是：不能，`std::terminate` 不可被普通的 `try-catch` 捕获，它是程序的强制终止。

### 练习 3：线程 ID 映射

写一个程序，创建 N 个线程（比如 4 个），每个线程把自己的 `std::this_thread::get_id()` 存入一个共享的 `std::map<std::thread::id, int>`（键是线程 ID，值是线程编号 0-3）。因为多个线程同时写 map 是 data race，这里先简单处理：每个线程把结果输出到 `std::cout`，主线程记录下来即可。这个练习的目的是让你熟悉 `std::thread::id` 的基本用法。

## 参考资源

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [std::thread::join — cppreference](https://en.cppreference.com/w/cpp/thread/thread/join)
- [std::thread::detach — cppreference](https://en.cppreference.com/w/cpp/thread/thread/detach)
- [std::thread::hardware_concurrency — cppreference](https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency)
- [C++ Core Guidelines: CP.20 — Use RAII, never plain `lock()`/`unlock()`](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [What does `decay_copy` in the constructor of `std::thread` do? — StackOverflow](https://stackoverflow.com/questions/67947814/what-does-decay-copy-in-the-constructor-in-a-stdthread-object-do)
