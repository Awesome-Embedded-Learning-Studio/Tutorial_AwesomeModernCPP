---
id: 025
title: "FreeRTOS + C++ 集成教程"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["024"]
blocks: ["026", "027"]
estimated_effort: large
---

# FreeRTOS + C++ 集成教程

## 目标
讲解 FreeRTOS 在 STM32F1 上的移植与 C++ 封装。通过设计现代 C++ 包装类（Task、Mutex、Queue、Semaphore），展示如何在嵌入式项目中优雅地使用 FreeRTOS。包含优先级设计最佳实践和与教学 RTOS 的对比分析。

## 验收标准
- [ ] FreeRTOS 在 STM32F103 上的完整移植步骤文档
- [ ] C++ 包装类实现：Task、Mutex、Queue、Semaphore、EventGroup
- [ ] 包装类使用 RAII 管理资源生命周期
- [ ] 任务创建与管理的完整示例（静态/动态创建）
- [ ] 优先级设计指南和常见反模式文档
- [ ] 与教学 RTOS 的逐模块对比分析章节
- [ ] 至少 3 个完整的多任务示例工程
- [ ] 中断安全（ISR 版本 API）的使用说明

## 实施说明
本教程承接教学 RTOS，转向实际项目中使用最广泛的 FreeRTOS。

**内容结构规划：**

1. **FreeRTOS 概述与移植** — FreeRTOS 体系结构总览（任务、队列、定时器、事件）。在 STM32F103 上的移植步骤：获取源码、配置 FreeRTOSConfig.h、修改启动文件（PendSV/SysTick/SVC 向量）、Heap 内存模型选择（heap_1 到 heap_5）。使用 STM32CubeMX 快速集成。

2. **C++ 包装类设计** — 设计原则：RAII 资源管理、零开销抽象、异常安全（嵌入式禁用异常下的替代方案）。`Task` 类：封装 `xTaskCreate`/`xTaskCreateStatic`，支持 lambda 和成员函数。`Mutex` 类：RAII 析构释放，`std::unique_lock` 风格的 `ScopedLock` 守卫。`Queue` 类：类型安全的消息队列，模板化消息类型。`Semaphore` 类：二值/计数信号量统一接口。`EventGroup` 类：事件组同步。

3. **任务管理实战** — 静态 vs 动态任务创建的选择。任务优先级分配策略（Rate Monotonic 分析基础）。任务间通信模式选择指南（何时用队列、何时用信号量、何时用事件组）。任务删除和资源清理的安全流程。

4. **优先级设计与调试** — 优先级分配最佳实践。常见问题：优先级反转、优先级置顶、死锁。FreeRTOS 运行时统计（任务执行时间、栈使用量）。使用 trace 工具（Tracealyzer / SystemView）分析任务行为。

5. **与教学 RTOS 对比** — 逐模块对比：调度器实现差异、同步原语 API 对比、内存管理策略对比、定时器服务实现差异。讨论 FreeRTOS 的设计权衡和适用场景。

6. **综合项目** — 多传感器数据采集系统：多个采集任务 + 数据处理任务 + 通信任务，通过队列和信号量协调。

## 涉及文件
- documents/embedded/rtos/03-freertos/index.md
- documents/embedded/rtos/03-freertos/01-freertos-porting.md
- documents/embedded/rtos/03-freertos/02-cpp-wrapper.md
- documents/embedded/rtos/03-freertos/03-task-management.md
- documents/embedded/rtos/03-freertos/04-priority-design.md
- documents/embedded/rtos/03-freertos/05-comparison.md
- documents/embedded/rtos/03-freertos/06-project-example.md
- codes/embedded/rtos/freertos-cpp/ (配套代码)

## 参考资料
- FreeRTOS 官方文档 (freertos.org)
- 《Mastering the FreeRTOS Real Time Kernel》— Richard Barry
- FreeRTOS Cortex-M 移植指南 (FreeRTOS\Source\portable\GCC\ARM_CM3)
- 《Real-Time Design Patterns》— Bruce Powel Douglass
- STM32CubeMX FreeRTOS 配置指南
