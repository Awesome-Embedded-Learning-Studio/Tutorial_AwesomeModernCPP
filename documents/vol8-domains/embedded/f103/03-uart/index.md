---
title: "UART 串口通信"
description: "从硬件协议到中断驱动接收，用现代 C++ 构建 STM32 UART 驱动"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# UART 串口通信

> 从串口协议原理到中断驱动接收，用现代 C++23 构建 STM32 UART 驱动与命令处理器。

## 阶段一：动机

- [第31篇：从按钮到串口](01-motivation-and-overview.md) — 为什么 UART 是嵌入式通信的基石

## 阶段二：硬件基础

- [第32篇：UART 协议详解](02-uart-protocol-basics.md) — 没有时钟线怎么同步
- [第33篇：STM32 USART 外设](03-stm32-usart-peripheral.md) — 芯片内部的串口引擎

## 阶段三：HAL + 阻塞 I/O

- [第34篇：HAL 初始化与发送](04-hal-uart-init-and-send.md) — 让芯片开口说话
- [第35篇：printf 重定向与阻塞接收](05-printf-redirect-and-blocking-receive.md) — 让芯片用 printf 说话，也学会倾听

## 阶段四：中断驱动

- [第36篇：中断基础与 NVIC](06-interrupt-fundamentals-and-nvic.md) — 让硬件主动通知 CPU
- [第37篇：无锁环形缓冲区](07-circular-buffer-lock-free-spsc.md) — ISR 与主循环之间的安全通道
- [第38篇：UART IRQ 处理与回调](08-uart-irq-handler-and-callback.md) — 中断接收的完整拼图

## 阶段五：C++ 抽象

- [第39篇：std::expected 错误处理](09-cpp-expected-and-error-handling.md) — 嵌入式中比异常更好的选择
- [第40篇：UART 驱动模板](10-cpp-uart-driver-template.md) — 零大小抽象与编译时分发
- [第41篇：Concepts + UartManager](11-cpp-concepts-and-uart-manager.md) — 类型安全的组装
- [第42篇：命令处理器与完整走读](12-command-processor-and-main-walkthrough.md) — 从串口输入到 LED 控制

## 阶段六：总结

- [第43篇：常见坑位与练习](13-pitfalls-and-exercises.md) — 把 UART 玩出花样来
