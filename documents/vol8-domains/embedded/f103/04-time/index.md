---
title: "时间:SysTick、定时器、PWM"
description: "从 SysTick 心跳走进通用定时器,再到 PWM 输出,把时间变成可以编程的资源"
chapter: 4
order: 0
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
difficulty: intermediate
platform: stm32f1
---

# 时间:SysTick、定时器、PWM

## 概述

这一站目前落地的是时间主题的前三篇。第一篇 TIM 家族当主线,tim.hpp 的三件套(TimBase 周期拍、TimOneShot 单发窗、TimPwm 占空比输出)由 consteval 求解器从周期解出 PSC 与 ARR,timclk 的 APB1×2 规则与假 UIF 的防法收在两个示例里,09_tim_heartbeat 用 1Hz 心跳配 WFI 主循环,10_button_confirm 用 EXTI 沿启动 20ms 单发确认窗,与轮询消抖链对拍。第二篇把配置写进类型:指定初始化器直接当模板实参,笔误在编译期报错,配置折叠成指令流里的立即数,与 HAL 的句柄路线对照着看。第三篇算量程:16 位 PSC 乘 16 位 ARR 在 64MHz 下配得下 1µs 到 67.108864s 的周期,近似路径的误差封顶 15ppm,0.1% 精度门槛拦下的其实是出了量程的配置。SysTick 与 PWM 呼吸灯等素材成熟再补。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="01-tim-family-confirm-debounce">TIM 家族与确认式消抖：从 1Hz 心跳到 20ms 确认窗</ChapterLink>
  <ChapterLink href="02-config-in-type">配置进类型：指定初始化器当模板实参</ChapterLink>
  <ChapterLink href="03-period-range-boundary">量程的边界：16 位乘 16 位能配多长</ChapterLink>
</ChapterNav>
