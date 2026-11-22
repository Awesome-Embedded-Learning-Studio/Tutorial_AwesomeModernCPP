---
title: "什么时候值得引入并发"
description: "区分并发、并行与异步三个词各管什么，看清吞吐量与延迟两类收益的来源，列出不该引入并发的情形，并给出 Amdahl 与 Gustafson 两种加速比的估算方法。"
chapter: 5
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
---

# 什么时候值得引入并发

咱们已经写过第一个线程程序，也知道怎么让它收尾了。现在碰到一个嫌慢的程序，您脑子里大概会冒出一个念头：再开几个线程一起干，能快多少？能快，但开线程本身是要付成本的。

我说程序员也要做合格的投资选手，这真的，额，尽管有一股AI喜欢使用的术语，但是我贫瘠的词汇实在找不到任何不蹩脚的比喻了。那就是这笔帐要算明白来。


## 并发、并行、异步三个词各管什么

三个词在日常聊天里经常混用，到了设计里必须分开。并发回答“程序怎么组织”，并行回答“任务是否真的同时运行”，异步回答“等待怎么处理”。您把三个问题分开问，工具就好选了。

### 并发：程序的组织方式

**并发指的是结构**。咱们把一个程序划分成多个可以各自推进的任务，再用某种机制管理它们的推进顺序，这样组织出来的程序就是并发程序。换而言之，只要你看起来一起执行了，管你是任务可能交替执行（哇，好强大的切换速度，我在1ms的间隔中轮流做ABC三件事情各10ms，人一眼看上去哦吼牛逼哦，你在同时推进ABC三件事情，尽管您其实是在串行的做事），或者也可能真的同时执行（一个CPU搞定A，一个CPU搞定B，另一个CPU搞定C），这个结构本身不要求多个 CPU 核心。单核机器上，操作系统按时间片轮转（每个线程轮流拿到一小段 CPU 时间）安排几个线程交替运行，从外面看每个任务都在推进。这就是并发，哪怕同一时刻 CPU 只在跑其中一个。

C++ 里表达并发结构的机制您后面全会学到：`std::thread`、`std::async`（两者都是 C++11 起的标准库设施）、协程，它们都只是把“程序由多个可推进的任务组成”这件事落到语言设施上。至于这些任务能不能真的同时跑，那是下一小节的事。

### 并行：同一时刻的执行

并行指的是执行。多个操作在物理上的同一时刻各自运行，这才叫并行，它需要硬件兜底：多核 CPU、多处理器、GPU 都算。您想知道自己的机器能同时跑多少执行流，有两个现成办法可以查：Linux 和类 Unix 系统的命令行 `nproc`，或者 C++11 起 `std::thread` 的静态成员函数 `hardware_concurrency()`。[cppreference][cppref-hw] 对后者的描述是“返回实现支持的并发线程数”，这个值只是提示，算不出来时返回 0。

<OnlineCompilerDemo
  title="查询机器的并发能力"
  source-path="code/examples/vol5/chapter01/01_hardware_concurrency.cpp"
  description="hardware_concurrency 返回实现支持的并发线程数，算不出来时返回 0，结果跟 nproc 对得上。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 查询机器的并发能力:hardware_concurrency 返回实现支持的并发线程数
#include <print>
#include <thread>

