---
title: "怎样衡量并发带来的收益"
description: "串行基线与测量条件，基准的三个常见陷阱，Google Benchmark 的用法，数据规模、任务粒度与线程数量三个变量的实测读数，以及用 perf stat 读硬件计数器。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 怎样衡量并发带来的收益

上一篇[《怎样寻找并发错误》](01-finding-concurrency-bugs)管的是程序对不对，重复测试与 ThreadSanitizer 都归它。您手里的并发版本通过了检查之后，马上会碰上另一个问题：它到底比串行版快多少？并发改造的收益，经常靠一句“感觉快了”交代过去，然后被一次真实的测量推翻。本篇把感觉换成读数：动手改设计之前，咱们量出一条串行基线，把测量条件固定下来，再用基准测出收益。

教程里把基准（benchmark）一词留给测量程序自身性能的程序：输入固定、步骤固定、只量时间与计数，不承担业务。咱们写它，就是为了让并发版本与串行版本在同样的活上比出高低。

素材从旧目录 ch08 的《并发性能测试与基准》来，例子全部重写，读数全部重测。并发读数对量级不对尾数，报告时把测量条件一起交出来，咱们一路在用，3.2《[写一个有容量限制的任务队列](../chapter03-wait-and-coordinate/02-bounded-queue)》收尾也交代过。本篇把它正式铺成三步做法。

实验都在临时目录里编译跑过（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2 内核 6.18.33.2，AMD Ryzen 7 9700X，8 核 16 线程，2026-11-17），机器跟第二单元、第五单元是同一台。多线程的读数都是单次运行的读数，咱们要跑几轮，就把几轮的原始读数全部列出来，不挑最好看的一轮。Google Benchmark 在我机器上没有安装，它的用法按 v1.10.0 用户指南核对，本篇的读数出自咱们自己搭的计时架子，架子在后面 Google Benchmark 入门一节交代。

## 为什么要有一条串行基线

咱们手里得有一条串行基线，并发版本的读数才有对照。道理听着理所当然，做起来经常被跳过：并发版写完了，跑一次，三秒，写的人很满意。三秒是快是慢？没有串行版，问题答不出来。

咱们拿同一个求和活做示范。程序对从 0 到 n 减 1 的每个整数累加 `i % 7`，串行版一遍算完，并发版把区间切成若干块，8 条线程用原子下标动态领活，谁空谁领。两边的加法次数一样，代码在后面各节反复出现。代码里的 `do_not_optimize` 是防编译器删结果的函数，防的什么事，架子小节里交底。

```cpp
long long sum_range(long long from, long long to) {
  long long s = 0;
  for (long long i = from; i < to; ++i) s += i % 7;
  do_not_optimize(s);
  return s;
}
```

小数据的读数给咱们上了第一课。n 等于一百万，串行半毫秒，8 条线程进出场，三轮读数排在一起：

```text
n=1000000 round=0 serial=0.54ms parallel8=0.60ms speedup=0.91x
n=1000000 round=1 serial=0.50ms parallel8=0.25ms speedup=2.02x
n=1000000 round=2 serial=0.50ms parallel8=0.23ms speedup=2.17x
```

咱们看第一轮的加速比，0.91 倍，并发版比串行版还慢。后两轮又跳回两倍上下。半毫秒的活，线程建好、闸门放开、结果汇总，每个环节都比活本身贵。您此刻诚实的读法只有一个：量级上没有可见收益，尾数在两倍与零点九倍之间打架，不能拿尾数去宣传。

咱们把数据规模放大到一亿，看看收益会不会站得住：

```text
n=100000000 round=0 serial=51.68ms parallel8=11.60ms speedup=4.46x
n=100000000 round=1 serial=51.22ms parallel8=12.80ms speedup=4.00x
n=100000000 round=2 serial=51.55ms parallel8=11.88ms speedup=4.34x
```

