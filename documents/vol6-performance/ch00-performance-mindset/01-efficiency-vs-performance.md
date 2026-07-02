---
title: "性能思维:efficiency 与 performance 不是一回事"
description: "从一段同是 O(log n) 的查找代码出发,讲透 efficiency(算法复杂度)与 performance(硬件上的真实表现)的鸿沟,立下本卷两条铁律与 Amdahl 天花板,附平台/库选型决策一页"
chapter: 0
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 优化
  - 实战
difficulty: intermediate
platform: host
reading_time_minutes: 16
cpp_standard: [14, 17]
prerequisites:
  - "C++ 容器与算法基础(std::vector / std::set / std::lower_bound)"
related:
  - "为什么 microbenchmark 会骗你"
  - "存储层次与延迟阶梯"
  - "ASan 工具家族与内存安全"
---

# 性能思维:efficiency 与 performance 不是一回事

## 一个让很多人不舒服的事实

我们先看一段几乎人人都写过的代码:在一个集合里查一个数。最自然的两个选择——把数据放进 `std::set`,或者放进排好序的 `std::vector` 然后用 `std::lower_bound` 做二分查找。两边查一次都是 $O(\log n)$,复杂度完全一样,教科书讲到这一步通常就停了,告诉你「随便选,差不多」。

但如果你真的去测,会发现事情没那么简单。下面这张表是我在自己机器上跑出来的(WSL2/Linux,2,000,000 次随机命中查询,5 次取中位数,完整代码见本章「代码示例」一节):

| 元素数 N | `vector`+二分 (ns/次) | `set`.find (ns/次) | `set` / `vector` |
|---:|---:|---:|---:|
| 1,024 | 43 | 39 | 0.9× |
| 4,096 | 51 | 63 | 1.3× |
| 16,384 | 61 | 98 | 1.6× |
| 65,536 | 81 | 275 | 3.4× |
| 262,144 | 105 | 578 | **5.5×** |
| 1,048,576 | 185 | 1006 | **5.4×** |

N 超过 6 万以后,`set` 比 `vector` 慢了 3 到 5 倍;而在 N 很小(1024)的时候,`set` 反而还略快一点点。两边复杂度一模一样,凭什么差这么多?更尴尬的是,如果你的面试题答案是「二者等价」,你在真实代码里就会莫名其妙地交出一个慢几倍的服务。

答案就是这一整卷要反复回到的命题:**efficiency(效率)和 performance(性能)不是一回事。**

## efficiency 与 performance,到底差在哪

先把两个词分清楚,本卷后面所有讨论都建立在这个区分上:

- **efficiency(效率)** 是算法复杂度维度:总工作量(work)、关键路径长度(span)、big-O 记号。这是一种**数学性质**,和具体硬件无关——你说二分查找是 $O(\log n)$,不管它在 x86、ARM 还是一台纸带机上跑,这个结论都成立。
- **performance(性能)** 是你的数据**在真实硬件上怎么流动**:命中了哪一级缓存、有没有触发分支预测失败、有没有被向量化、有没有伪共享。这是一种**工程性质**,只能在具体的硬件上测出来,换个 CPU 型号可能结论就变了。

问题出在:big-O 把所有「硬件相关」的效应——缓存命中与否、分支预测准不准、常量因子的实际大小——一股脑塞进一个隐含的常数 $C$ 里,然后假装它不重要。`std::set` 主流实现(libstdc++/libc++/MSVC)是红黑树,每个节点单独分配,散落在堆的各个角落;查找时每往下一层都是一次指针解引用,跳到的下一个节点位置不可预测——这是一种叫 **pointer chasing**(指针追逐)的模式,硬件预取器学不会,于是大 N 下几乎每一层都是一次 cache miss。`std::vector` 的元素是**连续存放**的,二分查找跳到的那些点至少落在一段紧凑的内存里(一个 cacheline——CPU 从内存取数据的最小单位,通常 64 字节——能装下 16 个 `int`,整段数组容易被 L2/L3 装下)。复杂度分析把这条鸿沟全部塞进了常数 $C$,于是一句「都是 $O(\log n)$」就把 5 倍的真实差距抹平了。

Denis Bakhvalov 在《Performance Analysis and Tuning on Modern CPUs》第 1 章举了一个更反直觉的例子:在**小规模**输入下,InsertionSort($O(n^2)$)实测打败 QuickSort($O(n \log n)$)——因为 Big-O 无法刻画分支预测和缓存效应,它俩的差异被藏进了那个「不重要」的常数里。他在书里这段话的大意是:复杂度分析无法考虑各种算法的分支预测和缓存效应,所以只能把它们封装在一个隐含的常数 $C$ 里,而这个常数有时会对性能产生决定性的影响。

