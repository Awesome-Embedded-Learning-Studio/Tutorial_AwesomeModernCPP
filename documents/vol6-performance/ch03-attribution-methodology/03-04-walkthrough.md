---
chapter: 3
cpp_standard:
- 17
description: 把 ch03 前三篇的 USE / Roofline / TMAM / 火焰图串成一个完整工作流。用一个加权点积案例(工作集卡在 L3 边界、带宽受限),演示一步步用四套工具定位到「它是
  Backend Memory Bound、贴着 DRAM 带宽斜线,优化点是 AoS→SoA 省 pad 流量」,并强调瓶颈迁移的迭代性质
difficulty: advanced
order: 4
platform: host
prerequisites:
- USE 方法与 Roofline 模型
- TMAM 四桶与硬件采样
- 火焰图、perf 工作流与 COZ / eBPF
reading_time_minutes: 8
related:
- 后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch
- 循环与计算优化:code motion、展开与多累加器
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: 归因实战:从一个慢程序到定位瓶颈
---
# 归因实战:从一个慢程序到定位瓶颈

## 四套工具怎么串起来

ch03 前三篇我们学了四套工具:USE(看系统全局)、Roofline(判算力 vs 带宽)、TMAM(归到流水线哪段)、火焰图 + COZ(落到代码)。但真上手时,你不会四套全跑一遍——那太慢。它们的正确关系是一条**漏斗**,从粗到细逐层过滤:

```text
慢程序
  │
  ├─ 1. USE 扫系统:是不是根本不在 CPU?(内存换页?磁盘满?网络?)
  │     └─ 排除系统性问题后,确认瓶颈在 CPU 计算
  │
  ├─ 2. Roofline 定性:算力受限 还是 带宽受限?(决定优化大方向)
  │
  ├─ 3. TMAM 四桶:瓶颈在流水线哪段?(Frontend/Backend Memory/Backend Core/Bad Spec)
  │     └─ 下钻到 cache 层级,用精确事件定位到汇编指令
  │
  └─ 4. 火焰图:这段慢的代码,在哪个函数?(确认改哪行)
        └─ COZ 补充:改这个函数整体收益多大?(排优化优先级)
```

这一篇我们走一遍完整流程。先讲清楚:**我没有在写文章这台 WSL2 机器上跑 perf/toplev**(本机没装 perf)。所以下面涉及的 profiler 输出,是从 ch02 已实测的数据(那些是本机真跑的)+ Bakhvalov/easyperf 的标准案例**推演**出来的「如果跑了会看到什么」。能本机实测的部分(算术强度、原始耗时、cache 行为)我都标了「实测」;profiler 的具体输出标了「推演/引自」,绝不假装跑过。

## 场景:一个慢得离谱的点积

假设你写了个粒子物理模拟,核心是一个「加权点积」:对 N 个粒子,算 `result = Σ w[i] * x[i] * y[i]`。N = 100 万(每粒子 16 字节,工作集 16 MB,正好卡在本机 L3 容量边界),跑出来单次约 0.9 ms(本机实测量级)。你想知道它有没有优化空间、卡在哪——**用归因工作流查一遍**。

代码长这样(简化):

```cpp
struct Particle { float w, x, y, pad; };   // AoS:每粒子 16 字节
float weighted_dot(const std::vector<Particle>& ps) {
    float acc = 0.0f;
    for (size_t i = 0; i < ps.size(); ++i)
        acc += ps[i].w * ps[i].x * ps[i].y;   // 每次访问三个字段
    return acc;
}
```

## 第 1 步:USE 扫系统——先确认是 CPU 问题

动手前先花两分钟扫一眼系统,排除「问题根本不在 CPU」:

```bash
vmstat 1     # 看 %us+%sy(用户+系统 CPU)、r 列(运行队列)、si/so(换页)
free -m      # 看内存有没有吃满到换页
iostat -xz 1 # 看磁盘(这程序不读盘,应该全空)
```