int main() {
    std::println("{}", std::thread::hardware_concurrency());
}
```

</OnlineCompilerDemo>

咱们在自己的机器上跑出来是 16，跟 `nproc` 一致。

Rob Pike 在《Concurrency Is Not Parallelism》的演讲里给过一句话，我把这句话原文放在这里，链接收在参考资源里：

> Concurrency is about dealing with lots of things at once. Parallelism is about doing lots of things at once.
>
> —— Rob Pike，Heroku Waza 2012

这句话，咱们放到自己的代码上看。咱们写并发程序，哪怕只跑在一个核心上，它也能正常运转，只是拿不到加速。而并行程序必须有多个执行单元，否则“同时做”无从谈起。

这个区分落到 C++ 里有一个实际的后果。咱们用 `std::thread` 这类机制写出的是并发结构，任务最后是分时复用一个核心，还是真的各自占住一个核心，由操作系统的调度和硬件能力决定，这部分不在语言的控制范围里。所以并发程序的正确性不能押在“它跑在几个核心上”的假设里，无论实际拿到几个核心，您的程序都必须是正确的。

### 异步：等待的组织方式

异步管的是等待怎么组织。咱们发起一个耗时操作之后不等它完成，把控制权拿回来做别的。等操作完成了，再通过回调、future 或者协程把结果接回来。future 代表一个结果将来才就绪的对象，讲任务组织的时候会专门跟它打交道。异步这件事跟核数没有直接关系。您可以在一个线程里发起磁盘读取，回来处理下一条请求，数据到了再回头继续，整个过程只发生在一个线程里。

C++ 里有一个现成例子，特别适合把异步跟并行拉开距离：`std::async` 配 `std::launch::deferred` 策略（C++11 起）。[cppreference][cppref-launch] 对 deferred 策略的描述是，任务在调用线程上执行，时机是第一次向它要结果的时候，也就是惰性求值。您发起了一次“异步”调用，它却不在另一个线程上跑，也不并行，只是把“什么时候算”往后推了。同在标准里的另一条策略 `std::launch::async` 则写明任务在另一个线程上执行。

咱们编译跑了一个最小实验（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2，2026-11-14 实测）：

<OnlineCompilerDemo
  title="deferred 与 async：谁在哪个线程上跑"
  source-path="code/examples/vol5/chapter01/02_async_deferred_strategy.cpp"
  description="同一套 std::async，两种启动策略：deferred 把执行推迟到 get() 且跑在调用线程上，async 真正另起线程。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// deferred 与 async 两种启动策略:谁在哪个线程上跑、耗时落在哪一步
#include <chrono>
#include <future>
#include <print>
#include <sstream>
#include <thread>

using namespace std::chrono;

std::string id_str() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

int main() {
    const std::string main_id = id_str();

    auto t0 = steady_clock::now();
    auto fut = std::async(std::launch::deferred, [] {
        std::this_thread::sleep_for(milliseconds(300));
        return id_str();
    });
    auto t1 = steady_clock::now();
    const std::string who = fut.get();
    auto t2 = steady_clock::now();

    std::println("async() call spent: {} ms", duration_cast<milliseconds>(t1 - t0).count());
    std::println("get()   spent    : {} ms", duration_cast<milliseconds>(t2 - t1).count());
    std::println("deferred task ran on {}",
                 who == main_id ? "the calling thread" : "another thread");

    auto af = std::async(std::launch::async, [] { return id_str(); });
    std::println("std::launch::async task ran on {}",
                 af.get() == main_id ? "the calling thread" : "another thread");
}
```

</OnlineCompilerDemo>

咱们跑出来的四行如下（线程编号因机器而异）：

```text
async() call spent: 0 ms
get()   spent    : 300 ms
deferred task ran on the calling thread
std::launch::async task ran on another thread
```

这几行输出值得您逐行读一遍。`async()` 调用本身花了 0 毫秒，说明 deferred 策略在发起时没有启动任何执行流。任务里睡了 300 毫秒，这 300 毫秒完整出现在 `get()` 的耗时里，而且任务跑在调用线程上，跟主线程编号相同。`std::launch::async` 那一行则显示任务确实跑在另一条执行流上。`std::async` 的完整故事（默认策略、资源不足时的回退、future 的生命周期）我留到讲任务组织时再展开，这里借它只为了把三个词分开。

