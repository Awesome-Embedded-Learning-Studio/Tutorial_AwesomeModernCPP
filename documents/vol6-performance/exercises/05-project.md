---
title: "卷六 Project：点积优化工作台 dotbench"
description: "卷六综合项目：做一个自包含的点积优化工作台——从「不骗人的基准框架」起步（中位数+cv+防 DCE+运行期数据），依次加入多累加器、sanitizer/警告/双编译器质量门，最后挑战手写 AVX2+FMA 并画出一条「带宽墙」缩放曲线。任务分四层，难度 L1~L5，所有数字按 ch01 方法论上报。"
chapter: 6
order: 5
tags:
  - host
  - advanced
  - cpp-modern
  - 优化
  - 测试
  - 工程实践
difficulty: advanced
platform: host
reading_time_minutes: 5
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷六 Project：点积优化工作台 dotbench

## 项目定位

把卷六的家当全部用进一个真实的小程序：`dotbench`——一个点积优化工作台。它自己就是一台「可信测量仪器」（中位数、cv、防 DCE、运行期数据、环境快照），然后用这台仪器去优化它测的内核——这本身就是本卷命题的闭环：**先有一把校准过的尺子，再谈哪个实现更快**。

点积（`Σ a[i]*b[i]`）是完美的试刀石：算术强度只有 0.25 FLOP/byte，是带宽受限家族的代言人；它的依赖链、向量化、带宽墙三个阶段正好对应本卷 ch04-02、ch04-05、ch03-01 的三层知识。做完你会对「先正确、再测量、按瓶颈优化」有肌肉记忆。

任务分四层，一层一层往上盖；卡住了看[参考实现](06-project-solutions.md)，它按层组织，可以只读你卡住的那层。

> 环境：g++ 16 / clang++ 22，C++17 起步；L5 需要 `-mavx2 -mfma`（本机支持 AVX2 即可）。所有性能数字按 ch01 方法论：**预热后多轮（≥5 轮）取中位数 + cv**，贴环境快照，并**如实报告**你的机器。

## 任务分层

### 核心任务（L2）：能跑起来的基准框架 {#pj-core}

**L1 热身**：先把正确性参照立起来——`ref_dot` 用 `std::inner_product` 实现，`dot_scalar` 写单累加器标量版；再加一个 `check`：打印 `dot_scalar` 与 `ref_dot` 的最大差。热身验收：对 N=1M 的随机数据，两个结果之差必须打印为 **0**（为什么是 0 而不是 1e-6？想想两者求和顺序）。

实现自包含的测量框架，不依赖任何外部库：

1. `do_not_optimize(float x)`——用一条空的 `asm volatile("" : : "g"(x) : "memory")` 把结果钉住，语义对齐 Google Benchmark 的 `DoNotOptimize`；
2. `bench(f, rounds)`——跑 `rounds` 轮（每轮计时 → 消费结果 → 记录耗时），返回**中位数**与 **cv**（变异系数）；
3. 数据必须**运行期生成**：`std::mt19937` 固定种子填 `a`、`b`（为什么固定种子？为什么不能写死成 constexpr 数组？）；
4. `env_snapshot()`——打印 CPU 型号（读 `/proc/cpuinfo` 的 `model name`）与 `hardware_concurrency`。

**验收标准**：`g++ -O2 -mavx2 -mfma -std=c++17 -Wall -Wextra` 编译**零警告**；对 N=4,000,000、rounds=9 跑一遍，贴出「正确性行 + 三行基准（先只有 dot_scalar 一行也行）+ 环境快照」。你的 dot_scalar 吞吐应落在什么量级（对比 6.3-A 的带宽墙）？

