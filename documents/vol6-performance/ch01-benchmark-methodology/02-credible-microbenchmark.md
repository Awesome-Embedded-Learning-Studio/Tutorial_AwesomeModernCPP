---
title: "怎么写一个可信的 microbenchmark"
description: "从 ch01-01 的「骗术」走到「对策」:用 Google Benchmark 写一个不(那么)骗人的微基准,拆透 DoNotOptimize / ClobberMemory 的语义与坑、参数扫描、重复聚合、UseRealTime,配最小可运行例子和真实输出"
chapter: 1
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 优化
  - 测试
difficulty: intermediate
platform: host
reading_time_minutes: 10
cpp_standard: [14, 17]
prerequisites:
  - "为什么 microbenchmark 会骗你"
related:
  - "测量陷阱与环境就绪"
  - "统计与报告"
---

# 怎么写一个可信的 microbenchmark

## 上一篇甩下的问题

ch01-01 把 microbenchmark 那三套骗术摆完了——编译器把你优化成空、缓存假热、噪声淹信号。骗术讲完了,这一篇给解药。

解药其实只管第一套骗术(结果被优化掉),顺手把参数扫描、重复聚合、墙钟计时这几件马上要用的姿势做对。第三套(系统噪声)得靠 ch01-03 那份环境 checklist,分布怎么变成结论是 ch01-04 的事——这两件先放着,这一篇先把「测的对象是不是真东西」立住。

## 别自己写计时循环

你大概想这么干:一个 `for` 循环、`std::chrono::steady_clock` 掐表、跑完除一下。ch00-01 那个 `vector_vs_set` 就是这么写的——但那是**为了讲命题故意用最朴素写法**,别学。自己搓的计时循环,「该跑多少轮」「怎么算统计」「结果怎么不被优化掉」全得你自己管,而这几件事每一件都有坑。一个合格的 benchmark 框架替你把这三件机械活包了,你只管写「测什么」。本卷主力是 Google Benchmark(下面简称 GBench)。

看一个最小、但完整的例子,测 `std::vector::push_back`:

```cpp
// push_bench.cpp —— GBench 最小完整例子
#include <benchmark/benchmark.h>
#include <vector>

static void BM_PushBack(benchmark::State& state) {
    for (auto _ : state) {                       // 计时循环:框架控制迭代次数
        std::vector<int> v;
        for (int i = 0; i < state.range(0); ++i) {
            v.push_back(i);
            benchmark::DoNotOptimize(v.data());  // 防 DCE + 内存 barrier
        }
        benchmark::ClobberMemory();              // 确保写真正落内存
    }
    state.SetComplexityN(state.range(0));        // 告诉框架 big-O 的 N,自动拟合
}

BENCHMARK(BM_PushBack)
    ->RangeMultiplier(2)->Range(8, 8 << 6)       // 参数扫描:8,16,32,...,512
    ->UseRealTime()                              // 报墙钟时间,不是 CPU 时间
    ->Repetitions(3)                             // 跑 3 轮
    ->ReportAggregatesOnly(true);                // 只报 mean/median/stddev/cv

BENCHMARK_MAIN();
```

我在自己机器上(GCC 16.1.1,GBench v1.9.5,FetchContent 拉的)实跑了一遍,输出长这样(截几行代表):

```text
Run on (14 X 3193.92 MHz CPU s)
CPU Caches:
  L1 Data 32 kiB (x7)  L2 Unified 512 kiB (x7)  L3 Unified 16384 kiB (x1)
-------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations
-------------------------------------------------------------------------------------
BM_PushBack/8/repeats:3/real_time_mean           44.0 ns         44.0 ns            3
BM_PushBack/8/repeats:3/real_time_median         44.0 ns         44.0 ns            3
BM_PushBack/8/repeats:3/real_time_stddev        0.137 ns        0.137 ns            3
BM_PushBack/8/repeats:3/real_time_cv             0.31 %          0.31 %             3
BM_PushBack/64/repeats:3/real_time_mean           105 ns          105 ns            3
BM_PushBack/64/repeats:3/real_time_median         105 ns          105 ns            3
BM_PushBack/256/repeats:3/real_time_mean          242 ns          242 ns            3
BM_PushBack/256/repeats:3/real_time_median        242 ns          242 ns            3
```

