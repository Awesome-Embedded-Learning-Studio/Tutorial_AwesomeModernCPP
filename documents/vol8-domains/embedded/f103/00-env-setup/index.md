---
title: "起步:开发环境搭建"
description: "工具链、Renode 模拟器、工程结构、CMake 构建到调试与 clangd——把整套 F103 教程的地基一次铺好"
chapter: 0
order: 0
tags:
  - stm32f1
  - beginner
  - 入门
  - 工具链
difficulty: beginner
platform: stm32f1
---

# 起步:开发环境搭建

从交叉编译工具链到 Renode 模拟器,从工程结构到 CMake 构建系统,再到调试与 IDE——这一章咱们把后面所有实战都站着的地基铺好。中心思想就一条:**模拟器先行**。一条命令跑通固件并验证行为,实际板子是每站末尾的选修加餐,您买不买板子都不耽误学。

## 工具与模拟器

- [从零搭建 STM32 开发工具链](01-toolchain-setup.md) — 交叉编译原理,Ubuntu 与 Arch 两条安装路线
- [Renode 先行:不买板子,先点第一盏灯](02-renode-first-light.md) — 一条命令跑通工程,采样证明 LED 在闪,外带两个观测坑

## 工程与构建

- [项目结构:HAL 库的获取与目录搭建](03-project-structure.md) — 三层架构、submodule 陷阱、启动文件命名玄学、hal_conf 四坑
- [CMake 配置:从零构建 STM32 构建系统](04-cmake-configuration.md) — template 过滤、generator expression、链接脚本与双验证目标

## 实际板子与调试(选修)

- [WSL2 USB 透传(想用实际板子再看)](05-wsl2-usb.md) — usbipd-win 全流程与 OpenOCD 烧录
- [调试:从 printf 到 GDB,模拟器与实际板子两条路](06-debugging-guide.md) — Renode 里零硬件练 GDB,实际板子的 OpenOCD 方案在后半
- [嵌入式 clangd:让 vscode 看懂交叉编译的代码](07-clangd-for-cross-compilation.md) — query-driver 三件套,根治满屏红波浪线
