---
title: "STM32F103 + Renode"
description: "每一站在一个外设上把现代 C++ 用起来,Renode 模拟器先行,没有板子也能跟完;反汇编负责证明零开销"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# STM32F103 + Renode

这套教程全部跑在 Renode 模拟器里,您不需要买任何硬件,一台装了工具链的电脑就够了。手头有块 Blue Pill(STM32F103C8T6)更好,每站末尾在实际板子上的验证可以跟着做;有它更好,没它不挡路。

每一站围绕一个外设,主线是在 HAL 之上写现代 C++ 应用层:库里的轮子只讲不造,库里没有的(消抖状态机、环形缓冲、命令解析这类)咱们自己写。LED 站还会带咱们下到地砖下面,看一眼裸寄存器和官方库在做什么。模拟器负责验证功能,反汇编负责验证零开销。

## 章节导航

内容正在按这个顺序逐步上线,起步站已经上线,后面陆续跟上:

<ChapterNav>
  <ChapterLink num="0" href="00-env-setup/">起步:开发环境搭建</ChapterLink>
  <ChapterLink num="1" href="01-led/">LED:地砖下面看裸寄存器,HAL 之上写现代 C++</ChapterLink>
  <ChapterLink num="2" href="02-button/">按键:消抖、状态机、variant</ChapterLink>
  <ChapterLink num="3" href="03-uart/">UART:中断驱动、环形缓冲、expected</ChapterLink>
  <ChapterLink num="4" href="04-time/">时间:SysTick、定时器、PWM</ChapterLink>
  <ChapterLink num="5" href="05-i2c/">I2C:传感器驱动设计</ChapterLink>
  <ChapterLink num="6" href="06-patterns/">模式:对象池、侵入式容器、中断安全</ChapterLink>
  <ChapterLink num="7" href="07-f103-to-f407/">从 F103 到 F407:换芯片不换代码</ChapterLink>
</ChapterNav>
