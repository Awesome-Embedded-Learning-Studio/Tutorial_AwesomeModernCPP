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

<ChapterNav variant="sub">
  <ChapterLink href="01-toolchain-setup">从零搭建 STM32 开发工具链</ChapterLink>
  <ChapterLink href="02-renode-first-light">Renode 先行:不买板子,先点第一盏灯</ChapterLink>
</ChapterNav>

## 工程与构建

<ChapterNav variant="sub">
  <ChapterLink href="03-project-structure">项目结构:HAL 库的获取与目录搭建</ChapterLink>
  <ChapterLink href="04-cmake-configuration">CMake 配置:从零构建 STM32 构建系统</ChapterLink>
</ChapterNav>

## 实际板子与调试(选修)

<ChapterNav variant="sub">
  <ChapterLink href="05-wsl2-usb">WSL2 USB 透传(想用实际板子再看)</ChapterLink>
  <ChapterLink href="06-debugging-guide">调试:从 printf 到 GDB,模拟器与实际板子两条路</ChapterLink>
  <ChapterLink href="07-clangd-for-cross-compilation">嵌入式 clangd:让 vscode 看懂交叉编译的代码</ChapterLink>
</ChapterNav>
