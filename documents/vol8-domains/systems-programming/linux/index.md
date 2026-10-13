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

Linux 侧从文件 I/O 打地基:fd 是 Linux 一切资源的句柄底座,咱们后面要碰的 socket、epoll、mmap 也全踩在它上面。全系列公共工具(`unique_fd`、`sys_call`、`errno_code`)沿用[思维基石](../thinking/)两篇的定义,后续的文章咱们只引用、不再重定义。

<ChapterNav variant="sub">
  <ChapterLink href="file-io/" desc="open/read/write 与 fd 的一生、dup2 重定向、pread、页缓存与 fsync;mmap 内存映射">文件 I/O</ChapterLink>
  <ChapterLink href="memory/" desc="/proc/pid/maps 全图与 41 段解剖、变量落位对表、malloc 分水岭与动态阈值、smaps 的 Rss/Pss、ASLR;虚拟内存 API、共享内存与对齐分配大页四篇">进程内存</ChapterLink>
  <ChapterLink href="process/" desc="进程的一生与信号的消费全链:fork/exec/posix_spawn 与 COW 的 Pss 实证、僵尸双阶段与三种收尸姿势、孤儿收养的 subreaper 机制、RAII child_process;setsid 双 fork 的六组实测、rlimit 与 environ;管道/FIFO/POSIX mq 与 SCM_RIGHTS 七通道加选型矩阵;sigaction 与异步信号安全、实时信号、signalfd 与 pidfd 收在优雅关闭">进程</ChapterLink>
  <ChapterLink href="io-multiplexing/" desc="select、poll 与 epoll 三代等待 API 的行为边界与成本曲线(1024 上限的归属翻案、扫描量与成本曲线实测),timerfd 与 eventfd 把时间与事件化成 fd,以及 io_uring 的提交与完成环,衔接网络卷的 epoll 篇、不重复展开机制课">I/O 多路复用与异步 I/O</ChapterLink>
  <ChapterLink href="time/" desc="九个 clockid 的普查与分辨率精度之辨、vDSO 十倍差实测,C++20 chrono 的时钟落点对拍/闰秒时间轴/时区硬案例(含中国 1988 夏令时),定时器四代 API 与周期任务漂移对照(相对 sleep 漂约 78ms 对绝对到期 0.08ms 以下)">时间与定时器</ChapterLink>
  <ChapterLink href="terminal/" desc="termios 四组标志逐位实测:canonical 的行缓冲与 ^C 的信号翻译(^C 的冲队与递信号可以只做一件)、VMIN/VTIME 字符间计时器每收一字节重置的 D+F 合判、RAII terminal_guard 与 TCSAFLUSH 的滞留输入处理,全部实验跑在自建 pty 台架上、主终端零接触;伪终端 posix_openpt 四步与 forkpty、script(1) 复刻与按 timing 回放(1257.1ms 对 1255.8ms、cmp 逐字节一致)、master 读写不对称的 EIO 判据">终端与控制台</ChapterLink>
</ChapterNav>

文件 I/O 六篇、内存篇的四篇、进程篇的五篇、多路复用篇的三篇、时间篇的三篇与终端篇的两篇都已经就位,Linux 侧的六个章全齐了,路线与[总纲](../00-overview.md)的学习路线对齐。多路复用篇拿同一批管道 fd 量三代 API 的边界与成本,epoll 的内核机制课衔接网络卷讲过的 epoll、不重复展开。时间篇把九把钟、chrono 的日历时区与定时器四代 API 走全了。终端篇把 termios 的开关面板与伪终端的录制回放走全了,Windows 侧的控制台篇在另一棵子树里应着。最后 Linux 与 Windows 的两侧一起收进平台抽象。