三个词合起来看就清爽了。并发管结构，也就是任务怎么划分、怎么组织。并行要更深一层，问的是这些任务是否真的在同一时刻运行。异步管的是等待，落点是一句忠告：别傻等。多线程程序跑在多核机器上，三种属性同时在场。有的服务用事件循环（程序里的一个主循环，守着一堆事件，谁的回应到了就处理谁）支撑上千条连接，可能只有一个线程，全程异步，毫无并行。您要是把矩阵乘法分给十六个核心，那是纯并行，未必有异步。

## 收益从哪来：吞吐量与延迟

吞吐量指单位时间内完成的任务总数，延迟指单个任务从提交到完成经过的时间。咱们引入并发图的就是这两样，多数时候图的是吞吐量。

### 来源一：多个核心同时干活

第一条收益渠道最直观。工作能划分成互不依赖的块，就能交给多个核心同时算，同样的一秒钟完成更多工作。咱们在自己的机器上跑了一个求和实验：对 4 亿个整数累加（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，16 逻辑核，WSL2，2026-11-14 实测）。

<OnlineCompilerDemo
  title="4 亿整数求和：串行对双线程"
  source-path="code/examples/vol5/chapter01/03_sum_two_threads.cpp"
  description="把可划分的纯计算分给两个核心，从读数里看加速比与任务划分的开销。在线沙箱核数与 CPU 配额受限，加速比请以本地实测为准。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 4 亿整数求和:串行对双线程,观察多核加速比与划分开销
#include <chrono>
#include <cstdint>
#include <print>
#include <thread>

using namespace std::chrono;

long long sum_range(long long from, long long to) {
    long long s = 0;
    for (long long i = from; i < to; ++i)
        s += i % 1000;
    return s;
}

