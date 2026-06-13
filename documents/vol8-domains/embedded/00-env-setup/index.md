---
title: "开发环境搭建"
description: "从工具链、项目结构、CMake 到 WSL2 USB 透传与 GDB 调试，搭起 STM32 开发的完整脚手架"
platform: stm32f1
tags:
  - cpp-modern
  - intermediate
  - stm32f1
---

# 开发环境搭建

> 从交叉编译工具链到完整 GDB 调试环境，把 STM32 开发的地基一次铺好——后面所有实战都站在这套环境上。

## 工具链与项目结构

- [第1篇：从零搭建 STM32 开发工具链](01-toolchain-setup.md) — 交叉编译原理与安装指南
- [第2篇：项目结构篇](02-project-structure.md) — HAL 库获取、启动文件坑位与目录搭建
- [CMake 配置篇](03-cmake-configuration.md) — 从零构建 STM32 构建系统

## WSL2 与调试

- [环境搭建（四）：WSL2 USB 透传](04-wsl2-usb.md) — 让 ST-Link 穿越虚拟化边界
- [第5篇：调试进阶篇](05-debugging-guide.md) — 从 printf 到完整 GDB 调试环境
