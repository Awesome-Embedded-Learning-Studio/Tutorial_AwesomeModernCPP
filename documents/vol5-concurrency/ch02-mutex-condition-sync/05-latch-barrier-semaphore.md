---
chapter: 2
cpp_standard:
- 20
description: C++20 同步原语：一次性/多次使用的同步屏障与计数信号量，场景选择与工程模式
difficulty: advanced
order: 5
platform: host
prerequisites:
- condition_variable 与等待语义
reading_time_minutes: 19
related:
- atomic 操作
- 线程池设计
tags:
- host
- cpp-modern
- advanced
- mutex
title: latch、barrier 与 semaphore
---
# latch、barrier 与 semaphore

上一篇我们深入拆解了 `condition_variable` 的等待-通知机制——虚假唤醒、丢失唤醒、带谓词的 `wait`。有了这些基础，我们现在可以面对一个更实际的问题：很多时候我们并不需要"某个条件满足才继续"这种通用的等待语义，而是只需要"等到大家都到齐了再继续"或者"限制同时访问资源的线程数量"。这两种需求分别对应**屏障（barrier）**和**信号量（semaphore）**两种同步模式，而 C++20 终于把这两个概念以 `std::latch`、`std::barrier` 和 `std::counting_semaphore` 的形式纳入了标准库。

说实话，在此之前，我们只能用 mutex + condition_variable + 一个手动计数器来模拟这些模式——代码冗长、容易出错、而且每次都要重新写一遍。C++20 这三个原语的引入，本质上是把这些高频模式标准化了。但要用好它们，我们需要搞清楚每个原语的语义边界和适用场景，而不是拿着 hammer 把所有钉子都敲一遍。

## std::latch：一次性倒计时屏障

`std::latch` 定义在 `<latch>` 头文件中，它是一个**单向递减计数器**。你可以把它想象成一扇门，门前有一道闩（latch），闩的强度由初始计数决定。每有一个线程执行 `count_down()`，闩就松开一格；当计数减到零时，门打开，所有在 `wait()` 上阻塞的线程可以通过。关键特性是：**latch 是一次性的**——一旦计数归零，它就永远保持"打开"状态，不能重置。

`std::latch` 的 API 非常精简：构造时传入初始计数值 `expected`（类型是 `std::ptrdiff_t`）；`count_down(n = 1)` 将计数减 n（不阻塞）；`wait()` 阻塞当前线程直到计数归零；`arrive_and_wait(n = 1)` 是 `count_down(n)` 加上 `wait()` 的原子组合——当前线程既贡献一个递减，又等待计数归零；`try_wait()` 是非阻塞检查——当计数器归零时返回 `true`（注意：允许极低概率的虚假返回 `false`）。我们来通过一个具体的场景理解它的用法。

### 模式：一次性初始化

假设我们的程序启动时需要初始化三个子系统——日志、配置、网络连接——每个子系统由一个独立线程负责初始化，而主线程必须等到所有子系统就绪后才能开始业务逻辑。这是一个典型的一次性同步场景：

