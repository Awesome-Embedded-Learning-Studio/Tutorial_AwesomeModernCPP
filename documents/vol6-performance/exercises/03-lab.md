---
title: "卷六 Lab：缓存与分支探针台"
description: "卷六动手实验：把 ch00 的 sanitizer 地基、ch01 的测量纪律、ch02 的存储层次、ch04 的布局优化、ch05 的伪共享与 ch04-06 的分支预测拧成一条探针线——六步从「给带 UB 的基准做体检」一路做到「alignas(64) 拆掉伪共享」，最后附一道 L5 挑战（排序 vs 打乱分支预测完整实验，改编自 Stack Overflow 传奇问题与 Bakhvalov 第 3 章）。"
chapter: 6
order: 3
tags:
  - host
  - intermediate
  - cpp-modern
  - 优化
  - 内存管理
  - 调试
difficulty: intermediate
platform: host
reading_time_minutes: 9
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷六 Lab：缓存与分支探针台

## 实验目标

本卷的知识点像一台精密仪器：sanitizer 是校准源、测量方法论是读数规程、存储层次和分支预测是仪器内部的两个核心机构。这个 Lab 把它们拧成一条探针线——你不是「读一遍概念」，而是拿着这台仪器，把「带 UB 的基准为什么不可信」「vector 和 set 到底差几倍」「本机 L1/L2/L3/DRAM 各多少纳秒」「64 字节缓存行能不能用行为测出来」「布局改写凭什么值 3 倍」「伪共享把并行拖慢几倍」「分支预测的惩罚到底多重」这些问题一个个**量出来、跑出来**。

六步难度 L1→L4 递进，外加一道 L5 挑战。所有实验在 `/tmp` 下独立目录做，每步有验收标准；卡住先回每步标注的章节链接读教材，再不行看[实验参考](04-lab-solutions.md)。

> 编译约定：普通构建 `g++ -O2 -std=c++17 -Wall -Wextra`（多线程加 `-pthread`）；涉及 UB 的步骤过 UBSan。**本机是 WSL2 虚拟机**（参考答案环境：Intel i7-14650HX，24 线程），噪声比裸机大，所有计时多跑几轮取中位数/最快；教材的数字在 AMD 5800H 上测得，绝对数对不上正常，**秒数会抖、量级趋势稳定**——对趋势就对答案。

## 步骤 1：给「看起来很快」的基准做体检 {#lab-1}

难度 **L1** · 涉及[从「先正确」到「再快」:为什么 sanitizer 是性能卷的地基](../ch00-performance-mindset/02-from-correctness-to-performance.md)、[Sanitizer 工具链全景](../ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md)

**目标**：写一个「很快」的 `fast_abs` 基准，然后用 sanitizer 戳穿它藏在速度底下的 UB。

1. 写 `__attribute__((noinline)) int fast_abs(int x) { return x < 0 ? -x : x; }`，循环 1 亿次对一个「大部分是负数」的数组求绝对值和并计时；**在数组末尾埋一颗雷**：`arr[N-1] = INT_MIN`。
2. 普通构建跑一遍：贴出耗时、总和，以及 `fast_abs(INT_MIN)` 的值——它「看起来」正常吗？
3. 用 `-fsanitize=undefined` 重编重跑：贴出 UBSan 报告（精确到哪一行），并观察 UBSan 构建的耗时变成了几倍。

**验收标准**：贴出两份输出；一句话说清「这个基准的数字为什么不可信」——不是因为它慢，是因为它**在 INT_MIN 上算错了还继续跑**，带 UB 的性能数字建立在一个错误的结果上。

