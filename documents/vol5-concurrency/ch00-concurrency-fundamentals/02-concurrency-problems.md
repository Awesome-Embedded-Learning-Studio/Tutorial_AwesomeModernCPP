---
title: "并发基本问题"
description: "识别并发编程中最常见的 bug：data race、race condition、死锁、活锁、饥饿与优先级反转"
chapter: 0
order: 2
tags:
  - host
  - cpp-modern
  - beginner
  - atomic
  - mutex
difficulty: beginner
platform: host
reading_time_minutes: 25
cpp_standard: [11, 17, 20]
prerequisites:
  - "为什么需要并发"
related:
  - "mutex 与 RAII 守卫"
  - "std::atomic 原子操作"
---

# 并发基本问题

上一篇我们聊了"为什么需要并发"，建立了基本的判断力。但知道为什么还不够，我们还需要知道并发代码到底会出什么问题。说实话，并发 bug 令人头疼的地方不在于它有多复杂——而在于它**不可预测**。一个多线程程序在你本机跑了十万次都正常，上线后凌晨三点在客户环境崩了，你拿到 dump 一看，跟你的预期完全对不上！

这些问题实际上倒是都有确定的概念的，我们可以先简单的列写出来：data race、race condition、死锁、活锁、饥饿和优先级反转。每个问题我们都会给出代码示例——有错误版本，也有修复版本。我们的目标不是记住定义，而是培养一种直觉：看到一段多线程代码，你能很快判断出它哪里可能有问题。

## data race：C++ 标准规定的未定义行为

这是整卷最重要的一节。如果你只记住这一篇的一个知识点，就记住这个：**data race 在 C++ 标准中是未定义行为（Undefined Behavior, UB）**。不是"可能出错"，不是"结果不确定"，是完完全全的 UB——意味着编译器有权在 data race 发生时做出任何事情，包括但不限于返回错误结果、崩溃、或者表面上看起来正常但暗中埋下隐患。

### C++ 标准怎么说

C++ 标准（[intro.races]）对 data race 的定义是：当两个线程访问同一个内存位置，至少有一个访问是写入，并且它们之间没有 happens-before 关系时，就构成了 data race。任何 data race 都会导致未定义行为。

为什么标准要规定得这么严格？Hans Boehm（C++ 内存模型的主要设计者之一）在一篇文章中解释了原因：如果允许 data race 有任何确定的语义（比如"可能读到旧值"），编译器的很多优化就不得不被禁止。因为编译器需要对单线程代码做指令重排、循环变换、常量传播等优化，而这些优化在多线程环境下可能改变 data race 的结果。标准选择把 data race 定义为 UB，就是为了不限制编译器的优化能力——代价是程序员必须保证自己的程序没有 data race。

### 一个最小的 data race 示例

```cpp
#include <thread>
#include <iostream>

// 不知道有没有学习单片机的朋友，笔者就注意到很多人很喜欢直接丢一个全局变量放在这里
// 当然，自己熟悉不是一种罪过，但是下面的代码中，我们这样编程就会出现问题。。。
int counter = 0;  // 全局变量，非 atomic

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        ++counter;  // 非原子写入
    }
}

int main()
{
    std::thread t1(increment, 1000000);
    std::thread t2(increment, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter << "\n";
    // 期望 2000000，实际可能是任何值：1345687, 1789234, ...
    return 0;
}
```

`++counter` 看起来是一条语句，但在机器层面它是"读 → 加 → 写"三步操作。两个线程同时执行这个序列时，可能发生这样的情况：线程 A 读到 counter=100，线程 B 也读到 counter=100，线程 A 写入 101，线程 B 也写入 101——一次增量丢失了。在一个百万次的循环里，这种丢失会大量发生，最终结果远小于期望的 2000000。

### 修复：用 std::atomic

最直接的修复方式是把 `counter` 改成 `std::atomic<int>`：

```cpp
#include <thread>
#include <iostream>
#include <atomic>

std::atomic<int> counter{0};

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
        // 或简单地写 ++counter;
    }
}

int main()
{
    std::thread t1(increment, 1000000);
    std::thread t2(increment, 1000000);

    t1.join();
    t2.join();

    std::cout << "counter = " << counter.load() << "\n";
    // 现在稳定输出 2000000
    return 0;
}
```

