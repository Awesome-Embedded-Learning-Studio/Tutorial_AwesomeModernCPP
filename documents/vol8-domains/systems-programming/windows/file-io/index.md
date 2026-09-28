---
title: "Windows 文件 I/O"
sidebar_order: 10
description: "Win32 文件 I/O:句柄、CreateFileW、ReadFile/WriteFile,以及文件映射 CreateFileMapping 与 MapViewOfFile"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
---

# Windows 文件 I/O

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-win32-file-io" desc="失败值两套约定、dwShareMode 独占语义、unique_handle 与 check_win32 的定义处、句柄泄漏计数、SetStdHandle 与 CRT 的分家、手搓复制器胜 copy_file 四倍">Win32 文件 I/O:句柄、CreateFileW 与同步读写</ChapterLink>
  <ChapterLink num="2" href="02-file-mapping" desc="两步走:先造映射对象、再贴视图;偏移按 64 KiB 分配粒度对齐而不是 4 KiB 页、失败值是 NULL 不是 INVALID_HANDLE_VALUE、映射句柄先关视图照活、FILE_MAP_COPY 私有副本永不落盘、FlushViewOfFile 只刷脏页不刷元数据、SIGBUS 剧本在 Windows 拍不成(文件在映射之下砍不动);mapped_view RAII 收口">文件映射:CreateFileMapping 与 MapViewOfFile</ChapterLink>
</ChapterNav>