这张表怎么读。`Time` 是墙钟(因为用了 `UseRealTime`),`CPU` 是 CPU 时间,`Iterations` 在聚合行里显示的是重复次数(3,即 `Repetitions(3)` 那个 3),不是每轮真实迭代数——每轮框架估了很多次,只是在 `ReportAggregatesOnly` 模式下被聚合藏起来了。`mean` / `median` / `stddev` / `cv` 是对这 3 轮做的统计,其中 `cv`(coefficient of variation,`stddev/mean`)是最该盯的——它告诉你「这一组测量有多散」。44ns 那行 cv 是 0.31%,很稳;哪天 cv 飙到 5% 以上,这轮就别信了,先去查噪声源(ch01-03)。

> 笔者第一次用 GBench 的时候,光盯着 `mean` 看,后来吃了几次亏才学会先扫一眼 `cv`——`cv` 大的 `mean` 没有意义,你对着一个噪声比信号还大的分布下结论,纯属自欺。

时间随 N 涨(8→44ns,64→105ns,256→242ns),这才是 `push_back`「随规模变贵」的真实样子。不是 ch01-01 那个被 DCE 删成一条 `ret` 的空壳。

## DoNotOptimize: 它救你,但救不到底

这一节是整篇最该讲透的,也是新手最爱用错的地方。把 ch01-01 的 `foo()` 和这里的 `BM_PushBack` 摆一起:同样是「循环里创建/写入东西」,`foo()` 没用 `DoNotOptimize`,整段被编译器删成一条 `ret`;`BM_PushBack` 用了,真跑了、时间随 N 缩放。`DoNotOptimize` 干的事,就是把「结果」钉到内存或寄存器,让编译器没法判定它是死代码。

但是有个大问题，我先直接引 Google Benchmark `user_guide` 原文:`benchmark::DoNotOptimize(expr)` 把 `expr` 的结果存到 memory 或 register,对 GNU 编译器它还是个全局内存的 read/write barrier(冲刷 pending 写);**但它不阻止 `expr` 本身被优化**——`expr` 的结果要是编译期就能算出来,它可能被整个算掉,只剩一个常量。

听着矛盾,其实是分工:`DoNotOptimize` 防的是「整段循环因为结果没人用而被删」(`foo()` 那种);它**不防**「循环体内部被常量传播算穿」。所以写 benchmark 的时候,输入数据必须是**运行期产生**的——从随机数、从文件、从参数,不能是编译期常量。否则编译器一路算穿,`DoNotOptimize` 也救不了你。Bakhvalov 在 §2.6 也强调了这一句: **先确保「你想测的场景」在运行期真被执行了。**（这就回到了我上一节的提示了，麻烦看看汇编）

`benchmark::ClobberMemory()` 是配套的另一件,强制把所有 pending 写真正落回内存。`push_back` 改了 `vector` 的内部状态(大小、可能的扩容),编译器要是判定「这个 `vector` 后面没人看」,某些边界条件下可能省掉一部分写。`ClobberMemory` 就是兜底那句「别省,真写」。常见的安全写法:热循环里每次写完目标数据 `DoNotOptimize` 钉一下地址,循环结束 `ClobberMemory` 兜底。

## 别只测一个 N

`BENCHMARK(BM_PushBack)->RangeMultiplier(2)->Range(8, 8 << 6)` 这行,让框架自动用 `8, 16, 32, 64, 128, 256, 512` 这一组 N 跑同一个 benchmark。为什么要扫一整组 N,而不是挑一个顺手测?

复杂度的真实形状,扫一组 N 才看得出来。`push_back` 均摊是 $O(1)$,但你扫一遍会发现小 N 时被缓存吃掉、大 N 时触发扩容的尖刺;只测一个 N,你看到的可能是缓存红利,也可能是扩容惩罚,完全取决于你手气。更狠的是 crossover 藏在尺度里——ch00-01 那个 `vector` vs `set`,只在 N=1024 看,`set` 反而略快;扫到 N=65536 才看到它被 `vector` 打到 5 倍。不扫尺度,这种翻转你根本看不见。