```cpp
#include <latch>
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

void init_logger()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Logger initialized\n";
}

void init_config()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Config loaded\n";
}

void init_network()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << "Network connected\n";
}

int main()
{
    constexpr int kInitCount = 3;
    std::latch init_done(kInitCount);

    std::vector<std::thread> threads;
    threads.emplace_back([&init_done]() {
        init_logger();
        init_done.count_down();
    });
    threads.emplace_back([&init_done]() {
        init_config();
        init_done.count_down();
    });
    threads.emplace_back([&init_done]() {
        init_network();
        init_done.count_down();
    });

    init_done.wait();
    std::cout << "All subsystems ready, starting application\n";

    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

这里每个初始化线程在完成自己的任务后调用 `init_done.count_down()`，主线程调用 `init_done.wait()` 阻塞等待。当三个 `count_down` 都执行完毕后，主线程被唤醒，继续执行。注意工作线程调用的是 `count_down()` 而不是 `arrive_and_wait()`——因为工作线程不需要等其他人，它做完自己的事情就可以退出了，只有主线程需要等待。

如果工作线程也想"做完自己那部分然后等所有人一起继续"，那就用 `arrive_and_wait()`：

```cpp
void worker(int id, std::latch& sync)
{
    std::cout << "Worker " << id << " phase 1 done\n";
    sync.arrive_and_wait();  // 贡献一个递减，同时等待计数归零
    std::cout << "Worker " << id << " phase 2 starts\n";
}
```

`arrive_and_wait()` 的语义是原子的"递减 + 等待"——调用它的线程自己也会被阻塞，直到计数归零。在内部，它等价于 `count_down(); wait();`，但标准保证这两步的原子性。这意味着在"递减"和"等待"之间不会出现其他线程把计数减到零并导致等待者错过唤醒的情况。

有一个容易忽视的细节：`count_down` 的参数可以大于 1。比如一个线程负责完成三项任务，可以一次性 `count_down(3)`。如果传入的值会导致计数变为负数，行为是未定义的——所以调用者必须保证计数不会减过头。

## std::barrier：可重用的阶段同步

`std::latch` 解决了"一次性等大家都到齐"的问题，但很多并行算法需要**反复同步**——比如迭代式计算中，每一轮迭代都要求所有线程完成当前步骤后才能进入下一步。如果用 latch，每轮迭代都得创建一个新的 latch 对象，这既浪费又不优雅。`std::barrier` 就是为此设计的：它是一个**可重用**的同步屏障，每次所有参与线程都到达屏障点后，屏障自动重置，可以用于下一轮同步。

`std::barrier` 定义在 `<barrier>` 头文件中，它是一个类模板 `std::barrier<CompletionFunction>`，其中 `CompletionFunction` 默认是一个空函数。构造时传入参与线程的数量（以及可选的完成函数）。核心 API 有三个：`arrive()` 通知屏障"我到了"但不阻塞；`arrive_and_wait()` 通知并阻塞，直到所有线程都到达；`arrive_and_drop()` 通知并永久减少参与线程数（用于动态缩减参与者的场景）。

### 基本用法：多阶段并行计算

先看一个简单的多阶段并行计算场景。假设我们有 4 个工作线程，每个线程需要依次执行三个阶段（phase），每个阶段之间要求所有线程同步：

```cpp
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <syncstream>

