---
title: "按键输入：消抖、状态机与类型安全"
description: "从 GPIO 输入电路到 7 状态消抖状态机，再用 variant/concepts 把按键代码重构成类型安全的形态"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# 按键输入：消抖、状态机与类型安全

> 按键比 LED 难——硬件抖动、非阻塞消抖、状态机，再加上 C++ 的类型安全重构。

## 动机

- [第19篇：从输出到输入](01-from-output-to-input.md) — 为什么按钮比 LED 难

## 硬件基础

- [第20篇：GPIO 输入模式内部电路](02-gpio-input-circuits.md) — 芯片是如何 "听" 到外部信号的
- [第21篇：按钮电路与机械抖动](03-button-hardware-and-bounce.md) — 真实世界的信号长什么样

## HAL 与轮询

- [第22篇：HAL GPIO 输入 API](04-hal-gpio-input.md) — 怎么用代码读到按钮状态
- [第23篇：C 语言轮询按钮](05-c-polling-button.md) — 第一次亲手让按钮控制 LED

## 消抖

- [第24篇：非阻塞消抖](06-non-blocking-debounce.md) — 不让 CPU 停下来等
- [第25篇：7 状态消抖状态机](07-debounce-state-machine.md) — 本系列的核心

## C++ 重构演进

- [第26篇：`enum class` 重构按钮代码](08-cpp-enum-class-button.md) — 类型安全的输入
- [第27篇：`std::variant` 事件 + `std::visit` 分发](09-cpp-variant-and-visit.md) — 类型安全的 "发生了什么"
- [第28篇：Button 模板类设计](10-cpp-template-button.md) — 把一切交给编译器
- [第29篇：Concepts 约束回调](11-cpp-concepts-callback.md) — 完整代码走读

## 中断与总结

- [第30篇：EXTI 中断](12-exti-interrupt-and-exercises.md) — 坑位与练习
