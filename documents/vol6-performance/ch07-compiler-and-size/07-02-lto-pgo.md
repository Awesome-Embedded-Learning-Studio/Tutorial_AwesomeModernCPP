---
chapter: 7
cpp_standard:
- 17
description: LTO(链接期优化)让编译器跨翻译单元内联,消除「跨 TU 调用」这个 optimization blocker——实测同一个跨文件调用快
  3.9 倍;PGO(剖面引导优化)在大型代码库上收益显著但对微基准无效果(诚实 null 结果)。本篇讲两者的机制、工程接入(怎么构建)、以及代价
difficulty: advanced
order: 2
platform: host
prerequisites:
- -O 级别与 optimization blockers
- 前端优化:代码布局、PGO 与 BOLT
reading_time_minutes: 5
related:
- 链接性能、多编译器对比与编译期元编程
- 体积优化:-Os、--gc-sections 与模板膨胀控制
tags:
- host
- cpp-modern
- advanced
- 优化
- 工具链
title: LTO、ThinLTO 与 PGO 的工程接入
---
# LTO、ThinLTO 与 PGO 的工程接入

ch07-01 讲了「跨翻译单元」是 optimization blocker 之一:编译器编译一个 `.cpp` 时看不见别的 `.cpp` 里的实现,不敢跨文件内联。本篇讲它的解法 **LTO**,加上「按真实剖面优化代码布局」的 **PGO**。两者都是 release 构建级的工程接入,在大项目上收益显著。

## LTO:链接期跨文件内联

**LTO(Link-Time Optimization)** 的思路是这样的:编译时给每个 `.o` 文件带上「中间表示(GIMPLE/LLVM IR)」而不只是机器码,链接时链接器把所有 IR 合并,**重新做一遍跨文件的优化和内联**。于是跨 TU 的函数调用能被内联,常量传播、死代码消除都能跨文件进行。

我们测一个最简单的跨 TU 场景:`main.cpp` 调 `helper.cpp` 里的 `helper(int)`:

```bash
# 无 LTO:helper 在另一个 TU,编译器看不见实现,不能内联
g++ -O2 main.cpp helper.cpp -o lto_nolto
# 有 LTO:链接期合并,helper 能被内联 → 进一步常量传播
g++ -O2 -flto main.cpp helper.cpp -o lto_lto
```

实测(helper 被调一亿次):

```text
无 LTO:  178.6 ms
有 LTO:   46.2 ms   ← 3.9 倍
```

**3.9 倍。** 这个差距全部来自「跨 TU 内联 + 后续优化」:LTO 让编译器看见 `helper` 的实现,把它内联进 `main` 的循环,然后常量传播/循环简化把整段代码压到接近最优。无 LTO 时,每次循环是一次真函数调用(且 helper 内部有自己的循环)。

附带好处:LTO 还做**跨文件死代码消除**,二进制往往更小(本例 16136 → 16024 字节,回收了未用代码)。

### ThinLTO:可扩展的 LTO

全 LTO 把所有 IR 合并成一个巨视图,大项目上**链接慢、内存吃得多**:Chrome 级项目全 LTO 链接要几十分钟、几十 GB 内存。**ThinLTO**(LLVM,`-flto=thin`)把工作分片,先做轻量摘要(import/export 决策),再并行优化每个模块。大项目上 ThinLTO 比 full LTO **链接快得多、内存省**,优化效果接近。GCC 也有类似机制(`-flto=auto` 并行)。

### LTO 的代价

- **链接变慢**(全 LTO 尤其),构建内存上升。
- **构建复杂度**:CMake 等构建系统要正确传 `-flto` 给编译和链接、ar 要用 gcc-ar(处理 LTO 对象)。
- **调试**:LTO 后符号可能被打乱,调试器体验下降。

实战上,**release 构建开 LTO/ThinLTO,debug 构建不开**。大项目用 ThinLTO。这是「免费午餐」级的优化(一行 flag、几个百分点到十几百分点提速),代价只是链接时间。

## PGO:按真实剖面布局代码

**PGO(Profile-Guided Optimization)** 在 ch04-07 讲过原理(三阶段:仪器化 → 跑剖面 → 用剖面重编),这里讲工程接入和一个诚实结论。

### 诚实的结论:PGO 对微基准无收益

笔者用 ch04 的 `pgo_demo`(99/1 分支的小函数)严格跑完三阶段 PGO,和纯 -O2 基线对比:

```text
纯 -O2 基线: 3.57-3.82 ms
-O2 + PGO:  3.78-4.17 ms   ← 没有任何收益(甚至略慢,噪声内)
```

**PGO 对微基准无效果。** 原因是这个小函数就两个分支、几行代码,`-O2` 已经把它优化得很好;PGO 的价值是**大代码库的代码布局**(把分散在几千个函数里的热路径物理聚到一起,改善 icache),对几十行的小函数没什么可布局的。

> 笔者第一遍跑时「PGO 版」看起来快了 4 倍,激动了一下,后来发现那个 4 倍是**仪器化二进制的计数器开销**(阶段 1 的 build 自带性能计数器),不是 PGO 收益。**测 PGO 必须用「纯 -O2 无仪器化」基线对照**,且确认阶段 3 的 profile 真被应用(编译器 warning `profile count data file not found` 说明没找到)。这个翻车经历记在这里,呼应 ch01 纪律:**你以为测的 PGO 收益,可能测的是仪器化开销**。

### PGO 真正的价值:大代码库

PGO 的公开收益都来自大项目:**Chrome、Firefox、各大数据库**,报告**个位数到十几个百分点**提速,前提是代码库大到 icache/分支布局真的成为瓶颈。所以三条:

- **别期望 PGO 在微基准或小项目上有效。**
- **在大型 release 构建里开**,用**代表性生产 workload** 采样剖面(不是随便跑跑)。
- 接入流程(CMake):`-fprofile-generate` 编译 → 跑 workload → `-fprofile-use` 重编。CI 集成要保存剖面、跨构建复用。

PGO + LTO 叠加是大项目 release 的标配组合。

## 参考资源

- GCC 文档 `-flto` / `-fprofile-generate` / `-fprofile-use`
- LLVM 文档 *ThinLTO*(llvm.org/docs/ThinLTO.html)
- ch04-07 前端优化:代码布局、PGO 与 BOLT(本卷,PGO 原理与「对微基准无收益」的首次讨论)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch07/lto_main.cpp` + `lto_helper.cpp`;PGO 复用 `ch04/pgo_demo.cpp` + `ch04/pgo.sh`

一句话收口:**LTO 跨 TU 内联实测 3.9×**(跨文件 helper 调用),release 开、大项目用 ThinLTO,代价只是链接变慢;**PGO 按剖面布局,对微基准无收益(诚实 null)**,价值在大型代码库(Chrome/Firefox 级,个位数到十几百分点),那个一度看到的 4× 是仪器化开销不是 PGO。**PGO + LTO** 是大项目 release 标配,接入是工程活(CMake 传 flag、保存剖面),一次性配置长期受益。