[实验参考 →](04-lab-solutions.md#lab-1)

## 步骤 2：vector vs set——换一种查询模式 {#lab-2}

难度 **L2** · 涉及[性能思维:efficiency 与 performance 不是一回事](../ch00-performance-mindset/01-efficiency-vs-performance.md)、[Benchmark 方法论参考卡](../ch01-benchmark-methodology/06-methodology-reference.md)

**目标**：复现教材 ch00-01 的 `vector`+二分 vs `set` 查找曲线，但把查询模式换成**全未命中**（教材是全命中）——看曲线怎么变。

1. 照教材的结构写基准（200 万次查询、5 轮取中位数、`volatile` sink 防 DCE），key 取偶数（稀疏），但**查询值全部取「不存在的奇数」**（`key + 1`）。
2. 扫 N = 1024 / 4096 / 16384 / 65536 / 262144 / 1048576，贴出每个 N 的 `vector(ns/q)`、`set(ns/q)` 和比值。

**验收标准**：贴出曲线；回答两个问题：①和教材「全命中」曲线相比，`set` 在**哪个 N 开始**被 `vector` 甩开（更早还是更晚）？为什么「找不到」对两边的影响不一样？②两条曲线同样是 $O(\log n)$，比值却随 N 拉大——这一步的机制和缓存行的什么性质有关？

[实验参考 →](04-lab-solutions.md#lab-2)

## 步骤 3：亲手画出本机延迟阶梯 {#lab-3}

难度 **L2** · 涉及[存储层次与延迟阶梯:为什么顺序访问快 100 倍](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)

**目标**：用指针追逐把本机 L1/L2/L3/DRAM 四级延迟一次扫出来——这就是教材 ch02-01 那张延迟阶梯在你机器上的复刻。

1. 写指针追逐探针（置乱单环 + `idx = nxt[idx]` 真依赖），工作集从 4 KB 扫到 128 MB，共 16 档（每档 2 的幂递增）。
2. 每档先热身整条链，再计步；贴出 `工作集 | ns/访问` 的表。
3. 在表上标出四级边界（结合你的 CPU 型号查 L1/L2/L3 大小，或直接从曲线的平台/拐点推断）。

**验收标准**：贴出完整表；说出你的 L1 和 DRAM 之间差多少倍、每级大致落在哪个工作集区间；说明为什么「探针测出来的才是裸访存延迟」（预取器为什么帮不上）。

[实验参考 →](04-lab-solutions.md#lab-3)

## 步骤 4：用行为测出 64 字节缓存行 {#lab-4}

难度 **L3** · 涉及[缓存行与局部性:64 字节的最小搬运单位](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)

**目标**：不查任何文档，用「步长扫描」这个行为实验定位缓存行大小。

1. 写步长扫描：工作集固定 2 MB（512K 个 `int`，落在 L3），stride 取 4 / 8 / 16 / 32 / 48 / 56 / 64 / 72 / 96 / 128 / 256 / 512 字节，每个 stride 走 6400 万次，打印 M 访问/秒。
2. 用 `getconf LEVEL1_DCACHE_LINESIZE` 查出真实值，对照你的曲线：断崖（如果存在）出现在哪个 stride？
3. **如实报告**：参考答案作者的机器上（i7-14650HX）这个断崖比教材 5800H 上的**平缓得多**——预取器更激进。贴出你的曲线原样，指出哪些点「不合常理」（可能有离群值或预取器凸点），并说出教材 ch02-02 对这类凸点的纪律是什么（怀疑预取器 ≠ 证明预取器）。

**验收标准**：贴出曲线与 `getconf` 输出；一句话说清「64B 以下同行的多次访问被摊薄、64B 以上每次踩新行」这个机制在你的曲线上是怎么体现的（哪怕只是趋势）。

[实验参考 →](04-lab-solutions.md#lab-4)

## 步骤 5：AoS → SoA 与 Roofline 手算 {#lab-5}

难度 **L4** · 涉及[后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)、[USE 方法与 Roofline 模型:先看全局,再判算力还是带宽](../ch03-attribution-methodology/03-01-use-and-roofline.md)

**目标**：粒子位置更新——一个「只更新部分字段」的经典场景，先手算再实测。

1. `struct Particle { float x, y, z, vx, vy, vz; }`（24 字节/粒子，100 万个），热循环只做 `x += vx * dt`（只碰 2 个字段、8 字节）。先做 Roofline 手算：这个更新的算术强度是多少（写出运算数与字节数）？它落在斜线还是水平线、优化方向是什么？
2. 写 AoS 版和 SoA 版（六个独立数组），各跑 20 轮取最快，贴出耗时与加速比；验证两版更新后的 x 数组**逐位一致**。
3. 你的实测加速比和「流量比 24/8 = 3」这个理论预期差多少？差出来的部分可能来自哪些机制？

**验收标准**：贴出手算过程、两个耗时与加速比；说出为什么「只碰 8 字节却按 24 字节搬数据」是这道题的题眼。

[实验参考 →](04-lab-solutions.md#lab-5)

## 步骤 6：伪共享——对齐之前的惨状 {#lab-6}

难度 **L4** · 涉及[伪共享:同一缓存行把多核拖回单核](../ch05-multicore-performance/05-01-false-sharing.md)、[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)

**目标**：两个线程各自自增「自己的」计数器，一次挤在同一条缓存行、一次用 `alignas(64)` 分开——量出那条看不见的缓存行收的税。

1. 写 `BadCounters`（两个 `atomic<long>` 挨着，16 字节）和 `GoodCounters`（每个 `alignas(64)` 独占一行，128 字节），打印两个 `sizeof`。
2. 两个线程各对自己的计数器 `fetch_add(1, relaxed)` 5000 万次，两种布局各跑 3 轮，贴出每轮耗时与比值。

**验收标准**：贴出 3 轮数据；解释 ①两个逻辑上无关的原子计数器为什么会互相拖慢（MESI 协议的哪一步）；②`alignas(64)` 为什么能根治；③**如实报告**你的倍数——教材机器是 15~48×，参考答案作者的机器只有约 4×，试着从「lock 前缀指令延迟 vs RFO 往返增量」的相对比例解释为什么倍数会随机器差这么多，以及什么结论跨机器不变。

[实验参考 →](04-lab-solutions.md#lab-6)

## 附加挑战（L5）：排序 vs 打乱——把分支预测罚金量到底 {#lab-l5}

难度 **L5** · 涉及[流水线、ILP 与分支预测](../ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md)、[分支:branchless、predication 与「别盲目无分支」](../ch04-tuning-by-bottleneck/04-06-branch-branchless.md)

**目标**：完整复现并超越「为什么处理排序数组更快」这个传奇问题（改编自 Stack Overflow 的 muffinista / Mysticial 经典问答与 Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 3 章；早期阶段 L5＝「用该卷知识可解的最难问题」，档位口径见[练习总览](index.md)）。32768 个 `uint8_t`，一半 ≥ 128；`sum_gt128` 对 `d[i] >= 128` 的元素累加。准备「打乱」和「排序」两份数据。

1. **先按默认 `-O2` 编译跑**：分支版在打乱/排序上的耗时比值是多少？如实记录——如果比值 ≈ 1.0，先别改，解释为什么「分支没了」（编译器做了什么），这正是教材 ch04-06 的诚实结论。
2. 再用 `-O2 -fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion` 重编（三个旗标分别挡什么？），跑分支版打乱/排序的比值——这次真分支暴露出来，比值是多少？
3. 写一个**手写 branchless** 版本：`int t = (int)d[i] - 128; uint64_t m = ~(uint64_t)((int64_t)t >> 63); s += d[i] & m;`——解释这个掩码怎么做到「≥128 保留、<128 归零」。在同一份（禁优化的）构建里跑它，打乱/排序比值是多少？它和「排序 + 可预测分支」哪个快？这验证了教材哪句话？
4. 三个版本的结果必须完全一致（用同一份数据对答案）；若不一致，**先修正确性再谈性能**——本卷铁律一。

**验收标准**：贴出两套构建下的完整表格；用「预测失败冲刷流水线」解释第 2 步的比值；用「cmov/掩码没有控制依赖」解释第 3 步；最后回答：为什么第 1 步（默认 -O2）的 1.0× 不是「分支预测免费」的证据，而是「你的 if 不一定是分支」的证据？

[实验参考 →](04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码、每步终端记录（`stepN.log`），以及 200 字以内的小结——用你自己的话说清「数据在硬件上怎么流」这个总命题，你在哪一步看得最真切。
