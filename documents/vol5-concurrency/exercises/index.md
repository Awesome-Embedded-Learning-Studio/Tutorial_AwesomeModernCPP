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
