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

这是卷八的系统编程子领域,主线是咱们定下的**两大阵营并行**:同一个主题,Linux 侧用 POSIX 讲了一遍,Windows 侧跟着用 Win32 也讲了一遍。每个主题的文末都配了“另一侧怎么看”的小节,专门把两边的概念互相接上。真实世界里您往往两边都要写,而 API 会过时、概念不会。

咱们建议您从[总纲](./00-overview.md)读起,对全卷的布局有个数,再去[思维基石](./thinking/)把跨阵营的两件大事办好:资源怎么保证不漏,错误怎么保证报得准。两侧的篇章同时从文件 I/O 起步,Linux 侧的 fd 与 Windows 侧的 HANDLE,则是各自世界的句柄底座。内存一章的两侧也都已就位,进程章与异步 I/O 章的两侧跟着落地:进程章 Linux 侧从 fork/exec 一路走到信号与优雅关闭、Windows 侧把咱们带进了控制台事件与 APC,异步 I/O 章 Linux 侧从三代等待 API 走到 io_uring、Windows 侧收进了完成端口,跨平台一篇咱们再用 concepts 把异步后端约束在编译期。时间章的三篇与终端章的三篇(两侧)也都齐了,从九把钟一路走到定时器的漂移对照,再从 termios 的开关面板走到伪终端的录制回放。最后平台抽象的两篇也收了卷:抽象层设计把 #ifdef 退到选后端的一处,syskit 把全子卷的公共工具收编成两侧都编译得起来的库。咱们全卷的四十篇,到这里就齐了。

咱们跟同卷的[网络编程](../networking/index.md)分好了工:那边讲 socket 与事件循环(epoll/Reactor),这边讲的是文件、内存、进程、终端这类通用 OS 资源。两边的交汇处在 epoll,总纲的学习路线到 I/O 多路复用一章会直接衔接过去、不重复展开。

## 思维基石

<ChapterNav variant="sub">
  <ChapterLink href="thinking/" desc="跨阵营的概念地基:OS 资源的 RAII 范式(fd、HANDLE、映射的 move-only 骨架)与错误处理范式;全系列公共工具 unique_fd/unique_handle/mapped_region 的定义处">思维基石</ChapterLink>
</ChapterNav>

## Linux 侧

<ChapterNav variant="sub">
  <ChapterLink href="linux/file-io/" desc="fd 的一生:open/read/write、dup2 重定向、页缓存与 fsync,以及 mmap 内存映射;全系列公共工具 unique_fd/sys_call/errno_code 沿用思维基石两篇的定义">文件 I/O</ChapterLink>
  <ChapterLink href="linux/memory/" desc="进程地址空间:/proc/pid/maps 全图、虚拟内存 API、共享内存与对齐大页">进程内存</ChapterLink>
  <ChapterLink href="linux/process/" desc="进程的一生与信号的消费全链:fork/exec/posix_spawn 与 COW 实证、守护进程、IPC 七通道选型、sigaction 与异步信号安全、signalfd 与 pidfd 收在优雅关闭">进程</ChapterLink>
  <ChapterLink href="linux/io-multiplexing/" desc="三代等待 API 的行为边界与成本曲线(1024 上限归属翻案)、timerfd 与 eventfd 把时间与事件化成 fd、io_uring 的提交与完成环,衔接网络卷的 epoll 篇不重复机制课">I/O 多路复用与异步 I/O</ChapterLink>
  <ChapterLink href="linux/time/" desc="九个 clockid 的普查与分辨率精度之辨、vDSO 十倍差实测,C++20 chrono 的时钟落点对拍/闰秒时间轴/时区硬案例,定时器四代 API 与周期任务漂移对照">时间与定时器</ChapterLink>
  <ChapterLink href="linux/terminal/" desc="termios 四组标志逐位实测与 VMIN/VTIME 的字符间计时器判别、伪终端 posix_openpt 四步与 script(1) 复刻录制回放,全部跑在自建 pty 台架上">终端</ChapterLink>
</ChapterNav>

## Windows 侧

<ChapterNav variant="sub">
  <ChapterLink href="windows/file-io/" desc="HANDLE、CreateFileW 与同步读写,以及文件映射 CreateFileMapping 与 MapViewOfFile;Windows 侧公共工具 unique_handle/check_win32/last_error_code 沿用思维基石两篇的定义">文件 I/O</ChapterLink>
  <ChapterLink href="windows/memory/" desc="VirtualAlloc 保留/提交两段式、分配粒度、PAGE_GUARD、VirtualQuery,以及页面文件后备的命名共享内存">内存</ChapterLink>
  <ChapterLink href="windows/process/" desc="CreateProcessW 解剖、四种死法清理矩阵与 Job 对象治理;控制台事件的忽略位与投递模型,APC 的排队与执行">进程</ChapterLink>
  <ChapterLink href="windows/async-io/" desc="OVERLAPPED 的在途世界与 ReadFileEx 完成例程,以及完成端口 IOCP 的队列收割">异步 I/O</ChapterLink>
  <ChapterLink href="windows/console/" desc="字符缓冲区网格直写与光标语义、输入事件队列、VT 序列开关前后的读回对照">控制台</ChapterLink>
</ChapterNav>

## 跨平台

<ChapterNav variant="sub">
  <ChapterLink href="cross-platform/" desc="四后端差异矩阵与 concepts 编译期约束、平台抽象层设计(死分支零诊断的实证与 #ifdef 退场)、syskit 工具库收编(两套近亲的逐项对照与统一方案)">跨平台与收卷</ChapterLink>
</ChapterNav>