`std::atomic` 保证了 `fetch_add` 是原子的——不会有中间状态被其他线程看到。关于 `memory_order_relaxed` 以及其他内存序选项，我们会在后面的原子操作章节深入展开。现在只需要知道：`std::atomic` 可以消除 data race。

另外，用 `std::mutex` 保护临界区也能消除 data race，对于更复杂的临界区逻辑来说 mutex 往往更合适。选择 atomic 还是 mutex 取决于你的临界区有多复杂——如果只是一个简单的计数器，atomic 更轻量；如果临界区包含多个变量的协同修改，mutex 更安全也更清晰。

## race condition：逻辑层面的竞态

race condition 和 data race 经常被混用，但它们不是同一个概念。data race 是 C++ 标准层面的定义（两个无同步的冲突访问），而 race condition 是一个更宽泛的概念：**程序的输出依赖于线程的执行顺序**，而这个顺序是不确定的。

一个经典的 race condition 例子是"检查-然后-操作"（check-then-act）模式：

```cpp
#include <thread>
#include <iostream>
#include <vector>

std::vector<int> data;

void add_if_not_full(int value)
{
    if (data.size() < 100) {     // 检查
        data.push_back(value);   // 操作
    }
}
```

即使我们用 `std::mutex` 保护了 `push_back`（从而没有 data race），这个函数仍然有 race condition：两个线程可能同时通过 `size() < 100` 的检查，然后都执行 `push_back`，导致 vector 里实际装了超过 100 个元素。问题不在于内存访问是否冲突，而在于"检查"和"操作"之间有一个时间窗口，其他线程可以插进来改变状态。

修复的关键是让"检查"和"操作"成为一个不可分割的原子操作——在 mutex 章节我们会详细展开怎么做到这一点。

可以这样总结两者的关系：data race 一定是 race condition（因为结果依赖于交错顺序），但 race condition 不一定是 data race（即使用了正确的同步原语，逻辑上仍然可能有竞态）。消除 data race 是底线要求，消除 race condition 需要更仔细的接口设计。

## 死锁：永远的等待

死锁可能是最广为人知的并发 bug 了。它的定义是：两个或多个线程互相等待对方持有的资源，导致所有线程都无法继续执行。（笔者在写操作系统那段时间简直是天天碰见，动一下你动一下啊！）

死锁的发生需要同时满足四个条件（称为 Coffman 四条件）：
1. 互斥（资源同一时刻只能被一个线程持有）
2. 持有并等待（线程持有至少一个资源，同时等待其他资源）、
3. 不可抢占（资源不能被强制剥夺）、
4. 循环等待（存在一个线程等待环）。

只要打破其中任何一个条件，死锁就不会发生。但是很不幸运的是，这四个条件在实际代码中往往非常容易同时满足。

来一个最小的死锁复现吧！

```cpp
#include <thread>
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 先锁 A
    std::cout << "thread1: locked A, waiting for B\n";
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 再锁 B
    std::cout << "thread1: locked A and B\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 先锁 B
    std::cout << "thread2: locked B, waiting for A\n";
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 再锁 A
    std::cout << "thread2: locked A and B\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
```

如果 thread1 拿到 mtx_a 的同时 thread2 拿到 mtx_b，双方就卡住了——thread1 等 mtx_b（被 thread2 持有），thread2 等 mtx_a（被 thread1 持有），谁都不会放手。

### 修复：统一的锁顺序

最实用的死锁预防策略是**统一锁顺序**：所有需要同时获取多个锁的代码，必须按照相同的顺序获取。如果 thread1 和 thread2 都先锁 A 再锁 B，死锁就不可能发生——因为只有一个线程能先拿到 A，另一个会在 A 上等待，不会在持有 B 的同时去等 A。

C++17 提供了 `std::scoped_lock`，它可以一次性获取多个互斥量，并且使用避免死锁的算法（内部尝试不同的获取顺序）：

```cpp
#include <thread>
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id)
{
    // scoped_lock 同时获取 mtx_a 和 mtx_b，内部避免死锁
    std::scoped_lock lock(mtx_a, mtx_b);
    std::cout << "thread" << id << ": locked both mutexes\n";
}

int main()
{
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
    return 0;
}
```

