---
chapter: 3
cpp_standard:
- 17
description: TMAM(Top-Down Microarchitecture Analysis)把流水线 slot 分成 Retiring / Frontend
  Bound / Backend Bound / Bad Speculation 四个桶,告诉你瓶颈在流水线的哪一段;本篇讲四桶怎么分、toplev 工作流,以及支撑它的硬件采样机制
  LBR / PEBS / Intel PT 各自能拿到什么数据
difficulty: advanced
order: 2
platform: host
prerequisites:
- USE 方法与 Roofline 模型
- 流水线、ILP 与分支预测
reading_time_minutes: 7
related:
- 火焰图、perf 工作流与 COZ / eBPF
- 前端优化:代码布局、PGO、BOLT
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: TMAM 四桶与硬件采样:LBR / PEBS / Intel PT
---
# TMAM 四桶与硬件采样:LBR / PEBS / Intel PT

## 把瓶颈归到流水线的哪一段

上一篇的 Roofline 能区分「算力受限」还是「带宽受限」,但「算力」和「带宽」都还是太粗——CPU 流水线有取指、译码、执行、访存、分支预测好多段,「算力不够」到底是译码跟不上(前端),还是执行单元等数据(后端),还是分支预测老错白白冲刷(投错机)?这三种情况的对策天差地别。

**TMAM(Top-Down Microarchitecture Analysis Method)** 就是 Intel 提出来回答这个问题的框架(Yasin, *A Top-Down Method for Performance Analysis and Tuning*, 2014)。它把流水线每周期能进入的 slot,**按它们最终的命运**分成四个桶。看哪个桶占比异常高,就知道瓶颈在流水线的哪一段。这个框架后来被 Andi Kleen 的 `pmu-tools/toplev` 工具实现成一键命令,是现代 CPU 性能分析的主力。

## 四个桶:slot 的四种命运

CPU 前端每周期会尝试「分配」一定数量的 slot(slot 数 = 流水线宽度)。这些 slot 最后只有四种归宿:

| 桶 | 含义 | 占比高说明 | 典型对策(对应 ch04)|
|---|---|---|---|
| **Retiring** | slot 真的退休成了一条有效指令 | **越高越好**(理想) | 结构良好,继续 |
| **Frontend Bound** | slot 因为前端(取指/译码)跟不上而空转 | icache miss / iTLB miss / 代码膨胀 | 代码布局、PGO、BOLT(ch04-07)|
| **Backend Bound** | slot 卡在后端——执行单元在**等数据**(memory)或**等端口**(core) | cache miss / 数据依赖 / 执行端口冲突 | cache 优化、SIMD、打破依赖链(ch04-01/02/03)|
| **Bad Speculation** | slot 浪费在**投机的错误路径**上(分支预测失败被冲刷) | 不可预测分支 | branchless、predication(ch04-06)|

读这四桶的姿势:**Retiring 是好桶,其余三个是坏桶,谁的占比异常高谁就是当前瓶颈**。一个调得好的数值计算内核,Retiring 能到 50%–70%(SIMD 满载);如果它只有 20% 而 Backend Memory 占 60%,那你该去治 cache,不是加 SIMD。

Backend Bound 还能再往下分两支——**Backend Memory Bound**(等数据:cache miss、带宽)和 **Backend Core Bound**(等执行端口:除法、长延迟依赖链)。这个细分极其有用,它直接对应 ch04 里「治内存」还是「治计算」两条路。

> 边界提醒:TMAM 四桶是**归因框架**,讲「瓶颈落在哪类」;具体怎么改(向量化、减访存、branchless)是 ch04 的事。别在归因章里展开优化细节,那是 ch04 的主场。

## toplev 工作流:一层层下钻

`toplev`(`pmu-tools` 里的 Python 工具,包装了 Intel TMAM 的性能计数器)的工作流是**逐层下钻**,典型三步:

```bash
# 1. 先看 L1 四桶,定主战场
toplev -l1 -- ./app
# 输出示例(引自 easyperf.net 示例,非本机):
#   Frontend_Bound:      12.5%   ← 正常
#   Backend_Bound:       58.0%   ← 主桶!
#   Bad_Speculation:      8.3%
#   Retiring:            21.2%

# 2. Backend 是主桶,下钻 L2 看是 Memory 还是 Core
toplev -l2 -- ./app
#   Backend_Bound.Core_Bound:   15.0%
#   Backend_Bound.Memory_Bound: 43.0%   ← 内存受限

# 3. Memory Bound 再下钻 L3,看卡在哪一级 cache / DRAM
toplev -l3 -- ./app
#   ... L3_Bound.DRAM_Bound: 38%   ← 大概率 cache miss 打到 DRAM
```

下钻到 L3,你就知道「瓶颈是 L3 miss 打到 DRAM」这种粒度。**但这还不够**——你还得知道**是哪条指令在 miss**,才能动手改。这就要靠**带精确地址的硬件采样事件**。

