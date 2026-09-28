---
title: "Linux 系统编程"
sidebar_order: 10
description: "POSIX 应用层 API 的 Modern C++ 封装:文件 I/O、内存映射、进程线程与同步"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
---

# Linux 系统编程

Linux 侧从文件 I/O 打地基:fd 是 Linux 一切资源的句柄底座,socket、epoll、mmap 全踩在它上面。第一篇同时是全系列契约工具(`unique_fd`、`sys_call`、`errno_code`)的就地定义处,后续文章只引用、不再重定义。

<ChapterNav variant="sub">
  <ChapterLink href="file-io/" desc="open/read/write 与 fd 的一生、dup2 重定向、pread、页缓存与 fsync;mmap 内存映射">文件 I/O</ChapterLink>
</ChapterNav>

规划中(路线对齐[总纲](../00-overview.md)学习地图):内存(虚拟内存、mmap 与堆、分配器)→ 进程与 IPC(创建与等待、管道、信号)→ I/O 多路复用(select/poll/epoll 的取舍,衔接网络卷讲透的 epoll、不重复展开)→ 时间(时钟与定时器)→ 终端(tty 与原始模式);最后与 Windows 侧一起收进平台抽象。