如果你看到 `si/so > 0`(在换页),那「慢」可能根本是内存不够换页造成的,跟你的算法无关——先加内存。如果 CPU 利用率打满一个核但其它资源空闲,确认瓶颈在 CPU 计算,**进入第 2 步**。

这一步看起来 trivial,但它能在 5 分钟内救你于「花一天优化算法,结果是磁盘满了」这种悲剧。USE 的价值就在这儿。

## 第 2 步:Roofline 定性——算力还是带宽?

现在确认是 CPU 计算瓶颈,但还要分清:是算不过来,还是数据喂不上?算一下算术强度(这个本机可算,无需 profiler):

每次循环迭代:

- 运算:2 次乘 + 1 次乘 + 1 次加 = 算 3 次浮点运算(严格说 `w*x*y+acc` 是 2 乘 1 加 = 3 FLOP)。
- 访存:读 `w`、`x`、`y` 三个 float = 12 字节(`pad` 字段也被同一 cacheline 带进来,但不算「有用流量」,先按有用算)。
- **算术强度 ≈ 3 FLOP / 12 B = 0.25 FLOP/byte**。

参考 03-01:5800H 这类芯片的脊点 AI 在 ~20 FLOP/byte 量级(8 核 AVX2 FMA 峰值 ~900 GFLOPS / ~40 GB/s)。0.25 **远低于脊点**——所以这个内核**铁定是内存带宽受限**,贴着带宽斜线跑。优化方向立刻明确:**减访存,别去抠 SIMD 通道**(就算 SIMD 满载,带宽瓶颈不变,等于空转)。

这一步只花了一支笔的功夫,但已经帮你排除了「加 SIMD」这条至少半天工作量的错路。Roofline 的杠杆就在这里。

## 第 3 步:TMAM 下钻——是哪一级 cache miss?

Roofline 说了「带宽受限」,但带宽卡在哪一级?L2 命中但 L3 miss?还是连 L3 都打穿了到 DRAM?这就得 `toplev` 了(以下输出为按本机 cache 参数 + Bakhvalov 案例推演):

```bash
toplev -l1 -- ./weighted_dot
#   Frontend_Bound:       8.0%
#   Backend_Bound:       62.0%   ← 主桶,符合 Roofline 的「带宽受限」
#   Bad_Speculation:      5.0%
#   Retiring:            25.0%

toplev -l3 -- ./weighted_dot
#   Backend_Bound.Memory_Bound.L3_Bound.DRAM_Bound: 45%  ← 连 L3 都打穿到 DRAM
```

「DRAM Bound」告诉我们:数据没命中 L3(16 MB)、去了主存。但要注意:**加权点积是流式单遍扫描**(每元素只访问一次、零时间复用),所以**这里没有 L3 容量颠簸**(颠簸要被反复重访的元素互相驱逐才会发生,流式扫描没有重访)。工作集 16 MB 卡在/略超 L3 边界,真因是它**贴着 DRAM 带宽斜线跑**——参考 ch02-01 的 memory mountain,工作集超过 L3 后吞吐落在带宽斜线低端。**根因是带宽受限**(不是容量颠簸)。

下钻到汇编,用精确事件确认是哪条 load 在 miss:

```bash
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./weighted_dot
perf annotate
# 高亮显示:循环里那三条读 ps[i].w/x/y 的 movss 是 miss 重灾区
```

定位完成:瓶颈 = Backend Memory Bound,DRAM 级 miss,制造 miss 的是循环里的字段 load。**现在可以动手改了**。

## 第 4 步:火焰图确认 + 改

火焰图在这个例子里其实不关键(因为就一个循环),但它在大程序里能告诉你「这 45% 的 DRAM miss 是分布在 5 个函数里、还是全在 `weighted_dot` 一个函数里」——如果分散,你得逐个改;如果集中,改一个就行。这里假设火焰图确认全在 `weighted_dot`,集中。

