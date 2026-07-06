---
chapter: 4
cpp_standard:
- 17
description: Bad Speculation 桶的对策是消除不可预测的分支。本篇讲 branchless(用 cmov/位运算消除分支)、predication
  的原理,但核心信息是反直觉的:现代 CPU 分支预测器极强 + 编译器常自动无分支化(循环向量化成 SIMD 掩码、标量转 cmov),「别盲目 branchless」——可预测分支几乎免费,branchless
  改写若增加指令或数据依赖反而更慢。永远 benchmark
difficulty: advanced
order: 6
platform: host
prerequisites:
- 流水线、ILP 与分支预测
- 数据类型与算术:整数/浮点、乘除与跳转表
reading_time_minutes: 6
related:
- 循环与计算优化:code motion、展开与多累加器
- 前端优化:代码布局、PGO、BOLT
tags:
- host
- cpp-modern
- advanced
- 优化
title: 分支:branchless、predication 与「别盲目无分支」
---
# 分支:branchless、predication 与「别盲目无分支」

## Bad Speculation 桶的对策

ch02-03 我们测过:一个 50/50 随机的分支,被预测器猜错要冲刷流水线,代价惨重,排序数组(可预测)vs 打乱数组(不可预测)差 **4.2 倍**。TMAM 把这种浪费归到 **Bad Speculation** 桶。这一桶的对策只有一条思路:**消除不可预测的分支**。

消除有两种做法:**branchless**(把分支改成无分支的 cmov/位运算)和 **predication**(让两条路径都算、最后按条件选,反正都算就无所谓「猜错」)。听起来都是银弹,但本篇的核心信息是反直觉的:**别盲目 branchless**。我们用实验说明为什么。

## branchless:cmov 与位运算

最常见的 branchless 改写是 `std::min`/`std::max`/`std::clamp` 这类,它们底层用 **cmov**(条件传送)指令:两条路径的结果都算出来,然后按条件「选」一个,全程没有分支跳转,自然没有预测失败。

```cpp
// 有分支:不可预测时预测失败冲刷
int clamp_if(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
// 无分支(std::min/max 编译成 cmov)
int clamp_cmov(int x, int lo, int hi) {
    return std::min(std::max(x, lo), hi);
}
```

`cmov` 的代价:两条路径都算了(多算一次)、有数据依赖(选的结果依赖条件),但**没有控制依赖、不冲刷流水线**。对**不可预测**的分支,cmov 完胜;对**可预测**的分支,cmov 反而更慢(因为多算了 + 数据依赖链拉长,而有分支的版本预测器命中、几乎免费)。

## 上手跑一跑(以及一个诚实的结果)

我在 50/50 随机数据上测 clamp 的三种写法:if 分支 / `std::min`+`std::max`(cmov)/ 位运算 trick:

```text
===== branchless vs 分支(clamp,随机数据 50/50)=====
  if 分支:       0.27 ns/次(预测失败冲刷)
  std::min/max:  0.25 ns/次
  位 trick:      0.25 ns/次
  if / cmov = 1.07x
```

**差距只有 1.07 倍,不是我预期的「branchless 大胜」。** 为什么?看汇编(`g++ -O2 -S branchless.cpp`)——**整个循环里 `cmov` 指令数 = 0**:默认 `-O2` 下 GCC 把三种 clamp 的循环都**自动向量化成了相同的 SIMD 掩码操作**(`-fopt-info-vec` 报 `loop vectorized using 16 byte vectors`)。也就是说,你写 `if`、写 `std::min/max`、写位 trick,在循环里经 -O2 编译后**变成了同一份向量代码**,所以三者一样快。**注意不是「if 被转成 cmov」**(标量单次 clamp 编译器才会用 cmov)**而是「循环被向量化」,别用错机制解释**。

这是个**极重要的诚实结果**:你写的 `if` 不一定是真分支,编译器可能早就替你 branchless 了(循环里常靠 SIMD 向量化、标量单次常靠 cmov)。要确认,得看汇编(`-S`)+ 向量化诊断(`-fopt-info-vec`):真分支是 `jcc`(条件跳转),标量无分支是 `cmov`/`cset`,循环无分支是 SIMD 掩码指令。**别假设你的 if 是分支,别假设手写 branchless 一定更快**。