串行稳在 51 到 52 毫秒，并发版落在 11 到 13 毫秒，四倍上下的加速比在三轮里都成立。它没到 8 倍，候选的解释有几个：8 条线程全开之后每条核的频率与资源要分摊，领任务用的原子操作要抢同一条缓存行（缓存搬运的最小单位），WSL2 的调度也会掺一手。我没有逐项验证，能验证的只有读数本身：四倍上下，三轮一致。[《什么时候值得引入并发》](../chapter01-start-concurrency/01-when-concurrency)的扩展节讲过 Amdahl 定律，加速比的天花板由串行部分占的比例决定，本篇的求和活几乎全是并行段，四倍的读数离天花板还远，原因正好由后面的 perf stat 一节接着查缓存行争用的线索。

从几轮读数里，咱们把习惯铺成三步：

1. 串行版本保留下来，跟并发版本一起进基准，任何收益都对着它说话。
2. 测量条件写成记录：机器、核数、编译选项、数据规模、轮次、日期，缺一样，读数就少一分可信。
3. 读数只信量级与倍数，不信尾数。要下判断，多跑几轮，把原始读数列全。

## 测量条件怎么固定

条件里最容易动手脚的是编译选项。咱们全篇用 `-O2`。2.2《[用互斥保护共享状态](../chapter02-sharing-data/02-mutex-and-locks)》数计数器时已经见过，优化级别一换，读数差在编译选项上，对照的两边必须同档，一组选项都不能差。

计时用的是 `std::chrono::steady_clock`。它从 C++11 起可用，cppreference 给它的定位是单调时钟：时间点只往前走，刻度间隔恒定，跟墙上的钟没有关系，最适合测量间隔。系统管理员改时间、网络对时，都动不了它。咱们测量程序跑多久，就用它。

机器与负载也要写进记录。我的读数全部出自上面交代的 8 核 16 线程 WSL2，后台还开着编辑器与浏览器。您重跑时读数一定会变，差多少不好说，因为机器不同、负载不同。机器不动，我连续开三个进程量同一个求和，首轮读数是 50.0、51.9、51.8 毫秒，进程之间差不到百分之四。所以单次读数不当真值，几轮读数咱们原样摆出来。

还有一个条件没法完全固定：CPU 频率。我装机器的时候没有关频率调节。Google Benchmark 的文档专门警告过您：频率调节开着的时候，实时测量给出的读数会抖。关频率要动系统设置，各平台做法不一样，我没有动它，靠多跑几轮把抖动分摊掉。您要是能锁频，锁住之后读数会更稳。

## 编译器把基准删了

您动笔写基准，头一个对手就是编译器。它的任务按标准来说是模拟抽象机的可观察行为，5.2《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/02-happens-before)》里，咱们讲 as-if 规则时已经领教过它多狠。单线程程序里没人观察的中间计算，它有权整个删掉。基准程序恰恰全是没人观察的中间计算，结果算出来，扔掉，完事。

咱们真删一次给它看。两个函数，循环一模一样，区别只在结果怎么处置：

```cpp
static long long sum_ignored(long long n) {
  long long s = 0;
  for (long long i = 0; i < n; ++i) s += i % 7;
  return s;                          // 调用方不要返回值
}

static long long sum_kept(long long n) {
  long long s = 0;
  for (long long i = 0; i < n; ++i) s += i % 7;
  do_not_optimize(s);                // 结果交给防优化函数看一眼
  return s;
}
```

咱们在调用处对 `sum_ignored` 的返回值看都不看。一亿次的循环，三轮读数：

```text
round=0 ignored=0.000ms kept=51.9ms
round=1 ignored=0.000ms kept=50.9ms
round=2 ignored=0.000ms kept=51.5ms
```

咱们看读数：`ignored` 一轮 0.000 毫秒，编译器把整个循环删了个干净。证据在汇编里，`-S` 生成的汇编文件里，`sum_ignored` 的修饰名（编译器把函数名连同参数类型编码出来的内部名字）出现零次，连函数标签都没了，只留下调用点一个空位。`kept` 的三轮还是 51 毫秒上下，因为防优化函数把结果看住了。