int main() {
    const long long n = 400000000LL;

    auto t0 = steady_clock::now();
    long long s1 = sum_range(0, n);
    auto t1 = steady_clock::now();
    std::println("serial    : {} ms, sum {}", duration_cast<milliseconds>(t1 - t0).count(), s1);

    long long sa = 0, sb = 0;
    t0 = steady_clock::now();
    std::thread ta([&] { sa = sum_range(0, n / 2); });
    std::thread tb([&] { sb = sum_range(n / 2, n); });
    ta.join(); // join 等待该线程结束
    tb.join();
    t1 = steady_clock::now();
    std::println("two threads: {} ms, sum {}", duration_cast<milliseconds>(t1 - t0).count(),
                 sa + sb);
}
```

</OnlineCompilerDemo>

```text
serial    : 194 ms, sum 199800000000
two threads: 109 ms, sum 199800000000
```

输出里有两个信号值得您注意。串行大约 190 毫秒，两个线程大约 110 毫秒，加速约 1.7 倍，两个核心确实在同一时刻干活。同时 1.7 倍离理想的 2 倍有距离，任务划分、线程启动、调度开销都在里面花钱。咱们重复跑了几轮，加速比稳定在 1.7 到 1.8 之间。同一个程序在您机器上的数字多半跟咱们的不同，但可划分的纯计算交给多核，方向上一定有收益。

我写到这里停了一下：两个线程各写各的 `sa` 和 `sb`，等主线程 `join`（C++11 起）完再读，真的安全吗。是安全的，cppreference 在 [`std::thread::join`][cppref-join] 的页面写明，线程的完成与 join 的成功返回之间有同步关系，所以主线程一定能看到两个线程写好的值。这套保证背后的机制属于原子操作与内存模型的范围，眼下您知道 join 完成之后才能读它的结果，就已经够用了。

### 来源二：把等待的时间用起来

第二条渠道不依赖多核。真实任务几乎都混着计算和等待：等磁盘、等网络、等锁。一个任务在等 I/O 的时候，它的线程是闲着的。让别的任务在这个空档里跑，吞吐量就上来了，哪怕所有线程共享一个核心。我见过的多数服务端程序开多线程，图的正是第二条渠道。

### 两个指标经常打架

吞吐量和延迟的优化方向经常冲突。咱们拿一组假想数字摆一摆：假设每个任务需要 1 毫秒纯计算。来一个处理一个，每个任务的延迟是 1 毫秒，可调度和切换的开销让吞吐量打折。改成攒一批 100 个一起处理，批内还能合并 I/O，吞吐量明显上升，排在一个批次尾部的任务则要等接近 100 毫秒。这组数字是估算不是实测，但它指出的取舍在任何攒批的设计里都会出现。

分配策略上同理。最短队列策略（把新任务交给当前队列最短的工作线程）拉低平均延迟，可每次分配都要比较一遍队列，判断本身有开销。轮询分配便宜，吞吐量通常更好，但忙闲不均时个别任务的等待时间会冒尖。这类排在分布末端的延迟有个名字，叫尾部延迟。您在动手设计前要想清楚自己的系统更在乎哪个指标：实时交易最在乎延迟，批处理管道最在乎吞吐量，多数 Web 服务要的是在可接受的延迟范围内把吞吐量做上去。

收益讲完了，反面也得看。咱们把并发放一放反而更划算的情况不止一种，动手前对照一遍，比事后返工省事。

## 什么时候不该用并发

### 任务划分不了

您手上是一个纯计算的任务，如果算法本身划分不出互不依赖的块，加线程就买不到并行，多出来的线程只能在旁边轮流借用 CPU，切换还要花时间，程序反而可能变慢。判断能不能划分，直接看依赖结构：后一步必须等前一步的结果才能算，您就挖不出并行。咱们遇到这类程序，起点是把串行依赖改写成可分解的结构，之后才轮到加线程。

### 任务太细

就算能划分，块划得太碎也会亏。创建、调度、唤醒线程都有固定成本。咱们做了一个刻意夸张的实验：2000 个小任务，每个任务跑一百次循环，每次循环做一次加法配一次取模（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，2026-11-14 实测）。

<OnlineCompilerDemo
  title="任务太细的代价：一任务一线程"
  source-path="code/examples/vol5/chapter01/04_tiny_task_thread_overhead.cpp"
  description="串行循环对每任务开一个线程，线程开销是计算量的几百上千倍。在线沙箱限制并发线程数，示例收窄为 8 个任务；正文读数来自本地 2000 任务实测。"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 任务太细的代价:微任务串行循环对一任务一线程
// 在线沙箱限制并发线程数,这里收窄为 8 个任务演示开销量级;正文读数来自本地 2000 任务实测
#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace std::chrono;

long long tiny_task(int base) {
    long long s = 0;
    for (int i = 0; i < 100; ++i)
        s += (base + i) % 64;
    return s;
}

int main() {
    constexpr int kTasks = 8;
    std::vector<long long> results(kTasks);

    // 串行:一个循环做完所有任务
    auto t0 = steady_clock::now();
    for (int k = 0; k < kTasks; ++k)
        results[k] = tiny_task(k);
    auto t1 = steady_clock::now();
    std::println("serial loop             : {} us", duration_cast<microseconds>(t1 - t0).count());

    // 每个任务单独开一个线程
    t0 = steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(kTasks);
    for (int k = 0; k < kTasks; ++k) {
        threads.emplace_back([&results, k] { results[k] = tiny_task(k); });
    }
    for (auto& t : threads)
        t.join();
    t1 = steady_clock::now();
    std::println("one thread per tiny task: {} us", duration_cast<microseconds>(t1 - t0).count());
}
```

</OnlineCompilerDemo>

```text
serial loop             : 26 us
one thread per tiny task: 144306 us
```

串行循环把 2000 个任务全做完用了 26 微秒，给每个任务单独开线程则花了约 144 毫秒。咱们把 144 毫秒摊到 2000 个线程上，每个大约 70 微秒，而单个任务的计算量只够跑十几纳秒，开销是计算量的几千倍。

这组数字是刻意选的极端，真实的任务不会这么小，但您能带走一个量级判断：单个任务的计算量至少要明显高于它的调度开销才值得单独派发。