那真分支的代价在哪?要逼出真分支,得**禁用编译器的 if-conversion**(就像 ch02-03 我用 `-fno-if-conversion` 才测出 4.2 倍)。ch02-03 的那个实验(打乱 vs 排序,4.2×)才是真分支惩罚的诚实证据。本篇这个 1.07× 是「编译器已 branchless」的诚实证据。两个合起来才是完整图景。

## predication:两条路都算

`cmov` 的思想推广就是 **predication**:与其分支选一条路算,不如**两条路都算,最后按条件选**。GPU 和向量体系结构(SSE/AVX 的 mask)大量用 predication,因为它们讨厌分支(发散)。x86 上 predication 体现为 cmov / cset。

predication 的代价:总计算量 = 两条路径之和(即使只取一条的结果)。所以它**只适合「两条路径都很便宜」**的场景(`max`、`min`、`clamp`、简单赋值)。如果两条路径之一很贵(比如一次除法、一次函数调用),predication 强行都算就亏大了,这时**有分支更好**(贵的路径只在命中时才算)。这条权衡是「别盲目 branchless」的另一面。

## `[[likely]]` / `[[unlikely]]`:给编译器分支概率

C++20 标准化了 `[[likely]]` / `[[unlikely]]`(老办法是 GCC 的 `__builtin_expect`):给编译器一个分支概率提示,让它把 likely 路径的代码布局到一起(改善 icache,详见 ch04-07 前端优化)、调整分支预测假设。

```cpp
if (rare_error) [[unlikely]] { handle_error(); }  // 告诉编译器这条路很少走
```

注意 `[[likely]]` 的**主要收益是代码布局**(把热路径聚一起、冷路径扔到函数末尾),让 icache 命中率更高,这是 Frontend 优化。它**不是**「让分支预测器更准」(硬件预测器不看你的源码注解,它看运行时历史)。所以 `[[likely]]` 在「分支非常倾斜 + 函数较大」时有用,在「小循环里的分支」基本没用。

## 「别盲目 branchless」:四条纪律

把这一篇的诚实结果压成四条纪律:

1. **可预测的分支几乎免费**。循环退出条件、`if (ptr == nullptr)` 这种绝大多数走同一方向的分支,预测器命中率 99%+,不用费心消除。该消除的是**数据相关的、不可预测的分支**。
2. **你的 `if` 不一定是真分支**。编译器常自动无分支化(循环里向量化成 SIMD 掩码、标量里转 cmov),看汇编 + `-fopt-info-vec` 确认。
3. **branchless 不是银弹**。如果 predication 让两条路径都算(其中一条很贵),或者增加数据依赖链,branchless 反而更慢。
4. **永远 benchmark 对照**。改 branchless 前后用同一套方法论测一遍(ch01),信号比直觉可靠。

这条「别盲目」的纪律其实贯穿 ch04。04-02 的循环优化、04-04 的 inline、这一篇的 branchless,故事都是同一个:**现代编译器 + 硬件预测器替你做了大半,你的工作不是「使劲手写优化」,而是「测出真瓶颈、精准改、改完验证」**。性能优化不是堆 trick,是测量驱动的精准手术。

回头看这一篇:Bad Speculation 桶的对策是消除不可预测的分支,手法是 branchless(cmov/位运算)和 predication;实测 clamp 的 if/cmov/位 trick 三者基本一样快(1.07×),因为 `-O2` 下循环里的三种写法都被向量化成相同 SIMD 掩码代码(`-S` 下 cmov 数 = 0),真分支惩罚的证据是 ch02-03 的 4.2×;predication 只适合两条路径都便宜的场景,一条贵时反不如有分支;`[[likely]]`/`[[unlikely]]` 主要收益是代码布局(icache),不是预测器;归根结底是别盲目 branchless,可预测分支免费、你的 if 可能已是 cmov、benchmark 是唯一裁判。

## 参考资源

- Agner Fog《The microarchitecture of Intel, AMD and VIA CPUs》分支预测章节。本地
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 9 章 *Optimizing Bad Speculation*
- ch02-03 流水线、ILP 与分支预测(本卷,4.2× 真分支惩罚的实测出处)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch04/branchless.cpp`
