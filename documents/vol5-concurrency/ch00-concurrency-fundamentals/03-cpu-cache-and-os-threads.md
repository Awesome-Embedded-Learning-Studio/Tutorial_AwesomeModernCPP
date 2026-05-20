---
title: "CPU cache 与 OS 线程"
description: "从硬件缓存层次结构到操作系统线程模型，理解多线程程序运行的真实物理舞台"
chapter: 0
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 基础
  - atomic
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard: [11, 17, 20]
prerequisites:
  - "为什么需要并发"
  - "并发基本问题"
related:
  - "std::thread 基础"
  - "原子操作模式"
---

# CPU cache 与 OS 线程

前两篇我们建立了并发的"为什么"和"出什么问题"这两层认知。但有一个很现实的事情一直被我们有意无意地绕过去了：多线程程序到底跑在什么样的硬件和操作系统之上？我们写 `std::thread t(func)` 的时候，背后发生了什么？为什么有时候多线程程序跑起来不仅没变快，反而比单线程还慢？

这一篇我们要潜入到更底层去看看。先从 CPU cache 的层次结构聊起，搞清楚缓存一致性是怎么保证的，然后理解一个很实际的问题——false sharing，它能让你的多线程程序在不知不觉中损失一半以上的性能。之后我们再往上走一层，看看操作系统是怎么实现线程的，上下文切换到底有多贵，以及 Linux 的 pthread 与 futex 是怎么配合的。理解了这些，后面我们学 `std::atomic` 的内存序、mutex 的实现原理时，就不会觉得那些概念是凭空冒出来的了。

## CPU cache 层次结构

先别急着看多线程，我们先想一个更基本的问题：CPU 为什么需要缓存？

原因很简单——CPU 太快了，内存太慢了。一颗现代的 x86 CPU，主频动辄 几个 GHz，每个时钟周期大概 0.5-1 纳秒；而一次 DDR4/DDR5 内存访问的延迟大约是 50-100 纳秒。也就是说，CPU 直接从内存读数据的话，要空转几百个周期等着数据回来。这就像一个顶尖厨师每秒能切 100 刀，但冰箱在三公里外——切一刀跑一个来回，效率直接归零。

解决这个瓶颈的思路很直白：在 CPU 和主存之间加几层更小、更快但更贵的存储，把常用的数据放在离 CPU 更近的地方。这就是大名鼎鼎的 CPU cache。现代多核处理器通常有三层缓存，从内到外分别是 L1、L2 和 L3。

L1 cache 离 CPU 核心最近，分为指令缓存（L1i）和数据缓存（L1d），每核独占。L1d 的典型大小是 32-48 KB，延迟大约 4-5 个时钟周期（这是 load-use latency——数据从 L1 到达寄存器所需的周期数；不要跟吞吐量混淆，L1 每个周期可以接受一次 load）。这层缓存的速度跟寄存器差不多在一个量级上，但容量非常有限。

L2 cache 也是每核独占的，不过不区分指令和数据。典型大小 256 KB 到 1 MB，延迟大约 10-15 个周期。它充当 L1 和 L3 之间的缓冲——L1 容纳不下的热点数据会溢出到这里。

L3 cache 是所有核心共享的最后一道防线。典型大小从几 MB 到几十 MB 不等（服务器芯片甚至能到上百 MB），延迟大约 30-50 个周期。因为所有核心共享，所以 L3 也是核间数据传递的关键枢纽——一个核心写入了某个数据，其他核心要能看到，一致性协议就是在这个层面协调的。

可以用 `lscpu` 在 Linux 上查看自己机器的缓存配置，输出中的 `L1d cache`、`L2 cache`、`L3 cache` 会告诉你每一层的大小。如果你在写多线程性能测试，先看一眼这些数字是很有帮助的。

### 缓存行：缓存的最小单位

cache 并不是一个字节一个字节地跟主存交换数据的。它以**缓存行（cache line）**为单位进行操作，在几乎所有现代处理器上，一行是 64 字节。这意味着当你访问内存中的某个地址时，整条 64 字节的缓存行都会被加载到 cache 里，即使你只读了其中一个字节。