int main()
{
    constexpr int kNumThreads = 4;
    std::barrier sync_point(kNumThreads);

    auto worker = [&sync_point](int id) {
        for (int phase = 1; phase <= 3; ++phase) {
            // 每个线程独立完成当前阶段的工作
            std::osyncstream(std::cout)
                << "Thread " << id << " phase " << phase << " working\n";

            // 到达屏障，等待其他线程
            sync_point.arrive_and_wait();

            std::osyncstream(std::cout)
                << "Thread " << id << " phase " << phase << " done\n";
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

这段代码的关键在于：每个线程在完成一个阶段后调用 `arrive_and_wait()`。当所有 4 个线程都调用了 `arrive_and_wait()` 时，屏障"打开"——所有线程被同时释放，进入下一个阶段。屏障自动重置为初始计数，等待下一轮。你会发现，整个过程完全不需要额外的 mutex 或者 condition_variable，barrier 内部处理了所有的等待和唤醒逻辑。

### 完成函数：阶段间的集中处理

`std::barrier` 有一个强大但不太为人所知的特性——**完成函数（completion function）**。当所有参与线程都到达屏障时，屏障会在释放线程之前，在其中一个到达线程的上下文中执行这个完成函数。这个机制非常适合"归约"操作：每个线程独立计算部分结果，当所有线程到达屏障时，完成函数负责汇总这些部分结果。

```cpp
#include <barrier>
#include <iostream>
#include <thread>
#include <vector>
#include <array>
#include <numeric>

int main()
{
    constexpr int kNumThreads = 4;
    constexpr int kDataSize = 1000;
    constexpr int kChunkSize = kDataSize / kNumThreads;

    std::array<int, kDataSize> data;
    for (int i = 0; i < kDataSize; ++i) {
        data[i] = i + 1;
    }

    // 每个线程的部分和
    std::array<long long, kNumThreads> partial_sums{};
    long long total_sum = 0;

    // 完成函数：在所有线程到达后，汇总部分和
    auto on_completion = [&]() noexcept {
        total_sum = std::accumulate(partial_sums.begin(),
                                     partial_sums.end(), 0LL);
    };

    std::barrier sync_point(kNumThreads, on_completion);

    auto worker = [&](int id) {
        int start = id * kChunkSize;
        int end = start + kChunkSize;

        // 阶段 1：每个线程计算自己那部分的和
        long long local_sum = 0;
        for (int i = start; i < end; ++i) {
            local_sum += data[i];
        }
        partial_sums[id] = local_sum;

        // 同步并触发完成函数汇总
        sync_point.arrive_and_wait();

        // 阶段 2：所有线程都能看到 total_sum
        std::osyncstream(std::cout)
            << "Thread " << id << ": total_sum = " << total_sum << "\n";
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

这里我们定义了一个 `on_completion` lambda 作为 barrier 的完成函数。当所有线程都到达屏障后，屏障会调用这个函数，把 `partial_sums` 中的部分和累加到 `total_sum` 中。完成函数执行完毕后，所有线程才被释放——这意味着线程在 `arrive_and_wait()` 返回后可以安全地读取 `total_sum`，因为完成函数已经执行完了。

完成函数有几个约束需要注意。首先，它必须是 `noexcept` 的——因为 barrier 在释放线程之前执行它，如果它抛出异常，整个程序会调用 `std::terminate()`。其次，完成函数在"其中一个到达线程"的上下文中执行（具体是哪个线程由实现决定），所以它不应该做阻塞操作或者耗时操作。最后，完成函数中对共享状态的访问不需要额外加锁——因为在完成函数执行时，其他线程都还在屏障上阻塞，没有并发访问。

### arrive() 与 arrive_and_drop()

`arrive()` 是"只报到不等"的版本——线程通知屏障"我到了"，然后立刻返回，不阻塞。这适合"生产者只管到达，消费者负责等待"的场景。不过要注意，`arrive()` 返回一个 `arrival_token`，这个 token 目前在标准中没有实际用途（它是为未来扩展预留的），但你仍然需要确保每个 `arrive()` 调用对应一个参与线程。

`arrive_and_drop()` 是一个更特殊的操作——它通知屏障"我到了，但以后不参与了"。每次调用 `arrive_and_drop()`，屏障的参与计数永久减 1。这适合线程池中"工作线程动态退出"的场景：线程做完最后一轮工作后调用 `arrive_and_drop()`，后续的同步轮次就不再等它了。

## std::counting_semaphore：通用计数信号量

`std::latch` 和 `std::barrier` 解决的是"线程间同步"的问题——大家到齐了一起走。而 `std::counting_semaphore` 解决的是"资源计数"的问题——限制同时访问某种资源的线程数量。它定义在 `<semaphore>` 头文件中，是一个类模板 `std::counting_semaphore<LeastMaxValue>`，其中 `LeastMaxValue` 是信号量的最大值（默认是一个实现定义的值，至少和 `ptrdiff_t` 的最大值一样大）。

信号量的核心概念很简单：内部维护一个计数器。`acquire()` 尝试将计数器减 1，如果计数器已经为 0 就阻塞等待；`release(n = 1)` 将计数器加 n，并唤醒等待中的线程。这种"获取-释放"的语义可以建模很多实际问题。

`std::counting_semaphore<1>` 有一个类型别名 `std::binary_semaphore`——当最大值为 1 时，信号量退化为一个简单的二元信号量，计数器只有 0 和 1 两个状态。

### 模式：资源池

假设我们有一个数据库连接池，最多允许 3 个线程同时持有连接。用 `counting_semaphore` 来控制非常自然：

```cpp
#include <semaphore>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <syncstream>

class DatabaseConnectionPool {
public:
    explicit DatabaseConnectionPool(int max_connections)
        : semaphore_(max_connections)
    {}

    void use_connection(int thread_id)
    {
        semaphore_.acquire();  // 获取一个连接名额
        std::osyncstream(std::cout)
            << "Thread " << thread_id << " acquired connection\n";

        // 模拟使用连接
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::osyncstream(std::cout)
            << "Thread " << thread_id << " releasing connection\n";
        semaphore_.release();  // 释放连接名额
    }

private:
    std::counting_semaphore<> semaphore_;
};

int main()
{
    DatabaseConnectionPool pool(3);  // 最多 3 个并发连接

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(&DatabaseConnectionPool::use_connection,
                             &pool, i);
    }
    for (auto& t : threads) {
        t.join();
    }
    return 0;
}
```

8 个线程竞争 3 个连接名额。前 3 个线程立即获得连接，接下来的 5 个线程在 `acquire()` 上阻塞。每当一个线程 `release()`，一个等待中的线程被唤醒获得连接。整个过程完全由信号量的计数来控制，不需要任何 mutex 或 condition_variable。

### std::binary_semaphore：信号量形态的 mutex

`std::binary_semaphore` 是 `std::counting_semaphore<1>` 的别名，计数器只有 0 和 1 两种状态。它可以用在需要简单互斥的场景中，比如线程间的一次性信号通知：

```cpp
#include <semaphore>
#include <iostream>
#include <thread>

std::binary_semaphore signal{0};

void waiting_thread()
{
    std::cout << "Waiting for signal...\n";
    signal.acquire();
    std::cout << "Signal received, proceeding\n";
}

void signaling_thread()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Sending signal\n";
    signal.release();
}

int main()
{
    std::thread t1(waiting_thread);
    std::thread t2(signaling_thread);
    t1.join();
    t2.join();
    return 0;
}
```

信号量的初始值为 0（构造函数参数），`waiting_thread` 在 `acquire()` 上阻塞；`signaling_thread` 调用 `release()` 把计数器从 0 变成 1，唤醒等待线程。

你可能会问：`binary_semaphore` 和 `mutex` 有什么区别？从能力上看它们很相似——都能实现互斥和等待-通知。但语义上有关键差异：mutex 强调**所有权**（谁 lock 谁 unlock），而信号量没有所有权概念——线程 A 可以 `acquire()`，线程 B 来 `release()`。这种解耦在某些场景下非常有用（比如生产者-消费者中，生产者 release 信号量通知消费者），但也意味着信号量不能替代 mutex 来保护临界区——因为你无法保证只有持锁线程才能解锁。

### 信号量与条件变量的比较

既然信号量也能做等待-通知，为什么我们还需要 condition_variable？反过来，既然 condition_variable 更通用，为什么 C++20 还要引入信号量？这个问题的核心在于两者的**语义复杂度**和**性能特征**。

信号量的优势在于轻量。它不需要配合 mutex 使用（内部自己维护状态），不需要处理虚假唤醒，API 只有 `acquire`/`release` 两个核心操作。对于简单的资源计数或一次性通知场景，信号量的代码比 condition_variable 简洁得多。从性能上看，信号量通常基于平台原生的信号量实现（Linux 上的 `sem_t`，Windows 上的 `Semaphore` 对象），在简单的等待-通知场景中可能比 condition_variable 更快——因为 condition_variable 需要配合 mutex 工作，每次 wait/notify 都涉及 mutex 的获取和释放。

条件变量的优势在于**表达力**。当等待条件不是简单的"计数器是否为 0"，而是"队列是否为空且 shutdown 标志未设置"这种复合条件时，condition_variable 配合 mutex 和谓词可以精确表达这种逻辑。条件变量还支持超时等待（`wait_for`/`wait_until`）。信号量的 `acquire()` 本身不支持超时，但 C++20 同时提供了 `try_acquire_for()` 和 `try_acquire_until()` 用于带超时的获取——如果你需要更精细的超时控制或者复合条件判断，condition_variable 仍然是更好的选择。

一句话总结选择策略：如果你的同步逻辑可以用"计数"来表达，优先用信号量；如果你的同步逻辑涉及复杂的条件判断或者需要超时，用 condition_variable。

## 没有 C++20 怎么办：mutex + CV 模拟

如果你的项目还在用 C++17 或者更早的标准，不要灰心——这三个原语的语义都可以用 mutex + condition_variable + 一个计数器来模拟。虽然代码更冗长，但理解这些模拟实现有助于你深入理解 C++20 原语的底层机制。

### 模拟 latch

```cpp
#include <mutex>
#include <condition_variable>

class Latch {
public:
    explicit Latch(std::ptrdiff_t count)
        : count_(count)
    {}

    void count_down(std::ptrdiff_t n = 1)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ -= n;
        if (count_ <= 0) {
            cv_.notify_all();
        }
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return count_ <= 0; });
    }

    void arrive_and_wait(std::ptrdiff_t n = 1)
    {
        count_down(n);
        wait();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::ptrdiff_t count_;
};
```

我们看到这个模拟实现恰好是上一篇学到的"带谓词 wait + notify_all"模式的标准运用。`count_down` 在持锁状态下递减计数器，当计数归零时调用 `notify_all` 唤醒所有等待者。`wait` 用带谓词的 `wait` 防止虚假唤醒和丢失唤醒。`arrive_and_wait` 把 `count_down` 和 `wait` 组合在一起——注意这里没有原子性保证（`count_down` 释放锁之后、`wait` 获取锁之前，其他线程可能把计数减到零），但因为 `wait` 带谓词，即使通知先发生了也不会丢失。

### 模拟 barrier

```cpp
#include <mutex>
#include <condition_variable>

