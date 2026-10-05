---
title: "Linux I/O 多路复用与异步 I/O"
sidebar_order: 40
description: "select、poll 与 epoll 三代等待 API 的行为边界与成本曲线,timerfd 与 eventfd 把时间与事件化成 fd,以及 io_uring 的提交与完成环"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
---

# Linux I/O 多路复用与异步 I/O

咱们在一个循环里等一批 fd,这样的活 Linux 给过的 API 前后有三位,select 的位图、poll 的条目数组、epoll 的常驻注册表。本目录拿同一批管道 fd 把三代的边界与成本量成数字,timerfd、eventfd 与 io_uring 是后面的篇目。epoll 的内核机制课在网络卷讲全了,这里的每篇只做行为对照与衔接。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-select-poll-epoll" desc="同一批管道 fd 交给三代等待 API:实测翻案 select 的 1024 上限(FD_SETSIZE 只是 glibc 位图的 128 字节,手工放大到 2048 位后 fd=1025 照常就绪,内核不查 rlimit,真正查的 poll 实测 EINVAL)、一次醒来的扫描量对照(select 查 1002 个 fd、poll 500 条、epoll 只拿就绪的 1 个)、成本曲线(poll 每轮 2527 到 104460 纳秒随 N 线性,epoll 三档 994/994/985 纳秒纹丝不动,500 档 select 反超 poll 的结构性原因)、select 改写 timeout 与位图入参的漏事件现场、管道版 LT/ET 行为对照(LT 连醒 15 次,ET 只醒 1 次剩 55904 字节无人再报)、/proc/self/fdinfo 直接看兴趣表(注册 EPOLLIN 实记 0x19,内核自动补 EPOLLERR 与 EPOLLHUP),fd 源一概是 pipe 呼应 IPC 篇,机制课挂网络卷链接">I/O 多路复用:select、poll 与 epoll 的边界与成本</ChapterLink>
  <ChapterLink num="2" href="02-timerfd-eventfd" desc="时间与事件怎么化成 fd、与数据源进同一个循环:eventfd 的计数器语义(write 是加法一次读走、EFD_SEMAPHORE 读一次减一,信号量加 LT 的忙通知档连醒 5 次,上限就是 2 的 64 次方减 2 本身),timerfd 的到期计数器(睡过 350ms 一次 read 报 3 的合并、改 interval 首档沿用旧 it_value 的教材少写语义、1ms 档中位 999.3 微秒对照 Windows 侧 Sleep(5) 实睡 12.6ms),fdinfo 三字段直接读,收尾 pipe 加 eventfd 加 timerfd 同挂一个 epoll 的 151 到 901ms 交错时间线">timerfd 与 eventfd:时间与事件化成 fd</ChapterLink>
  <ChapterLink num="3" href="03-io-uring" desc="提交与完成两个共享内存环,把等待 I/O 变成收割完成事件:裸 syscall 建环讲机制(x86-64 编号 425 起、三段 mmap、手工填 SQE,衔接信号下篇的编号表),liburing 2.15 走工程姿势,64 个读一次 submit 加一次 wait 对 read(2) 的 64 次调用,链式请求 read 加 write 加 fsync 挂 IOSQE_IO_LINK 顺序与失败传播都在内核侧(链头坏 fd 变 EBADF、下游 ECANCELED),epoll 对普通文件 ADD 实测 EPERM 而磁盘文件的统一异步只剩 io_uring,页缓存热路径上墙钟与 read(2) 打平而系统调用数差 128 倍的诚实口径,超时也是请求对照 timerfd 的 fd 进表,优雅关闭留给跨平台抽象篇对照">io_uring:提交环与完成环</ChapterLink>
</ChapterNav>

第二篇讲的是 timerfd 与 eventfd,把定时器与通知也化成了 fd、接进同一个循环,第三篇讲的是 io_uring 的提交与完成环。三篇正文与实验存档都已就位,跨平台的异步抽象咱们按[总纲](../../00-overview.md)的学习路线接着走。
