---
title: "平台教程标题"
series: stm32f1|esp32|rp2040
chapter: XX
section: XX
# 标签体系（四维分类）
tags:
  # platform（必填 1 个）
  - stm32f1
  # topic（必填 ≥1 个）: cpp-modern | peripheral | rtos | debugging | toolchain | architecture
  - peripheral
  # difficulty（必填 1 个）: beginner | intermediate | advanced
  - beginner
  # peripheral（可选）: gpio | uart | spi | i2c | adc | timer | pwm | dma | can
  - gpio
difficulty: beginner|intermediate|advanced
platform: stm32f1  # stm32f1 | stm32f4 | esp32 | rp2040 | host
cpp_standard: "C++XX"
created: YYYY-MM-DD
author: charliechen
hardware:
  platform: STM32F103C8T6
  board: Blue Pill
  peripherals:
    # 使用标准词汇：gpio | uart | spi | i2c | adc | timer | pwm | dma | can
    - gpio
---

# 教程标题

## 本章目标

一句话说明这一章要解决什么问题。

## 硬件原理

只讲这一章对应 feature 的硬件原理。

## HAL 接口

只讲这一章对应 feature 的 HAL API。

## 最小 Demo

给出可以直接跑的最小例子。

## C++23 封装

演示如何用现代 C++ 改善接口。

## 常见坑

列出最常见的问题。

## 练习题

让读者自己扩展。

## 可复用代码片段

沉淀到 code/ 目录中的模块。

## 本章小结

总结本章唯一核心 feature。
