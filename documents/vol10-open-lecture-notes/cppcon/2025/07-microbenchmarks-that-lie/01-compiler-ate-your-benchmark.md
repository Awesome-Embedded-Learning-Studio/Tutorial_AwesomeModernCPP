---
title: "编译器把你骗了：微基准的头号谎言"
description: "CppCon 2025 笔记 —— 拆微基准测试的第一层骗子：编译器。GCC 16.1.1 实测 -O2 把 10 亿次累加循环优化成 0 次，sum 却还是对的，靠的是常量折叠"
chapter: 7
order: 1
conference: cppcon
conference_year: 2025
talk_title: 'Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!'
speaker: Kris Jusiak
cpp_standard: [20]
difficulty: intermediate
platform: host
reading_time_minutes: 12
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
related:
  - "噪声压得下去，偏差才是噩梦"
  - "分支预测器在帮你作弊"
---

# 编译器把你骗了：微基准的头号谎言

::: tip 这一系列笔记的来历
这一系列基于 CppCon 2025 上 Kris Jusiak 的演讲 *Why 99% of C++ Microbenchmarks Lie – and How to Write the 1% that Matter!* 做的二次发散。Kris 是 [Boost].UT 的作者，长期泡在编译期计算和测试框架里。原讲视频在 [YouTube](https://www.youtube.com/watch?v=s_cWIeo9r4I)。笔者把演讲里的方法论拆开，每一层骗子配一段本机实测，不是转述 PPT。
:::

笔者先把话撂在这：如果你写过一个 C++ 微基准，跑出一个漂亮的纳秒数，然后照着它优化代码——你大概率被编译器骗过，而且不止一次。这一篇咱们只拆第一层骗子，也就是最容易栽的那种：**你以为你在测一个循环，编译器直接把循环删了，你还对着一个零跑半天。**

## 先跑一个最朴素的基准

为了让咱们注意力都放在测量本身，挑一个最无聊的函数：把 `0` 到 `n-1` 全加起来。

```cpp
// opt_away.cpp
#include <chrono>
#include <cstdio>

static long heavy_sum(long n) {
    long acc = 0;
    for (long i = 0; i < n; ++i) acc += i;
    return acc;
}

int main() {
    const long N = 1'000'000'000L;  // 10 亿次
    auto t0 = std::chrono::steady_clock::now();
    long s = heavy_sum(N);
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
    std::printf("sum=%ld  per-iter=%.4f ns\n", s, ns);
}
```

逻辑简单到不需要解释。咱们用三个优化等级各跑一次，本机环境见系列首页：GCC 16.1.1，`-std=c++20`。

```bash
$ g++ -std=c++20 -O0 opt_away.cpp -o opt_away_O0 && ./opt_away_O0
sum=499999999500000000  per-iter=0.4567 ns
$ g++ -std=c++20 -O2 opt_away.cpp -o opt_away_O2 && ./opt_away_O2
sum=499999999500000000  per-iter=0.0000 ns
$ g++ -std=c++20 -O3 opt_away.cpp -o opt_away_O3 && ./opt_away_O3
sum=499999999500000000  per-iter=0.0000 ns
```

咱们停下来看这两行数字。

`-O0` 下，`per-iter=0.4567 ns`，这是真的老老实实跑了 10 亿次加法该有的量级——一个迭代大约一个时钟周期出头的活儿。但 `-O2` 和 `-O3` 下，`per-iter=0.0000 ns`。10 亿次加法，耗时测出来是零（或者说，被整数除法截断成了零）。

更诡异的是：**`sum` 的值是对的**。三种优化等级都打印出同一个 `499999999500000000`，而这正好是 `0+1+2+...+999999999` 的结果。

如果你拿 `-O2` 的 `0.0000 ns` 去跟另一个函数对比，然后宣布"我的累加函数每个迭代只要零纳秒，已经到物理极限了"——恭喜，你被编译器骗了。

## 编译器到底干了什么

咱们把这件事拆成两个动作看。

第一个动作叫**常量折叠**（constant folding）。`heavy_sum` 的入参 `N` 是个 `const long`，编译器看得见它的值是 `1000000000`。一个从 `0` 加到 `N-1` 的循环，结果是 `N*(N-1)/2`，这是初中数学。编译器在编译期就把这个公式算出来了，把 `499999999500000000` 当作一个立即数塞进二进制里。所以 `sum` 的值是对的——它根本不是跑出来的，是算出来的。

第二个动作叫**死代码消除**（dead code elimination）。既然 `sum` 已经在编译期知道了，那个循环就没有存在的必要：它不产生任何新的信息，也不读写任何编译期看不见的内存。编译器把它整段删掉。

两个动作叠起来，效果就是：**你的 10 亿次循环，一次都没执行。** 你测到的零纳秒，是真实的零纳秒——因为确实什么都没跑。

咱们可以用汇编验证这件事。`objdump` 看一下两个二进制里的符号表：

```bash
$ objdump -t opt_away_O0 | grep heavy_sum
0000000000000000 l    F .text  00000000000000xx  heavy_sum(long)
$ objdump -t opt_away_O2 | grep heavy_sum
# (空)
```

`-O0` 下 `heavy_sum` 是一个独立的函数符号；`-O2` 下这个符号**整个消失了**——它被内联进 `main`，然后循环体被常量折叠消除，连函数自己都没了。再数一下两个二进制里条件跳转指令（`jne`/`je`/`jmp` 这些，循环必备）的数量：

```bash
$ objdump -d -M intel opt_away_O0 | grep -cE 'j(ne|e|mp|b|a|ge|le)\b'
23
$ objdump -d -M intel opt_away_O2 | grep -cE 'j(ne|e|mp|b|a|ge|le)\b'
15
```

`-O2` 比 `-O0` 少了 8 条跳转，循环的跳转没了。这不是 GCC 16.1.1 的什么新花样，任何现代编译器在 `-O2` 下都会这么干，这一行为也已经稳定了十几年。

## 为什么这件事这么容易栽

你可能会想：这种 `N` 是编译期常量的情况，现实里哪有这么多？笔者以前也这么觉得，直到踩过几个坑才发现，能触发同类优化的场景远比想象中多。

最常见的一种：**循环的结果没被真正消费**。把 `heavy_sum` 改成这样，假设调用方压根没用返回值：

```cpp
long s = heavy_sum(N);
// s 后面再没出现过
```

这回 `N` 哪怕是运行时读进来的，编译器一看 `s` 没人用，照样能把整个循环干掉——只是不一定能用公式替代，但删掉是肯定的。你的 benchmark 跑出零纳秒，你以为是函数快，其实是函数没了。

再常见的一种：**结果只被 `printf` 用了一次**。编译器如果足够激进，能把整个计算挪到编译期做完，只在打印时把常量塞进去。你测到的"运行时"其实是 `printf` 的时间。

还有更隐蔽的：哪怕你把结果存进了一个变量，只要编译器能证明这个变量后面只被读、不被外部观察（比如没逃逸出函数、没写进全局内存），它依然有空间消除计算。这正是为什么所有正经的 benchmark 框架都要求你做一件事——**把结果"钉"在编译器动不了的地方**。

## 三种钉住手段

下面这三种手段，强度和代价各不相同，咱们挨个看。

### 手段一：`volatile` 强制写内存

最老土也最直观的办法，是把累加结果存进一个 `volatile` 变量。`volatile` 告诉编译器：这个变量的每一次读写都必须老老实实发生，不许省、不许折叠、不许挪到编译期。

```cpp
volatile long sink = 0;
for (long i = 0; i < N; ++i) sink += i;
```

加了 `volatile` 之后，编译器不敢把循环删掉，因为每次 `sink += i` 都是一次真实的内存写（`volatile` 语义要求如此）。循环保住了。

但 `volatile` 有代价：它强制每次迭代都做一次内存写，而原本 `acc` 可以一直待在寄存器里。你测到的是"带一次内存写的循环"，不是"原始循环"。在简单的整数累加里这个代价可能不明显，但要是循环体本来就轻，`volatile` 引入的内存访问就会显著拉高测量值——你从一个极端（零纳秒）跳到了另一个极端（偏高）。

另外要注意，**`volatile` 不是原子操作，也不建立内存屏障**。它在多线程场景下不能用来做同步，这点 C++20 之后标准反复强调过。它在这里的作用只有一个：挡住编译器的优化。

### 手段二：`benchmark::DoNotOptimize`

Google Benchmark 提供了一个专门解决这个问题的工具，叫 `DoNotOptimize`。它的实现原理很巧：既不让编译器消除你的数据，又尽量不引入额外开销。

```cpp
#include <benchmark/benchmark.h>

static void BM_Sum(benchmark::State& state) {
    long N = state.range(0);
    for (auto _ : state) {
        long acc = 0;
        for (long i = 0; i < N; ++i) acc += i;
        benchmark::DoNotOptimize(acc);  // 钉住 acc，不让编译器消除
    }
}
BENCHMARK(BM_Sum)->Arg(1000000000);
BENCHMARK_MAIN();
```

`DoNotOptimize(a)` 的底层是一个带特殊约束的内联汇编片段，把 `a` 的地址"喂"给编译器看，让它认为 `a` 的值可能被外部观察，于是不敢删。跟 `volatile` 不同，它不强制每次都写内存，开销小得多。

本机没装 Google Benchmark，但原理可以复刻。下面这段用一段内联汇编模拟 `DoNotOptimize` 的核心效果，同样是"骗编译器说这个值被外部用了"：

```cpp
// 简化版 DoNotOptimize：用 asm 让编译器以为 acc 可能被异步修改
static inline void do_not_optimize(long& x) {
    asm volatile("" : "+r"(x) : :);
}

long acc = 0;
for (long i = 0; i < N; ++i) acc += i;
do_not_optimize(acc);
```

`"+r"(x)` 这条约束告诉编译器：这个汇编片段（虽然是空的）会读写 `x`，而且 `x` 必须放在寄存器里。编译器于是不敢把 `acc` 相关的计算消除掉——因为它"看见"了 `acc` 被一个它无法分析的黑盒使用。这是 GCC/Clang 通用的写法，开销极小，通常只是一两个时钟周期的扰动。

### 手段三：`ClobberMemory` 强制刷新

有时候光钉住一个值还不够。如果循环里有对一块内存的写入，编译器可能把整块写入合并、重排甚至消除，光钉最后一个值拦不住。这时要刷新整个内存屏障：

```cpp
// Google Benchmark 里：
benchmark::ClobberMemory();
// 等价的手写版本：
asm volatile("" : : : "memory");
```

`"memory"` 这个 clobber 告诉编译器：这段汇编可能读写任何内存。于是编译器得把之前所有 pending 的内存写都落实，不能跨过这个点做优化。它比 `DoNotOptimize` 重，但能挡住更激进的内存相关优化。

::: warning 别急着信数字
养成一个习惯：写完 benchmark，先用 `g++ -S` 或 Compiler Explorer 看一眼汇编，确认你要测的那个循环还在、没被折叠成立即数。这一步不花几秒钟，但能挡掉大部分"测了个寂寞"的事故。如果汇编里你测的函数符号已经不见了，或者循环体被替换成几条 `mov` 载入常量，那 benchmark 的数字就毫无意义，先改代码再说。
:::

## 这一层的边界

把编译器这层骗子揭掉之后，你会进入一个更麻烦的层次。即使循环保住了、结果钉住了、汇编也对，你跑出来的纳秒数依然可能在撒谎——因为运行它的那台机器本身在不停抖动：操作系统随时把你切出去、CPU 频率忽高忽低、缓存里装着什么完全不可控。这些东西加起来叫噪声，它们会让你的数字上下跳。

更讨厌的还有一类叫偏差的东西，它不是随机的跳，而是系统性地把你的数字往一个方向拉，跑多少遍都拉偏。

这两类东西性质完全不同，对付它们的办法也完全不同。咱们下一篇挨个拆。

[下一篇：噪声压得下去，偏差才是噩梦 →](02-noise-vs-bias.md)
