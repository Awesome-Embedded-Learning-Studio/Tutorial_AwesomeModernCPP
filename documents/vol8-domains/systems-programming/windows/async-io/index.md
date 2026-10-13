---
title: "Windows 异步 I/O"
sidebar_order: 40
description: "OVERLAPPED 与 FILE_FLAG_OVERLAPPED 的在途世界:ReadFile 的三形态、ReadFileEx 完成例程、CancelIo 家族、WFMO 的上限与散序完成,以及完成端口 IOCP"
platform: host
tags:
  - cpp-modern
  - host
  - advanced
  - 系统编程
  - Win32
  - 异步编程
---

# Windows 异步 I/O

文件 I/O 章攒下的伏笔在这一章集中兑现:W01 请咱们认过脸熟的 `FILE_FLAG_OVERLAPPED`,W05 首演过的 OVERLAPPED 结构与 997、995,进程篇留话的 ReadFileEx 完成例程,还有 WaitForMultipleObjects 那句顶多 64 个的上限,都在这里拿到实测的数字。第一篇咱们走事件与完成例程的两条路,第二篇再把两条路收进完成端口的队列。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-overlapped" desc="同一个 ReadFile 从读满才回到交一个 OVERLAPPED 就在途:三形态一屏对齐(同步句柄带 OVERLAPPED 调用仍阻塞但文件指针实测从 16 跟到 8008、异步五连发全 FALSE+997、正对 EOF 的 GOR 回 FALSE+38 而同步侧回 TRUE+0、裸读 87 与指针架空),ReadFileEx 完成例程只在可警告等待里跑(500ms 不可警告等待例程执行数 0、一进可警告同 tick FIFO 连跑、等待以 192 提前返回、hEvent 哨兵读回原样),CancelIo 家族的线程归属(worker 发的读主线程 CancelIo 撤不动、GOR 仍 996,CancelIoEx 不点名才 995,997/996/995 三个码三个语义),WFMO 与 MsgWait 两堵墙分开量(64/65 与 63/64,64 减一的说法属于 MsgWait 族),六发在途投递序 1..6 完成序 6 5 4 3 2 1、hEvent=NULL 两发在途句柄第一包到就亮分不清谁完成,每发配一枚手动重置事件的实测理由">OVERLAPPED 异步 I/O 与 WaitForMultipleObjects</ChapterLink>
  <ChapterLink num="2" href="02-iocp" desc="完成端口怎么把等一堆在途请求变成从队列里取完成包:三件套 key 与 OVERLAPPED 指针逐一相认(乱序三发偏移读、空队列 813ms 回 258、第二把句柄 key=777 各认各的),六管道镜像换收割完成序仍 6 5 4 3 2 1,四工线程四种组合:间隔投喂的两场单线包圆,八包一口气时并发值 0 是四线全醒分包而并发值 1 仍单线连收(上限不派活、只放行),PQCS 让裸端口当唤醒通道(eventfd 的 Windows 同构物、优雅关闭的标准信号),在途读直接 CloseHandle 完成包立即以 109 断管送达而正路子 CancelIoEx 收 995 再关,Job 挂端口 NEW_PROCESS 与 EXIT_PROCESS 与 ACTIVE_PROCESS_ZERO 三包字段落点全录(兑现进程篇的死讯送达),100 发在途事件式被 64 上限逼成 64+36 两段轮询每醒全量重扫而 IOCP 单端口零扫描,墙钟同量级、差别在结构">IOCP 完成端口</ChapterLink>
</ChapterNav>
