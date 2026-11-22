---
title: "量之前，先把条件立住：串行基线与测量的陷阱"
description: "串行基线与测量条件，基准的三个常见陷阱：编译器删基准、只测同步、预热与尾差。"
chapter: 5
order: 3
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

# 量之前，先把条件立住：串行基线与测量的陷阱

您手里的并发版本通过了检查之后，马上会碰上另一个问题：它到底比串行版快多少？并发改造的收益，经常靠一句“感觉快了”交代过去，然后被一次真实的测量推翻。本篇把感觉换成读数：动手改设计之前，咱们量出一条串行基线，把测量条件固定下来，测量的陷阱挨个认一遍。基准怎么跑、收益怎么量，下一篇《基准工具与三个变量》接手。

教程里把基准（benchmark）一词留给测量程序自身性能的程序：输入固定、步骤固定、只量时间与计数，不承担业务。咱们写它，就是为了让并发版本与串行版本在同样的活上比出高低。

多线程的读数都是单次运行的读数，咱们要跑几轮，就把几轮的原始读数全部列出来，不挑最好看的一轮。

## 为什么要有一条串行基线

咱们手里得有一条串行基线，并发版本的读数才有对照。道理听着理所当然，做起来经常被跳过：并发版写完了，跑一次，三秒，写的人很满意。三秒是快是慢？没有串行版，问题答不出来。

咱们拿同一个求和活做示范。程序对从 0 到 n 减 1 的每个整数累加 `i % 7`，串行版一遍算完，并发版把区间切成若干块，8 条线程用原子下标动态领活，谁空谁领。两边的加法次数一样，代码在后面几篇里反复出现。代码里的 `do_not_optimize` 是防编译器删结果的函数，防的什么事，《基准工具与三个变量》的架子一节里交底。

<OnlineCompilerDemo
  title="串行基线对 8 线程动态领活：小数据的第一课"
  source-path="code/examples/vol5/chapter06/12_sum_range.cpp"
  description="n 取一百万，同一份求和活三轮：串行半毫秒上下，8 线程第一轮常比串行慢（进出场比活贵），后两轮才回到两倍上下——加速比只看量级不看尾数。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// sum_range.cpp —— 同一份求和活：串行基线对 8 线程动态领活
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

long long sum_range(long long from, long long to) {
  long long s = 0;
  for (long long i = from; i < to; ++i) s += i % 7;
  do_not_optimize(s);
  return s;
}

constexpr int kThreads = 8;
constexpr int kChunks = 8;

// 并发版：区间切成若干块，8 条线程用原子下标动态领活，谁空谁领
double run_parallel8(long long n) {
  using clock = std::chrono::steady_clock;
  std::atomic<int> next{0};
  long long slots[kThreads] = {};
  std::vector<std::thread> pool;
  auto t0 = clock::now();   // 计时含线程进出场，正文小数据读数就是这么量的
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&, t] {
      long long s = 0;
      for (;;) {
        int c = next.fetch_add(1, std::memory_order_relaxed);
        if (c >= kChunks) break;
        long long from = c * n / kChunks;
        long long to = (c + 1) * n / kChunks;
        for (long long i = from; i < to; ++i) s += i % 7;
      }
      slots[t] = s;
    });
  }
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  long long total = 0;
  for (long long v : slots) total += v;
  do_not_optimize(total);
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

