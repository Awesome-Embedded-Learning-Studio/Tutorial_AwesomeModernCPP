---
title: 'Volume 5: Exercise System'
description: 'The complete lab exercise system for Volume 5 Concurrent Programming:
  from thread lifecycle to a mini concurrent runtime'
translation:
  source: documents/vol5-concurrency/exercises/index.md
  source_hash: cc98768bb5e41d150c8e8ae3cc9c994205e777fe01d65416b6521984fb5b0b34
  translated_at: '2026-05-26T11:49:15.098444+00:00'
  engine: anthropic
  token_count: 600
---
# Volume 5 Exercise System

The exercises in Volume 5 are divided into three tiers, progressing from easy to hard, and from localized components to full systems.

**Tier 1** consists of inline exercises attached to the end of each chapter. These verify individual concepts—such as identifying a data race, predicate waiting with `condition_variable`, or selecting the right `atomic` memory order. Each exercise takes 10–20 minutes to complete and requires no additional project setup.

**Tier 2** comprises chapter Labs, which are the eight Labs listed on this page. Each Lab is a runnable mini-system broken down into 3–5 milestones. Every milestone has a clear interface, Catch2 tests, and acceptance criteria. Upon completion, learners should have a reusable concurrent component, rather than a collection of scattered demos.

**Tier 3** is the end-of-volume Capstone project, which strings together all the components from the previous Labs to form a mini concurrent runtime.

## Lab Overview

| Lab | Project Name | Chapters Covered | Suggested Duration | Difficulty | Prerequisite Labs |
|-----|--------------|------------------|--------------------|------------|-------------------|
| [Lab 0](00-thread-lifecycle.md) | Thread Lifecycle | ch00–ch01 | 4–6h | intermediate | None |
| [Lab 1](01-bounded-queue.md) | Bounded Queue & Sync Primitives | ch02–ch04 | 8–12h | intermediate | Lab 0 |
| [Lab 2](02-atomic-spsc.md) | Atomic Metrics & SPSC Ring Buffer | ch03–ch04 | 6–8h | intermediate | Lab 0 |
| [Lab 2.5](02.5-debugging.md) | Concurrency Debugging | ch08 | 3–4h | intermediate | Lab 0–2 |
| [Lab 3](03-thread-pool.md) | Production-style Thread Pool | ch05 | 10–14h | advanced | Lab 0–1 |
| [Lab 4](04-coroutine-scheduler.md) | Coroutine Scheduler & Event Loop | ch06 | 12–16h | advanced | Lab 3 |
| [Lab 5](05-channel-actor.md) | Channel or Actor Runtime | ch07 | 8–12h | advanced | Lab 1, 4 |
| [Capstone](06-capstone-mini-runtime.md) | Mini Concurrent Runtime | ch08–ch09 | 8–12h | advanced | Lab 0–5 |

The minimum requirement to cover the core competency curve of Volume 5 is completing **Lab 0, Lab 1, Lab 3, and the Capstone** (approximately 30–45 hours). Completing all Labs in full takes roughly 60–85 hours.

## Lab Dependencies

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

## Environment Setup

All Labs share the following environment requirements:

- **Compiler**: GCC 12+ or Clang 15+ (C++20 with full coroutine support)
- **CMake**: 3.14+
- **Testing framework**: Catch2 v3 (header-only, fetched via FetchContent)
- **TSan**: compiler flag `-fsanitize=thread -g`
- **Platform**: Linux or WSL2 (required for the epoll portion of Lab 4)
- **Valgrind** (optional, required for helgrind in Lab 2.5)

Each Lab article begins with a specific `CMakeLists.txt` template that can be used directly.
