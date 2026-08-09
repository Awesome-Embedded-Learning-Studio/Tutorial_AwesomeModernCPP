---
title: "STM32F407"
sidebar_order: 1
---

# STM32F407

F407 线的目标芯片是 STM32F407ZGT6(Cortex-M4F,168MHz,带单精度 FPU 和 DSP 指令,1MB Flash / 192KB RAM)。选它是因为外设多、能玩的花样多,而且中文社区最熟(正点原子 Apollo 同款),读者便宜买同款跟着做。

下面两条轨道,代码同一份,验证方式不同:

- **[Renode 模拟器线](renode/)** —— 主线,现在的内容。不用买板子,在电脑上跑模拟器。
- **[真板线](board/)** —— 等核心板到位再补。