double run_serial(long long n) {
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  long long s = sum_range(0, n);
  auto t1 = clock::now();
  do_not_optimize(s);
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  const long long n = 1000000;
  for (int r = 0; r < 3; ++r) {
    double s = run_serial(n);
    double p = run_parallel8(n);
    std::printf("n=%lld round=%d serial=%.2fms parallel8=%.2fms speedup=%.2fx\n",
                n, r, s, p, s / p);
  }
  return 0;
}
```

</OnlineCompilerDemo>

机器跟第二单元、第五单元是同一台，AMD Ryzen 7 9700X，8 核 16 线程。

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

串行稳在 51 到 52 毫秒，并发版落在 11 到 13 毫秒，四倍上下的加速比在三轮里都成立。它没到 8 倍，候选的解释有几个：8 条线程全开之后每条核的频率与资源要分摊，领任务用的原子操作要抢同一条缓存行（缓存搬运的最小单位），WSL2 的调度也会掺一手。笔者没有逐项验证，能验证的只有读数本身：四倍上下，三轮一致。[《什么时候值得引入并发》](../chapter01-start-concurrency/08-when-concurrency)的扩展节讲过 Amdahl 定律，加速比的天花板由串行部分占的比例决定，本篇的求和活几乎全是并行段，四倍的读数离天花板还远，差在哪儿，《基准工具与三个变量》的 perf stat 一节顺着缓存行争用的线索接着查。

从几轮读数里，咱们把习惯铺成三步：

1. 串行版本保留下来，跟并发版本一起进基准，任何收益都对着它说话。
2. 测量条件写成记录：机器、核数、编译选项、数据规模、轮次、日期，缺一样，读数就少一分可信。
3. 读数只信量级与倍数，不信尾数。要下判断，多跑几轮，把原始读数列全。

## 测量条件怎么固定

条件里最容易动手脚的是编译选项。咱们全篇用 `-O2`。2.2《[丢掉的数怎么收回来？——std::mutex 与三种作用域锁](../chapter02-sharing-data/02-mutex-and-locks)》数计数器时已经见过，优化级别一换，读数差在编译选项上，对照的两边必须同档，一组选项都不能差。

计时用的是 `std::chrono::steady_clock`。它从 C++11 起可用，cppreference 给它的定位是单调时钟：时间点只往前走，刻度间隔恒定，跟墙上的钟没有关系，最适合测量间隔。系统管理员改时间、网络对时，都动不了它。咱们测量程序跑多久，就用它。

机器与负载也要写进记录。笔者的读数全部出自上面交代的 8 核 16 线程 WSL2，后台还开着编辑器与浏览器。您重跑时读数一定会变，差多少不好说，因为机器不同、负载不同。机器不动，笔者连续开三个进程量同一个求和，首轮读数是 50.0、51.9、51.8 毫秒，进程之间差不到百分之四。所以单次读数不当真值，几轮读数咱们原样摆出来。

还有一个条件没法完全固定：CPU 频率。笔者装机器的时候没有关频率调节。Google Benchmark 的文档专门警告过您：频率调节开着的时候，实时测量给出的读数会抖。关频率要动系统设置，各平台做法不一样，笔者没有动它，靠多跑几轮把抖动分摊掉。您要是能锁频，锁住之后读数会更稳。

## 编译器把基准删了

您动笔写基准，头一个对手就是编译器。它的任务按标准来说是模拟抽象机的可观察行为，5.3《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/03-happens-before)》里，咱们讲 as-if 规则时已经领教过它多狠。单线程程序里没人观察的中间计算，它有权整个删掉。基准程序恰恰全是没人观察的中间计算，结果算出来，扔掉，完事。

咱们真删一次给它看。两个函数，循环一模一样，区别只在结果怎么处置：

<OnlineCompilerDemo
  title="编译器把没人看的基准删成 0.000 毫秒"
  source-path="code/examples/vol5/chapter06/13_sum_ignored_kept.cpp"
  description="两个循环一模一样的函数，返回值没人看的那个被 -O2 整段删掉，三轮都是 0.000ms。结果交给 do_not_optimize 看一眼的照跑，50ms 上下。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// sum_ignored_kept.cpp —— 编译器把没人看的基准删了个干净
#include <chrono>
#include <cstdio>

// 对应 benchmark::DoNotOptimize 的写法：非 const 引用，读写约束
template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

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

int main() {
  const long long n = 100000000LL;
  using clock = std::chrono::steady_clock;
  for (int r = 0; r < 3; ++r) {
    auto a0 = clock::now();
    sum_ignored(n);                  // 返回值看都不看
    auto a1 = clock::now();
    sum_kept(n);
    auto a2 = clock::now();
    std::printf("round=%d ignored=%.3fms kept=%.3fms\n", r,
                std::chrono::duration<double, std::milli>(a1 - a0).count(),
                std::chrono::duration<double, std::milli>(a2 - a1).count());
  }
  return 0;
}
```

</OnlineCompilerDemo>

咱们在调用处对 `sum_ignored` 的返回值看都不看。一亿次的循环，三轮读数：

```text
round=0 ignored=0.000ms kept=51.9ms
round=1 ignored=0.000ms kept=50.9ms
round=2 ignored=0.000ms kept=51.5ms
```

咱们看读数：`ignored` 一轮 0.000 毫秒，编译器把整个循环删了个干净。证据在汇编里，`-S` 生成的汇编文件里，`sum_ignored` 的修饰名（编译器把函数名连同参数类型编码出来的内部名字）出现零次，连函数标签都没了，只留下调用点一个空位。`kept` 的三轮还是 51 毫秒上下，因为防优化函数把结果看住了。

防优化的原理，藏在 `do_not_optimize` 的实现里。它是咱们架子里的函数，实现只有一条空汇编加约束，骗编译器说：值可能被外界读，也可能被外界写，别动它。Google Benchmark 给的同名工具 `benchmark::DoNotOptimize` 干一样的事，文档还提醒了一个更狡猾的场面：表达式能在编译期算出值的时候，`DoNotOptimize(foo(0))` 会退化成 `DoNotOptimize(42)`，计算还是没了。所以 v1.10.0 的用户指南建议把结果存进局部变量，再把变量交给它。咱们上面就是这么写的。