这就是本卷的总命题:**别只看 big-O,要看数据在硬件上怎么流。** 后面 ch02 会把缓存层级、cacheline、延迟阶梯这些硬件细节正式展开;但你现在就要建立一个直觉——「复杂度低 = 跑得快」是一个会在真实代码里让你出丑的错觉。同一个 $O(\log n)$,差 5 倍;同一个 $O(n)$,差几十倍都有可能(顺序遍历 vs 随机访问)。

## 铁律一:先正确,再快

CS:APP 第 5 章开篇第一句就把这条钉死了(原文我们直接引,因为它没法说得更准):

> The primary objective in writing a program must be to make it work correctly under all possible conditions. A program that runs fast but gives incorrect results serves no useful purpose.
>
> (写程序的首要目标,是让它在所有可能条件下都算对。一个跑得飞快但结果错误的程序,没有任何用处。)

听起来像废话,但放进性能优化这个场景,它有一条非常具体的、会被反复违反的推论:**带着未定义行为(UB)去谈性能数字,等于在地基没打牢的工地上盖楼。** UB 在 `-O2` 下不会乖乖待着——编译器会基于「这段 UB 永远不会发生」做激进优化,结果常常是:你的 benchmark 测的早就不是一个真实的函数调用,而是一个被优化得面目全非的空壳,你对着它得出一堆精美又全错的结论,还信心满满地拿去「优化」生产代码。

所以本卷把 sanitizer 工具链(ASan / UBSan / MSan / TSan)放在 ch00 当地基,而不是当成「调试工具混进了性能卷」。没有 sanitizer 兜底的正确性,性能数字一律不可信;并发代码没过 TSan 的数字,同样不可信。我们后面会专门讲这条链路,这里你只需要记住结论——**先正确,再快,这条没有商量余地。**

## 铁律二:先测量,再优化

你的直觉,在微架构这个层面,经常是错的。「数据紧凑一点、少做一次分配」这种大方向的直觉当然对;但一到「该不该无分支」「循环要不要手展」「虚函数到底慢不慢」这种指令级细节,直觉就开始追不上硬件了。这不是贬低谁,这是现代 CPU 的复杂度决定的——一颗当代 CPU 里有乱序执行、分支预测、缓存层级、预取器、SIMD、微操作融合……你的「感觉」追不上这些。

举几个本卷后面会逐个用实测拆给你看的案例:

- 你以为无分支(branchless)更快,结果现代分支预测器把**可预测的**分支处理得几乎免费,你的 branchless 改写反而多塞了几条指令、引入了数据依赖,更慢;
- 你以为手写循环展开能提速,结果编译器在 `-O2` 早就展开过了,你再写一通只让代码更难读、icache 更差;
- 你以为虚函数调用很慢,结果编译器早就根据类型层次把它去虚拟化(devirtualize)成了直接调用,甚至内联了。

这些都不是假设,是后面 ch04 / ch06 的真实内容。结论只有一句话:**优化之前先 profile。** 火焰图上最宽的那个盒子,才是值得动手的地方;凭直觉改代码,大概率你在优化那 5% 的部分,而真正的瓶颈在另外 95% 里躺着睡大觉。

这条铁律直接引出了 ch01——Benchmark 方法论。那是本卷的**锚点章**:后面每一篇性能文章,开头都会回引它讲的那套测量规矩,就像 vol5 用 TSan 贯穿并发正确性一样。如果你时间只够读这一卷里的一篇文章,读 ch01。

## Amdahl:优化的天花板

在动手改任何代码之前,还得知道一条硬规矩——Amdahl 定律。它最早由 Gene Amdahl 在 1967 年提出,一句话说清加速比的上限在哪:

$$S = \frac{1}{(1 - p) + \dfrac{p}{N}}$$

其中 $p$ 是「能被加速的部分」占总时间的比例,$N$ 是你给这部分施加的加速比。分母里那个孤零零的 $(1 - p)$ 就是串行部分——它不吃你的加速,原封不动留在那里。

代几个数你就 felt 到它的残酷了:哪怕你把占 90% 的部分加速 1000 倍($p=0.9, N=1000$),总加速也只有 $1 / (0.1 + 0.0009) \approx 9.9\times$。上限在哪?让 $N \to \infty$(你把那 90% 无限加速),$p/N \to 0$,$S \to 1/(1 - p) = 1/0.1 = 10\times$——这就是「锁死在 10 倍以内」的来历:剩下那 10% 的串行代码,你拿它一点办法都没有,再怎么把并行部分往死里压榨,串行部分岿然不动。

