---
id: "014"
title: "卷五：并发编程 TODO"
category: content
priority: P0
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: large
---

# 卷五：并发编程 TODO

## Current Assets

- `documents/vol5-concurrency/`：56 个 md（中文），`documents/en/vol5-concurrency/` 完整英文翻译 56 个 md。
- ch00-ch09 覆盖并发基础、线程生命周期、锁、条件变量、atomic、并发数据结构、future/task/threadpool、协程 I/O、actor/channel、调试测试性能、分布式桥接。
- `documents/vol5-concurrency/exercises/` 有 8 个练习/项目文件，包括 capstone mini runtime。
- `code/volumn_codes/vol5/`：9 个章节目录，22 个 .cpp 文件，约 16K 行代码。
- `changelogs/v0.3.0.md` 明确卷五全面重写，v0.4.0 增加练习手册。

## 代码覆盖度矩阵

| 章节 | 文章数 | 代码文件数 | 缺口 | 状态 |
|------|--------|-----------|------|------|
| ch00 并发基础 | 3 | 1 | 低 | ✅ 稳定 |
| ch01 线程生命周期 | 4 | 4 | 无 | ✅ 稳定 |
| ch02 互斥与同步 | 5 | 5 | 无 | ✅ 稳定 |
| ch03 原子与内存模型 | 5 | 4 | 低 | ✅ 稳定 |
| ch04 并发数据结构 | 4 | 3 | 中（02-thread-safe-containers 无代码） | ⚠️ |
| ch05 Future/ThreadPool | 4 | 1 | **高**（async、promise、线程池 43K 均无代码） | ❌ |
| ch06 异步I/O/协程 | 5 | 3 | **高**（event-loop 31K、echo-server 53K 无代码） | ❌ |
| ch07 Actor/Channel | 2 | 2 | 无 | ✅ 稳定 |
| ch08 调试测试性能 | 2 | 2 | 低 | ✅ 稳定 |
| ch09 分布式桥接 | 2 | 0 | 不补（概念导览，见下方决策） | ✅ 概念导览 |

## 已有资产维护决策

### M1: ch05 代码补齐（高优先级）

- 补 `std::async + future` 组合用法示例
- 补 `promise + packaged_task` 用法示例
- 补线程池完整示例（04-thread-pool.md 是全卷最大单篇 43K）
- 更新 `code/volumn_codes/vol5/ch05-future-task-threadpool/CMakeLists.txt`
- **教学边界**：线程池为教学简化版（基础任务队列 + 工作线程），不追求 work-stealing

### M2: ch06 代码补齐（高优先级）

- 补 event loop 基础示例（04-async-io-and-event-loop.md，31K）
- 补 coroutine echo server 完整项目（05-coroutine-echo-server.md，53K）
- **平台策略**：以 epoll（Linux）为主，不追求跨平台
- 更新 `code/volumn_codes/vol5/ch06-async-io-coroutine/CMakeLists.txt`

### M3: ch04 代码补齐（中优先级）

- 补 thread-safe containers 独立示例（02-thread-safe-containers.md 无对应代码）
- 更新 `code/volumn_codes/vol5/ch04-concurrent-data-structures/CMakeLists.txt`

### M4: Capstone 工程骨架

- 在 `code/volumn_codes/vol5/` 下创建 capstone 目录结构
- 提供 CMakeLists.txt 骨架
- 明确测试框架选择
- 编写 milestone 验收表（阶段 1/2/3 各做什么、怎么判定通过）

### M5: 文章-代码映射与双向链接

- 为每章建立”文章 → 示例代码 → 练习”映射表
- 在文章中增加对应代码目录的链接
- 在代码目录 README 中回链文章
- 为 exercises 增加推荐完成顺序和最小完成路径

### M6: ch09 定性为概念导览

- **决策**：ch09（分布式桥接）保持概念导览定位，不补代码，不引入外部依赖
- 在 ch09 文章中明确标注”本节为概念导览，不配可运行代码”
- 分布式实战内容留待卷八扩展，在 vol8 TODO 中记录交叉引用

### M7: 交叉卷链接

- ch06 文章开头增加”前置阅读：卷四协程章节”回链
- ch08 性能文章增加”深入阅读：卷六性能工程”前向链接
- ch03 文章增加”应用场景：卷八中断安全编程”前向链接

## 增量资产规划

> 前提：M1-M7 维护收尾完成后启动增量资产建设。
> 推进顺序：第一层（现在可教）→ 第二层（标准演进中）→ 远期候选。

### 第一层：现在可教、有真实工具支撑

#### I1: Hazard Pointer 完整实现（ch04 扩展，独立新文章）

