---
title: "Windows 系统编程"
sidebar_order: 20
description: "Win32 应用层 API 的 Modern C++ 封装:文件 I/O、文件映射、进程线程与同步"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
---

# Windows 系统编程

Windows 侧同样从文件 I/O 打地基:HANDLE 是 Windows 一切内核对象的句柄底座,文件、进程、事件全握在它上面。第一篇同时是 Windows 侧契约工具(`last_error_code`、`check_win32`、`unique_handle`)的就地定义处,后续文章只引用、不再重定义。

<ChapterNav variant="sub">
  <ChapterLink href="file-io/" desc="句柄、CreateFileW 与同步读写,以及文件映射 CreateFileMapping 与 MapViewOfFile">文件 I/O</ChapterLink>
</ChapterNav>

规划中(路线对齐[总纲](../00-overview.md)学习地图):内存(虚拟内存与堆、分配器)→ 进程与 IPC(进程与线程、管道)→ I/O 多路复用与异步 I/O(衔接网络卷、不重复展开)→ 时间(时钟与定时器)→ 控制台;最后与 Linux 侧一起收进平台抽象。