防优化的原理值得交代一句。`do_not_optimize` 是咱们架子里的函数，实现只有一条空汇编加约束，骗编译器说：值可能被外界读，也可能被外界写，别动它。Google Benchmark 给的同名工具 `benchmark::DoNotOptimize` 干一样的事，文档还提醒了一个更狡猾的场面：表达式能在编译期算出值的时候，`DoNotOptimize(foo(0))` 会退化成 `DoNotOptimize(42)`，计算还是没了。所以 v1.10.0 的用户指南建议把结果存进局部变量，再把变量交给它。咱们上面就是这么写的。

它旁边还有一个 `benchmark::ClobberMemory()`，告诉编译器内存可能被外界改写过，挡住读写缓存那一路的优化。文档注明它只对 GNU 与 MSVC 系的编译器可用。它比 `DoNotOptimize` 暴力，咱们滥用它的话，本该留在寄存器的值会被赶到内存里，读数反而变难看，用在真正需要的地方就好。

## 只测了同步，没测活

另一种容易踩进去的失败，是把同步的开销当成活本身，测了半天，测的全是等待与进场，真正要量的逻辑一点没出现。线程的创建与销毁就是最常见的混入物。咱们量一次它的价钱：循环里建 200 条线程再逐条 join，跟不建线程做同样的活，放在一起跑：

```text
round=0 with_spawn=16.046ms (80.2 us per thread) plain=0.0001ms
round=1 with_spawn=16.650ms (83.2 us per thread) plain=0.0001ms
round=2 with_spawn=16.229ms (81.1 us per thread) plain=0.0001ms
```

我机器上一条线程从建到收，80 微秒上下。被测的活要是比 80 微秒短，读数里唱主角的就是线程进出场的开销，跟您想测的逻辑一点关系都没有。所以计时的边界要划清楚：线程建好、摆到位之后才开始计时，活干完、结果汇总到手之后才停表。Google Benchmark 的多线程基准把边界划在框架里，线程在计时循环外由框架托管。咱们自己的并发实验代码用一把闸门，摆的是同一套安排：工作线程到闸门口排队（C++20 的 `std::latch`，3.4《[按协作需求选择工具](../chapter03-wait-and-coordinate/04-choosing-tools)》讲过的一次性倒计时），主线程按下秒表之后放闸。

锁的开销是另一个容易误读的对象。2.2 量过一把锁与四把锁的差别，读数是 216 毫秒对 152 毫秒，量出来的是争用，不是加锁本身：没有竞争时，一次加锁解锁在几十纳秒的量级，跟一次原子操作差不多。您给单线程的代码测一把锁，量到的是后一种。四条线程抢一把锁，量到的才是前一种。读数讲的是哪一种，报告里就要写清楚是哪一种。

## 预热与尾差

同一个程序连跑几轮，读数会自己找到稳态。咱们把一亿次的求和放进一个进程里连跑六轮：

```text
round=0 50.6ms
round=1 51.0ms
round=2 50.2ms
round=3 49.9ms
round=4 50.5ms
round=5 50.3ms
```

六轮挤在 49.9 到 51.0 之间，咱们看到纯算术循环没有给它任何惊喜：数据在缓存里住着，分支预测早摸熟了路径。可是并行的第一轮没有这么客气，您回看开头的小数据读数，n 等于一百万时第一轮 0.60 毫秒，比串行还慢，后两轮只要 0.2 毫秒出头。第一轮里，八条线程刚被拉起来，调度器还没把它们安顿到各自的核上，缓存也是冷的。首轮读数要单独标记，咱们别让它混进判断。

尾差藏在平均数里。Google Benchmark 动态决定迭代次数：试跑几轮估好时间，再决定跑多少轮，官方文档写明迭代次数至少一次、不超过十亿，以 CPU 时间超过下限或墙钟时间到达下限五倍为止，下限默认 0.5 秒。您看到的读数，是总时间除以总迭代数的平均值，开头那几次试跑、收尾没凑整的部分，全摊在里面。基准越短，启动开销在平均值里占的比重越大，这跟上面半毫秒求和打不过线程进出场的道理是同一个。咱们把架子写得笨一点：它一次只量一轮，多轮由调用方自己排，每轮读数原样交出来，平均的事交给看读数的人。