`scoped_lock` 的底层使用了 `std::try_lock` 的策略：尝试按某种顺序获取所有锁，如果有获取失败的，就释放已获取的锁重试。这是一种避免死锁但不保证公平性的方式。在后面的 mutex 章节我们会更深入地讨论死锁预防的各种策略。

## 活锁：忙碌的等待

活锁和死锁刚好反过来：线程没卡住，CPU 在转，但程序就是不往前走。

一个典型的场景是"礼貌退让"——两个线程在窄桥上相遇，各自退后让对方先过，然后又同时前进，再次相遇，再次退后……在代码里，这种事经常发生在基于重试的锁策略中：冲突之后双方都回退重试，但回退的节奏太一致了，导致每次重试都会再次撞上。

我们看一段简化的代码：

```cpp
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>

std::atomic<bool> flag1{false};
std::atomic<bool> flag2{false};

void thread1()
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        flag1.store(true);
        if (flag2.load()) {
            // 对方也想进，我退让
            flag1.store(false);
            continue;
        }
        // 进入临界区
        std::cout << "thread1 in critical section\n";
        flag1.store(false);
        return;
    }
    std::cout << "thread1: gave up after 100 attempts\n";
}

void thread2()
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        flag2.store(true);
        if (flag1.load()) {
            // 对方也想进，我退让
            flag2.store(false);
            continue;
        }
        // 进入临界区
        std::cout << "thread2 in critical section\n";
        flag2.store(false);
        return;
    }
    std::cout << "thread2: gave up after 100 attempts\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
```

这段代码的问题在于：如果两个线程的执行节奏恰好对上了，它们就会不断互相退让。当然，实际运行中由于调度的不确定性，它们最终大概率能进临界区（所以代码里用了有限次重试兜底），但活锁的风险是真实存在的。

怎么解决？思路是引入**随机退避（random backoff）**——冲突之后不要立刻重试，而是等一个随机时间再上，这样两个线程的节奏就很难总是同步了。这个思路在网络协议里也随处可见，比如以太网的 CSMA/CD 就是靠随机退避来解决信道冲突的。

## 饥饿：永远轮不到

饥饿和死锁不一样：死锁是所有线程都卡住，饥饿则是某些线程被"饿着"——它想拿资源，但一直轮不到它，而其他线程该跑跑该吃吃。

最常见的场景是不公平的调度策略。比如一个读写锁总是优先授予读锁，那在持续有读请求的情况下，写线程可能永远等不到机会——这就是"写者饥饿"。类似地，线程池的任务队列如果用了优先级调度，低优先级任务也可能永远排不上队。

解决饥饿的核心思路是引入**公平性**，具体做法取决于场景：读写锁可以换成写者优先策略，任务队列可以用轮询或优先级老化，锁的实现可以用 ticket lock 这类公平锁。公平性通常会牺牲一些吞吐量——毕竟公平的调度策略比贪婪策略更保守——但这是保证系统稳定运行的必要代价。

## 优先级反转：当高优先级被低优先级阻塞

优先级反转是一个隐蔽但影响巨大的问题。有没有嵌入式来的朋友？RTOS都玩过吧，我相信大家八股文背诵的一个都比一个六！最经典的案例是 1997 年 NASA 的 **Mars Pathfinder** 火星探测器——探测器上的实时系统跑着跑着就复位了，地面团队排查了好一阵才发现是优先级反转在捣鬼：高优先级的总线管理任务被低优先级的气象任务间接卡死，系统反复重启。

我们把这个过程拆开看看。假设有三个任务 `high_prio_task`、`mid_prio_task`、`low_prio_task`，优先级依次递减。`low_prio_task` 先拿到一把锁，正在用它；这时候 `mid_prio_task` 就绪了，优先级更高，于是抢占了 `low_prio_task`。紧接着 `high_prio_task` 也就绪了——它优先级最高，但需要 `low_prio_task` 持有的那把锁，于是只能阻塞等待。可问题是，`low_prio_task` 此刻已经被 `mid_prio_task` 抢占了，根本没机会运行，自然也没办法释放锁。结果就是：`high_prio_task` 这个最高优先级的任务，被优先级比自己低的 `mid_prio_task` 间接卡住了。这不是哪段代码写错了，而是调度机制本身的结构性缺陷。

