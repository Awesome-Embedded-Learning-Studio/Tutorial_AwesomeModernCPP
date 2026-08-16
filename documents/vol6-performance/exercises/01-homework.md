---
title: "卷六课后练习（Homework）"
description: "卷六性能优化的课后练习：ch00~ch07 每章 2 题（基础+进阶），另加 2 道跨章综合与 1 道 L5 挑战（分块矩阵乘，改编自 CSAPP 第 6 章 cache blocking）。难度覆盖 L1~L5，题目都做了变式处理，参考答案独立成文件、逐步解答附知识点链接，全部代码在 WSL（g++ 16 / clang++ 22）真编译真跑，计时题多轮取中位数并注明抖动。"
chapter: 6
order: 1
tags:
  - host
  - intermediate
  - cpp-modern
  - 优化
  - 测试
  - 工程实践
difficulty: intermediate
platform: host
reading_time_minutes: 25
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷六课后练习（Homework）

## 引言

这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](index.md)）和涉及章节。题目都是「变式」——换场景、换数据、换推理方向，照抄教材例题抄不出答案；每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一章做完再看答案。编译约定：普通题 `g++ -O2 -std=c++17 -Wall -Wextra` 起步，个别题目要求别的旗标会写明；涉及内存/UB 的题过 UBSan 或 ASan；计时类题目多轮取中位数，本卷所有性能数字都会抖，**秒数会抖、量级趋势稳定**——你的数字和参考答案对不上量级以内都正常，对趋势就对答案。

## 6.0 性能思维与正确性前置

### 6.0-A {#hw-6-0-a}

难度 **L1** · 涉及[性能思维:efficiency 与 performance 不是一回事](../ch00-performance-mindset/01-efficiency-vs-performance.md)

三道小题。①某程序 75% 的计算可并行、25% 必须串行。用 Amdahl 定律手算 2 核、8 核、无穷核的理论加速比；再回答：如果我把可并行部分的比例从 75% 提到 90%，无穷核的加速比上限从多少变成多少？②判断题（对/错，并说一句理由）：a)「两个算法都是 $O(n)$，它们的运行速度也差不多」；b)「性能优化应该在动手改代码之前先做 profile」；c)「Amdahl 定律告诉我们应该优先优化耗时占比最大的串行部分」。③写一个程序打印 `sizeof(std::vector<int>)`、`sizeof(std::set<int>)`、`sizeof(int)`，再用 `getconf LEVEL1_DCACHE_LINESIZE` 查出本机缓存行大小——这三个数字放在一起，能不能解释为什么同样的 $O(\log n)$，`vector`+二分和 `set` 查找会差几倍？不能解释的部分又是什么造成的？