怎么改?带宽受限场景下,关键是**降低无用流量**。问题在 **AoS 布局**:`Particle{w,x,y,(pad)}` 把三个要用的字段和用不到的 `pad` 挤一起,每次循环沿数组扫,`pad` 白白占 1/4 带宽。改成 **SoA**(详细机制见 ch04-01),三个字段各自连续、不再带 `pad`:

```cpp
struct Particles {
    std::vector<float> w, x, y;   // 三个数组分开
};
float weighted_dot(const Particles& ps) {
    float acc = 0.0f;
    for (size_t i = 0; i < ps.w.size(); ++i)
        acc += ps.w[i] * ps.x[i] * ps.y[i];
    return acc;
}
```

改完**先回第 2 步重测**(迭代!),发现单次降到 ~0.7 ms(本机实测量级,SoA 省掉了 pad 的 25% 流量)。再看 toplev,Backend Memory 占比明显下降,Retiring 上升——好多了。

## 别高兴太早:瓶颈会迁移

这是 TMAM 最坑新手的一点。你 Backend Memory 修到 20%,**重测总时间**,可能发现只快了一点点——因为现在 **Bad Speculation 升上来了**(原来被内存瓶颈掩盖)。或者 Retiring 上去了,但 **Frontend Bound** 因为代码布局变差而抬头。每次修完都要**回第 2 步重新看四桶**,处理新的最大桶,直到 Retiring 满意或剩下的桶都小到不值得。

这一条「瓶颈迁移」决定了归因是**迭代**的,不是「跑一次 profiler 改一次」。这也是为什么 ch01 反复说「优化前后都同一套方法论测一遍」——你以为修好了,可能只是把短板挪了个位置。

## 收尾:COZ 排优先级

如果这个程序还有别的函数(不止 `weighted_dot`),你怎么决定**先改哪个**?火焰图按耗时排序,但「耗时最多的函数」未必是「优化收益最大的函数」。这时 COZ 能帮你:它对每个函数画「虚拟提速 → 整体提速」曲线,斜率最陡的就是最值得优化的。在你有多个候选瓶颈、预算有限时,COZ 给的优先级比火焰图更可靠。

## 小结:这套工作流的价值

走完一遍你会发现,归因的核心不是「某个工具多神」,而是**按漏斗一步步缩小战场**:

1. USE(2 分钟)排除系统性问题,确认是 CPU。
2. Roofline(一支笔)判算力 vs 带宽,定大方向。
3. TMAM(几次 toplev)下钻到流水线段 + cache 层级,精确事件定位到指令。
4. 火焰图确认函数,COZ 排优先级。
5. 改,然后**迭代**——瓶颈会迁移。

**每一步都比它前一步贵(花的时间、需要的工具、侵入性都递增),但每一步都缩小了下一步的战场。** 跳过前几步直接上火焰图,你会在一个 50 函数的程序里迷失;跳过 Roofline 直接改 SIMD,你可能改错方向。这套「由粗到细」的纪律,是 ch04 按部位优化能对症下药的前提。

到这一篇,ch03 归因方法论就讲完了。从下一篇开始,我们正式进入 **ch04 按瓶颈部位优化**——对着 Backend Memory / Backend Core / Bad Speculation / Frontend 四个桶,逐个讲怎么治。

## 参考资源

- ch03-01 USE 方法与 Roofline 模型(本卷)
- ch03-02 TMAM 四桶与硬件采样(本卷)
- ch03-03 火焰图、perf 工作流与 COZ / eBPF(本卷)
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》——完整案例走查在 ch6–11(TMAM/CPU features/Cache/Memory/Core/Frontend/多线程,全书共 11 章)
- ch02-01 存储层次延迟阶梯(本卷,memory mountain 工作集跨 L3 边界的吞吐实测出处)