class Barrier {
public:
    explicit Barrier(std::ptrdiff_t count)
        : initial_count_(count), count_(count), generation_(0)
    {}

    void arrive_and_wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::ptrdiff_t gen = generation_;
        if (--count_ == 0) {
            // 所有线程到齐，重置屏障
            generation_++;
            count_ = initial_count_;
            cv_.notify_all();
        } else {
            cv_.wait(lock, [this, gen] { return gen != generation_; });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::ptrdiff_t initial_count_;
    std::ptrdiff_t count_;
    std::ptrdiff_t generation_;
};
```

barrier 的模拟比 latch 复杂的地方在于"可重用"。我们不能简单地在计数归零时重置——因为可能有上一轮的线程还没从 `wait` 中返回，新一轮的线程已经开始 `arrive_and_wait` 了。解决方案是引入一个**代数（generation）**计数器：每次屏障重置时递增 generation，等待线程检查的是"我这一代的 generation 是否已经变化"——如果变了，说明屏障已经打开，可以继续了。

这个 generation 技巧是实现可重用屏障的核心手法，也是 C++20 `std::barrier` 内部使用的机制。理解了这个技巧，你在阅读标准库实现或者第三方并发库时就不会对 generation 计数器感到陌生了。

## 场景选择指南

我们现在有了五个主要的同步原语（mutex、condition_variable、latch、barrier、counting_semaphore），面对一个具体的同步需求时该如何选择？笔者根据自己的经验总结了一个简单的决策路径。

如果你的需求是"保护临界区，同一时刻只有一个线程能进入"，用 mutex（配合 `lock_guard` 或 `unique_lock`）。如果你的需求是"等某个条件成立"，用 condition_variable 配合 mutex 和谓词。如果你的需求是"等 N 个线程都完成某件事后一起继续，且只需同步一次"，用 latch。如果你的需求是"反复同步——每轮迭代、每个阶段都要等大家到齐"，用 barrier。如果你的需求是"限制同时访问某种资源的线程数量"或者"简单的线程间信号通知"，用 counting_semaphore。

有时候一个场景可能同时满足多个条件——比如 barrier 内部可以用 condition_variable 模拟，counting_semaphore 也可以用来做一次性通知（退化为 binary_semaphore）。选择的关键是看哪个原语的语义最匹配你的问题——语义匹配度越高，代码越不容易出错。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 练习

### 练习 1：多阶段并行矩阵计算

给定一个大小为 N x N 的整数矩阵，使用 4 个线程并行计算矩阵的转置和所有元素之和。要求将计算分为三个阶段：阶段一，每个线程计算矩阵的一部分元素之和；阶段二，汇总所有部分和得到总和；阶段三，每个线程负责转置矩阵的一部分。阶段一和阶段三之间、阶段三之后各需要一个同步点。

提示：使用 `std::barrier` 配合完成函数。阶段一的完成函数负责汇总部分和，阶段三之后主线程需要等待所有工作线程完成。思考一下：阶段二只有一个汇总操作，它应该在工作线程中执行还是作为完成函数执行？

### 练习 2：用 counting_semaphore 实现有界阻塞队列

用 `std::counting_semaphore`（而不是 condition_variable）重新实现上一篇中的 `BoundedQueue`。提示：你需要两个信号量——`items_available` 初始为 0（追踪队列中的元素数），`spaces_available` 初始为队列容量（追踪剩余空位数）。`push` 时先 `spaces_available.acquire()`，加锁放入元素，`items_available.release()`；`pop` 时先 `items_available.acquire()`，加锁取出元素，`spaces_available.release()`。注意：你仍然需要 mutex 来保护队列容器本身——信号量只控制"能不能操作"，不保护数据结构的一致性。

### 练习 3：用 mutex + condition_variable 模拟 counting_semaphore

用 `std::mutex`、`std::condition_variable` 和一个内部计数器实现一个简单的计数信号量类，提供 `acquire()`、`release()` 和 `try_acquire()` 方法。`try_acquire()` 尝试获取一个资源，成功返回 `true`，计数器为零时返回 `false`（不阻塞）。写一个简单的测试程序验证你的实现：创建 5 个线程竞争初始计数为 2 的信号量，观察同时获得资源的线程数是否不超过 2。

## 参考资源

- [std::latch -- cppreference](https://en.cppreference.com/w/cpp/thread/latch)
- [std::barrier -- cppreference](https://en.cppreference.com/w/cpp/thread/barrier)
- [std::counting_semaphore -- cppreference](https://en.cppreference.com/w/cpp/thread/counting_semaphore)
- [Synchronization Primitives in C++20 -- KDAB](https://www.kdab.com/synchronization-primitives-in-c20/)
- [Latches and Barriers -- Modernes C++](https://www.modernescpp.com/index.php/latches-and-barriers/)
- [Semaphores in C++20 -- Modernes C++](https://www.modernescpp.com/index.php/semaphores-in-c-20/)
- [P0666R2: Revised Latches and Barriers for C++20 (提案文档)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0666r2.pdf)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