真实阈值随硬件和运行时变，您拿不准就回到测量（是的，找一台典型的机器测试，并发编程中如果谈论详细的受益，一定推介测量说结论，而不是臆想！）。太细的任务攒成一批交给一个线程，比硬开线程好。但另一头也有风险，整摊活全塞给一个线程，就跟单线程没有区别，多出来的核心白白闲着。任务的粒度，也就是您把工作划成多大的块，就在这两头之间找。线程池把创建成本摊销掉，是这件事的进阶写法，我计划把它写进 cookbook 的选读区，写好后在参考资源补链接。

### 已经够快了

程序的处理延迟已经远低于业务要求的阈值时，并发买不到新东西，买回来的是全套复杂性。已经够快是其中最朴素的情形，也最容易忘了检查。您可以在动手前给自己一个数值化的门槛，比如延迟要求是 100 毫秒而当前实测是 5 毫秒，那就不存在引入并发的理由。

### 程序要求确定性

有些系统要求执行顺序可复现，典型的例子是控制类程序和一些嵌入式场景。多线程引入的调度不确定性对它们可能直接不可接受，这与性能无关，是需求层面就把并发挡在门外。本卷的内容都在主机上，您以后读到本教程的嵌入式分卷时，这类约束会频繁出现。

### 您需要的是异步 I/O

一个网络服务要同时照顾上千条连接，如果您给每条连接开一个线程，线程数量本身就会变成瓶颈，内存和调度两头吃紧。这类场景要的是异步 I/O。事件驱动的写法用少量线程配合 I/O 多路复用（一个线程同时守着很多连接，谁的回应到了就处理谁）管理大量连接。协程是这件事的现代 C++ 写法，可以在执行中途暂停、稍后从暂停处继续，本卷后面讲协程的单元会专门展开。事件循环与异步 I/O 的实战也归入 cookbook 的选读区，写好一并补链接。

对照完上面这些情形，还剩一件贯穿所有情形的事要交代，就是复杂度本身。它不挑场景，跟着每一类走。数据竞争指多个执行流同时读写同一份数据又没有同步。cppreference 的语言页面对它有正式定义，链接收在参考资源里。死锁是几个线程互相等待对方手里的锁，谁也走不了。条件变量还有虚假唤醒的问题，等待会被无缘无故惊醒。对象生命周期问题也在其列。它们共同的特征是难复现、难调试。我写过的并发代码里，当场写错的错误都好办，真正吃时间的是复现，同样的程序跑一百次，失败那一次才肯现身。后面讲并发基本问题的那一篇，会从几个错误的程序开始，让您亲眼见到竞争长什么样。

讲到这里，我把立场收拢，后面全卷沿用：单线程能解决的问题，不为了技巧引入并发。值得引入并发的正当理由只有一条，就是单线程真的不够用了，算不过来，或者等不起。剩下的问题只是选哪条路、付出多少代价。

## Amdahl 与 Gustafson：加速比怎么估（扩展）

您如果已经在心里盘算“加 N 个线程能快多少”，两个估算公式能帮您把直觉换成数字，建议读完。

### Amdahl 定律：固定负载下的加速上限

咱们把程序的总量记成 1，其中比例 $f$ 的部分可以并行，剩下 $(1 - f)$ 只能串行。用 $N$ 个处理器跑，理论加速比是

$$S(N) = \frac{1}{(1 - f) + \frac{f}{N}}$$

您把串行部分单独看，它不随核心数变化，$N$ 增大只能把 $f/N$ 缩小，加速比因此逼近上限 $\frac{1}{1-f}$。上限这个词只属于 Amdahl，因为它假定您要解决的问题规模不变。Gustafson 是换一个问题设定来估算，后面单独讲。咱们代入两组数字感受一下：$f = 0.95$，即 5% 串行，上限是 20 倍，而 16 核实际能估到 $1/(0.05 + 0.95/16) \approx 9.1$ 倍。$f = 0.75$，即 25% 串行，上限 4 倍，16 核约 3.4 倍。5% 和 25% 的串行比例之间，上限差了五倍。

