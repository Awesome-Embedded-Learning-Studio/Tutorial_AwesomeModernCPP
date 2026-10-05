---
title: "Windows 系统编程"
sidebar_order: 20
description: "Win32 应用层 API 的 Modern C++ 封装:文件 I/O、文件映射、虚拟内存与共享内存、进程线程与同步"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
---

# Windows 系统编程

Windows 侧同样从文件 I/O 打地基:HANDLE 是 Windows 一切内核对象的句柄底座,文件、进程、事件全握在它的上面。第一篇咱们把公共工具直接领来用:`unique_handle` 与 `last_error_code` 沿用思维基石两篇的定义,Windows 侧的 `check_win32` 则定义在这一篇,后面的文章咱们只引用、不再重定义。

<ChapterNav variant="sub">
  <ChapterLink href="file-io/" desc="句柄、CreateFileW 与同步读写,以及文件映射 CreateFileMapping 与 MapViewOfFile">文件 I/O</ChapterLink>
  <ChapterLink href="memory/" desc="VirtualAlloc 的保留/提交两段式、64KiB 分配粒度双轨、PAGE_GUARD 一次性陷阱、VirtualQuery 全景与堆的三层,以及页面文件后备的命名共享内存">内存</ChapterLink>
  <ChapterLink href="process/" desc="CreateProcessW 十参数解剖、四种死法的清理矩阵与 Job 对象的治理;控制台事件的忽略位与投递模型,APC 的排队与执行">进程</ChapterLink>
  <ChapterLink href="async-io/" desc="OVERLAPPED 三形态与在途收割、ReadFileEx 完成例程、CancelIo 家族的线程归属、WFMO 与 MsgWait 的两堵上限墙,以及散序完成与手动重置事件的实测理由">异步 I/O</ChapterLink>
  <ChapterLink href="console/" desc="字符缓冲区网格直写与光标语义、输入事件队列的 Peek/Read/Flush、VT 序列开关前后的读回对照,Win11 26200 的 \x1b[2J 不归零光标">控制台</ChapterLink>
</ChapterNav>

文件 I/O、内存篇的两篇、进程篇的两篇、异步 I/O 的两篇与控制台篇都已经就位,Windows 侧的六个章全齐了。进程篇讲的是进程的创建、退场、Job 对象的治理,以及 Windows 拿什么回应信号那道题:控制台事件与 APC。异步 I/O 篇咱们从 OVERLAPPED 与事件收割讲起,再收进完成端口的队列,与网络卷的衔接口径不变、不重复展开。控制台篇把输入输出面走全:字符缓冲区、事件队列与 VT 序列。最终 Linux 与 Windows 的两侧一起收进平台抽象。