[参考答案 →](02-homework-solutions.md#hw-6-0-a)

### 6.0-B {#hw-6-0-b}

难度 **L2** · 涉及[从「先正确」到「再快」:为什么 sanitizer 是性能卷的地基](../ch00-performance-mindset/02-from-correctness-to-performance.md)、[Sanitizer 工具链全景](../ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md)

教材用 `(x+1) > x` 演示了「带 UB 的代码被 -O2 折叠」。这次换方向：写 `bool always_less(int x) { return (x - 1) < x; }`，`main` 从命令行读入 x，分别用 `-O2`、`-O2 -fwrapv` 编译，都喂 `x = -2147483648` 跑一遍。①贴出两份结果，解释为什么不一样（结合汇编：-O2 下 `always_less` 的函数体被编译成什么？）。②用 `-fsanitize=undefined` 在 -O0 和 -O2 下分别重编重跑——你观察到了什么、**没有**观察到什么？如果 UBSan 没报，说出它为什么抓不到（提示：减法还在不在？）。③把这个函数拆成两行（`int y = x - 1; return y < x;`），再用 UBSan 跑一遍，对比②——UBSan 能抓到哪个版本、为什么？④把三份结果合起来，回答：为什么「带 UB 的性能数字不可信」这句话，在这个实验里变成了「带 UB 的 benchmark 连测的对象都可能是错的」？

[参考答案 →](02-homework-solutions.md#hw-6-0-b)

## 6.1 Benchmark 方法论

### 6.1-A {#hw-6-1-a}

难度 **L2** · 涉及[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)、[怎么写一个可信的 microbenchmark](../ch01-benchmark-methodology/02-credible-microbenchmark.md)

三道小题。①写出教材三骗各自的名字，并把下面三个场景各归到一骗：「空闲机器上独占全部 cache 的对比」「结果没人消费的循环」「笔记本 Turbo 进进出出导致两轮数字差 10%」。②写一个 `dead_loop`：循环 200 万次，每次 `std::vector<int> v(64); v[0] = i;` 但 v 永远没人读；再写 `live_loop`：同样循环，但把 `v[0]` 累加进 `volatile long long`。分别用 `-O0` 和 `-O2` 编译跑，贴出四次耗时；用 `g++ -O2 -S` 贴出两个函数在 -O2 下的汇编——`dead_loop` 剩几条指令？③为什么 `volatile` 在这里救回了 `live_loop`，但它不是 benchmark 的正规解法（正解是什么，见教材 ch01-02 的两个 API）？

[参考答案 →](02-homework-solutions.md#hw-6-1-a)

### 6.1-B {#hw-6-1-b}

难度 **L3** · 涉及[统计与报告:把分布变成结论](../ch01-benchmark-methodology/04-statistics-and-reporting.md)、[测量陷阱与环境就绪:16 条 checklist](../ch01-benchmark-methodology/03-pitfalls-and-env.md)

写一个**不依赖任何框架**的可信微基准，A/B 对比「`push_back` 前先 `reserve`」与「不 reserve」：两个 `noinline` 函数分别往空 `vector<int>` 里 `push_back` 100 万个整数（N 和轮数从命令行读入），主程序**交替**测两版各 15 轮，把两组的耗时排序后报**中位数、均值、cv（变异系数）**和比值。①贴出你的实现与输出——本机两个 cv 大概是什么量级？对照教材 ch01-02「cv > 5% 这组数不可信」，说说你这次测量「可信」到什么程度、哪些噪声源没关（回顾 ch01-03 的 checklist，本机 WSL2 哪些条做不到）。②为什么 N 必须从命令行读入、不能写死成 constexpr？③为什么报中位数而不是单次均值？④把轮数从 15 加到 31，cv 有没有变小？如实记录。

[参考答案 →](02-homework-solutions.md#hw-6-1-b)

## 6.2 CPU 微架构与存储层次

### 6.2-A {#hw-6-2-a}

难度 **L2** · 涉及[存储层次与延迟阶梯:为什么顺序访问快 100 倍](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)、[缓存行与局部性:64 字节的最小搬运单位](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)

三道小题。①一条 64 字节缓存行能装下几个 `int`？几个 `double`？「访问 1 个 `int` 触发 miss 时，硬件实际搬运了多少字节」——把这个模型用一句话讲清。②写一个**指针追逐（pointer chasing）探针**：构造一条贯穿全部节点的置乱单环（`nxt[perm[i]] = perm[(i+1) % elems]`），对 4 KB、256 KB、8 MB、64 MB 四个工作集各走若干步，打印每个工作集的 `ns/访问`。贴出你的四级延迟，指出每一级大致落在哪层 cache（L1/L2/L3/DRAM），并算出 L1 与 DRAM 之间差多少倍。③为什么这个探针测出来的才是「裸访存延迟」——预取器为什么在这个循环里帮不上忙（说出关键的那一个词）？

[参考答案 →](02-homework-solutions.md#hw-6-2-a)

### 6.2-B {#hw-6-2-b}

难度 **L3** · 涉及[缓存行与局部性:64 字节的最小搬运单位](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)、[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)

教材在 2048×2048（16 MB，正好等于教材那台机器的 L3）上测了行优先 vs 列优先的 6 倍差距。这次做**双尺寸**变式：对 `int` 矩阵（`a[i*N+j]` 行优先存储）分别用 N=1024（4 MB）和 N=4096（64 MB）测「沿行求和」与「沿列求和」的耗时，各跑 3 轮，贴出两个尺寸下的比值。①两个尺寸的比值一样吗？如果不一样，先**预测**哪个尺寸比值更大，再验证，并解释原因（提示：工作集和 L3 的关系；你的机器 L3 多大可以从上一题 6.2-A 的探针数据推断）。②编译器为什么不帮你自动交换循环次序（说清楚它「不敢」的理由）？③写出「把列优先遍历改成行优先」应该怎么改，并验证改完耗时回到行优先水平。

[参考答案 →](02-homework-solutions.md#hw-6-2-b)

## 6.3 归因方法论:从测量到瓶颈

### 6.3-A {#hw-6-3-a}

难度 **L2** · 涉及[USE 方法与 Roofline 模型:先看全局,再判算力还是带宽](../ch03-attribution-methodology/03-01-use-and-roofline.md)

三道小题。①手算下面两个内核的**算术强度（FLOP/byte）**：点积 `dot = Σ a[i]*b[i]`、AXPY `y[i] = α*x[i] + y[i]`，写出每个的运算次数、内存字节数和最终值。②Roofline 图上横轴、纵轴、两条屋顶线分别是什么？「程序点贴着斜线」和「贴着水平线」各说明什么、优化方向各是什么？③写一个探针：对 800 万个 `float` 的 `a`、`b` 两数组做 20 遍点积，打印单遍耗时、总内存流量和吞吐（GB/s）。把实测吞吐和你①里的算术强度合起来，判断这个内核落在哪条线上——为什么说「给它加 SIMD 也突破不了带宽墙」（先答，后面 6.4-B 和 Lab 会亲眼看到这件事）？

[参考答案 →](02-homework-solutions.md#hw-6-3-a)

### 6.3-B {#hw-6-3-b}

难度 **L4** · 涉及[TMAM 四桶与硬件采样:LBR / PEBS / Intel PT](../ch03-attribution-methodology/03-02-tmam-and-hw-sampling.md)、[归因实战:从一个慢程序到定位瓶颈](../ch03-attribution-methodology/03-04-walkthrough.md)

四道推理题。①TMAM 四个桶的名字、各代表什么？哪一桶是「好桶」？②给四个场景判桶（Backend Memory / Backend Core / Bad Speculation / Frontend，各说一句理由）：a) 一个热循环遍历 64 MB 数组、每次访问踩新缓存行；b) 循环里有 `if (data[i] & 1)` 且 data 是随机数；c) 循环里连续做 10 次互相依赖的浮点加法；d) 二进制里塞了几千个模板实例化出来的函数、热点在几个函数之间跳来跳去。③「瓶颈会迁移」是什么意思？你修好了 Backend Memory，接下来哪个桶最容易抬头？为什么「改完必须用同一套方法论重测」？④写一个小实验佐证 a) 场景：对 8 MB 和 128 MB 两个 `long` 数组各反复求和若干遍，打印吞吐（GB/s）——两个吞吐差多少？这个差就是 a) 场景「Backend Memory Bound」的直接证据，为什么？

[参考答案 →](02-homework-solutions.md#hw-6-3-b)

## 6.4 按瓶颈部位优化

### 6.4-A {#hw-6-4-a}

难度 **L2** · 涉及[数据类型与算术:整数/浮点、乘除与跳转表](../ch04-tuning-by-bottleneck/04-03-types-and-arithmetic.md)

写一个逐元素算术成本实验：准备 1 亿个 `int64_t`（`a[i] = i*3+5`），分六个循环各跑一遍并计时（每元素平均 ns）：`a[i]/8`、`a[i]>>3`、`a[i]/7`、`a[i]/d`（d 从命令行读入，如 19）、`a[i]*3`、`a[i]%d`。①贴出你的六个数，变量除法是乘法的几倍？②`a[i]/8` 和 `a[i]>>3` 为什么几乎一样快？`a[i]/7` 比变量除法快是为什么（编译器做了什么）？③一个哈希表把桶数取成 2 的幂、用 `i & (size-1)` 取模——说出这条优化省掉了什么、前提是什么。④**坑**：如果题目让你测的是 `x/d`（x、d 都是循环外的变量），你会掉进什么陷阱？本卷参考答案作者第一次就是这么翻车的——循环不变量会被谁提走？你怎么改（看上面的题面）才能保住「每次迭代都真做一次除法」？

[参考答案 →](02-homework-solutions.md#hw-6-4-a)

### 6.4-B {#hw-6-4-b}

难度 **L4** · 涉及[循环与计算优化:code motion、展开与多累加器](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)、[SIMD 与向量化:自动向量化条件、intrinsics 与 CPU 分发](../ch04-tuning-by-bottleneck/04-05-simd.md)

平方和 `acc += x[i]*x[i]`（400 万个 float）是教材点积的变式，同样是 FP 归约。写两个版本：单累加器 `sos1` 和手写 4 累加器 `sos4`（`noinline`），各跑 15 轮报最快。①分别用 `-O2`、`-O3` 编译跑，贴出两个优化级别下的比值与两个版本的结果差（绝对差多大？这是谁的代价？）。②再分别用 `-O3 -ffast-math` 编译跑——单累加器发生了什么变化？两个版本的结果差变成了多少？③为什么编译器**默认**不敢把单累加器自动拆成多通道，而 `-ffast-math` 一开就敢（说出标准里的那两个字）？④`-ffast-math` 为什么不能全局无脑开（它对 NaN/Inf 做了什么）？工程上怎么在「要快」和「要严格 FP 语义」之间划界？

[参考答案 →](02-homework-solutions.md#hw-6-4-b)

## 6.5 多核性能

### 6.5-A {#hw-6-5-a}

难度 **L2** · 涉及[锁的开销与「无锁不是银弹」](../ch05-multicore-performance/05-03-locks-vs-lockfree.md)

写一个单线程同步成本实验：各跑 1 亿次并报 ns/op——①普通 `long long` 自增；②`std::atomic` `fetch_add(1, relaxed)`；③`fetch_add(1, seq_cst)`；④`mutex` 加解锁 + 普通自增；⑤「`mutex` 锁住后自增 100 次」的每元素摊薄成本。①贴出你的五个数。教材（AMD Zen 3）测出 mutex 是 atomic 的 3.6 倍——**你的机器上这个倍数很可能是倒过来的**。如实贴出你的比值，并用两件事解释：不同机器上「lock 前缀指令延迟」和「futex 快速路径」的相对成本不同；无论顺序怎么翻，两个结论不变的是哪两条（量级、以及 plain 自增为什么是 0.00）？②⑤比④说明「临界区变大」对 mutex 的开销意味着什么——这也解释了决策表里为什么「竞争不强 + 复杂临界区」推荐用 mutex 而不是原子。③为什么 plain 自增测出 0.00 ns，这是不是「无同步免费」的证据——它被优化成了什么？

[参考答案 →](02-homework-solutions.md#hw-6-5-a)

### 6.5-B {#hw-6-5-b}

难度 **L4** · 涉及[NUMA、affinity 与扩展性曲线](../ch05-multicore-performance/05-02-numa-scaling.md)、[性能思维:efficiency 与 performance 不是一回事](../ch00-performance-mindset/01-efficiency-vs-performance.md)（Amdahl）

写一个双负载扩展性实验：负载 A（带宽受限）是「并行求和 3200 万个 `long`（256 MB）」；负载 B（算力受限）是「对 32 万个元素各做 1000 次乘加混叠」。分别用 1/2/4/8 个 `std::thread` 分块跑，打印每种配置的耗时和加速比。①贴出你的两条扩展性曲线。②两个负载在 8 线程的加速比差多少？为什么 A 的天花板来得更早（说清「共享内存带宽先打满」这件事）？③教材 ch05-02 的 8 线程只有 2.53×，你的 B 负载为什么更接近线性——它躲开了哪个共享瓶颈？④用 Amdahl 定律反推：如果你的 8 线程加速比是 X，等效的「串行比例」大约是多少？⑤这个实验的线程创建开销怎么处理才算公平（为什么计时要放在线程创建之前）？

[参考答案 →](02-homework-solutions.md#hw-6-5-b)

## 6.6 C++ 抽象的性能成本

### 6.6-A {#hw-6-6-a}

难度 **L2** · 涉及[RVO、NRVO 与 move 的真实成本](../ch06-cpp-abstraction-cost/06-05-rvo-move.md)、[C++ 抽象的成本速查表](../ch06-cpp-abstraction-cost/06-04-abstraction-cost-cheatsheet.md)

写一个带拷贝/移动构造**计数器**的 `struct Tracked { int v; static int64_t copies, moves; ... }`（拷贝构造 `++copies`，移动构造 `++moves` 并把源置 -1），四个 `noinline` 工厂函数分别返回：`Tracked(1)`（URVO）、局部变量 `t`（NRVO）、`std::move(t)`、全局 `g_global`（lvalue）。逐个调用并在每个之间重置计数器。①贴出四行计数（copies/moves），哪一行是「反模式」、它为什么永远只会更慢（提示：它禁用了什么）？编译时那个 `-Wpessimizing-move` 警告原文是什么？②加 `-fno-elide-constructors` 重编重跑，哪几行变了、哪一行**没变**——没变的那行对应 C++17 的哪条强制规则？③写第二个实验：`noinline` 的 `copy_of(const vector<int>&)` 和 `move_of(vector<int>&&)`，对 100 万个 `int`（4 MB）各测 300 次，贴出 copy 与 move 的单次成本比值——move 为什么是 O(1)？④「按值返回大对象要改用指针/引用」这条旧教条在现代 C++ 里为什么基本过时了？

[参考答案 →](02-homework-solutions.md#hw-6-6-a)

### 6.6-B {#hw-6-6-b}

难度 **L3** · 涉及[std::function 的小缓冲区优化:类型擦除的代价](../ch06-cpp-abstraction-cost/06-03-std-function-sbo.md)、[C++ 抽象的成本速查表](../ch06-cpp-abstraction-cost/06-04-abstraction-cost-cheatsheet.md)

三道小题。①打印 `sizeof(std::function<int(int)>)`。②调用成本三连：1 亿次「noinline 函数指针」、1 亿次「直接调用一个 lambda」、1 亿次「通过 `std::function` 调用同一个函数」——**关键坑（本卷参考答案作者实测翻过两次车）**：累加结果必须放进**互相独立的局部变量、全部循环结束后一次性喂给 volatile**。否则会出现两种假象：a) 直接对 `volatile long long` 累加，比值会被什么压平、压到多少？b) 用同一个累加器、每个循环之间清零，哪几个循环会被整段优化掉、测出 0.00 ns？贴出三个数（修正坑之后）和 function 相对 lambda 的倍数，解释类型擦除为什么贵（相对内联的 lambda）。③构造成本：把 100 万个 `std::function` 分别从「捕获 1 个 int 的 lambda」和「捕获 5 个 int 的 lambda」构造出来（**装进 vector 让它们逃逸**，否则小捕获版会被整段折叠——实测过，折叠成 0.00 ns），贴出两种构造的单次成本与比值。5 个 int 为什么触发堆分配（结合你的 `sizeof` 和 libstdc++ 的 SBO 缓冲大小说）？热路径上遇到「反复构造 function + 大捕获」，有哪三个对策？

[参考答案 →](02-homework-solutions.md#hw-6-6-b)

## 6.7 编译器优化边界与体积评估

### 6.7-A {#hw-6-7-a}

难度 **L2** · 涉及[-O 级别与 optimization blockers:编译器能做什么、做不了什么](../ch07-compiler-and-size/07-01-opt-levels-and-blockers.md)、[体积优化:-Os、--gc-sections 与模板膨胀控制](../ch07-compiler-and-size/07-04-size-optimization.md)

写一个带 `volatile float scale` 的循环函数（400 万次 `acc += a[i]*b[i]*scale`），外加**两个从不被调用的** `static` 死函数（`dead_fn1`、`dead_fn2`，注意 `-Wall` 会警告它们 unused——这就是「死代码」的现场）。①分别用 g++ 的 `-O0/-O2/-O3` 编译跑，贴出三次耗时——本机 `-O0→-O2` 几倍？`-O3` 一定比 `-O2` 快吗（如实回答）？再用 clang++ 同样三档跑一遍，两个编译器的 `-O2` 谁快？②`size` 命令看五个二进制的 **text 段**（别用 `ls -l`，说清为什么）：`-O0`、`-O2`、`-O3`、`-O2 -ffunction-sections -fdata-sections`（链接不带 `--gc-sections`）、`-O2 -ffunction-sections -fdata-sections -Wl,--gc-sections`。`--gc-sections` 回收掉了多少字节、那是什么代码？③「体积为什么会影响性能」——说出 icache 这条路径，以及 `--gc-sections` 为什么是「release 免费午餐」。

[参考答案 →](02-homework-solutions.md#hw-6-7-a)

### 6.7-B {#hw-6-7-b}

难度 **L4** · 涉及[LTO、ThinLTO 与 PGO 的工程接入](../ch07-compiler-and-size/07-02-lto-pgo.md)、[-O 级别与 optimization blockers](../ch07-compiler-and-size/07-01-opt-levels-and-blockers.md)

两个跨翻译单元的实验。①**LTO 常量传播**：`config.cpp` 里 `float scale_factor() { return 3.0f; }`（假装从配置读），`main.cpp` 里 800 万次 `s += a[i] * scale_factor()`，10 轮取最快。分别用 `-O2` 与 `-O2 -flto` 编译（两个文件一起），贴出两个耗时。用 `nm` 证明 LTO 后 `scale_factor` 符号消失了，用 `-fopt-info-vec` 证明 LTO 版的循环被向量化了——无 LTO 版为什么向量化不了（说清「跨 TU 调用是 optimization blocker」）？②**`__restrict` 干净对照**：两个 `noinline` 函数 `sa_alias(float* a, float* b, float* scale, int n)` 和 `sa_restrict(float* __restrict a, float* __restrict b, ...)`（**两版除了 `__restrict` 一字不差**，不要像教材 ch07-01 警告框里那个教学 demo 一样混入其它变量），**循环体统一是写内存的 `a[i] += b[i] * *scale`**（教材 ch07-01 的 scale_add 形；别写成纯 load 归约 `acc += a[i] * b[i] * *scale`——实测那两版都会直接向量化、不产生别名版本化），`-O3` 编译各测 30 轮。如实贴出比值——如果 ~1.0x，用 `-fopt-info-vec` 的诊断解释为什么（编译器对别名版做了什么？你**预期看到**别名版报 `loop versioned for vectorization because of possible aliasing` 而 restrict 版不报，这句是什么意思），并说出 `__restrict` 真正值钱的场景是什么、撒谎的代价是什么。

[参考答案 →](02-homework-solutions.md#hw-6-7-b)

## 6.C 跨章综合与挑战

### 6.C-1 {#hw-6-c-1}

难度 **L3** · 涉及[后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)、[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)、[怎么写一个可信的 microbenchmark](../ch01-benchmark-methodology/02-credible-microbenchmark.md)

综合题：一个订单结构 `struct Order { int id; double price; int quant; char name[32]; char notes[64]; }`（打印 `sizeof` 确认），100 万个订单，热循环只算 `Σ price*quant`。做三种布局的对比：A = `vector<Order>`（120 字节/订单，只碰其中 12 字节）；B = SoA（`vector<double> price; vector<int> quant;` 两个独立数组）；C = 冷热分离（`struct Hot { double price; int quant; }` 独立数组，冷字段按 id 另存）。三版各测 15 轮取最快，验证三版结果一致。①贴出你的三个耗时与比值——「只碰 12 字节却按 120 字节搬数据」的带宽浪费在哪一级缓存体现得最狠？②B 和 C 为什么差不多快（它们各自的流量是多少字节/订单）？③工程权衡：C 相比 B 保留了「按订单对象思考」的哪些好处、付出了什么？什么时候你会选 B 而不是 C？④如果热循环改成「按 name 前缀筛选订单」呢——B 还成立吗？为什么「按怎么被访问组织数据」这句话里，「怎么被访问」是关键。

[参考答案 →](02-homework-solutions.md#hw-6-c-1)

### 6.C-2 {#hw-6-c-2}

难度 **L4** · 涉及[归因实战:从一个慢程序到定位瓶颈](../ch03-attribution-methodology/03-04-walkthrough.md)、[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)、[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)

综合题：粒子物理里的「加权点积」`Σ w[i]*x[i]*y[i]`，`struct Particle { float w, x, y, pad; }`（16 字节，`pad` 不用），N=400 万。①按 ch03-04 的漏斗走一遍：Roofline 手算这个内核的算术强度（写出运算数和字节数），它落在斜线还是水平线、优化方向是什么？②实测 AoS 版（`vector<Particle>`）与 SoA 版（三个独立数组）各 15 轮，贴出耗时与加速比，验证结果一致。③你的实测加速比和「pad 白占 1/4 带宽」的理论预期（1.33×）差多少？差出来的部分还来自哪些机制（提示：向量化、预取）？④「瓶颈会迁移」：如果这个程序还有第二个函数——一个对同一份数据的分支密集过滤——你把 AoS→SoA 修好之后，下一步该重测什么、预期看到什么？⑤把这个内核的 SoA 版再手写 4 累加器跑一遍（复用 6.4-B 的手艺），还有没有收益？如实报告，并解释「带宽受限的内核再提 ILP 为什么经常是空转」（结合 6.3-A 的 Roofline 结论）。

[参考答案 →](02-homework-solutions.md#hw-6-c-2)

### 6.C-3 {#hw-6-c-3}

难度 **L5** · 涉及[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)、[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)、[循环与计算优化](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)

挑战题（改编自 CSAPP 第 6 章 *The Memory Hierarchy* 的 cache blocking 练习（6.45–6.49 系列），按竞赛挑战强化——矩阵乘的三种写法的缓存行为差距随矩阵尺寸增长而放大；早期阶段 L5＝「用该卷知识可解的最难问题」，档位口径见[练习总览](index.md)）。矩阵乘 `C += A×B`（`double`，固定种子初始化）。实现三个版本：①`ijk` 朴素（k 在最内层）；②`ikj`（交换 j/k 循环次序）；③`ikj + 分块`（块大小 bs=64，`i0/k0/j0` 三层块循环）。在 n=512 上各跑一遍，打印耗时、GFLOPS 和**三个版本的 C 两两最大差**（应为 0——为什么？）。①贴出三个数——ijk 为什么最慢（说清 B 的访问模式：`B[k*N+j]` 在内层循环里怎么走、为什么每一步都是 cache miss）？②再把三个版本放到 n=1024 上跑（ijk 会慢到让你怀疑人生——给它设个 `timeout 60`，如实记录它有没有跑完），贴出三个数。n 从 512 到 1024，ijk 与分块版的差距从几倍扩大到几倍？解释这个「放大」（工作集和 L3 的关系——你的 L3 多大，从 6.2-A 的探针推断）。③块大小为什么不能太大也不能太小：bs=64 的块里，一个 $bs×bs$ 的 B 子块占多少字节？要「装进 L2/L3」这个约束怎么翻译成对 bs 的上限？④为什么矩阵乘的算术强度远高于点积——它落在 Roofline 的哪条线上？这跟②里「分块版 GFLOPS 随 n 变大反而上升（或至少不掉）」有什么联系？

[参考答案 →](02-homework-solutions.md#hw-6-c-3)
