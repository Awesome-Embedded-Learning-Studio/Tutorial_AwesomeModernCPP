---
id: 024
title: "自实现教学 RTOS 教程"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["023"]
blocks: ["025"]
estimated_effort: epic
---

# 自实现教学 RTOS 教程

## 目标
在协作调度器基础上实现抢占式 RTOS，帮助读者深入理解 RTOS 内部原理。从零构建一个教学用 RTOS，覆盖抢占调度、优先级算法、任务间通信、内存管理和定时器服务等核心概念。读者完成后应能理解商业 RTOS 的设计动机和实现细节。

## 验收标准
- [ ] 完成教学 RTOS 核心代码实现，代码可在 STM32F1 上运行
- [ ] SysTick 中断驱动的抢占式调度器实现并验证
- [ ] 优先级调度算法（优先级继承、同优先级时间片轮转）实现并测试
- [ ] 信号量（二值/计数）实现，含优先级反转防护演示
- [ ] 互斥量实现，含所有权语义和优先级继承协议
- [ ] 消息队列实现，支持阻塞式收发
- [ ] 静态内存池管理器实现（多种块大小）
- [ ] 软件定时器服务实现（单次/周期）
- [ ] 教程文档包含完整的设计决策说明和代码注释
- [ ] 每个模块配备独立的测试示例工程
- [ ] 与协作调度器的对比章节，说明演进动机

## 实施说明
本教程是嵌入式 RTOS 系列的核心篇章。在 023 协作调度器的基础上，逐步引入抢占机制。

**内容结构规划：**

1. **从协作到抢占** — 回顾协作调度器的局限，引入中断驱动的抢占模型。讲解 PendSV 和 SysTick 的角色，展示上下文保存/恢复的完整汇编代码。

2. **抢占式调度核心** — 实现 `Scheduler` 类，使用 SysTick 定时触发调度。讲解任务控制块（TCB）设计、就绪队列管理、上下文切换汇编实现（ARM Cortex-M3 的 MSP/PSP 双栈模型）。

3. **优先级调度算法** — 实现多优先级就绪队列（位图 + 链表 O(1) 查找）。实现同优先级时间片轮转。讨论优先级反转问题并演示经典案例。

4. **同步原语** — 信号量（`Semaphore`）：二值信号量用于中断同步、计数信号量用于资源管理。互斥量（`Mutex`）：所有权语义、递归锁、优先级继承协议的实现。条件变量基础概念。

5. **消息传递** — 消息队列（`MessageQueue`）：固定大小消息的阻塞式收发、队列满/空时的任务阻塞与唤醒、ISR 安全版本。

6. **内存管理** — 静态内存池（`MemoryPool`）：多种块大小的池分配、碎片分析、`new`/`delete` 运算符重定向到内存池。

7. **定时器服务** — 软件定时器（`SoftwareTimer`）：基于 Tick 的定时器链表、单次触发与周期模式、定时器命令队列。

8. **综合示例** — 多任务协作的完整项目：生产者-消费者模型、传感器采集-处理-显示流水线。

**教学重点：** 每个模块先讲解"为什么需要"，再讲解"如何实现"，最后展示"真实 RTOS 怎么做"。

## 涉及文件
- documents/embedded/rtos/02-teaching-rtos/index.md
- documents/embedded/rtos/02-teaching-rtos/01-preemptive-scheduling.md
- documents/embedded/rtos/02-teaching-rtos/02-priority-scheduling.md
- documents/embedded/rtos/02-teaching-rtos/03-synchronization.md
- documents/embedded/rtos/02-teaching-rtos/04-message-queue.md
- documents/embedded/rtos/02-teaching-rtos/05-memory-management.md
- documents/embedded/rtos/02-teaching-rtos/06-timer-service.md
- documents/embedded/rtos/02-teaching-rtos/07-integration-example.md
- codes/embedded/rtos/teaching-rtos/ (配套代码)

## 参考资料
- 《Mastering the FreeRTOS Real Time Kernel》— FreeRTOS 官方教材
- ARM Cortex-M3 Technical Reference Manual (DDI 0337) — PendSV/SysTick 章节
- 《Real-Time Concepts for Embedded Systems》— Qing Li, Caroline Yao
- FreeRTOS 源码 (queue.c / tasks.c / timers.c) — 实现参考
- Brian Adamson 的 TNeo RTOS 源码 — 简洁的教学实现参考