我建议的用法是投入前估一遍。串行比例高的时候，改算法结构降低串行占比，比堆核心划算得多。公式带着两个假设，负载固定、并行零开销，所以它给出的是上限，实测只会更低。也正因为假设足够粗，它适合在动手前回答一个问题：这项改造值不值得投，您只需要一个粗数字。

### Gustafson 定律：把规模放大之后的视角

咱们把两个估算公式的假设摆在一起看。Amdahl 假设要解决的问题不变：核心多了，算的还是同一件事。现实里另一种做法更常见，资源多了就把问题做大。Gustafson 的出发点正是让并行部分的工作量随 $N$ 线性增长，串行部分保持不变。设串行部分占比为 $\alpha$，加速比

$$S(N) = \alpha + (1 - \alpha) \cdot N$$

同一组参数在两个视角下的差距值得您亲眼对比。串行占 5%（$\alpha = 0.05$）时，16 核的 Gustafson 加速比是 $0.05 + 0.95 \times 16 = 15.25$ 倍，Amdahl 视角下同样配置是 9.1 倍。两个公式都按自己的设定算得对，分岔在问题设定：Gustafson 允许渲染更多的帧，Amdahl 只渲染原来那些帧。

拿渲染当例子。一个渲染程序在单核上渲染 1 分钟的视频要 10 分钟。现在给您 16 个核心，按 Amdahl 的口径，您渲染同一段 1 分钟的视频，看能快到多少分钟。按 Gustafson 的口径，您渲染 16 分钟的视频，时间也还在 10 分钟的量级。两个口径没有高下，对应两类问题：交互式任务要更快出结果，用 Amdahl 估。批处理要同样时间里消化更多数据，用 Gustafson 估。

两个公式算完，回到上一节的判断上：如果单线程已经够用，公式估出的倍数再高也跟您没有关系。如果确实不够用，Amdahl 和 Gustafson 算出的两个数字，就是您决定投入多少的第一块依据。

## 下一步

如果您想知道线程的开销具体花在了哪里，可以接着看 Linux 上的一条线程是怎么建起来的。 [线程在机器上是什么](./09-what-is-a-thread)

## 参考资源

本篇写作时对照借鉴了 Anthony Williams《C++ Concurrency in Action》的章节推进思路，也借了 Rob Pike 对概念区分的讲法。例子与文字都是我自己写的。涉及 C++ 行为的断言，均按文中标注编译实测或查证 cppreference。

- [Multi-threaded executions and data races（cppreference）][cppref-multithread]，数据竞争在语言层面的规定。
- [std::async（cppreference）][cppref-async]，启动策略语义、资源不足时的回退行为。
- [std::launch（cppreference）][cppref-launch]，async 与 deferred 两条策略的定义。
- [std::thread::hardware_concurrency（cppreference）][cppref-hw]，返回值语义。
- [std::thread::join（cppreference）][cppref-join]，join 的同步保证。
- [Amdahl's law — Wikipedia][wiki-amdahl]
- [Gustafson's law — Wikipedia][wiki-gustafson]
- [Concurrency Is Not Parallelism — Rob Pike（Heroku Waza 2012，视频）][pike-talk]与[讲稿][pike-slides]，并发与并行的经典区分。
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm][boehm]，为什么数据竞争在 C++ 里被规定为未定义行为。

[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[cppref-launch]: https://en.cppreference.com/w/cpp/thread/launch
[cppref-hw]: https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[wiki-amdahl]: https://en.wikipedia.org/wiki/Amdahl%27s_law
[wiki-gustafson]: https://en.wikipedia.org/wiki/Gustafson%27s_law
[pike-talk]: https://www.youtube.com/watch?v=oV9rvDllKEg
[pike-slides]: https://go.dev/talks/2012/waza.slide
[boehm]: https://www.hboehm.info/c++mm/why_undef.html