这个设计背后的逻辑是**空间局部性（spatial locality）**：如果你访问了地址 A，大概率你很快也会访问 A 附近的地址。数组遍历就是一个典型的受益场景——第一个元素被加载时，后面的 15 个 `int` 也一起进了 cache，后续的访问就是 cache hit，几乎零延迟。（注意，1个int是4字节大小，这就是为什么实际加载了15 + 1 = 16个`int`）。

但对于多线程程序来说，缓存行有一个非常讨厌的副作用——false sharing，这个我们稍后会详细展开。现在先记住一个数字就好：**64 字节**，这是理解后续所有缓存相关问题的关键参数。

## 缓存一致性与 MESI 协议

在单核时代，缓存很简单——只有一个核心用，数据只存在于一个地方，读写都不存在歧义。但多核处理器打破了这一点：每个核心有自己的 L1 和 L2，同一个内存地址的数据可能同时存在于多个核心的 cache 中。如果核心 A 修改了自己 cache 里的某个值，核心 B 的 cache 里还存着旧值，它怎么知道数据已经过期了？

这就是**缓存一致性（cache coherence）**要解决的问题。现代 x86 和 ARM 处理器普遍使用 **MESI 协议**（Modified / Exclusive / Shared / Invalid）来维护多核之间的缓存一致性。MESI 给每条缓存行赋予了四种状态之一：

**Modified（M）**：这条缓存行被当前核心修改过了，跟主存中的值不一致。当前核心是唯一持有这条数据的有效副本的——其他核心的 cache 里如果有同一地址的数据，状态必须是 Invalid。当这条缓存行被驱逐（evict）时，必须写回主存。

**Exclusive（E）**：这条缓存行跟主存中的值一致，而且只有当前核心持有它。虽然数据没被修改，但"独占"意味着当前核心可以随时修改它而不用通知其他核心——因为其他核心都不持有它的副本。

**Shared（S）**：这条缓存行跟主存一致，而且可能同时存在于多个核心的 cache 中。当前核心可以读它，但不能直接写——写之前必须先让其他核心的副本失效。

**Invalid（I）**：这条缓存行无效，相当于没有缓存任何有用的数据。访问 Invalid 状态的缓存行会触发 cache miss，需要从主存或其他核心的 cache 中重新加载。

状态之间的迁移由总线上的监听协议（snooping）或基于目录的协议来驱动。举个具体的例子：核心 A 读某个地址，缓存行不在任何核心的 cache 里，它从主存加载进来，状态设为 Exclusive。核心 B 也读同一个地址，总线上的监听机制发现核心 A 已经有一份了，于是把两边的状态都改成 Shared。然后核心 A 要写入这个地址，它先发出一个 **RFO（Read For Ownership）** 请求——意思是"我要独占这条缓存行来写它，请其他持有者把副本作废"。核心 B 收到 RFO 后把自己的缓存行状态改成 Invalid，核心 A 拿到独占权后执行写入，状态变为 Modified。

这个 RFO 请求就是性能开销的来源之一。在多线程程序中，如果两个核心频繁写入同一缓存行的不同位置，就会反复触发 RFO——缓存行在两个核心之间来回弹，每次都要走总线做 invalidation。这就引出了我们接下来要说的 false sharing。

值得一提的是，MESI 协议保证了**缓存一致性（cache coherence）**——也就是说，对于任何一个单独的内存地址，所有核心最终看到的值是一致的。但"缓存一致"不等于"立即可见"——一个核心写入的值，其他核心可能暂时看不到。原因不在 MESI 协议本身，而在处理器内部的**存储缓冲区（store buffer）**：写入操作先进入 store buffer，核心可以继续执行后续指令，等 cache 准备好了再把写入提交上去。在写入真正进入 cache 并触发 invalidation 之前，其他核心看到的一直是旧值。此外，读取端也有**无效化队列（invalidation queue）**——收到的 invalidation 消息可能排队等待处理，这进一步拉长了"新值变可见"的时间窗口。这些微架构层面的缓冲机制让多线程程序的行为比单纯的 MESI 模型要复杂得多，这也是为什么 C++ 的 `std::atomic` 需要不同的 `memory_order` 来控制可见性的粒度——这个话题我们会在后面的原子操作章节展开。

