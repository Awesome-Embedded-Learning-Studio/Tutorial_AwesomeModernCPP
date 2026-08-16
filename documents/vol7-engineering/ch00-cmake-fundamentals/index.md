---
title: "CMake 基础"
description: "CMake 的定位、两段式流水线、target 心智模型、find_package 与 C++ 标准、CMakePresets——从照抄到看懂"
chapter: 7
order: 0
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
---

# CMake 基础

本子卷从「CMake 到底是什么」讲起，落到 target 心智模型、依赖管理、可复现配置。目标是让读者不再照抄 `CMakeLists.txt`，而是理解每一条命令背后的设计意图。

<ChapterNav variant="sub">
  <ChapterLink href="01-what-is-cmake">CMake 是什么——构建系统生成器的两段式流水线</ChapterLink>
  <ChapterLink href="02-target-and-usage-requirements">Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求</ChapterLink>
  <ChapterLink href="03-find-package-and-cxx-standard">依赖与 C++ 标准——find_package 和 cxx_std_NN 的现代写法</ChapterLink>
  <ChapterLink href="04-cmake-presets">CMakePresets.json——从 cmake -D 老式到 --preset 可复现</ChapterLink>
</ChapterNav>