- **位置**：`ch04-concurrent-data-structures/05-hazard-pointer-implementation.md`
- **依据**：ch04/03 已有 HP 概念级介绍 + 伪代码，读者停在”知道概念”到”能实现”之间。P2530 已进 C++26 草案。Folly 参考实现证明生产可用。
- **教学路径**：回顾 HP 概念 → hazard_pointer_obj_base + acquire/retire/release 完整实现 → 应用到 lock-free stack 内存回收 → 性能对比（HP vs 引用计数）
- **交付物**：1 篇文章 + 1 个完整代码示例
- **风险**：低。概念模型稳定，不依赖未实现的标准库。

#### I1b: Epoch-Based Reclamation（ch04 扩展，独立新文章）

- **位置**：`ch04-concurrent-data-structures/06-epoch-based-reclamation.md`
- **依据**：EBR 与 HP 并列为无锁内存回收两大方案，适合不同场景（HP 适合写少读多，EBR 适合批量回收）。ch04/03 已点名 EBR 但未实现。
- **教学路径**：EBR 原理（全局 epoch → 线程本地 epoch → deferred deletion）→ 完整实现 → 与 HP 的场景对比和选择指南
- **交付物**：1 篇文章 + 1 个代码示例
- **风险**：低。无标准化提案但工程实践成熟。

#### I2: 结构化并发设计原则（ch05 扩展，独立新文章）

- **位置**：`ch05-future-task-threadpool/05-structured-concurrency.md`
- **依据**：CppCon 2024-2025 连续两届核心主题。当前卷五教了线程、锁、atomic、future，但缺少”怎么组织一整棵并发任务树”的设计层讨论。不需要任何库支持，是设计原则。Herb Sutter 2009 年 EC #28 已预见此方向。
- **教学路径**：为什么裸线程/裸 future 不够 → 结构化并发四约束（local, nested, bounded, deterministic）→ 用 jthread + stop_token 实现最简结构化并发 → 引出 sender/receiver 的动机
- **交付物**：1 篇文章
- **风险**：低。纯设计原则，不依赖具体库。

#### I3: Sanitizer 组合策略（ch08 扩展）

- **位置**：扩展 `ch08/01-debugging-concurrency.md` 或新增独立文章
- **依据**：当前只覆盖 TSan + Helgrind。实际工程中 TSan + ASan 组合、Clang Thread Safety Analysis 的 `REQUIRES`/`ACQUIRED_BEFORE` 注解、UBSan 对并发中 UB 的捕获都是常用手段。
- **教学路径**：各 sanitizer 能力边界 → 组合使用策略 → CI 集成 → Clang 注解实战
- **交付物**：扩展现有文章或新增 1 篇
- **风险**：低。工具现成。

#### I4: 高级并发性能测试（ch08 扩展）

- **位置**：扩展 `ch08/02-concurrency-benchmarks.md`
- **依据**：当前覆盖 GBench + perf stat，方法论扎实，但缺少百分位延迟（p50/p99/p999）、微基准 vs 宏基准区分、置信区间、跨平台可重复性。这是从”学生水平”到”工程水平”的关键差距。
- **交付物**：扩展现有文章
- **风险**：低。

### 第二层：值得教但依赖标准演进，需用参考实现教学

#### I5: Sender/Receiver 实战入门（ch06 扩展，独立新文章）

- **位置**：`ch06-async-io-coroutine/06-sender-receiver-basics.md`
- **依据**：ch06/01 已显式提及 P2300 进 C++26 并做了铺垫。Herb Sutter 称之为”和 C++11 一样的范式转换”。Citadel Securities 已在生产使用。P2300 已并入 C++26 工作草案。
- **决策**：**引入 NVIDIA/stdexec 作为代码依赖**（header-only，零构建侵入）。
- **教学路径**：从 ch06/01 的三模型比较出发 → Sender/Receiver 三核心概念（scheduler, sender, receiver）→ 用 stdexec 写组合管道（`schedule | then | then | when_all`）→ 与协程互操作 → “从 future 到 sender”迁移思路
- **交付物**：1-2 篇文章 + stdexec 代码示例
- **风险**：中。API 可能在最终标准中有微调；文章需明确标注”基于 stdexec 参考实现，非标准库”。
- **needs standard-status verification**：P2300 最终 API 是否与 stdexec 当前版本一致。

#### I6: async_scope 与结构化并发实战（ch06 扩展，独立新文章，依赖 I5）

- **位置**：`ch06-async-io-coroutine/07-async-scope-and-structured.md`
- **依据**：P3149 是 sender/receiver 框架中实现”非顺序结构化并发”的组件——spawn 子任务 + join 等待。从”知道 sender/receiver”到”能在真实项目中用”的关键一步。
- **教学路径**：I2 的设计原则 + I5 的 sender 工具 → async_scope 的 spawn/join 语义 → 与 I2 的 jthread 方案对比
- **交付物**：1 篇文章
- **风险**：中。P3149 仍在活跃修订。
- **depends on**：I5 完成后。