## False sharing：看不见的性能杀手

False sharing 是笔者觉得最"阴"的一个性能问题。你的代码逻辑上完全没有共享——线程 A 只写自己的变量 `a`，线程 B 只写自己的变量 `b`，没有任何 data race——但性能就是上不去，甚至比单线程还慢。原因就在于 `a` 和 `b` 恰好落在同一条缓存行上。

我们来看一个典型的案例：两个线程各自对一个计数器累加一亿次。

```cpp
#include <thread>
#include <iostream>
#include <chrono>

struct Counters {
    int a;  // 线程 1 写
    int b;  // 线程 2 写
};

int main()
{
    constexpr int kIterations = 100'000'000;
    Counters counters{0, 0};

    auto start = std::chrono::high_resolution_clock::now();

    std::thread t1([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counters.a++;
        }
    });
    std::thread t2([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counters.b++;
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time: " << ms.count() << " ms\n";
    std::cout << "a = " << counters.a << ", b = " << counters.b << "\n";
    return 0;
}
```

逻辑上，`counters.a` 和 `counters.b` 是完全独立的变量，两个线程各写各的，没有任何同步需求。但问题是 `Counters` 这个结构体只有 8 字节（两个 `int`），两个成员落在同一条 64 字节的缓存行里。当线程 1（跑在核心 A 上）写入 `counters.a` 时，核心 A 的缓存行状态变成 Modified；线程 2（跑在核心 B 上）要写入 `counters.b`，发现这条缓存行在核心 A 那里是 Modified 状态，于是发出 RFO 请求把核心 A 的副本 invalid 掉。核心 A 下一次再写 `counters.a` 时，又发现缓存行被 invalid 了，得重新拉回来……就这样来回弹了一亿次，缓存行在两个核心之间疯狂乒乓。

在你自己机器上跑一下就知道了——这段代码的执行时间通常比单线程版本还要慢好几倍。这完全是因为硬件层面的缓存行争用，跟你的代码逻辑没有半毛钱关系，但它的影响是实打实的。本项目的 `code/volumn_codes/vol5/ch00-concurrency-fundamentals/false_sharing_bench.cpp` 提供了完整的对比基准测试（含 false sharing、alignas 对齐、单线程三个版本），可以用 CMake 直接编译运行。以下是笔者在 WSL2 环境（x86-64, 7 核, GCC 16.1.1, `-O2`）上的实测结果：

| 版本 | 耗时 | 说明 |
|------|------|------|
| False sharing | ~500–700 ms | 两个 `int` 同缓存行，核心间乒乓 |
| Aligned (`alignas(64)`) | ~23–26 ms | 各占一条缓存行，真正并行 |
| 单线程基准 | ~47–50 ms | 顺序执行两次循环 |

可以看到，false sharing 版本比 alignas 对齐版本**慢 15–30 倍**，甚至比单线程还慢约 **10 倍**——而 alignas 版本因为两个核心真正并行，耗时只有单线程的一半左右。注意，测试代码中的计数器使用了 `volatile` 防止编译器在 `-O2` 下优化掉整个循环；教学代码省略了这一点，但实际测量时需要考虑。

## 消除 false sharing：alignas 与缓存行填充

解决 false sharing 的思路很直白：让两个变量不在同一条缓存行上就行了。在 C++11 中我们可以用 `alignas` 指定对齐方式：

```cpp
#include <thread>
#include <iostream>
#include <chrono>

// 通常定义为一个常量，方便复用
constexpr std::size_t kCacheLineSize = 64;

struct alignas(kCacheLineSize) AlignedCounter {
    int value{0};
};

int main()
{
    constexpr int kIterations = 100'000'000;
    AlignedCounter counter_a{};
    AlignedCounter counter_b{};

    auto start = std::chrono::high_resolution_clock::now();

    std::thread t1([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counter_a.value++;
        }
    });
    std::thread t2([&]() {
        for (int i = 0; i < kIterations; ++i) {
            counter_b.value++;
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time: " << ms.count() << " ms\n";
    std::cout << "a = " << counter_a.value
              << ", b = " << counter_b.value << "\n";
    return 0;
}
```