顺手 `state.SetComplexityN(state.range(0))`,框架还能根据你扫出来的时间自动拟合一个 big-O,输出里多一栏 `Big O`,让你对着复杂度直觉核一遍。比手算斜率省事。

## 重复几轮,报中位数别报单次均值

ch01-01 讲过性能是分布,一次测量没意义。GBench 的对策是 `Repetitions(n)`:同一个 benchmark 跑 n 轮(每轮内部迭代数框架自己估),然后 `ReportAggregatesOnly(true)` 只输出 `mean` / `median` / `stddev` / `cv` 这几个聚合,不把每轮原始值刷满屏。

为什么强调**中位数**而不只看均值:`push_back` 偶尔会撞上一次扩容——那是合法的均摊成本,但相对均值它是个离群值,均值被这种长尾拉高,中位数岿然不动。ch01-04 会专门讲什么时候用中位数、什么时候用均值、怎么报置信区间,这里你先记住一句:报中位数 + cv,比只甩一个均值诚实得多。`ReportAggregatesOnly(true)` 还有个隐形好处——CI 里跑 benchmark 时,聚合输出更适合做趋势对比和回归检测(ch01-05 接这条线)。

还有个细节得提一句:`UseRealTime()`。GBench 默认报的是 **CPU 时间**,多线程场景下会把别的核上跑的也算进来,往往不是你要的「这段代码墙上跑了多久」。`UseRealTime()` 把报告改成墙钟。这条跟 ch00-02 讲的 `clock()` 陷阱是一脉相承的——`clock()` 测 CPU 时间多线程失真,`steady_clock` 测墙钟。单线程测无所谓,一旦你的 benchmark 起了多线程(或者你想对标用户感受到的延迟),就加 `UseRealTime()`。

## 怎么编译

两条路,挑一条。

**系统装了 GBench**(Arch 是 `pacman -S benchmark`,macOS 是 `brew install google-benchmark`):

```bash
g++ -O2 -std=c++17 push_bench.cpp -o push_bench -lbenchmark -lpthread
./push_bench
```

注意链 `benchmark`(库)还是 `benchmark::benchmark_main`(自带 `main`):代码里写了 `BENCHMARK_MAIN()` 就链 `benchmark`;不想自己写 `main` 就链 `benchmark_main` 并删掉那行 `BENCHMARK_MAIN()`。

**用 CMake + FetchContent**(本卷代码示例走这条,reader 不用预装,clone 仓库就能跑):

```cmake
cmake_minimum_required(VERSION 3.20)
project(vol6_ch01_bench CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.5)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)   # 关掉它自己的测试目标
FetchContent_MakeAvailable(benchmark)
add_executable(push_bench push_bench.cpp)
target_link_libraries(push_bench PRIVATE benchmark::benchmark_main)
target_compile_options(push_bench PRIVATE -O2 -Wall -Wextra)
```

> ⚠️ **一个我踩过的坑**:关 benchmark 自己的测试目标,flag 是 `BENCHMARK_ENABLE_TESTING`(不是 `BENCHMARK_ENABLE_TESTS`)。写错名字的话,FetchContent 会去 build benchmark 的内部测试,缺 gtest 配置就炸,即便你的 `push_bench` 本身已经编过了,`cmake --build` 也会因为兄弟目标失败而整体返回非零。看 `make` 输出里有没有 `Built target push_bench` —— 有就说明你的可执行文件成了,直接 `./build/push_bench` 跑就行。

## 参考资源

- Google Benchmark:[user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md)(`DoNotOptimize` / `ClobberMemory` / `Range` / `UseRealTime` / `Repetitions` 各节,`DoNotOptimize` 的精确语义以这里的原文为准)
- Bakhvalov, D. 《Performance Analysis and Tuning on Modern CPUs》§2.6 *Microbenchmarks*(`foo()` 被 DCE 的例子、确保场景在运行期执行)
- 本卷 ch01-01「为什么 microbenchmark 会骗你」(三骗,本文是它的对策)