预热在 Google Benchmark 里默认是关着的，v1.10.0 文档写明预热时长默认为零，想要稳态得显式设置 `--benchmark_min_warmup_time` 或者代码里调 `MinWarmUpTime`。您想量冷启动，那就反过来，一次都不预热，把第一次执行单独量出来。想量稳态，就把预热时长显式设上，两份读数别混进同一张表。

## Google Benchmark 入门

### 装起来，跑一个最简单的基准

Google Benchmark，C++ 生态里最常用的微基准框架。咱们装它，有两条路：系统包，Debian 与 Ubuntu 的包名是 `libbenchmark-dev`，或者 CMake 的 FetchContent 直接拉源码进构建。FetchContent 的写法如下。版本号用 v1.10.0，2026 年 11 月 17 日查的，当时是 GitHub 发布页上的最新发布。您用的时候再核一次：

```cmake
cmake_minimum_required(VERSION 3.20)
project(concurrency_benchmarks CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF)   # 不让框架自己的测试跟着编译
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.10.0)
FetchContent_MakeAvailable(benchmark)

add_executable(bench_demo bench_demo.cpp)
target_link_libraries(bench_demo PRIVATE benchmark::benchmark)
```

您用它的方式是写一个接收 `benchmark::State&` 的函数，框架反复调用里面的循环，动态决定迭代次数，最后报平均。把咱们的求和交给它：

```cpp
#include <benchmark/benchmark.h>

static void bm_sum_range(benchmark::State& state) {
  const long long n = state.range(0);
  for (auto _ : state) {
    long long s = 0;
    for (long long i = 0; i < n; ++i) s += i % 7;
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(bm_sum_range)->Arg(100000000)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
```

`for (auto _ : state)` 就是计时循环，循环体每跑一次记一次迭代，官方指南推荐优先写 ranged-for，因为传统的 `KeepRunning()` 每次迭代多一次内存读写。您装好跑起来，会看到一张三列表格，Time、CPU、Iterations。列的格式从 v1.10.0 用户指南来，读数换成了咱们刚才的基准，看个样子：

```text
-------------------------------------------------------
Benchmark                    Time       CPU      Iterations
-------------------------------------------------------
bm_sum_range/100000000     51.5 ms    51.5 ms           10
```

数值是示意，跟咱们实测的 51 毫秒对得上，迭代次数按默认的 0.5 秒下限折出来也是十次上下。Time 是墙钟时间，CPU 是 CPU 时间，Iterations 是实际跑的迭代次数。单线程基准两列时间几乎重合，多线程基准就分家了：CPU 时间是所有线程的总和，8 条线程跑 100 毫秒墙钟，CPU 时间可能是 700 毫秒。看哪一列，取决于您要回答什么问题。

### 多线程基准：Threads 与 UseRealTime

咱们测线程数量，框架给两条路。一条是托管给框架，注册时写 `Threads(8)` 或者 `ThreadRange(1, 8)`，后者按 2 的倍数一档档跑，1、2、4、8 各来一遍。框架保证所有线程都走到循环起点才开始计时，全部退出才结束，起跑线是齐的，并发实验代码里的闸门干的就是同一份活。另一条是函数内部自己建线程，跟咱们架子的做法一致。官方文档建议自己建线程的基准配上 `UseRealTime()`，因为框架默认量的是主线程的 CPU 时间，工作线程干活的墙钟时间它看不见，读数会歪得没边。

```cpp
static void bm_parallel_sum(benchmark::State& state) {
  const long long n = 100000000LL;
  const int chunks = 8;
  std::atomic<int> next{0};
  for (auto _ : state) {
    next.store(0);
    auto worker = [=, &next] {
      long long s = 0;
      for (;;) {
        int c = next.fetch_add(1, std::memory_order_relaxed);
        if (c >= chunks) break;
        long long from = c * n / chunks;
        long long to = (c + 1) * n / chunks;
        for (long long i = from; i < to; ++i) s += i % 7;
      }
      benchmark::DoNotOptimize(s);
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < state.threads(); ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
  }
}
BENCHMARK(bm_parallel_sum)->ThreadRange(1, 16)->UseRealTime();
```

