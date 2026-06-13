---
title: "LED 点灯：从 C 到 C++ 的演进"
description: "以点亮 PC13 上的 LED 为线索，从 C 宏驱动一路重构到 C++23 模板与零开销抽象"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# LED 点灯：从 C 到 C++ 的演进

> 一盏 LED，一条完整的现代 C++ 重构之路——从 HAL 寄存器操作到模板与 `if constexpr` 的编译期优化。

## 动机

- [第6篇：从点亮第一盏 LED 开始](01-motivation-and-overview.md) — 我们为什么要用现代 C++ 写 STM32

## 硬件基础

- [第7篇：GPIO 到底是什么](02-what-is-gpio.md) — 通用输入输出的前世今生
- [第8篇：推挽、开漏与 PC13](03-output-modes-and-pc13.md) — LED 点亮的硬件秘密

## HAL 操作

- [第9篇：HAL 时钟使能](04-hal-gpio-clock.md) — 不开时钟，外设就是一坨睡死的硅
- [第10篇：HAL_GPIO_Init](05-hal-gpio-init.md) — 把引脚配置告诉芯片的仪式
- [第11篇：HAL_GPIO_WritePin 与 TogglePin](06-hal-gpio-output.md) — 让引脚动起来

## C 宏时代

- [第12篇：C 宏时代的 LED 驱动](07-c-macro-led-implementation.md) — 能跑但不优雅

## C++ 重构演进

- [第13篇：第一次重构 —— enum class](08-cpp-enum-class-revolution.md) — 取代宏，类型安全的开始
- [第14篇：第二次重构 —— 模板](09-cpp-template-gpio.md) — 编译时绑定端口和引脚
- [第15篇：第三次重构 —— if constexpr](10-cpp-if-constexpr-clock.md) — 让时钟使能在编译时自动选对
- [第16篇：第四次重构 —— LED 模板](11-cpp-led-template.md) — 从通用 GPIO 到专用抽象
- [第17篇：C++23 特性收尾](12-cpp23-attributes-and-features.md) — 属性、链接与零开销抽象的最终证明

## 总结

- [第18篇：常见坑位与实战练习](13-pitfalls-and-exercises.md) — 把 LED 玩出花样来
