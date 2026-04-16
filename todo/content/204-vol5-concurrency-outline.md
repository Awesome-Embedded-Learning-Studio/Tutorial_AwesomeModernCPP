---
id: "204"
title: "卷五：并发编程全史 — 全部章节大纲与文章规划"
category: content
priority: P2
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200", "201"]
blocks: []
estimated_effort: large
---

# 卷五：并发编程全史 — 全部章节大纲与文章规划

## 总览

- **卷名**：vol5-concurrency
- **难度范围**：intermediate → advanced
- **预计文章数**：25-30 篇
- **前置知识**：卷一 + 卷二
- **C++ 标准覆盖**：C++11(C++20 增强)
- **目录位置**：`documents/vol5-concurrency/`

## 章节大纲

### ch00：从单线程到多线程的思维转变

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 00-01 | 01-why-concurrency.md | 为什么需要并发 | 并发 vs 并行、Amdahl 定律、吞吐量 vs 延迟 | 分析并发收益 |
| 00-02 | 02-concurrency-problems.md | 并发基本问题 | 竞态条件、死锁、活锁、数据竞争 vs 竞态 | 识别并发问题 |

### ch01：线程原语

- **预计篇数**：4

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 01-01 | 01-std-thread.md | std::thread | 创建/管理/等待、参数传递、线程 ID | 多线程任务 |
| 01-02 | 02-mutex-lock.md | mutex 与锁 | mutex/recursive_mutex/timed_mutex、lock_guard/unique_lock/scoped_lock | 线程安全设计 |
| 01-03 | 03-condition-variable.md | condition_variable | 等待/通知、虚假唤醒、等待谓词 | 生产者-消费者 |
| 01-04 | 04-shared-mutex.md | 读写锁与 shared_mutex | shared_mutex(C++17)、读写场景、性能对比 | 读多写少优化 |

### ch02：原子操作与内存序

- **预计篇数**：4

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 02-01 | 01-atomic-operations.md | atomic 操作 | atomic<T>、load/store/compare_exchange、常见原子操作 | 无锁计数器 |
| 02-02 | 02-memory-ordering.md | 内存序详解 | relaxed/acquire/release/acq_rel/seq_cst、happens-before | 理解内存模型 |
| 02-03 | 03-fence-barrier.md | 内存屏障与 fence | atomic_thread_fence、compiler barrier、CPU barrier | 屏障应用 |
| 02-04 | 04-atomic-patterns.md | 原子操作模式 | SeqLock、Double-Checked Locking、RCU 简介 | 原子模式实现 |

### ch03：无锁编程

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 03-01 | 01-lock-free-basics.md | 无锁编程基础 | CAS 循环、ABA 问题、内存回收问题 | 无锁栈 |
| 03-02 | 02-lock-free-queue.md | 无锁队列 | Michael-Scott 队列、生产者-消费者无锁实现 | 无锁队列实现 |
| 03-03 | 03-hazard-pointer.md | Hazard Pointer 与内存回收 | Hazard Pointer、Epoch-based reclamation、风险分析 | 安全内存回收 |

### ch04：并发 STL 使用

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 04-01 | 01-thread-safe-containers.md | 线程安全容器设计 | 并发 vector/map/queue、细粒度锁、无锁方案 | 设计并发容器 |
| 04-02 | 02-parallel-stl.md | 并行 STL 算法 | C++17 并行策略、并行排序/变换、可扩展性 | 并行算法测试 |

### ch05：async/future

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 05-01 | 01-std-async.md | std::async | launch policy、future.get()、异常传播 | 异步任务 |
| 05-02 | 02-future-promise.md | future 与 promise | promise 设置值、shared_future、then(C++26?) | 值传递链 |
| 05-03 | 03-when-all-any.md | 组合 future | when_all/when_any (C++26)、parallel algorithms vs future | 并行任务组合 |

### ch06：协程与异步 I/O

- **预计篇数**：4

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 06-01 | 01-boost-asio-basics.md | Boost.Asio 基础 | io_context、异步模型、proactor 模式 | 异步定时器 |
| 06-02 | 02-asio-networking.md | Asio 网络编程 | TCP/UDP 异步、buffer 管理、错误处理 | 异步 Echo 服务器 |
| 06-03 | 03-coroutine-async.md | 协程 + Asio | co_await 集成、awaitable 操作符、协程调度 | 协程网络应用 |
| 06-04 | 04-libuv-intro.md | libuv 简介 | 事件循环、异步文件/网络、与 Asio 对比 | libuv 项目 |

### ch07：jthread 与停止令牌

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 07-01 | 01-jthread.md | std::jthread (C++20) | 自动 join、中断请求、cooperative cancellation | 可中断线程 |
| 07-02 | 02-stop-token.md | stop_token/stop_source | 停止机制、回调注册、与 condition_variable 结合 | 优雅停止 |

### ch08：Actor 模型

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 08-01 | 01-actor-basics.md | Actor 模型基础 | 消息传递、无共享、actor 生命周期、与 CSP 对比 | actor 框架设计 |
| 08-02 | 02-actor-practice.md | Actor 实战 | actor 框架实现、消息路由、错误处理策略 | 完整 actor 应用 |

### ch09：分布式概述

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 09-01 | 01-rpc-basics.md | RPC 基础 | gRPC 概述、Protocol Buffers、同步/异步调用 | gRPC 服务 |
| 09-02 | 02-consistency.md | 一致性模型 | CAP 定理、最终一致性、共识算法简介 | 分布式认知 |

## 练习与项目

### 文章末尾练习
- 每篇 3-5 道，重点关注并发正确性和性能

### 实战项目
1. **线程安全任务队列**：结合 mutex/condition_variable/atomic
2. **协程 HTTP 服务器**：协程 + Boost.Asio
3. **无锁消息队列**：无锁编程实战

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| core-embedded-cpp/ch10/* (6篇) | ch01-ch03 | 通用化重写 |
| cpp-features/coroutines/03-echo-server.md | ch06 | 融入协程+Asio |
