---
title: "系统编程"
description: "Linux 与 Windows 应用层 OS API 的 Modern C++ 封装:文件 I/O、进程线程、内存映射、同步原语,两大阵营两侧并行对照"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
---

# 系统编程

这是卷八的「系统编程」子领域。主线是**两大阵营并行**:同一个主题,Linux 侧用 POSIX 讲一遍,Windows 侧用 Win32 讲一遍,每个主题的文末都有「另一侧怎么看」互相搭桥——因为真实世界里两边都要写,而 API 会过时、概念不会。

先从[总纲](./00-overview.md)拿一张全景地图,然后两侧同时从文件 I/O 起步:Linux 侧的 fd 与 Windows 侧的 HANDLE 是各自世界的句柄底座。后续逐步推进到进程、内存映射、同步原语与更多领域专题。

顺带划一条与同卷[网络编程](../networking/index.md)的分界:那边讲 socket 与事件循环(epoll/Reactor),这边讲通用 OS 资源——文件、内存、进程、终端;两边在 epoll 处交汇,总纲的学习地图到 I/O 多路复用一章会直接衔接过去、不重复展开。

## Linux 侧

<ChapterNav variant="sub">
  <ChapterLink href="linux/file-io/" desc="fd 的一生:open/read/write、dup2 重定向、页缓存与 fsync,以及 mmap 内存映射;全系列契约工具 unique_fd/sys_call/errno_code 的定义处">文件 I/O</ChapterLink>
</ChapterNav>

## Windows 侧

<ChapterNav variant="sub">
  <ChapterLink href="windows/file-io/" desc="HANDLE、CreateFileW 与同步读写,以及文件映射 CreateFileMapping 与 MapViewOfFile;Windows 侧契约工具 unique_handle/check_win32/last_error_code 的定义处">文件 I/O</ChapterLink>
</ChapterNav>
