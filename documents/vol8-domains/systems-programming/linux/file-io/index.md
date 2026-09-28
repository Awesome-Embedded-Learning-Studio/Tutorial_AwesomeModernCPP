---
title: "Linux 文件 I/O"
sidebar_order: 10
description: "POSIX 文件 I/O:fd、open/read/write、dup2 重定向、pread、页缓存与 fsync,以及 mmap 内存映射"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
---

# Linux 文件 I/O

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-posix-file-io" desc="strace 实测 fd 的一生、flags 与 umask、部分读写、fd 表与 dup2、pread 不动偏移、页缓存与 fsync 崩溃窗口;unique_fd、sys_call 与 errno_code 的定义处">POSIX 文件 I/O:open/read/write 与 fd 的一生</ChapterLink>
  <ChapterLink num="2" href="02-mmap-memory-mapping" desc="读文件变成拿指针摸内存,read 从页缓存到用户缓冲的那次拷贝被整个省掉;六个参数与 MAP_FAILED 陷阱、实测缺页比二次访问贵一个数量级、文件在背后被截短摸越界吃 SIGBUS、MAP_PRIVATE 写时复制、512 MiB 顺序读 mmap 反而输给 read;mapped_region RAII 收口">mmap 内存映射:把文件贴进地址空间</ChapterLink>
</ChapterNav>
