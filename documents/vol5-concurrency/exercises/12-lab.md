---
title: "卷五 Lab：原子模式实验台"
description: "卷五动手实验：把 ch03 的原子操作、内存序、atomic_wait、atomic_ref、SeqLock、引用计数与 fence 拧成一条实验线——六步从「量 relaxed 与 seq_cst 的开销」一路做到「手写 Peterson 软件锁」，每步贴真实数据（含 TSan 报告），最后附一道 L5 挑战（Peterson 互斥的 C++ 内存模型正确实现，改编自 Peterson 1981 与 Preshing 的讨论）。"
chapter: 5
order: 12
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - memory_order
  - 无锁
difficulty: advanced
platform: host
reading_time_minutes: 7
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷五 Lab：原子模式实验台

## 实验目标

ch03 是卷五最硬核的一章：六种内存序、fence、atomic_wait、atomic_ref，还有 SeqLock、引用计数这些模式。本 Lab 把它们拧成一条实验线——你不是「读一遍概念」，而是拿着 `std::atomic` 这唯一一件工具，把「内存序到底贵不贵」「wait 和 notify 的语义边界」「SeqLock 为什么是读多写少的终极方案」这些问题一个个**量出来、跑出来**。

六步难度 L1→L4 递进，外加一道 L5 挑战。每步都在 `/tmp` 下独立目录做，验收标准见各步；卡住先回各步标注的章节链接读教材，再不行看[实验参考](13-lab-solutions.md)。

> 编译约定：普通构建 `g++ -std=c++20 -O2 -pthread`；并发正确性验收统一用 TSan（`-fsanitize=thread -g`，这是**编译期**选项，运行时不加任何旗标）。**如实记录**：本实验台的多处「意外结果」（TSan 误报 SeqLock、GCC TSan 不支持 fence）恰恰是教材「TSan 对无锁代码的误报率不低」的实锤，别把它们当 bug 抹掉。

## 步骤 1：量内存序的开销 {#lab-1}

难度 **L1** · 涉及[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)、[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)

**目标**：4 线程各对同一个 `std::atomic<int64_t>` 做 500 万次 `fetch_add`，分别用 `relaxed` 与 `seq_cst`（默认值）各跑 3 轮，比较耗时。

1. 写模板函数 `measure<Mo>(workers, iters)`，把内存序作为模板参数传入。
2. 贴出 3 轮数据并计算比值。

**验收标准**：贴出输出；一句话解释本机的比值为什么是这个样（提示：想想 x86 的 TSO 模型，以及这条结论在 ARM 上还成不成立）。