[参考实现 →](06-project-solutions.md#pj-core)

### 进阶任务（L3）：多累加器——以及一个诚实的结果 {#pj-adv}

加 `dot4`：4 个独立累加器把依赖链拆成四条。①打印 `dot4` 与 `ref_dot` 的最大差——这次**不是 0**，解释这行差值是谁的代价（说清标准里挡在编译器面前的那条规则）。②基准 dot_scalar vs dot4（N=1M 和 N=4M 各测一遍），贴出加速比与两个 cv。③**如实报告**：本卷 Project 参考答案作者在本机上测出 dot4 只有约 1.2×，远小于教材 ch02-03 点积的 2.9×——先别急着怀疑实现，用两个机制解释（提示：a) 本机 GCC 对标量点积在 -O2 下已经做了什么？b) 这个工作负载在 N=4M 时主要是链受限还是带宽受限？）。再用 `-fopt-info-vec` 或 `-S` 找证据支撑你的解释。④为什么「改之前先确认真瓶颈」这句话，在你自己的 1.2× 面前突然有了实感？

**验收标准**：贴出 dot4 的正确性差、两个 N 的加速比与 cv、以及你的第③问分析（含诊断输出）。

[参考实现 →](06-project-solutions.md#pj-adv)

### 再进阶任务（L4）：把门装上 {#pj-gates}

四道门。①**警告门**：`-Wall -Wextra` 编译零警告（含 L5 的 AVX2 代码在内）。②**sanitizer 门**：`-O1 -g -fsanitize=address,undefined` 构建，小规模（N=131072）跑一遍完整输出，必须零报告。③**双编译器门**：clang++ 同样旗标编译跑 N=4M，贴出与 g++ 的三行对照——如实说明哪个快、以及为什么「别为了听说谁快换工具链」（教材 ch07-03 的结论）。④**方法论报告**：写清你的测量规程（预热怎么做的、rounds 取几、报中位数还是均值、cv 多大算不可信），并回答：为什么并发/高频基准**不该**用单次运行的数字？

**验收标准**：贴出四道门的全部输出；sanitizer 构建零报告；报告里必须引用 ch01 的具体条款（报中位数、cv 阈值、环境快照）。

[参考实现 →](06-project-solutions.md#pj-gates)

### 终极挑战（L5）：手写 AVX2 与带宽墙 {#pj-l5}

四件挑战（L5＝用本卷知识可解的最难问题，档位口径见[练习总览](index.md)；风格改编自 CSAPP 第 5 章循环优化与第 6 章存储层次，intrinsics 写法对照 Intel Intrinsics Guide 的 AVX2 点积模板）。①实现 `dot_avx2`：4 条 `__m256` 累加器 × 每轮 32 个 float 的 `_mm256_fmadd_ps`，尾部标量兜底，horizontal 合并 8 个 lane——解释为什么是「4 条向量累加器」而不是 1 条（复用你 L3 层学到的那句话）。②**缩放曲线**：对 N = 65536 / 262144 / 1048576 / 4194304 / 16777216 各测 dot_scalar 与 dot_avx2（每档 9 轮），把每档的吞吐（GB/s）算出来画成表——dot_scalar 的吞吐在整条曲线上大约是多少？dot_avx2 的加速比从哪档到哪档、从几倍衰减到几倍？③用 Roofline 解释这条衰减：算术强度 0.25 FLOP/byte 意味着什么屋顶？「SIMD 加速被带宽墙稀释」的具体机制是什么？④**fast-math 对照**：用 `-O3 -mavx2 -mfma -ffast-math` 重编，只测 dot_scalar，与你的 dot_avx2 对比——如实报告结果，并说清「编译器自己把标量版重排成 4 向量累加器」这件事从结果差上怎么看出来。

**验收标准**：贴出缩放曲线表、fast-math 对照，以及 200 字以内的归因报告：你的机器、带宽墙的位置（GB/s）、「先减访存、再提 ILP、最后 SIMD」这个顺序在你的数据里是怎么体现的。

[参考实现 →](06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`dotbench.cpp` 单文件即可）+ 各层终端记录（`layerN.log`）+ 200 字以内小结：说说这个项目里哪一处让你对「先正确，再测量，按瓶颈优化」体会最深。