`state.threads()` 把当前档位的线程数交给您，一份代码跑遍 1、2、4、8、16 五档，线程数量变量的读数就这么排出来。

### 咱们本篇用的计时架子

我的机器上没有装 Google Benchmark 库，本篇的读数全部出自一个手搓的计时架子。架子很小，全貌如下，概念跟框架对得上，您装好了库，随时可以换回去：

```cpp
#pragma once
#include <chrono>
#include <vector>

// 对应 benchmark::DoNotOptimize 的 v1.10.0 写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

// 官方 v1.10.0 已把 ClobberMemory 改成 atomic_signal_fence（只约束信号处理器那一路内存次序的栅栏），这里摆的是旧式空汇编写法
inline void clobber_memory() {
  asm volatile("" ::: "memory");
}

// 跑 reps 轮，交回每一轮的毫秒数
template <class F>
std::vector<double> measure_ms(F&& f, int reps) {
  using clock = std::chrono::steady_clock;
  std::vector<double> out;
  for (int r = 0; r < reps; ++r) {
    auto t0 = clock::now();
    f();
    auto t1 = clock::now();
    out.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  return out;
}
```

架子的计时循环一次一轮，多轮由调用方排，读数逐轮交出，不做平均，咱们挑它就是为了把每一轮的原始读数都拿到手。`do_not_optimize` 跟 `benchmark::DoNotOptimize` 对应，咱们照 v1.10.0 的写法摆的非 const 引用加读写约束。`clobber_memory` 对应 `benchmark::ClobberMemory`，官方在 v1.10.0 把它改成了 `atomic_signal_fence`，也就是只约束信号处理器那一路内存次序的栅栏，咱们摆的还是旧式空汇编写法。它没有框架的动态迭代，预热这一档干脆没有，换来的是每一轮读数都看得见，这正是本篇要的：原始读数列全，量级自己说话。

## 数据规模：收益从哪里开始

三个变量，咱们从数据规模动起。同一份求和活，8 条线程，规模从一百万加到一亿，把三轮读数收拢成一张表：

| 元素个数 | 串行（毫秒） | 8 线程（毫秒） | 加速比 |
|---|---|---|---|
| 100 万 | 0.50 - 0.54 | 0.23 - 0.60 | 0.91 - 2.17 倍 |
| 1000 万 | 5.14 - 5.68 | 1.31 - 1.36 | 3.78 - 4.17 倍 |
| 1 亿 | 51.22 - 51.68 | 11.60 - 12.80 | 4.00 - 4.46 倍 |

表里收的是三轮的区间。一百万档与一亿档的三轮原始读数，前面两段已经列全，一千万档咱们补在这里：串行 5.14、5.68、5.26 毫秒，8 线程 1.36、1.36、1.31 毫秒，加速比 3.78、4.17、4.03 倍。

规模上去，加速比跟着上去，到 1000 万之后横住。一百万个元素的活不够 8 条线程分的，进场出场就把它吃光了。真正的并发负载里，规模对收益的影响比这更直接：数据大了，串行段（汇总、校验、写结果）的比例反而被摊薄，这也是 Gustafson 定律的出发点，《什么时候值得引入并发》的扩展节讲过。您给自己程序量规模的时候，起点就定在串行耗时与线程进场开销同量级的地方，从那里往上找收益的拐点。

## 任务粒度：切多细算合适

咱们把总工作量固定在两亿次加法，8 条线程，块数从 8 加到两千万，每档跑三轮：

```text
chunks=8        round=0 22.5ms
chunks=8        round=1 22.2ms
chunks=8        round=2 21.3ms
chunks=800      round=0 18.7ms
chunks=800      round=1 19.3ms
chunks=800      round=2 17.3ms
chunks=80000    round=0 18.2ms
chunks=80000    round=1 18.5ms
chunks=80000    round=2 18.3ms
chunks=20000000 round=0 257.6ms
chunks=20000000 round=1 258.7ms
chunks=20000000 round=2 262.0ms
```

