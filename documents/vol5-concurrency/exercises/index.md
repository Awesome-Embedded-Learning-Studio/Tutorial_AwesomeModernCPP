---
title: "卷五练习体系"
description: "卷五并发编程的完整 Lab 练习体系：从线程生命周期到 Mini Concurrent Runtime"
---

# 卷五练习体系

卷五的练习分为三层，从易到难、从局部到系统。

**第一层**是文章内小练习，附在每篇正文末尾，用于验证单个知识点——比如 data race 的识别、condition_variable 的谓词等待、atomic 的 memory order 选择。每道练习 10–20 分钟即可完成，不需要搭建额外工程。

**第二层**是章节大作业（Lab），即本页列出的 8 个 Lab。每个 Lab 是一个可运行的小系统，拆成 3–5 个 milestone，每个 milestone 都有明确接口、Catch2 测试和验收标准。学习者完成后应该得到一个可以复用的并发组件，而不是零散 demo。

**第三层**是卷末综合项目（Capstone），把前面所有 Lab 的组件串起来，形成一个 mini concurrent runtime。

## Lab 一览

| Lab | 项目名称 | 覆盖章节 | 建议时长 | 难度 | 前置 Lab |
|-----|----------|----------|----------|------|----------|
| [Lab 0](00-thread-lifecycle.md) | Thread Lifecycle | ch00–ch01 | 4–6h | intermediate | 无 |
| [Lab 1](01-bounded-queue.md) | Bounded Queue & Sync Primitives | ch02–ch04 | 8–12h | intermediate | Lab 0 |
| [Lab 2](02-atomic-spsc.md) | Atomic Metrics & SPSC Ring Buffer | ch03–ch04 | 6–8h | intermediate | Lab 0 |
| [Lab 2.5](02.5-debugging.md) | Concurrency Debugging | ch08 | 3–4h | intermediate | Lab 0–2 |
| [Lab 3](03-thread-pool.md) | Production-style Thread Pool | ch05 | 10–14h | advanced | Lab 0–1 |
| [Lab 4](04-coroutine-scheduler.md) | Coroutine Scheduler & Event Loop | ch06 | 12–16h | advanced | Lab 3 |
| [Lab 5](05-channel-actor.md) | Channel or Actor Runtime | ch07 | 8–12h | advanced | Lab 1, 4 |
| [Capstone](06-capstone-mini-runtime.md) | Mini Concurrent Runtime | ch08–ch09 | 8–12h | advanced | Lab 0–5 |

最低要求完成 **Lab 0、Lab 1、Lab 3 和 Capstone**（约 30–45 小时），即可覆盖卷五最核心的能力曲线。完整完成全部 Lab 约需 60–85 小时。

## Lab 依赖关系

```cpp
Lab 0 (joining_thread / thread_guard)
  │
  ├─→ Lab 1 (BoundedBlockingQueue, ConcurrentCache)
  │     │
  │     ├─→ Lab 2 (SpscRingBuffer)        ─ 独立实现，不依赖 Lab 1
  │     │
  │     ├─→ Lab 2.5 (Debugging Lab)        ─ 复用 Lab 0–2 的代码作为诊断素材
  │     │
  │     └─→ Lab 3 (ThreadPool)             ─ 复用 Lab 1 的 BoundedBlockingQueue
  │           │
  │           ├─→ Lab 4 (Coroutine Scheduler) ─ 关闭语义参考 Lab 3
  │           │     │
  │           │     └─→ Lab 5 (Channel/Actor) ─ 可复用 Lab 1 的队列
  │           │
  │           └─→ Capstone (Mini Runtime)   ─ 组合 Lab 0–5 的组件
```

## 环境准备

所有 Lab 共用以下环境要求：

- **编译器**：GCC 12+ 或 Clang 15+（C++20，完整协程支持）
- **CMake**：3.14+
- **测试框架**：Catch2 v3（header-only，通过 FetchContent 拉取）
- **TSan**：编译选项 `-fsanitize=thread -g`
- **平台**：Linux 或 WSL2（Lab 4 的 epoll 部分需要）
- **Valgrind**（可选，Lab 2.5 的 helgrind 需要）

每个 Lab 文章的开头都有具体的 CMakeLists.txt 模板，可以直接使用。

---

# 五档练习与作业（Homework / Lab / Project）

> 本卷另外提供一套「五档难度」练习集（10-homework.md 起）：与上面的里程碑式工程 Lab 互补——工程 Lab 给你脚手架、按 Milestone 验收；这套练习集给你题面与详细参考答案、按档位挑战。

**Homework（课后练习）** 按章出题，每章两道（基础 + 进阶），再加跨章综合与一道 L5 挑战；**Lab（动手实验）** 一条贯穿实验五到六步、附 L5 挑战任务；**Project（综合项目）** 任务分四层（核心 → 进阶 → 质量门 → 终极）。每份作业内部都覆盖全部五档，三份整体难度依次递进。

| 档位 | 对标 | 出题风格 |
| --- | --- | --- |
| L1 | 全国计算机等级考试三级 | 计算机通识加基础编程，考「知不知道」 |
| L2 | 全国计算机等级考试四级 | 计组、操作系统、数据结构、软件工程综合风格 |
| L3 | CS61A→B→C 作业与 lab、408 真题 | 经典课程作业改编与考研真题风格 |
| L4 | SICP 练习、CSAPP 练习 | 深度机制分析（C++ 侧还有 Effective Modern C++ / Core Guidelines 风格） |
| L5 | ICPC / IOI 等竞赛真题改编 | 挑战级，金牌难度。卷内口径：早期卷的 L5＝用本卷知识可解的最难问题；改编来源如实标注 |

题目都做「变式」处理（照抄教材例题抄不出答案），题面干净不剧透、参考答案独立成文件（11、13、15 号文件），每步解答标注知识点链接回教材对应章节。所有答案代码在 WSL（g++ 16 / clang++ 22，C++20）真编译真跑，并发题用 TSan 实测，验证输出全部真实。

| 类型 | 状态 |
| --- | --- |
| Homework（10） | [已上线](10-homework.md)（23 题） |
| Lab（12） | [已上线](12-lab.md) |
| Project（14） | [已上线](14-project.md) |
