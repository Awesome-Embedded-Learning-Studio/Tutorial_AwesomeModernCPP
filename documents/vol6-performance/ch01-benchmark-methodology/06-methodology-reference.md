---
title: "Benchmark 方法论参考卡"
description: "ch01 全章的速查页——后续每篇性能文章和 Lab 开头引用它。一张卡浓缩:环境就绪、可信 microbenchmark 写法、报告与比较、micro vs 生产/CI 的边界、perf 速查"
chapter: 1
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 优化
  - 测试
difficulty: intermediate
platform: host
reading_time_minutes: 6
cpp_standard: [11, 17]
prerequisites:
  - "怎么写一个可信的 microbenchmark"
related:
  - "为什么 microbenchmark 会骗你"
  - "测量陷阱与环境就绪:16 条 checklist"
  - "统计与报告:把分布变成结论"
  - "生产测量与 CI 性能回归检测"
---

# Benchmark 方法论参考卡

> 这是 ch01 全章的**速查页**。本卷后续每一篇性能文章和性能 Lab,开头都会回引这一页讲的那套规矩——就像 vol5 用 TSan 贯穿并发正确性。这不是教程(教程是 ch01-01 到 ch01-05),是 reference card,贴墙上看的那种。

## §0 前提(一句话)

**性能是随机变量,不是数。** 你测出来的一定是一个分布,要采样 + 统计推断,不能跑一次记个数。(详见 ch01-01)

## §1 测量前:环境就绪(micro A/B 场景)

| 必做 | 命令 / 做法 |
|---|---|
| 锁 CPU governor | `sudo cpupower frequency-set -g performance` |
| 关 Turbo | BIOS,或锁频 |
| 绑核 | `taskset -c <某个核> ./bench`(别挑 0 号) |
| NUMA 绑节点 | `numactl --cpunodebind=0 --membind=0 ./bench` |
| perf 可用 | `sudo sysctl -w kernel.perf_event_paranoid=1` |
| 编译选项 | `RelWithDebInfo`(`-O2 -g`)+ profiling 加 `-fno-omit-frame-pointer` |
| 体检 | `bash perf-env-check.sh`(见 ch01-03,只查不改) |

> ⚠️ **这些只在 micro A/B 场景做**。评估生产性能时**全部不做**——要复刻真实(保留 DFS、邻居、ASLR),用统计处理噪声。见 ch01-05。

## §2 写可信 microbenchmark

| 要点 | 做法 |
|---|---|
| 用框架,别手搓 | Google Benchmark 主力、nanobench 轻量补(讲微架构时即时反馈) |
| 防 DCE | `benchmark::DoNotOptimize(x)` 钉结果到内存/寄存器;**注意:不防 `x` 自身被常量传播算掉**,所以输入必须是运行期数据 |
| 强制写落内存 | `benchmark::ClobberMemory()` 兜底 |
| 扫参数 | `->RangeMultiplier(2)->Range(8, 8<<10)`;`state.SetComplexityN(...)` 自动拟合 big-O |
| 重复聚合 | `->Repetitions(3)->ReportAggregatesOnly(true)` 报 mean/median/stddev/cv |
| 墙钟 | `->UseRealTime()`(多线程必加) |

详见 ch01-02(含完整可运行例子和真实输出)。

## §3 报告与比较

- **必报**:中位数、IQR 或 95% CI、cv、样本数、环境快照(内核 / CPU / governor / `perf_event_paranoid`)。
- **禁报**:单次均值(性能数据右偏,长尾拉偏均值)。
- **A/B**:同环境、同二进制(只改一处)、多次重复(N ≥ 30);假设检验**默认 Mann-Whitney U**(非参数,性能数据几乎不正态),先验正态才用 t-test。
- **报效应量**:「快 12%(95% CI [10%, 14%], p<0.01)」这种完整说法,不只甩一个 p 值。统计显著 ≠ 工程有意义。
- **双峰分布是信号不是噪声**:混了两种行为(cache hit/miss、锁竞争),拆开分别测。

详见 ch01-04。

## §4 micro vs 生产/CI(边界,不许混)

| 场景 | 做什么 | 产出 |
|---|---|---|
| **micro A/B** | 消除噪声,干净对比两个实现 | 「改动方向对不对」 |
| **生产测量** | 复刻真实噪声,telemetry 采分位数(p90/p99),统计 A/B | 「用户实际快了吗」 |
| **CI 回归** | 变点检测(E-Divisive)/ PMC 指纹(AutoPerf),自动开 ticket | 「有没有悄悄退化」 |

**禁跨场景换算**:micro 的 30% 提升不会等比例带到生产。见 ch01-01、ch01-05。

## §5 vol6 文章 / Lab 引用规范

- 每篇性能文章、每个性能 Lab,开头声明「本文遵循 ch01 测量方法论」。
- 报性能数字时,附**环境快照** + **统计量**(中位数 / cv / 重复次数),不报单次裸值。
- 涉及 A/B 时,用 §3 那套(同环境同二进制 + Mann-Whitney + 效应量)。

## §6 perf 速查

```bash
# 基础计数(体检:看 IPC、cache miss、branch miss)
perf stat -r 5 ./bench
perf stat -e cycles,instructions,cache-misses,branch-misses ./bench

# 采样 profile(找热点;务必 -fno-omit-frame-pointer 或用 dwarf 解栈)
perf record -F 99 -g --call-graph dwarf -- ./bench
perf report                      # 交互式看
perf script | stackcollapse-perf.pl | flamegraph.pl > out.svg   # 火焰图

# 微架构归因(ch03 详谈)
toplev -l3 taskset -c 0 ./bench  # TMAM 四桶下钻,需 pmu-tools
```

火焰图、TMAM、`toplev` 的完整工作流是 ch03(归因方法论)的活,这里只给入口。

## 参考资源(教程正文)

- ch01-01「为什么 microbenchmark 会骗你」
- ch01-02「怎么写一个可信的 microbenchmark」
- ch01-03「测量陷阱与环境就绪:16 条 checklist」
- ch01-04「统计与报告:把分布变成结论」
- ch01-05「生产测量与 CI 性能回归检测」
- Bakhvalov, D. 《Performance Analysis and Tuning on Modern CPUs》第 2 章
- Google Benchmark [user_guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md)、Brendan Gregg [perf / FlameGraphs](https://www.brendangregg.com/linuxperf.html)
