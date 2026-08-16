---
title: "卷 10 Lab：零开销安检台"
description: "卷 10 动手实验：把五场讲座拧成一条「从 C++ 到汇编、再回到 C++」的安检线——六步从迭代器类别侦察走到 noexcept 扩容陷阱，最后附一道手写 SSE 汇编对决编译器自动向量化的 L5 挑战。全程真终端、真编译、真看汇编输出。"
chapter: 10
order: 3
tags: [host, advanced, cpp-modern, 调试, 优化, 类型安全, Ranges]
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 5
prerequisites: []
related: []
---

# 卷 10 Lab：零开销安检台

## 实验目标

本卷的五个讲座围着一个词打转：**零开销**。Stroustrup 说抽象不能比手写慢，Godbolt 说这个代价只有汇编看得见，Shah 说 ranges 把你从手写循环里解放出来，Saks 说移动把拷贝变成指针赋值，Downey 说 `optional<T&>` 就是个带约束的指针。这个 Lab 把它们拧成一条安检线：你拿着 `static_assert`、`-S` 汇编、位表、`checked_span`、管道计数、计数器类这几把刀，把「抽象到底付没付代价」一个个验过去。做完你会对「看完汇编再下结论」有肌肉记忆。

所有实验在 `/tmp` 下独立目录做，环境与笔记一致：WSL Arch，g++ 16.1.1。每步有验收标准；卡住先回题面每步标注的章节链接读教材，再不行看[实验参考](./04-lab-solutions.md)。

## 步骤 1：迭代器类别双探针 {#lab-1}

难度 **L1** · 涉及[从循环到迭代器](../cppcon/2025/03-back-to-basics-ranges/01-from-loops-to-iterators.md)

**目标**：用两套体系（C++98 legacy tag 与 C++20 concept）给常见容器的迭代器「验明正身」。

1. 写程序对 `array`、`vector`、`string`、`deque`、`list`、`forward_list`、`map` 和裸指针 `int*` 各打一行：legacy tag 是什么、C++20 concept 是什么。
2. 找出所有「两套体系答案不一致」的行，说明为什么。

**验收标准**：贴出完整表格；一句话说清 `deque` 为什么「随机可访问」却「不连续」。

[实验参考 →](./04-lab-solutions.md#lab-1)

## 步骤 2：ABI 读汇编 {#lab-2}

难度 **L2** · 涉及[阅读汇编与寄存器 ABI](../cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md)

**目标**：把 System V AMD64 的参数传递规则读到肌肉记忆里。

1. 写 `square(int)`、`add_three(long,long,long)`、`sum_seven(long×7)`，`g++ -O2 -S` 编译，贴出去掉 `.cfi_*` 噪音后的三个函数体。
2. 回答：`square` 里那条 `mov` 是干什么的；第七个参数从哪个偏移读、为什么不是 `[rsp]`；`main` 被优化成了什么。

**验收标准**：贴出汇编；对「前六个整数参数依次放哪」倒背如流。

[实验参考 →](./04-lab-solutions.md#lab-2)

## 步骤 3：位查找表的守卫 {#lab-3}

难度 **L2** · 涉及[Compiler Explorer 与 AI 辅助](../cppcon/2025/02-some-assembly-required/03-compiler-explorer-and-ai-assisted.md)

**目标**：亲手验证「移位量 ≥ 位宽是 UB」——先埋雷，再拆雷。

1. 写讲座的「数字位查找表」（bit 48..57 置位）判断函数，**故意不加 `uc >= 64` 守卫**，测 `'5'`、`'a'`、`'p'`，把结果贴出来。
2. 同一份代码用 UBSan 构建跑一遍，贴出运行时报告。
3. 加上守卫，对照朴素写法对 ASCII 0..127 全量验证一致。

**验收标准**：贴出两种构建的输出；说出「x86 会把移位量掩码」这句话为什么不能作为你代码的依据。

[实验参考 →](./04-lab-solutions.md#lab-3)

## 步骤 4：checked_span 与负数下标 {#lab-4}

难度 **L3** · 涉及[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)

**目标**：给裸指针加一层带检查的「视图」，并亲手抓住负数下标这个坑。

1. 实现 `checked_span<T>`：下标用 `ptrdiff_t`（有符号），先查负、再查上界，越界抛 `std::out_of_range`。
2. 加一条 CTAD 推导指引，让 `checked_span s(data, 5)` 自动推出元素类型。
3. 依次测试：正常访问、改写、上界越界、负数下标，贴出四次结果。

**验收标准**：贴出输出；说出下标类型为什么选 `ptrdiff_t` 而不是 `size_t`（负数传进去会发生什么）。

[实验参考 →](./04-lab-solutions.md#lab-4)

## 步骤 5：惰性管道与短路 {#lab-5}

难度 **L3** · 涉及[Ranges、Views 与管道组合](../cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md)

**目标**：用谓词计数器把「惰性」变成看得见的数字。

1. 造 1000 万个 `int`，用**恒真谓词** + `views::take(5)` 对比全量 filter 的谓词调用次数，贴出两个数字。
2. 跑 eager（`ranges::to` 物化）vs lazy 的求和基准，贴出耗时与结果。

**验收标准**：贴出输出；解释恒真谓词下 `take(5)` 的调用次数为什么是个位数。

[实验参考 →](./04-lab-solutions.md#lab-5)

## 步骤 6：noexcept 决定 vector 扩容路径 {#lab-6}

难度 **L4** · 涉及[移动操作、std::move 与拷贝消除](../cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md)

**目标**：实测一个 `noexcept` 关键字如何把「移动」打回「拷贝」。

1. 写一个带 `copies/moves` 计数器的 `char*` 持有类，移动构造用宏开关控制 `noexcept`。
2. `reserve(2)` 后塞 3 个元素（第三个触发扩容），编译运行无 noexcept、有 noexcept 两个版本，贴出两份计数。
3. 解释强异常安全保证为什么逼 vector 做这个选择。

**验收标准**：贴出两份计数；能说清「移动是破坏性的、回滚不了」这条理由链。

[实验参考 →](./04-lab-solutions.md#lab-6)

## 附加挑战（L5）：手写 SSE 对决编译器 {#lab-l5}

**目标**：受讲座「手写汇编的价值」一节启发（含其 `abs_array` 示例），把 `abs(x) = (x ^ mask) - mask` 用 SSE2 内联汇编写出来，与编译器自动向量化正面较量。

1. 写标量版 `abs_c`（朴素三目）与手写 SSE 版 `abs_sse`（`movdqu`/`psrad $31`/`pxor`/`psubd`，一次 4 个 `int32`），对 1M 个随机 `int32` 做正确性对比，必须 PASS。
2. 两个版本各自 `noinline`（**这个坑先想清楚为什么**：不 noinline，-O3 下编译器会把你重复算同一批数据的计时循环整体消除，标量版计时变成 0ms），在 `-O2` 和 `-O3 -march=x86-64-v2` 下各跑一次基准，贴出加速比。
3. 用 `-O3 -march=x86-64-v2 -S` 找 `pabsd` 的出现次数，证明编译器在 -O3 下自动向量化了。

**验收标准**：贴出正确性结果、两次基准数字和 `pabsd` 的汇编佐证；一句话回答「手写 SIMD 什么时候还值得」。

[实验参考 →](./04-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下全部源码、每步终端记录（`stepN.log`），以及 200 字以内的小结——用你自己的话说清「看完汇编再下结论」这件事你在哪一步看得最真切。