### 远期候选：只做路线卡，等标准或生态成熟后再评估

#### I7: Coroutine Task Type（P3552）

- **位置**：ch06 协程部分的远期扩展
- **依据**：C++20 给了协程语言特性但没给标准 task 类型。P3552 目标 C++26，仍在提案阶段。卷五 ch06 已教手写 Task<T>，未来需覆盖”手写的和标准的有什么关系”。
- **当前动作**：不投入资源。在 ch06 文章末尾加一条”C++26 标准库可能提供 std::task，参见 P3552”的前瞻注释。
- **needs standard-status verification**

#### I8: RCU 概念导览（ch04 远期扩展）

- **位置**：ch04 新增概念导览小节（类似 ch09 分布式的处理方式）
- **依据**：P2545 提案中，未确认进入 C++26 CD。无标准库实现。与 HP/EBR 并列的第三种内存回收方案，在 Linux 内核中广泛使用。
- **当前动作**：不投入资源。未来可做概念导览。

#### I9: Atomic Reduction Operations（P3111）

- **位置**：ch03 atomic patterns 的远期扩展
- **依据**：P3111 活跃提案 R8。一类新的原子读改写操作（不返回旧值），用于并行归约。
- **当前动作**：不投入资源。

#### I10: 并发模式选择参考卡

- **位置**：卷五附录或 index.md
- **依据**：横跨 ch02-ch07 的”何时用什么”决策树。
- **当前动作**：等 M1-M7 + I1-I6 全部完成后作为”毕业总结”。

#### I11: memory_order_consume 状态更新

- **依据**：ch03/02 已正确标注 consume 为”建议不使用”、C++26 正式弃用。无需增量内容。
- **当前动作**：已关闭。如果未来 consume 被移除或替代方案出现，再重新评估。

## 增量资产对文章结构的影响

```
ch04 并发数据结构
  ├── 现有 4 篇（稳定，M3 待补代码）
  ├── [第一层] 05-hazard-pointer-implementation.md（I1）
  └── [第一层] 06-epoch-based-reclamation.md（I1b）

ch05 Future/Task/ThreadPool
  ├── 现有 4 篇（M1 待补代码）
  └── [第一层] 05-structured-concurrency.md（I2）

ch06 异步I/O/协程
  ├── 现有 5 篇（M2 待补代码）
  ├── [第二层] 06-sender-receiver-basics.md（I5）
  └── [第二层] 07-async-scope-and-structured.md（I6）

ch08 调试/测试/性能
  ├── 现有 2 篇（稳定）
  ├── [第一层] sanitizer 组合策略扩展（I3）
  └── [第一层] 高级 benchmark 扩展（I4）

远期
  ├── I7: Coroutine Task Type（ch06 远期）
  ├── I8: RCU 概念导览（ch04 远期）
  ├── I9: Atomic Reduction（ch03 远期）
  └── I10: 并发模式选择参考卡（全卷毕业总结）
```

## Development Points

- **阶段一（维护收尾）**：先完成 M1-M7，补齐”能跑、能测、能验收”。
- **阶段二（第一层增量）**：I1 → I1b → I2 → I3 → I4 并行推进。
- **阶段三（第二层增量）**：I5 → I6 顺序推进，依赖 stdexec。
- **远期**：I7-I10 等标准或生态成熟后再评估。
- M1/M2 是阶段一最高价值收尾点。
- I2 结构化并发是阶段二最高价值增量——它为 I5/I6 铺垫动机。

## Old TODO Merge

- `204-vol5-concurrency-outline.md`

## Acceptance

- [ ] M1: ch05 有 3+ 个新代码文件，覆盖 future/promise/threadpool
- [ ] M2: ch06 有 2 个新代码文件，覆盖 event-loop 和 echo-server
- [ ] M3: ch04 有 1 个新代码文件，覆盖 thread-safe-containers
- [ ] M4: capstone 有代码目录、CMakeLists.txt、milestone 验收表
- [ ] M5: 每章有文章↔代码映射，exercises 有推荐完成路径
- [ ] M6: ch09 文章标注”概念导览”
- [ ] M7: 三个交叉卷链接到位
- [ ] I1: ch04 有 HP 完整实现文章 + 代码
- [ ] I1b: ch04 有 EBR 完整实现文章 + 代码
- [ ] I2: ch05 有结构化并发独立文章
- [ ] I3: ch08 有 sanitizer 组合策略扩展
- [ ] I4: ch08 有高级 benchmark 方法论扩展
- [ ] I5: ch06 有 sender/receiver 实战文章 + stdexec 代码
- [ ] I6: ch06 有 async_scope 实战文章（依赖 I5）
- [ ] I7-I10: 远期候选已记录，当前不投入资源