咱们看读数，从 8 块到 800 块，耗时从 22 毫秒掉到 18 毫秒上下。8 块意味着每条线程开局领一块、跑完收工，一块是两千五百万次加法，谁分到的活重谁就得自己扛到底，别人帮不上。800 块让跑得快的线程多领几块，负载的波动被摊掉了。到 8 万块，读数没有再动：每块两千五百次加法，领一次任务的成本还只是零头。

两千万块把成本翻到了台面上。每块只剩 10 次加法，领任务用的 `fetch_add` 却一步没少，8 条线程反复抢同一个原子下标，抢到的活还不够付抢的力气，读数从 18 毫秒涨到 258 毫秒，慢了 14 倍。粒度的两头都有代价：太粗，负载不均时有人干等。太细，协调本身变成主角。咱们在 2.2 讲锁的粒度时见过同一件事的锁版本，判据也一样：看协调的成本跟活的成本在哪个规模上交叉。交叉点因活而异。把求和换成 perf stat 一节的争用负载，8 条线程抢一个原子变量，交叉点立刻挪位置，您亲手跑一遍就能看见数字倒过来。

## 线程数量：加到哪一步停手

线程数量是最后一个变量。咱们还是用两亿次加法，块数跟线程数走，一档一线程，每档三轮：

```text
threads=1  round=0 109.5ms
threads=1  round=1 102.6ms
threads=1  round=2 102.8ms
threads=2  round=0 72.6ms
threads=2  round=1 56.6ms
threads=2  round=2 57.7ms
threads=4  round=0 32.1ms
threads=4  round=1 32.5ms
threads=4  round=2 36.8ms
threads=8  round=0 23.8ms
threads=8  round=1 27.5ms
threads=8  round=2 23.3ms
threads=16 round=0 19.7ms
threads=16 round=1 20.5ms
threads=16 round=2 19.0ms
```

有一件事交代在前头：粒度一节的读数与上面的读数是分开两个时段跑的，同样的 8 条线程跑两亿次，两组数也对不齐，负载有漂，您比的时候看量级，别抠个位数。折成加速比，串行一轮 103 毫秒上下：2 条线程 1.8 倍，4 条 3.0 倍，8 条 4.2 倍，16 条 5.2 倍。每翻一倍，收益都在缩水。8 条线程之后还有两成，我机器的 16 个逻辑核是 8 个物理核加超线程，手里的纯算术活 ALU（算术逻辑单元，CPU 里做加减运算的部件）有富余，两个线程挤一条核反而把空隙填上了，这是候选解释，我没有做关闭超线程的对照实验，读数本身只说明一件事：在我机器上，加到 16 没有变慢。

咱们换一个活，判断就会反过来。下一节要用到的争用实验里，8 条线程抢一个原子变量，线程越多争用越凶，加速比掉到一倍以下，收益全部赔进去，争用版与独占槽位差 108 倍。所以您判断线程数量的时候，必须连着负载一起量，空口的推荐没有意义。

## perf stat：硬件计数器看什么（扩展）

本节带（扩展）标记，跳过它不影响单元主线，想弄明白快慢背后的原因，咱们接着往下走。

基准测出多快，硬件计数器能回答为什么。CPU 内部有一组计数器，数周期、数指令、数缓存未命中，Linux 的 `perf` 工具读它们，`perf stat` 把程序跑一遍，把一路的计数报出来。我机器上的 perf 是 7.3.1，硬件计数器可用，下面的读数是实测。WSL2 环境下 perf 能不能读硬件计数器跟内核配置有关，您的环境要是读不到，perf 会把计数器标成不支持，本节跟着思路走就好。

咱们给 perf 准备两个对照程序，都是 8 条线程各做 625 万次加一，总共五千万次。一个各写各的，每条线程的累加结果放进自己独占的 64 字节槽位，64 字节是常见缓存行的宽度，`alignas(64)` 把相邻线程的计数器隔开，谁也不会跟谁挤在同一条缓存行上（挤在一起的后果归 6.3《[怎样调整设计](03-adjusting-design)》，那里专门讲 false sharing）。另一个让全部线程抢同一个 `std::atomic<long long>`，用 relaxed 的 `fetch_add`，内存序的道理 5.3《[选择内存序需要什么依据](../chapter05-atomic-memory-model/03-choosing-memory-orders)》讲过。写法上我把两个程序合在一个可执行文件里，用命令行参数切换，源码在下面：