## 硬件采样三件套:LBR / PEBS / Intel PT

`toplev` 告诉你「卡在哪一级」,但要落到「哪条汇编指令」,得靠 CPU 的硬件采样机制。现代 Intel/AMD CPU 有三套机制,各自能拿到不同粒度的数据:

**LBR(Last Branch Record)**:CPU 维护一个环形 buffer,记录最近几十到几百条**分支跳转**(from/to 地址对)。LBR 的强项是抓**控制流**——分支预测失败在哪、调用栈(用 LBR 重建栈,不需要 frame pointer)、热循环。代价:只记录分支跳转,不记录普通访存。

**PEBS(Precise Event-Based Sampling)**:`perf` 的「带 `:pp` 后缀」事件就靠它。普通采样是基于**指令指针**的,有 skid(事件发生后,中断交付前 CPU 还会跑几条指令,采样点「滑」过去,定位不准);PEBS 让 CPU 在事件发生时**精确地**把当时的寄存器状态(包括精确指令地址)存进 PEBS buffer,几乎无 skid。这对定位「哪条 load 指令 cache miss 了」是刚需:

```bash
# 用带 :ppp(精确 IP)后缀的事件定位 cache miss 的具体指令
perf record -e MEM_LOAD_RETIRED.L3_MISS:ppp -- ./app
perf report   # 或 perf annotate 看汇编级命中
```

> 这就是 ch01-03「测量陷阱」表里第 16 条「PEBS skid」的反面——用 `:ppp` 精确事件才能准确定位;用普通事件会滑几条指令,改错了函数。

**Intel PT(Processor Trace)**:更猛——它**连续记录**完整的控制流(每个分支的方向),可以**完整重建执行轨迹**(虽然不记数据值)。代价是产生大量数据(buffer 占内存)、解析需要专门工具(`perf script`、`libipt`)。Intel PT 用于「我要精确知道这次运行走了哪条路径、每个分支怎么转的」这类深度分析,日常 profiling 用 PEBS 就够。

AMD 这边对应机制名字不同(AMD 用 IBS — Instruction-Based Sampling,和 PEBS 思路类似但实现不同;分支记录叫 LBR 也有对应)。**框架(TMAM 四桶)是通用的,底层事件名随厂商变**,这是跨平台调优要注意的——在一台 Intel 上调好的 perf 命令,换到 AMD 上事件名得改。`perf list` 能列出本机可用事件。

## 瓶颈会迁移:迭代,不是一次性

TMAM 工作流有一个**极其重要**的性质,新手容易栽在这里:**修好一个瓶颈,会露出下一个**。

假设你下钻发现 Backend Memory 60%(L3 miss 打到 DRAM),你花力气优化了 cache 布局,Backend Memory 降到 20%。你重测,以为大功告成——结果发现总性能只提升了一点点,因为现在 **Bad Speculation 升到 35%**(原来被内存瓶颈掩盖,内存一快,分支预测失败成了新瓶颈)。这就是 TMAM 的「瓶颈迁移」:**流水线的短板是会变的,你修好一块,下一块就成了新的短板**。

所以 TMAM 是**迭代**流程,不是一次性诊断:

1. `toplev -l1` 找当前最大桶 → 下钻定位 → 改 → 回到 1。
2. 每轮处理**当前最大**的桶,直到 Retiring 占比满意或剩下的桶都不大了。

这条「瓶颈会迁移」的性质,也是为什么 ch01 反复强调「优化前后都用同一套方法论测一遍」——你以为搞定的事,可能只是把瓶颈挪了个地方。

## 小结

- **TMAM 四桶**:Retiring(好)/ Frontend Bound / Backend Bound(Memory + Core)/ Bad Speculation,看哪桶占比高就是瓶颈所在。
- **toplev 工作流**:L1 定主桶 → L2/L3 下钻到 cache 层级 → 用精确事件(`:ppp`)定位到具体汇编指令 → 改 → 迭代。
- **硬件采样三件套**:LBR 抓控制流/栈、PEBS 抓精确访存事件(治 skid)、Intel PT 连续重建完整轨迹。
- **瓶颈会迁移**:每轮治当前最大桶,改完重测,直到 Retiring 满意。

TMAM 答「哪类瓶颈」,但还没答「哪段代码、哪个函数」。定位到代码段,要靠火焰图——下一篇。

## 参考资源

- Yasin, *A Top-Down Method for Performance Analysis and Tuning*(2014)——TMAM 原始论文
- Andi Kleen `pmu-tools`(`toplev`)——github.com/andikleen/pmu-tools,TMAM 的命令行实现
- easyperf.net *Top-Down performance analysis methodology*(2019-02-09)——toplev 工作流的图文教程
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 6 章 *CPU Features For Performance Analysis*——LBR / PEBS / Intel PT 的机制讲法
- Intel《Optimization Reference Manual》附录 B——TMAM 与性能计数器的官方定义
- `perf` 文档:`perf record` / `perf annotate` / `:pp` 精确事件后缀