[实验参考 →](13-lab-solutions.md#lab-1)

## 步骤 2：atomic_wait 的语义边界 {#lab-2}

难度 **L2** · 涉及[atomic_wait 与 atomic_ref](../ch03-atomic-memory-model/04-atomic-wait-and-ref.md)

**目标**：把 `wait`/`notify` 的三条语义亲手验证一遍。

1. 4 个等待线程在 `g_signal.wait(0)` 上阻塞，主线程睡 200ms 后 `store(1, release)` + `notify_all()`，统计醒来的数量。
2. 重来一遍但用 `notify_one()`，200ms 后统计醒来数量（**如实记录**，允许伪唤醒），再用 `notify_all()` 兜底让程序退出。
3. 对一个当前值为 7 的 `atomic<int>` 调用 `wait(0)`，测量它是否立即返回、耗时多少——`wait` 的参数为什么是「期望的旧值」而不是「等待的目标值」？

**验收标准**：贴出三组输出；说清 `wait` 的「旧值」设计与 TOCTOU 竞态的关系。

[实验参考 →](13-lab-solutions.md#lab-2)

## 步骤 3：atomic_ref 的正确用法与红线 {#lab-3}

难度 **L3** · 涉及[atomic_wait 与 atomic_ref](../ch03-atomic-memory-model/04-atomic-wait-and-ref.md)

**目标**：给普通 `int` 数组「套上」原子操作，并亲手触碰它的红线。

1. 4 线程通过 `std::atomic_ref<int>` 对 `g_counters[0]`（普通 `int`）各做 100 万次 `fetch_add`，验证结果为 400 万整。
2. 红线实验：4 线程对 `g_counters[1]` **一半走 atomic_ref、一半裸写 `++g_counters[1]`**——普通构建贴出最终值，TSan 构建贴出报告。报告里的两条访问路径分别是什么？

**验收标准**：贴出两组输出与 TSan 报告；说出 `atomic_ref` 的第二条约束（混用即 UB）在本实验里是怎么被证实的。

[实验参考 →](13-lab-solutions.md#lab-3)

## 步骤 4：SeqLock——读多写少的终极形态 {#lab-4}

难度 **L4** · 涉及[原子操作模式](../ch03-atomic-memory-model/05-atomic-patterns.md)、[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)

**目标**：实现单写多读 SeqLock，并用不变量校验证明读者永远看到一致快照。

1. 数据是 `{a, b, c}` 三个 `int`，写者 10 万轮写 `{i, 2i, 3i}`；4 个读者循环「读序号（偶数才读数据）→ 拷快照 → 再读序号验证」，统计总读数、重试次数、以及「b≠2a 或 c≠3a」的**撕裂**次数。
2. 先贴普通构建输出（撕裂应为 0），再贴 TSan 构建输出。

**验收标准**：贴出两组输出。**如实报告**：TSan 大概率会对 `g_data` 报 race——解释它为什么误报（TSan 不理解「序号验证」这种自定义同步协议），以及你的实现为什么仍然正确（release/acquire 对 + 验证构成了什么样的 happens-before 链）；顺带说出生产环境里怎么让 TSan 闭嘴（`__tsan_acquire/__tsan_release` 注解）。

[实验参考 →](13-lab-solutions.md#lab-4)

## 步骤 5：引用计数的 acq_rel 选择 {#lab-5}

难度 **L4** · 涉及[原子操作模式](../ch03-atomic-memory-model/05-atomic-patterns.md)、[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)

**目标**：实现侵入式引用计数，并论证两个内存序选择缺一不可。

1. `struct RefCounted { std::atomic<int> refs{1}; ... }`：`add_ref()` 用 `fetch_add(1, relaxed)`，`release()` 用 `fetch_sub(1, acq_rel)`，减到 0 就 `delete this`。
2. 4 线程各做 50 万次 `add_ref/release` 对，join 后主线程释放最后一个引用，用 `g_destroyed` 标志验证析构**恰好一次**。
3. TSan 构建跑一遍贴结果。

**验收标准**：贴出两组输出；解释为什么 `add_ref` 可以 relaxed 而 `release` 必须 acq_rel（acquire 半边保护什么、release 半边保护什么）。

[实验参考 →](13-lab-solutions.md#lab-5)

## 步骤 6：fence 版消息传递 {#lab-6}

难度 **L4** · 涉及[fence 与编译器屏障](../ch03-atomic-memory-model/03-fence-and-barrier.md)、[内存序详解](../ch03-atomic-memory-model/02-memory-ordering.md)

**目标**：用「release fence + relaxed store」与「relaxed load + acquire fence」实现消息传递，并如实记录 TSan 的反应。

1. 写生产者（写 `g_payload` → release fence → relaxed store 标志）与消费者（relaxed 自旋 → acquire fence → 读 `g_payload`），跑 4 轮贴输出——值都应该正确。
2. 用 TSan 构建再跑一遍，贴出**编译警告**与运行报告。

**验收标准**：贴出两组输出。**如实报告**：GCC 16 的 TSan 明确警告 `atomic_thread_fence` 不受支持（`-Wtsan`），并会把 fence 版消息传递报成 race——这是 TSan 的已知限制，不是你的实现有错。用「带 release/acquire 的原子操作版本」（步骤参考里有）验证同样的消息传递在 TSan 下干净，说明两者语义等价但工具支持不同。

[实验参考 →](13-lab-solutions.md#lab-6)

## 附加挑战（L5）：Peterson 软件锁 {#lab-l5}

**目标**：把步骤 1~6 的全部功夫收进一道题——用 C++ 内存模型正确实现 Peterson 双线程互斥（改编自 Peterson (1981) 的原始算法与 Preshing 对「C++ 中软件锁正确实现」的讨论，L5＝用本卷知识可解的最难问题，档位口径见[卷五练习体系](index.md)）。

1. 实现 `PetersonLock`：`want_[2]` 两个原子 bool + `victim_` 原子 int，全部 `seq_cst`；两个线程各持 id=0/1 互斥自增一个**普通 int** 各 50 万次，验证 `counter == 1000000`，TSan 构建零报告。
2. 对照实验：把三个原子操作的内存序全部换成 `relaxed`，同样的负载再跑——普通构建贴出结果（本机大概率丢几个、也可能侥幸全对），TSan 构建贴出结果（**如实报告**，本机参考实现里它在 TSan 下直接死锁，`timeout` 退出码 124——这正是弱内存序软件锁的「不确定行为」）。
3. 解释：为什么 seq_cst 版正确、relaxed 版在 x86 上「大部分时候对」？把这条结论与步骤 1 的结果对照着说。

**验收标准**：贴出 seq_cst（普通 + TSan）与 relaxed（普通 + TSan）四组输出；一段话说明「软件锁 + 弱内存序」为什么是等死。

[实验参考 →](13-lab-solutions.md#lab-l5)

## 提交物清单

一个目录装下六个步骤与挑战的全部源码、每步终端记录（`stepN.log`）、以及 200 字以内的小结——用你自己的话说清「同样的原子操作，换一个内存序，换一台机器，结论就可能翻面」这件事你在哪一步看得最真切。