推论极其重要:**优化要打在「占比大的串行部分」上。** 这就是 profile 驱动优化的理论根据——不是「哪里看起来慢就改哪里」,而是「先测出哪里占比最大,再改那里」。Amdahl 定律的完整推导、它和 Gustafson 定律的对比(固定问题规模的强扩展 vs 随核数扩大问题规模的弱扩展),vol5 第 0 章已经讲透了,我们这里只取「优化天花板」这一个视角,不重复造轮子。

## 别忽略最大的那个杠杆:平台和库

讲完两条铁律和一条天花板,在进入微观优化之前,先退一步看宏观。Agner Fog 在他的优化手册第 1 卷里专门用了一整章(ch2 *Choosing the optimal platform*)讲「选择最优平台」,顺序是这样的:硬件平台 → 处理器型号 → 操作系统 → 编程语言 → 编译器 → 函数库 → UI 框架。他的态度很直白:**这些高层决策对性能的影响,通常比你后面抠的任何一条微优化都大。**

我们把它压成一页,几条关键判断:

- **先干掉最浪费的工具/框架。** 选了一个处处堆分配、层层虚函数的重量级框架,后面再怎么抠 cacheline、抠对齐都补不回来。Agner 引用了 Wirth's law 这条半开玩笑的格言:软件变慢的速度,比硬件变快的速度还快。这两件事同时发生的时候,用户体验就是原地踏步甚至倒退。
- **数据结构选型 > 微优化。** 我们开头那个 `vector` vs `set` 的例子就是明证——换一个缓存友好的容器,5 倍收益到手,比你手动展开循环、抠位运算实在得多。先把这种「结构性」的收益吃掉,再谈指令级优化。
- **嵌入式向:host 和 MCU 的资源差好几个数量级。** 在 host 上无所谓的一次堆分配,到 STM32 上可能就是一次碎片化灾难;host 上验证过的优化,原理在 MCU 上往往同样成立,但 MCU 的可用内存小太多,host 上根本不在意的开销,在 MCU 上可能就是性能或容量的硬瓶颈。本卷代码示例以 host 为主,涉及嵌入式向的话题会单独点名,带你去 vol8 嵌入式域看完整故事。

平台选型的完整清单 Agner 写了十几页,我们这里压成一页,因为它不是本卷的技术主体——但它常常是被忽略的、收益最大的那一刀。很多人性能优化做不动,不是微架构没吃透,是一开始工具/库就选错了。

## 代码示例:亲手验证 vector vs set

口说无凭,我们把开头那张表的代码摆出来。这是一个**自包含**的基准测试,不依赖任何外部库,标准 C++17 就能编(后面 ch01 会正式引入 Google Benchmark 那一套工业级方法论,这里先用最朴素的 `std::chrono`,免得在本卷第一篇就堆概念)。

```cpp
// vector_vs_set.cpp —— 同为 O(log n) 的查找,缓存效应能差多少?
// 编译:g++ -O2 -std=c++17 vector_vs_set.cpp -o vector_vs_set
#include <vector>
#include <set>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstdio>
#include <cstdint>

using Clock = std::chrono::steady_clock;

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    constexpr int queries = 2'000'000;   // 每个 N 查 200 万次,摊薄单次噪声
    constexpr int trials  = 5;            // 跑 5 轮取中位数,后续 ch01 会讲为什么
    volatile std::int64_t global_sink = 0; // 防止整段循环被死代码消除

    printf("%-10s %18s %18s %10s\n",
           "N", "vector(ns/q)", "set(ns/q)", "set/vector");
    for (int N : {1024, 4096, 16384, 65536, 262144, 1048576}) {
        std::mt19937_64 rng(12345);
        std::vector<int> keys(N);
        for (int i = 0; i < N; ++i) keys[i] = i * 2;          // 偶数、稀疏
        std::vector<int> sorted = keys;
        std::sort(sorted.begin(), sorted.end());              // vector 二分用
        std::set<int> sset(keys.begin(), keys.end());         // set 红黑树

        std::vector<int> toFind(queries);                     // 全部命中,消除「找不到」偏差
        for (int i = 0; i < queries; ++i) toFind[i] = keys[rng() % N];

        std::vector<double> tv, ts;
        for (int t = 0; t < trials; ++t) {
            std::int64_t acc = 0;
            auto a = Clock::now();
            for (int q : toFind) {
                auto it = std::lower_bound(sorted.begin(), sorted.end(), q);
                acc += (it != sorted.end() && *it == q);
            }
            auto b = Clock::now();
            tv.push_back(std::chrono::duration<double, std::nano>(b - a).count() / queries);
            global_sink += acc;

            acc = 0;
            auto c = Clock::now();
            for (int q : toFind) {
                auto it = sset.find(q);
                acc += (it != sset.end());
            }
            auto d = Clock::now();
            ts.push_back(std::chrono::duration<double, std::nano>(d - c).count() / queries);
            global_sink += acc;
        }
        double mv = median(tv), ms = median(ts);
        printf("%-10d %18.1f %18.1f %10.1fx\n", N, mv, ms, ms / mv);
    }
    printf("\nglobal_sink=%lld (防死代码消除)\n", (long long)global_sink);
}
```

