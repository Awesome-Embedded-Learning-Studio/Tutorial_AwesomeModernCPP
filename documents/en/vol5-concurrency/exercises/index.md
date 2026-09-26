---
title: 'Volume 5: Hands-On Labs'
description: 'The complete hands-on lab series for Volume 5 concurrent programming:
  from thread lifecycle to a Mini Concurrent Runtime'
translation:
  source: documents/vol5-concurrency/exercises/index.md
  source_hash: cc98768bb5e41d150c8e8ae3cc9c994205e777fe01d65416b6521984fb5b0b34
  translated_at: '2026-09-26T09:17:28+00:00'
  engine: anthropic
  token_count: 1000
---
# Volume 5: Hands-On Labs

The exercises in Volume 5 come in three tiers, going from easier to harder and from individual building blocks to complete systems.

**Tier one** is made up of the small in-article exercises attached to the end of each article, which verify a single knowledge point — for example, recognizing a data race, predicate-based waiting on a condition_variable, or picking the right memory order for atomics. Each exercise takes 10–20 minutes and needs no extra project scaffolding.

**Tier two** is the chapter-scale assignments (Labs): the 8 labs listed on this page. Each lab is a runnable mini system broken into 3–5 milestones, and every milestone has a well-defined interface, Catch2 tests, and acceptance criteria. When you finish one, you should walk away with a reusable concurrency component, not a pile of scattered demos.

**Tier three** is the end-of-volume capstone project (Capstone), which wires the components from all the earlier labs together into a mini concurrent runtime.

## Lab Overview

| Lab | Project Name | Chapters Covered | Suggested Time | Difficulty | Prerequisite Labs |
|-----|--------------|------------------|----------------|------------|-------------------|
| [Lab 0](00-thread-lifecycle.md) | Thread Lifecycle | ch00–ch01 | 4–6h | intermediate | None |
| [Lab 1](01-bounded-queue.md) | Bounded Queue & Sync Primitives | ch02–ch04 | 8–12h | intermediate | Lab 0 |
| [Lab 2](02-atomic-spsc.md) | Atomic Metrics & SPSC Ring Buffer | ch03–ch04 | 6–8h | intermediate | Lab 0 |
| [Lab 2.5](02.5-debugging.md) | Concurrency Debugging | ch08 | 3–4h | intermediate | Lab 0–2 |
| [Lab 3](03-thread-pool.md) | Production-style Thread Pool | ch05 | 10–14h | advanced | Lab 0–1 |
| [Lab 4](04-coroutine-scheduler.md) | Coroutine Scheduler & Event Loop | ch06 | 12–16h | advanced | Lab 3 |
| [Lab 5](05-channel-actor.md) | Channel or Actor Runtime | ch07 | 8–12h | advanced | Lab 1, 4 |
| [Capstone](06-capstone-mini-runtime.md) | Mini Concurrent Runtime | ch08–ch09 | 8–12h | advanced | Lab 0–5 |

The bare minimum — completing **Lab 0, Lab 1, Lab 3, and the Capstone** (about 30–45 hours) — already covers the core competency curve of Volume 5. Working through every lab in full takes roughly 60–85 hours.

## Lab Dependencies

```cpp
Lab 0 (joining_thread / thread_guard)
  │
  ├─→ Lab 1 (BoundedBlockingQueue, ConcurrentCache)
  │     │
  │     ├─→ Lab 2 (SpscRingBuffer)        ─ independent implementation, does not depend on Lab 1
  │     │
  │     ├─→ Lab 2.5 (Debugging Lab)        ─ reuses code from Labs 0–2 as diagnostic material
  │     │
  │     └─→ Lab 3 (ThreadPool)             ─ reuses Lab 1's BoundedBlockingQueue
  │           │
  │           ├─→ Lab 4 (Coroutine Scheduler) ─ shutdown semantics follow Lab 3
  │           │     │
  │           │     └─→ Lab 5 (Channel/Actor) ─ can reuse Lab 1's queue
  │           │
  │           └─→ Capstone (Mini Runtime)   ─ composes the components of Labs 0–5
```

## Environment Setup

All labs share the following environment requirements:

- **Compiler**: GCC 12+ or Clang 15+ (C++20, full coroutine support)
- **CMake**: 3.14+
- **Test framework**: Catch2 v3 (header-only, fetched via FetchContent)
- **TSan**: compile flags `-fsanitize=thread -g`
- **Platform**: Linux or WSL2 (required for the epoll part of Lab 4)
- **Valgrind** (optional; needed for helgrind in Lab 2.5)

Every lab article opens with a concrete CMakeLists.txt template that you can use directly.