`alignas(64)` 告诉编译器，`AlignedCounter` 的每个实例必须从 64 字节对齐的地址开始。因为缓存行大小就是 64 字节，所以 `counter_a` 和 `counter_b` 各占一整条缓存行，不可能落在同一条上。RFO 不再发生，两个核心可以各自愉快地写自己的缓存行，互不干扰。

C++17 还提供了一个更优雅的替代方案：`std::hardware_destructive_interference_size`，定义在 `<new>` 头文件中。这个常量的值就是目标平台上"会导致 false sharing 的最小对齐单位"——在几乎所有现有平台上就是 64。用这个常量替代手写的 64，代码的可移植性更好。不过要注意，这个常量的编译器支持参差不齐——GCC 从 12 起可用（依赖 `__GCC_DESTRUCTIVE_SIZE` 宏），但 Clang 截至目前仍未实现（编译错误——变量根本未声明，见 [LLVM#60174](https://github.com/llvm/llvm-project/issues/60174)），所以实际项目中手写 `constexpr std::size_t kCacheLineSize = 64;` 反而更稳妥。

你可能会问：一个 `int` 只有 4 字节，`alignas(64)` 让它占了 64 字节，这不是浪费内存吗？是的，这确实浪费了 60 字节的空间。但这是典型的**空间换时间**的权衡——60 字节的内存在现代机器上根本不算什么，但消除了 false sharing 之后性能可能提升好几倍。在并发编程里，这种"浪费一点空间来换取可扩展性"的做法是非常常见的。你会在很多高性能库和框架里看到这种模式：每个线程的本地计数器 `alignas(64)` 摆好，最后汇总——看起来浪费了几百字节，但换来的是线性的多核扩展性，这笔交易怎么算都划算。

还有另一种写法是在结构体里手动填充（padding）：

```cpp
struct PaddedCounter {
    int value{0};
    char padding[60];  // 填满到 64 字节
};
```

这种方式也能工作，但不如 `alignas` 优雅——你需要自己算填充多少字节，而且编译器不保证对齐。`alignas` 是更推荐的做法，语义也更清晰。无论用哪种方式，核心思路都是一样的：确保并发写入的独立变量之间至少间隔 64 字节，让它们不会共享同一条缓存行。

## OS 线程模型：从用户态到内核态

说完了硬件层面的缓存，我们再往上走一层，看看操作系统是怎么实现线程的。

在操作系统的视角里，线程是 CPU 调度的基本单位，进程是资源分配的基本单位。一个进程可以包含多个线程，这些线程共享同一个地址空间、文件描述符表、信号处理函数等资源，但每个线程有自己独立的栈、寄存器状态和程序计数器。这种"共享大部分资源但各自独立执行"的设计，让线程成为实现并发的天然载体。

线程之所以能"同时"运行，是因为操作系统实现了一套**上下文切换（context switch）**机制：把当前线程的寄存器状态保存到内存中（具体来说，是保存到这个线程对应的线程控制块 TCB 中），然后恢复下一个线程的寄存器状态，跳转到它上次暂停的地方继续执行。这一切都发生在内核空间里——线程的创建、调度、切换都是内核在管。

操作系统为每个线程维护一个**线程控制块（Thread Control Block, TCB）**，里面存储了这个线程的完整状态：寄存器快照、栈指针、程序计数器、调度优先级、信号掩码、以及各种调度相关的元数据。TCB 本身就占几百字节到几 KB 不等，加上每个线程默认的栈空间（Linux 默认 8 MB），一个线程的基础开销并不小。这也是为什么你不能随便开几万个线程——光是栈空间就要吃掉几十 GB 内存。

### 上下文切换的开销

上下文切换到底有多贵？我们可以拆开来看。首先是**直接开销**：保存和恢复通用寄存器（x86-64 上大约 16 个通用寄存器）、浮点/SIMD 寄存器（AVX-512 的 ZMM 寄存器组有 32 个 512 位寄存器，光保存它们就涉及好几 KB 的数据搬移），以及各种系统寄存器。这一步通常是几微秒的量级。

然后是**间接开销**，这一部分往往比直接开销更大。切换到一个新的线程后，TLB（Translation Lookaside Buffer，页表缓存）中缓存的是前一个线程的虚拟地址到物理地址的映射，对新线程来说大部分都是无效的。TLB miss 触发 page table walk，每次 walk 要访问内存好几次，代价不菲。同样地，新的线程执行时会访问它自己的数据，这些数据大概率不在当前核心的 cache 里，导致一轮 cache miss 风暴。冷启动的 cache 跟热 cache 之间的性能差距可能是十倍甚至百倍的。

如果你对具体的数字感兴趣，可以在 Linux 上用 `perf stat` 观察上下文切换的次数，或者用 `context_switch_bench` 这类微基准测试工具来测量。经验上，一次上下文切换的总代价（直接 + 间接）大约在几微秒到几十微秒之间，具体取决于硬件和工作集大小。对于一个计算密集型的循环，如果你的任务粒度只有几微秒，上下文切换的开销可能比实际计算还大——这就是上一篇提到的"任务粒度太细"的问题在硬件层面的体现。

## Linux 的线程实现：pthread、clone 与 futex

Linux 的线程实现有一个很有意思的历史。早期的 Linux 内核（2.4 之前）并没有原生的线程概念——内核只认识进程。所谓的"线程"是通过 `clone()` 系统调用创建的轻量级进程：它跟父进程共享地址空间、文件描述符表等资源，但在内核看来仍然是一个独立的调度实体。这种设计后来被标准化为 **NPTL（Native POSIX Thread Library）**，从 Linux 2.6 开始成为默认的线程实现。

`clone()` 是 Linux 最底层的线程创建原语。你可以把它理解为 `fork()` 的精细控制版本——`fork()` 创建一个全新的进程（所有资源都复制一份），而 `clone()` 允许你精确地指定哪些资源跟父进程共享、哪些要复制。当我们调用 `pthread_create()` 时，glibc 内部就是通过 `clone()` 加上一组特定的 flags 来创建新线程的，这些 flags 指定了共享地址空间（`CLONE_VM`）、共享文件描述符表（`CLONE_FILES`）、共享信号处理器（`CLONE_SIGHAND`）等等。

你可能会问：既然每个线程在内核里都是一个独立的调度实体，那 pthread 和 `std::thread` 是什么关系？其实很简单——`std::thread` 在 Linux 上的实现就是封装了 `pthread_create()`，而 `pthread_create()` 又封装了 `clone()` 系统调用。所以当你写 `std::thread t(func)` 的时候，调用链是：`std::thread` -> `pthread_create` -> `clone` -> 内核创建新的 task_struct。每一层都是对下一层的薄封装。

### futex：快在用户态，慢在内核态

说了线程创建，再来说说线程同步。互斥锁（mutex）是最常用的同步原语，但它的实现有个性能难题：如果锁没有被竞争，为什么还要跑一趟内核？`futex`（fast userspace mutex）就是为了解决这个问题而设计的。

futex 的核心思想是**快速路径在用户态完成，慢速路径才进内核**。当你尝试获取一个 mutex 时，glibc 的实现会先在用户态做一个原子操作（通常是 `compare-and-swap`）尝试获取锁。如果锁是空闲的，直接拿到，不需要任何系统调用——这就是快速路径，开销只有几十个时钟周期。如果锁被其他线程持有了，那就走慢速路径：调用 `futex(FUTEX_WAIT)` 系统调用，让内核把这个线程挂起，直到锁的持有者通过 `futex(FUTEX_WAKE)` 唤醒它。

这个设计非常精妙：无竞争的情况下，mutex 的开销接近一个原子操作；只有在真正发生竞争时才付出系统调用的代价。C++ 的 `std::mutex` 在 Linux 上就是基于这个机制实现的。理解了 futex 的工作方式，你就明白为什么"无竞争的 mutex 很便宜，但竞争激烈的 mutex 很贵"——前者全部在用户态完成，后者每次都要在用户态和内核态之间来回切换。

## 线程模型对比：1:1、M:N、N:1

接下来问题来了：用户态线程和内核态线程之间是什么映射关系？这就是所谓的线程模型。

**1:1 模型**是最直观的——每一个用户态线程对应一个内核线程。Linux 的 pthread（以及 `std::thread`）就是这种模型。它的优点是简单：线程可以直接跑在多个核心上实现真正的并行，阻塞操作（如 I/O）只会阻塞对应的内核线程，不影响其他线程。缺点是线程的创建和切换开销大（都要进内核），而且每个内核线程都有自己的栈和 TCB，线程数量受限。

**N:1 模型**是另一个极端——多个用户态线程全部映射到一个内核线程上。线程的创建和调度完全在用户态完成（不需要系统调用），所以非常轻量，切换也快。但它的致命问题是：任何一个用户态线程做了阻塞操作（比如读文件），整个内核线程就被卡住了，所有用户态线程都动不了。而且因为只有一个内核线程，这些用户态线程永远只能跑在一个核心上，没有真正的并行能力。早期的一些绿色线程（green thread）实现就是这种模型。

**M:N 模型**试图兼得两者的好处——M 个用户态线程映射到 N 个内核线程上（通常 M >> N）。调度器在用户态运行，把用户态线程分配到内核线程上执行，既保持了轻量级，又能利用多核并行。Go 的 goroutine 就是这种模型的经典实现：goroutine 非常轻量（初始栈只有 2-8 KB），Go 运行时的调度器负责把它们分配到少量的操作系统线程上，阻塞的 goroutine 不会卡住整个线程。但 M:N 模型的实现复杂度很高——调度器需要处理抢占、系统调用的包装、以及用户态和内核态之间的栈切换，一不小心就会引入新的问题。

对于 C++ 程序员来说，`std::thread` 在所有主流平台上都是 1:1 模型。如果你需要大量轻量级的并发任务，`std::thread` 不是好的选择——你应该考虑线程池（固定数量的工作线程 + 任务队列）或者协程（C++20 的 `std::coroutine`）。线程池和协程本质上都是在 1:1 模型之上构建的 M:N 调度策略，只不过调度逻辑由你自己或者运行时库来控制。

选哪种模型取决于你的具体场景。如果你只有几个 CPU 密集型的任务需要并行跑，直接用 `std::thread` 就行——1:1 模型简单可靠，没有额外的抽象层。如果你需要处理成千上万个并发连接或者任务，线程池是更务实的选择。（在练习中，我们会进行一定的编写！）。而如果你追求极低的任务切换开销、需要百万级的并发单元，那就得考虑协程或者类似 Go goroutine 那样的 M:N 运行时了。

## 线程调度：谁先跑，跑多久

最后简单聊一下操作系统的线程调度，这部分内容对理解并发程序的行为很有帮助。

现代操作系统普遍采用**抢占式调度（preemptive scheduling）**——操作系统给每个线程分配一个时间片（time slice，通常几毫秒到几十毫秒），时间片用完了就强制切换到下一个线程，不管当前线程愿不愿意。这跟协作式调度（cooperative scheduling）不同，协作式要求线程主动让出 CPU。抢占式的好处是任何一个线程都不能霸占 CPU（至少在正常情况下），坏处是上下文切换发生在你无法预测的时刻，这也是并发 bug 难以复现的原因之一。

在 Linux 上，普通线程的调度策略是 CFS（Completely Fair Scheduler）。CFS 不使用固定的时间片，而是根据线程的**nice 值**来分配 CPU 时间的比例。nice 值的范围是 -20 到 +19，默认 0；值越低优先级越高，能分到的 CPU 时间越多（但不是严格的优先级——CFS 追求的是"公平"而不是严格的优先级调度）。你可以用 `nice` 命令或 `setpriority()` 系统调用来调整。

另一个有用的概念是**CPU 亲和性（CPU affinity）**。默认情况下，操作系统的调度器可以把线程在任意核心之间迁移——核心 A 上跑了 50ms 的线程，下一个时间片可能被调度到核心 B 上跑。这种迁移会导致 L1/L2 cache 全部冷掉。如果你知道某个线程的工作集很大、cache 局部性很重要，可以用 `cpu_set_t` 和 `sched_setaffinity()` 把它"绑"到固定的核心上，不让调度器迁移它。下面的代码展示了基本的用法：

```cpp
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <iostream>

void pin_thread_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int result = pthread_setaffinity_np(
        pthread_self(), sizeof(cpu_set_t), &cpuset);

    if (result != 0) {
        std::cerr << "Failed to pin to core " << core_id << "\n";
    }
}
```

C++ 标准库本身不提供设置 CPU 亲和性的接口（这是平台相关的概念），但 `std::thread::native_handle()` 可以拿到底层的 `pthread_t`，然后你就可以用 POSIX 的接口来操作了。在实际的高性能场景里，合理地绑定线程到核心上（比如把生产者线程绑到核心 0，消费者线程绑到核心 1）是可以明显提升性能的——减少跨核心的缓存行迁移，降低 MESI 协议的 RFO 开销，跟我们前面讨论的 false sharing 的思路是一脉相承的。

## 小结

这一篇我们从硬件和操作系统两个层面深入理解了多线程程序运行的真实舞台。在硬件层面，CPU cache 的 L1/L2/L3 层次结构、缓存行的 64 字节粒度、MESI 协议的状态迁移和 RFO 请求，这些机制决定了多线程程序的实际性能表现。false sharing 是最容易踩的缓存性能陷阱——两个看似独立的变量因为落在同一条缓存行上而反复触发 MESI 协议的 invalidation，`alignas(64)` 是最直接有效的解法。

在操作系统层面，Linux 的线程是通过 `clone()` 系统调用实现的 1:1 模型——每个用户态线程对应一个内核调度实体。上下文切换的直接开销（寄存器保存/恢复）加上间接开销（TLB flush、cache miss）让线程切换成为不可忽视的成本。futex 的"快速路径在用户态、慢速路径进内核"设计让无竞争的 mutex 非常便宜，但竞争激烈时系统调用的代价会迅速显现。不同的线程模型（1:1、M:N、N:1）各有取舍，C++ 的 `std::thread` 采用 1:1 模型，对于大量轻量级并发任务需要借助线程池或协程来弥补。

现在我们有了并发的基本认知（ch00-01），知道了并发会出什么问题（ch00-02），也理解了硬件和 OS 是怎么支撑多线程的（本篇）。下一步，我们终于可以动手写代码了——下一篇将正式介绍 `std::thread` 的接口与使用。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch00-concurrency-fundamentals/`。

## 练习

### 练习 1：复现并消除 false sharing

编译并运行上面的 `Counters` 示例（未对齐版本），记录执行时间。然后改用 `alignas(64)` 的 `AlignedCounter` 版本，对比两者的执行时间。在你的机器上性能差多少？尝试增加线程数量到 4 个（4 个独立的计数器），观察性能差异是否更大。

### 练习 2：观察缓存行大小

在 Linux 上运行 `lscpu` 或 `cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` 查看你机器的缓存行大小。然后在 C++ 中用 `std::hardware_destructive_interference_size`（C++17，定义在 `<new>` 中）获取编译期可见的缓存行大小。如果编译器不支持这个常量，手写 `constexpr size_t kCacheLineSize = 64;` 也可以——目前几乎所有主流平台都是 64 字节。

### 练习 3：测量上下文切换的开销

写一个程序，创建两个线程，通过 `std::atomic<bool>` 做乒乓式的交替唤醒：线程 A 设 `flag = true` 然后等 `flag` 变回 `false`，线程 B 等 `flag` 变 `true` 然后设回 `false`，循环 100 万次。用总时间除以切换次数来估算一次上下文切换的大致代价。这个数字会包含 atomic 操作的开销和上下文切换的开销，但它给出了一个量级上的感觉。

## 参考资源

- [MESI protocol — Wikipedia](https://en.wikipedia.org/wiki/MESI_protocol)
- [False Sharing — Intel Developer Zone](https://www.intel.com/content/www/us/en/developer/articles/technical/avoiding-and-identifying-false-sharing-among-threads.html)
- [A futex overview and update — Ulrich Drepper (Red Hat)](https://man7.org/linux/man-pages/man7/futex.7.html)
- [The Native POSIX Thread Library for Linux — Ulrich Drepper, Ingo Molnar](https://www.akkadia.org/drepper/nptl-design.pdf)
- [CFS Scheduler Design — kernel.org](https://www.kernel.org/doc/html/latest/scheduler/sched-design-CFS.html)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