```cpp
struct alignas(64) Slot { long long v = 0; };

constexpr long long kPerThread = 6250000LL;   // 每线程 625 万次，八条共五千万
std::atomic<long long> shared{0};
std::vector<Slot> slots(8);
std::latch go{1};
bool use_shared = (argc > 1 && argv[1][0] == 's');
// 每条线程：
long long s = 0;
for (long long i = 0; i < kPerThread; ++i) {
  if (use_shared) shared.fetch_add(1, std::memory_order_relaxed);
  else { s += 1; do_not_optimize(s); }   // 不加它，编译器把整段循环折成常量
}
```

把片段连同架子的头文件编成 `exp_f_counter`（`g++ -std=c++20 -O2 -pthread exp_f_counter.cpp -o exp_f_counter`，main 函数与线程收拢的部分按片段补全），命令行参数 p 走各写各的，s 走抢共享的。咱们跑起来，读数如下：

```bash
perf stat -e task-clock,context-switches,cpu-migrations,cycles,instructions,cache-references,cache-misses ./exp_f_counter p
perf stat -e task-clock,context-switches,cpu-migrations,cycles,instructions,cache-references,cache-misses ./exp_f_counter s
```

```text
 Performance counter stats for './exp_f_counter p':

             16.34 msec task-clock:u
                 0      context-switches:u
                 0      cpu-migrations:u
        66,838,682      cycles:u
       302,708,996      instructions:u
           106,416      cache-references:u
            24,790      cache-misses:u

       0.004508114 seconds time elapsed
```

```text
 Performance counter stats for './exp_f_counter s':

          3,787.52 msec task-clock:u
                 0      context-switches:u
                 0      cpu-migrations:u
    19,710,897,115      cycles:u
       357,147,024      instructions:u
        36,971,112      cache-references:u
        26,197,532      cache-misses:u

       0.485875850 seconds time elapsed
```

计数名后面的 `:u` 是 perf 的事件修饰符，表示只统计用户态发生的事件。咱们把两份读数放在一起算：

| 指标 | 各写各的 | 抢同一个 atomic | 差异 |
|---|---|---|---|
| 墙钟时间 | 4.5 毫秒 | 486 毫秒 | 108 倍 |
| 指令数 | 3.03 亿 | 3.57 亿 | 多 18% |
| 周期数 | 0.67 亿 | 197.1 亿 | 295 倍 |
| IPC | 4.5 | 0.018 | 250 倍 |
| 缓存未命中率 | 23.3% | 70.9% | 高出 47.6 个百分点 |

咱们把读数放在一起看，故事很完整。指令数只多 18%，说明两边干的活差不多。周期数差 295 倍，多出来的时间全花在等待上。IPC 是每周期指令数，4.5 说明各写各的版本流水线满负荷运转，0.018 说明争用版几乎每个周期都空转，8 条核排着队等同一条缓存行从别的核手里赶过来。缓存未命中率从 23.3% 跳到 70.9%，反复失效的，正是人人都在加一的共享计数器的缓存行。各写各的版本未命中率的基数其实很小，缓存引用只有十万次出头，两万多次未命中不构成负担。争用版不一样，缓存引用三千七百万次，未命中跟着涨到两千六百万次，三亿多条指令里，访存的大头全耗在抢同一条缓存行上。墙钟 108 倍、周期 295 倍之间的差距来自并行度：争用版 8 条线程全在自旋，CPU 时间烧了 3.79 秒，各写各的只烧了 16 毫秒。