回到 C++ 这边，`std::mutex` 本身没有优先级概念，标准库也不管调度策略，所以在通用平台上你一般不用操心这个。但如果你在 RTOS 上跑 C++（比如 FreeRTOS、ThreadX），优先级反转就是绕不开的问题。最常用的解法是**优先级继承（priority inheritance）**——当 `low_prio_task` 持有 `high_prio_task` 需要的锁时，临时把 `low_prio_task` 的优先级拉到和 `high_prio_task` 一样高，这样 `mid_prio_task` 就抢不过它了，`low_prio_task` 能尽快把锁释放掉，`high_prio_task` 也就不用一直干等。POSIX 线程库提供了 `pthread_mutexattr_setprotocol` 配合 `PTHREAD_PRIO_INHERIT` 来启用这个机制，主流 RTOS 也基本都支持类似的操作。

## 把问题分类：我们的路线图

到这里我们已经认识了并发编程里最常见的问题家族。为了方便后续学习，我们把它们分成三类：

**正确性问题**是底线，必须消除。data race 导致 UB，race condition 导致逻辑错误——这些都是"程序行为不对"的问题。消除 data race 的工具是 atomic 和 mutex，消除 race condition 还需要仔细的接口设计（让检查和操作不可分割）。这是 ch01-ch03 的核心内容。

**活性问题**更隐蔽，需要通过分析和测试来发现。死锁是"所有线程都卡住"，活锁是"线程在跑但没进展"，饥饿是"部分线程被饿着"。解决它们需要特定的策略：统一锁顺序防死锁、随机退避防活锁、公平调度防饥饿。这是 ch02 和 ch04 的内容。

**实时性问题**在一般应用里不太突出，但在嵌入式和实时系统中至关重要。优先级反转是最典型的例子，需要操作系统的支持（优先级继承协议）。如果你的目标平台是 STM32 等 RTOS 环境，ch01-ch04 中会穿插嵌入式场景的讨论。

先正确性，再性能。先消除 data race 和 race condition，再考虑活性和实时性问题。这个顺序很重要——如果你的程序连正确性都保证不了，谈死锁预防或者优先级继承是没有意义的。

## 练习

### 练习 1：复现 data race

编译并运行上面的 data race 示例，多次运行观察结果。然后改用 `std::atomic<int>`，确认结果稳定在 2000000。试着增加线程数量（4 个、8 个），观察 non-atomic 版本的偏差是否更大。

### 练习 2：复现死锁

运行上面的死锁示例。程序大概率会卡住（如果没有，多试几次——死锁的触发依赖调度时序）。然后用 `std::scoped_lock` 替换两个 `lock_guard`，确认程序正常退出。

### 练习 3：识别 race condition

下面这段代码有 race condition 吗？如果有，问题在哪里？

```cpp
std::map<std::string, int> cache;
std::mutex cache_mutex;

int get_or_compute(const std::string& key)
{
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
    }
    // 锁外计算
    int value = expensive_computation(key);
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[key] = value;
    }
    return value;
}
```

提示：如果两个线程同时对同一个 key 进入"锁外计算"阶段，会发生什么？结果可能不是 bug（最终写入的值相同），但如果 `expensive_computation` 有副作用或者很耗时呢？这就是"检查-然后-操作"在更隐蔽的形式下的体现。

## 参考资源

- [[intro.races] C++ Standard Draft — eel.is](https://eel.is/c++draft/intro.races)
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm](https://www.hboehm.info/c++mm/why_undef.html)
- [Multi-threaded executions and data races — cppreference](https://en.cppreference.com/cpp/language/multithread)
- [Dealing with Benign Data Races the C++ Way — Bartosz Milewski](https://bartoszmilewski.com/2014/10/25/dealing-with-benign-data-races-the-c-way/)
- [What Really Happened on Mars? — Mike Jones（Mars Pathfinder 优先级反转案例）](https://research.microsoft.com/en-us/um/people/mbj/mars_pathfinder/what_really_happened_on_mars.html)
