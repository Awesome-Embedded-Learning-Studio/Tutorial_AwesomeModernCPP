---
chapter: 1
cpp_standard:
- 14
- 17
description: microbenchmark 是性能优化最常用的工具,也是最容易骗人的——拆开三类典型欺骗(编译器优化成空、缓存假热、噪声淹没信号),点破「让微基准干净的正是不让它真实的那只手」,给出
  Google Benchmark + nanobench 的选型
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 性能思维:efficiency 与 performance 不是一回事
- 从「先正确」到「再快」:为什么 sanitizer 是性能卷的地基
reading_time_minutes: 9
related:
- 怎么写一个可信的 microbenchmark
- 测量陷阱与环境就绪
- 统计与报告
tags:
- host
- cpp-modern
- intermediate
- 优化
- 测试
title: 为什么 microbenchmark 会骗你
---
# 为什么 microbenchmark 会骗你

## 性能不是一个布尔量

功能做对了就是对了,跑通了就是跑通了,这是布尔量。性能不是。性能是一个**分布**:同一段代码、同一份输入,你跑两遍数字就不一样;跑十遍,你能画出一张散点图。Bakhvalov 在《Performance Analysis and Tuning on Modern CPUs》第 2 章开篇就点明这件事:解压一个 zip 文件,你每次得到的结果一模一样(可复现);但要你「复现一模一样的性能曲线」,做不到。

这件事决定了本卷的整个方法论走向。既然性能是随机变量,「测性能」就不是「跑一次记个数」,而是采样一个分布、做统计推断,我们要的是平均意义上的统计量。

这一章讲的就是怎么把这件难事做对,它是全卷的锚点章。后面每一篇性能文章,开头都会回引它讲的那套规矩,就像 vol5 用 TSan 贯穿并发正确性一样。

但在这之前,得先认清一个让人不舒服的事实:你手上最趁手的那个工具 microbenchmark(微基准),恰恰是最容易骗你的。这一篇就把它的骗术拆开。

## 第一集:编译器把你的 benchmark 优化成空,或者优化出你没想到的代码

最经典也最尴尬的一种。你写了一段看上去在干活的循环,比如说,你想度量标准库字符串构造和析构的速度,好跟你自己写的字符串对比:

```cpp
// 这段「测试 string 创建性能」——其实什么都没测
void foo() {
    for (int i = 0; i < 1000; ++i) {
        std::string s("hi");   // 创建了,但从来没被读过
    }
}
```

`s` 创建出来,谁也没用。编译器一看,这是死代码,删了,整个循环连同 `string` 的构造全部消除(DCE)。我在自己机器上(GCC 16.1.1)实跑确认:这段代码 `-O2` 下 `foo` 的汇编就一条 `ret`,整个循环蒸发;同一段代码 `-O0` 下是 89 行汇编,循环和 `string` 构造全在。你美滋滋地跑完,记下「0.3 纳秒」,心想这函数真快。你测的是「什么都不做」。

> 笔者这里也要提醒一句:做 profile,除了一顿对自己的 perf 图高兴之外,请务必看看汇编。汇编是机器码的直接映射,读它大致就知道你的机器真正会执行什么。

这个坑我们在 ch00-02 讲 UB 的时候侧面碰过(那个 `(x+1)>x` 被折叠成常量的例子),这里换一个更直接的性能版本:**只要你的 benchmark 算出来的结果没人消费,编译器就有合法理由把它整段删掉。** 这不是编译器「bug」,这是允许的优化。

怎么办?强制让结果「被用到」。业内叫 `DoNotOptimize` 一类的辅助函数,底层用一点内联汇编把结果钉到内存或寄存器上,Google Benchmark 和 JMH(Java 的 `Blackhole.consume`)都内置了。它的语义和坑不少(ch00-01 那个 `volatile global_sink` 是手写版的近似),下一篇 ch01-02 会专门拆。

## 第二集:缓存总是热的,真实负载不是

microbenchmark 的标准做法是把一个函数反复跑成千上万次,取平均。问题就出在「反复」上:同一个函数反复跑同一份(或相似的)数据,那些数据从头到尾都赖在 L1/L2 cache 里,一次都不 miss。你测出来 2 纳秒,是这个「热缓存」条件下的 2 纳秒。

而真实负载里,这个函数两次调用之间,系统跑了一堆别的东西,cache 早被换成别人的数据了。它再被调用时,要从 L3 甚至 DRAM 现取,2 纳秒变 50、变 200。这就是 micro 和 macro 那道著名的数量级鸿沟(这里的 2/50/200 是业界量级说法,不是本文实测,意在给一个直觉)。

更阴险的版本,Bakhvalov 在第 2 章末尾点出来:一个在空闲机器上跑的 microbenchmark,**把全部 DRAM 和 cache 都据为己有**。于是你对比两个实现,A 更快但更吃内存,B 稍慢但省内存。在空闲系统的 microbenchmark 里,A 赢得很漂亮,因为它有吃内存的资本。可一旦上了生产,旁边挤着一堆邻居进程抢 DRAM,A 多占的那部分内存被挤到磁盘 swap,性能断崖式下跌,结论整个翻转。**让 A 看起来更快的,正是 microbenchmark 那个不真实的「全场没人跟我抢」的前提。**

这条推论极其重要,它是 ch00-01「efficiency ≠ performance」在测量层的镜像:别用 microbenchmark 的结论去给生产性能背书。micro 测的是「这个函数在理想条件下能跑多快」,不是「用户实际会体验到多快」。

## 第三集:系统噪声把信号淹了