有两项计数，咱们没有用上，但它们的读法值得您知道。上下文切换与 CPU 迁移在两边都是零：线程都在用户态自旋，没有阻塞，调度器没有理由搬动它们。要是您读数里的两项计数偏高，含义就换了：线程在频繁阻塞（等锁、等 I/O），或者线程数远超核数被来回换出。给线程绑核可以减少迁移，`taskset -c 0-7 ./your_program` 就是一条命令的事，本篇没有用到绑核的场面，我就不演示了。

perf 与基准框架也能联动。较新的 Google Benchmark 提供 `--benchmark_perf_counters` 参数（Linux，依赖 libpfm，一个读硬件计数器的库），把计数器直接并进基准报告，您跑一遍基准就能同时拿到时间与计数。更通用的做法还是上面的包法，`perf stat` 在外面把整个基准包一遍。

## 轮到您动手的时候

架子与全套实验我跑在临时目录里，您把自己的活交进去，几步就够：

1. 把您程序里最贵的一段活写成函数，给它配一个串行版本，两边用同一组编译选项。
2. 用 `measure_ms` 或者装好的 Google Benchmark 量三轮起步，原始读数全部记下，连同机器、核数、日期一起写进记录。
3. 三个变量挨个动：数据规模、任务块数、线程数，每次只动一个，动之前想好预期，动之后对着预期看读数。
4. 读数反常（比串行慢、随线程数上升、轮间跳动超过三成）的时候，用 perf stat 把周期与缓存未命中数拉出来，看看时间花在哪。

预期跟读数对不上的时候，别急着改设计，那正是您对程序的想象与机器的真实行为之间的缝隙。6.3《[怎样调整设计](03-adjusting-design)》讲怎么顺着缝隙调设计，入口见下一节。

## 下一步

下一篇 [《怎样调整设计》](03-adjusting-design)接着本篇的读数往下走，false sharing 与缓存行的关系，锁竞争、任务过细、共享过多各自的识别办法，还有调整的顺序，都在它的清单上。本篇量出来的四倍加速比里没算到的部分，还有争用版与独占版 108 倍的时间差，都是它的素材。到时候咱们按读数动代码，动哪一处，查缓存行还是拆锁，读数说了算。

翻页之前，请您把本篇的求和活换成您自己的最贵函数，跑出一条串行基线，再看三个变量里哪一个对收益影响最大。

## 参考资源

写本篇的时候，推进思路借鉴了两本书里讲测量的部分。一本是 Williams 的《C++ Concurrency in Action》，另一本是 Paul E. McKenney 的 perfbook（全名见下面的链接），讲测量与验证的思想。例子与文字都是我自己写的，读数在文中交代的机器上实测。Google Benchmark 的用法，我按 v1.10.0 用户指南核对。标准行为按 cppreference 与 eel.is 的草案条款核对。

- [Google Benchmark 用户指南][gb-guide]，`DoNotOptimize`、多线程基准与预热选项的出处，v1.10.0，2026-11-17 查证。
- [Google Benchmark 发布页][gb-releases]，FetchContent 用的版本号 v1.10.0 出自这里，2026-11-17 查证。
- [Is Parallel Programming Hard, And, If So, What Can You Do About It?（perfbook）][perfbook]，Paul E. McKenney 的免费在线书，kernel.org 镜像。
- [std::chrono::steady_clock（cppreference）][cppref-steady]，C++11 起的单调时钟，测量间隔用它。
- [std::latch（cppreference）][cppref-latch]，C++20 起的一次性闸门，并发实验代码用它齐起跑线。
- [std::atomic（cppreference）][cppref-atomic]，`fetch_add` 与内存序参数的出处。
- [perf stat 教程（perf wiki）][perf-wiki]，硬件计数器的官方教程。
- [C++ 标准草案：as-if 规则（intro.abstract）][draft-abstract]，编译器改写的边界，5.2 讲过，本篇的删基准证据落在它上面。

[gb-guide]: https://github.com/google/benchmark/blob/main/docs/user_guide.md
[gb-releases]: https://github.com/google/benchmark/releases
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
[cppref-steady]: https://en.cppreference.com/w/cpp/chrono/steady_clock
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[perf-wiki]: https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat
[draft-abstract]: https://eel.is/c++draft/intro.abstract
