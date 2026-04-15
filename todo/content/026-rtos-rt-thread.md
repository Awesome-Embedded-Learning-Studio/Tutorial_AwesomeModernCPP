---
id: 026
title: "RT-Thread 集成教程"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["025"]
blocks: []
estimated_effort: large
---

# RT-Thread 集成教程

## 目标
讲解 RT-Thread 国产 RTOS 与 C++ 的集成使用。覆盖 RT-Thread Nano 版本在 STM32F1 上的移植、C++ 对接方法、设备驱动框架的使用，以及 FinSH 控制台的集成。展示 RT-Thread 在中文嵌入式社区中的生态优势。

## 验收标准
- [ ] RT-Thread Nano 在 STM32F103 上的完整移植文档
- [ ] C++ 与 RT-Thread 内核 API 的对接方案
- [ ] 设备驱动框架使用教程（GPIO/UART/SPI/I2C）
- [ ] FinSH 控制台集成与自定义命令示例
- [ ] 与 FreeRTOS 的特性对比表
- [ ] 至少 2 个完整的示例工程
- [ ] 中文社区资源索引

## 实施说明
RT-Thread 是国产开源 RTOS，在中文嵌入式社区有广泛用户基础。本教程聚焦 Nano 版本（轻量级）在 STM32F1 上的使用。

**内容结构规划：**

1. **RT-Thread 概述** — RT-Thread 体系架构（Nano 版 vs 标准版 vs Smart 版）。内核特性：线程管理、IPC 机制、内存管理、设备 I/O。与 FreeRTOS 的定位差异和生态对比。

2. **Nano 版移植** — 获取 RT-Thread Nano 源码（rt-thread/nano）。BSP 配置：rtconfig.h 关键配置项。board.h 板级配置（时钟、堆大小）。启动流程修改：entry 函数、SystemInit 对接。链接脚本调整。使用 Env 工具（menuconfig/scons）的流程。

3. **C++ 对接** — RT-Thread 的 C++ 支持（RT-Thread 对 C++ 的线程安全支持）。封装 RT-Thread 内核对象为 C++ 类：`Thread`、`Mutex`、`Semaphore`、`Mailbox`、`MessageQueue`。全局构造函数调用保障（链接脚本中 `.init_array` 段处理）。

4. **设备驱动框架** — RT-Thread 设备模型：设备注册、打开、读写、控制。PIN 设备使用（GPIO 操作）。Serial 设备使用（UART 通信）。SPI/I2C 设备挂载。自定义设备驱动开发入门。

5. **FinSH 控制台** — FinSH 的工作原理（基于串口的命令行）。内置命令使用。自定义命令导出（`MSH_CMD_EXPORT`）。在 FinSH 中调用 C++ 函数。使用 FinSH 进行运行时调试。

6. **综合示例** — 基于 RT-Thread 的传感器采集项目：多线程采集 + FinSH 调试 + 设备驱动。

## 涉及文件
- documents/embedded/rtos/04-rt-thread/index.md
- documents/embedded/rtos/04-rt-thread/01-overview.md
- documents/embedded/rtos/04-rt-thread/02-nano-porting.md
- documents/embedded/rtos/04-rt-thread/03-cpp-integration.md
- documents/embedded/rtos/04-rt-thread/04-device-framework.md
- documents/embedded/rtos/04-rt-thread/05-finsh-console.md
- documents/embedded/rtos/04-rt-thread/06-project-example.md
- codes/embedded/rtos/rtthread-cpp/ (配套代码)

## 参考资料
- RT-Thread 官方文档 (rt-thread.org)
- RT-Thread 编程指南
- RT-Thread Nano 移植指南
- 《RT-Thread 内核实现与应用开发实战》— 野火
- RT-Thread GitHub 仓库 (RT-Thread/rt-thread)