哪怕你躲过了前两集(结果也消费了、缓存也认了它热),还有一类骗术来自系统本身:现代 CPU 和 OS 有一堆「为了提升性能」的特性,它们的副作用是让测量结果不稳定。

- **动态调频(DFS / Turbo)**:CPU 会根据温度和负载临时提频或降频。「冷」处理器跑第一轮时可能飙到 turbo 频率,跑第二轮时已经热了、降回基频,同一份代码两次跑差出百分之几到十几。笔记本上尤其严重(散热有限)。
- **文件系统缓存**:第一次跑要读盘,第二次数据都在缓存里了,第二次快得多。你以为第二次的优化见效了,其实只是盘不用再读了。
- **内存布局偏置**:这是最 spooky 的一种。Mytkowicz 等人 2009 年那篇经典论文证明,**UNIX 环境变量的总字节数、链接器输入的目标文件顺序**,都会改变程序的性能,而且方向不可预测。你什么代码都没改,只改了 `LINK_ORDER`,数字就动了。
- **甚至你用来监控的工具本身**:你在另一个核跑个 `top` 看 CPU 占用,那个核被激活、调频,可能连带干扰到跑 benchmark 的那个核。Bakhvalov 特意提醒,连开个任务管理器都能影响测量。

这三骗加起来,指向同一个结论:**一次性的、手搓的测量,几乎没有意义。** 你测到的那个数,是「这段代码在这个编译选项下在这台机器在这个温度这个频率这个内存布局这个缓存状态下」的数,换一个条件就变。

讲完三骗,值得退一步看一个更深的矛盾。为了让 microbenchmark 给出干净、稳定、可对比的数字,我们本能地会去**消除噪声**:锁死 CPU 频率、绑核、关超线程、预热缓存、跑够多轮取中位数。这些都没错,本卷后面会逐个教你怎么做。

但你要清醒地知道:**消除噪声的过程,就是让测量偏离真实环境的过程。** 你锁死了频率,可用户的手机从来不锁频;你绑死了一个核,可生产环境线程被调度来调度去;你把缓存预热到最佳,可真实调用 cache 冷得像冰。所以 Bakhvalov 那句忠告很关键:**评估真实性能时,不要去消除系统的非确定性,而要复刻目标环境。** 换个说法,让 micro 干净的那只手,正是让它不真实的那只手。

有人拍桌子了:你这是自相矛盾!不是,这是两种不同的测量场景,要分开用。

一方面,**microbenchmark** 做**相对比较**:同一个函数、两种实现、同一台机器、同一套控制条件,A 比 B 快多少。这时你要消除噪声,因为你要的是「干净的信噪比」。它的产出是「这个改动方向对不对」。而**生产测量 / 宏观 benchmark** 做**绝对判断**:用户实际感受到多快、能不能扛过下个月的流量。这时你反而要保留噪声、复刻真实,然后**用统计方法**处理那个噪声。它的产出是「这个数字扛不扛得住」。

一个常见的、灾难性的错误,是拿 micro 的相对结论去推 macro 的绝对判断:「我这个函数在 micro 里快了 30%,所以上线后服务会快 30%」。大概率不会,那 30% 里有一部分是「空闲系统让出来的红利」,在生产里根本不存在。这两种测量是两套语言,不能直接换算。生产测量和 CI 回归的事,ch01-05 专门讲。

## 别手搓,用框架

理解了为什么会骗,工具选型就很清楚了:千万别拿 `std::chrono` 手搓一个循环就开测(本卷 ch00-01 的那个 `vector_vs_set` 是为了讲命题故意用最朴素写法,那个例外)。一个合格的 benchmark 框架要替你处理掉「结果被优化掉」「跑多少轮」「怎么统计」这些机械活,让你专注写「测什么」。C++ 生态里几个主流选项:

| 框架 | 形态 | 防优化机制 | 统计输出 | 定位 |
|---|---|---|---|---|
| **Google Benchmark** | 静态库 | `DoNotOptimize` + `ClobberMemory` | mean / median / stdev,链式 API 最强 | **本卷主力** |
| **ankerl::nanobench** | 单头文件 | `doNotOptimizeAway` | ns/op + err%,**自带 IPC / branch miss%** | 轻量补充,讲微架构时即时反馈 |
| Catch2 `BENCHMARK` | Catch2 内置 | return 值当 sink | mean + 95% CI | 已用 Catch2 的项目顺手 |
| picobench / nonius / Hayai | 单头 | 各异 | 简单 | 不默认用,提一句 |

本卷的选型是 **Google Benchmark 主力 + nanobench 轻量补充**。理由是 GBench 那条链式 API,`BENCHMARK(f)->RangeMultiplier(2)->Range(8, 8<<10)->UseRealTime()->Repetitions(3)->ReportAggregatesOnly(true)` 一行就能表达「参数扫描 + 墙钟计时 + 多次重复 + 只报聚合」,其他库做不到这种组合;而 nanobench 自带硬件计数器(IPC、branch miss%),讲到微架构那几章时能给你即时反馈,很方便。从下一篇起,我们的代码示例会切到 GBench。

## 参考资源

- Bakhvalov, D. 《Performance Analysis and Tuning on Modern CPUs》第 2 章 *Measuring Performance*(噪声源、micro vs production、DCE 的 string 例子)
- Google Benchmark:[user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md)(`DoNotOptimize` / `ClobberMemory` / `Range` / `UseRealTime` / `Repetitions`)
- easyperf.net:*How to get consistent results when benchmarking on Linux*
- Mytkowicz et al., *Producing Wrong Data Without Doing Anything Obviously Wrong*, ASPLOS 2009(测量偏置:环境变量大小、链接顺序)
- ankerl::nanobench:[README](https://github.com/martinus/nanobench)