我们挑几处关键的讲一下为什么这么写,因为这里的每一个细节都是后面 ch01 测量方法论的伏笔。

第一,**结果必须被消费**。`acc` 累加命中数,最后喂给 `volatile global_sink`。没有这一步,编译器会发现「这个循环算出来的结果没人用」,直接把整段循环删掉(DCE),你测的是一个空程序。`volatile` 强制每次真的写内存,堵死这条优化路径。

第二,**全部查命中**。`toFind` 里的数都是集合里真实存在的 key。如果不控制,「查得到」和「查不到」走的路径长短不一样,会污染结果。我们要比的是纯查找成本,不是命中率。

第三,**多轮取中位数**。单次跑出来 50ns 还是 80ns,可能只是这一轮 CPU 被调度走了、或者 Turbo 频率没爬上来。跑 5 轮取中位数,把这种离群值压下去。这一条看似琐碎,但它是 ch01 整章要展开的「性能数字是随机变量」的起点——你测出来的不是一个数,是一个分布。

第四,**用 `steady_clock`**,不用 `clock()`。`clock()` 测的是进程 CPU 时间——多线程下会把别的核的繁忙也算进来,阻塞/睡眠又不计入,根本不是我们要的「这段代码墙上跑了多久」;`steady_clock` 是单调递增的高分辨率时钟,不会像 `system_clock`(那才是真正的墙钟)那样在系统改时间时被回拨,专门用来量「两件事之间隔了多久」,正是这里要的。ch01 会把「该用哪个时钟」单独讲。

跑出来你会看到和本篇开头那张表一致的趋势:小 N 时 `set` 在我机器上反而略快(0.9× 左右),N 一旦超出缓存,`set` 就被 cache miss 拖着成倍变慢,而 `vector` 靠连续内存把曲线压得很平。

小 N 那个「反常」值得多说一句,因为它正好暴露了 big-O 藏不住的第二样东西——**分支预测**。N=1024 时,`set` 的整棵红黑树和 `vector` 二分会跳到的那些元素,都还全在 L1 里,缓存这把刀根本还没发威。真正的差异在分支:`lower_bound` 每一步的比较,对随机命中的查询来说几乎是 50/50,分支预测器猜不中,猜错一次要冲刷流水线(代价十几个周期);而 `set::find` 每层除了那次 key 比较,还有几个高度可预测的操作(空指针检查、指针更新),缓存不缺位时,这点指令混合的差异足以让它反超。Bakhvalov 在书里专门讲过 binary search 这种「缓存还没发力时,反倒被分支预测拖累」的反直觉现象。注意这个「小 N set 略快」是在我这台机器 + libstdc++ 上稳定复现的,但**换编译器、换 STL 实现、换微架构就可能翻转**——所以下面警告框里那句「`set` 略快那行可能消失」指的是换环境,不是同一台机器上的随机抖动。两条都是 $O(\log n)$,小 N 谁快由分支预测和指令混合决定,大 N 谁快由缓存决定,这就是 efficiency ≠ performance。

> ⚠️ **别把这张表当普适结论。** 你在不同 CPU、不同编译器、不同 libc++ 实现上跑出的绝对数字会变,`set` 略快的那一行可能消失,也可能变得更明显。我们关心的是**趋势**(连续内存 vs 节点分散)和**命题**(同复杂度差几倍),不是某个具体倍数。把别人的性能数字直接抄进自己的工程,是另一种「猜」。

性能问题永远从「数据在硬件上怎么流」出发,不从「我觉得」出发——至于怎么把「我觉得」换成「我测过」,那是 ch01 的事。

## 参考资源

- Bryant, R. E., O'Hallaron, D. R. 《Computer Systems: A Programmer's Perspective》第 5 章 *Optimizing Program Performance*(「先正确再快」、优化分层、Amdahl 视角)
- Bakhvalov, D. 《Performance Analysis and Tuning on Modern CPUs》第 1 章 *Introduction*(complexity analysis 的局限、InsertionSort vs QuickSort 案例)
- Fog, A. 《Optimizing Software in C++》第 1–2 章(Why software is often slow / Choosing the optimal platform)
- cppreference:[`std::lower_bound`](https://zh.cppreference.com/w/cpp/algorithm/lower_bound)、[`std::set::find`](https://zh.cppreference.com/w/cpp/container/set/find)
- Amdahl 定律原始出处:Gene Amdahl, *Validity of the single processor approach to achieving large scale computing capabilities*, AFIPS 1967