它旁边还有一个 `benchmark::ClobberMemory()`，告诉编译器内存可能被外界改写过，挡住读写缓存方向的优化。文档注明它只对 GNU 与 MSVC 系的编译器可用。它比 `DoNotOptimize` 暴力，咱们滥用它的话，本该留在寄存器的值会被赶到内存里，读数反而变难看，用在真正需要的地方就好。

## 只测了同步，没测活

另一种容易踩进去的失败，是把同步的开销当成活本身，测了半天，测的全是等待与进场，真正要量的逻辑一点没出现。线程的创建与销毁就是最常见的混入物。咱们量一次它的价钱：循环里建 200 条线程再逐条 join，跟不建线程做同样的活，放在一起跑：

```text
round=0 with_spawn=16.046ms (80.2 us per thread) plain=0.0001ms
round=1 with_spawn=16.650ms (83.2 us per thread) plain=0.0001ms
round=2 with_spawn=16.229ms (81.1 us per thread) plain=0.0001ms
```

笔者机器上一条线程从建到收，80 微秒上下。被测的活要是比 80 微秒短，读数里唱主角的就是线程进出场的开销，跟您想测的逻辑一点关系都没有。所以计时的边界要划清楚：线程建好、摆到位之后才开始计时，活干完、结果汇总到手之后才停表。Google Benchmark 的多线程基准把边界划在框架里，线程在计时循环外由框架托管。咱们自己的并发实验代码用一把闸门，摆的是同一套安排：工作线程到闸门口排队（C++20 的 `std::latch`，3.7《[按协作需求选择工具](../chapter03-wait-and-coordinate/07-choosing-tools)》讲过的一次性倒计时），主线程按下秒表之后放闸。

锁的开销是另一个容易误读的对象。2.3 量过一把锁与四把锁的差别，读数是 216 毫秒对 152 毫秒，量出来的是争用，不是加锁本身：没有竞争时，一次加锁解锁在几十纳秒的量级，跟一次原子操作差不多。您给单线程的代码测一把锁，量到的是后一种。四条线程抢一把锁，量到的才是前一种。读数讲的是哪一种，报告里就要写清楚是哪一种。

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

尾差藏在平均数里。Google Benchmark 动态决定迭代次数：试跑几轮估好时间，再决定跑多少轮，官方文档写明迭代次数至少一次、不超过十亿，以 CPU 时间超过下限或墙钟时间到达下限五倍为止，下限默认 0.5 秒。您看到的读数，是总时间除以总迭代数的平均值，开头几次试跑、收尾没凑整的部分，全摊在里面。基准越短，启动开销在平均值里占的比重越大，这跟上面半毫秒求和打不过线程进出场的道理是同一个。咱们把架子写得笨一点：它一次只量一轮，多轮由调用方自己排，每轮读数原样交出来，平均的事交给看读数的人。

预热在 Google Benchmark 里默认是关着的，v1.10.0 文档写明预热时长默认为零，想要稳态得显式设置 `--benchmark_min_warmup_time` 或者代码里调 `MinWarmUpTime`。您想量冷启动，那就反过来，一次都不预热，把第一次执行单独量出来。想量稳态，就把预热时长显式设上，两份读数别混进同一张表。

## 参考资源

写本篇的时候，推进思路借鉴了两本书里讲测量的部分。一本是 Williams 的《C++ Concurrency in Action》，另一本是 Paul E. McKenney 的 perfbook（全名见下面的链接），讲测量与验证的思想。例子与文字都是笔者自己写的，读数在文中交代的机器上实测。Google Benchmark 的用法，笔者按 v1.10.0 用户指南核对。标准行为按 cppreference 与 eel.is 的草案条款核对。

- [Google Benchmark 用户指南][gb-guide]，`DoNotOptimize` 的表达式退化、`ClobberMemory` 的边界、动态迭代次数与预热选项的出处，v1.10.0，2026-11-17 查证。
- [std::chrono::steady_clock（cppreference）][cppref-steady]，C++11 起的单调时钟，测量间隔用它。
- [std::latch（cppreference）][cppref-latch]，C++20 起的一次性闸门，并发实验代码用它齐起跑线。
- [C++ 标准草案：as-if 规则（intro.abstract）][draft-abstract]，编译器改写的边界，5.3 讲过，本篇的删基准证据落在它上面。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，测量相关章节。
- [Is Parallel Programming Hard, And, If So, What Can You Do About It?（perfbook）][perfbook]，Paul E. McKenney 的免费在线书，kernel.org 镜像。

[gb-guide]: https://github.com/google/benchmark/blob/main/docs/user_guide.md
[cppref-steady]: https://en.cppreference.com/w/cpp/chrono/steady_clock
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[draft-abstract]: https://eel.is/c++draft/intro.abstract
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
