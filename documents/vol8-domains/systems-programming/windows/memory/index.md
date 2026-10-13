---
title: "Windows 内存"
sidebar_order: 20
description: "Win32 虚拟内存与堆:VirtualAlloc 保留/提交两段式、分配粒度、PAGE_GUARD、VirtualQuery,以及页面文件后备的命名共享内存"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
  - 内存管理
---

# Windows 内存

文件 I/O 一章里咱们已经在 W02 碰过文件后备的映射与 SEC_RESERVE 的两段式,这一章咱们把文件请出去,站到纯内存的视角:进程怎么向系统要内存、承诺与物理页怎么分家、越界的警报怎么埋,最后收在两个进程怎么共享同一块内存。公共工具照旧:`unique_handle` 与 `last_error_code` 沿用思维基石两篇的定义,`check_win32` 沿用 W01 的定义。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-virtualalloc" desc="Windows 进程直接向系统要内存的入口:保留/提交两段式实测(MEM_RESERVE 1GiB 约 2~5 微秒到手而提交额度分文不动,提交 72KiB 额度 +80KiB、触碰 18 页后工作集才 +72KiB——提交是承诺,触碰才占物理;MEM_DECOMMIT 退回保留态可再提交,MEM_RELEASE 只认整块基址加 dwSize=0,非基址与带尺寸实测都报 87 而不是直觉的 487);分配粒度双轨(地址一律 64KiB 对齐而 RegionSize 按页取整:1 字节→1 页、0xFFFF→16 页、0x10001→17 页、0x100001→257 页,粒度块尾巴是 FREE 不是隐藏预约,MEM_TOP_DOWN 落 0x7FF4 高地址带);PAGE_GUARD 一次性陷阱三连证据链(0x104→0x80000001→自灭 0x004→再武装又能响一次),系统栈就是 guard 带实现的(2MiB 预约里 COMMIT 段加 2~3 页 guard 带,压栈 768KiB 后 guard 带下移 764KiB 而扩展期间 VEH 零命中,预约耗尽收 0xC00000FD);VirtualQuery 193 步扫完 128TiB(IMAGE 99/MAPPED 16/PRIVATE 17/RESERVE 24/FREE 空洞 37,对照 /proc/self/maps 的行数与后备差异,探针语义:BaseAddress 下取整、RegionSize 是剩余量、分组键含 AllocationBase 不合并);malloc→HeapAlloc→VirtualAlloc 三层与三档阈值(≤384KiB 主堆段、416~1016KiB 新堆段、≥1MiB 直发 VirtualAlloc 释放后整段归还,malloc(32) 与默认堆 HeapAlloc(32) 同一个 AllocationBase——CRT 坐在进程堆上)">虚拟内存:VirtualAlloc 与 VirtualProtect</ChapterLink>
  <ChapterLink num="2" href="02-shared-mem" desc="两个进程怎么共享一块内存:答案藏在 W02 露过半面的 INVALID_HANDLE_VALUE 里,页面文件后备的命名映射对象在本篇当主角。同名再 Create 收 err=183 而请求尺寸被静默忽略,名字死于最后一个句柄、对象死于最后一个引用的两段死;跨进程视图实测基址相差 502 GiB、offset 按 64KiB 粒度对齐而长度只按页,父进程的指针值在子进程 VirtualQuery=MEM_FREE、平级的探针进程解引拿 0xC0000005;命名同步三件套(无锁两进程丢 83411/200000、命名互斥体护住恰好二十万、持锁暴毙等待方收 WAIT_ABANDONED 锁还能接着用);招牌 SPSC 环形队列 100 万条零丢失零乱序,spin 2.88 亿条/s 对逐条通知 471 万条/s,同机 WSL2 对拍 shm_open 加八行对照表收束">共享内存:页面文件后备的命名映射对象</ChapterLink>
</ChapterNav>
