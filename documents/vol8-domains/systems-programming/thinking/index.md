---
title: "思维基石"
sidebar_order: 5
description: "系统编程的概念地基:OS 资源的 RAII 范式(fd、HANDLE、映射的 move-only 骨架),以及错误处理范式"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
---

# 思维基石

咱们要在 Linux 与 Windows 两侧开工之前,把跨阵营共用的地基打好:资源怎么保证不漏,错误怎么保证报得准。全系列的公共工具 unique_fd、unique_handle、mapped_region 在这里定义,两侧的篇章只引用、不重定义。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-raii-paradigm" desc="fd、HANDLE、内存映射全是拿到手就必须还的东西;实测异常路径裸 fd 漏 1000 个对照 unique_fd 零泄漏、析构里 close 返回值按 man 2 close 的口径丢弃、Windows 失败值 -1 与 NULL 两套并存、CloseHandle 对伪句柄静默 TRUE、release/reset/swap 归还语义、vector 扩容搬迁 7 次 shuffle 零移动、move-only 类型不标 noexcept 也不会退拷贝的实测纠正">OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架</ChapterLink>
  <ChapterLink num="2" href="02-error-paradigm" desc="errno 与 GetLastError 的线程局部性、失败之后立刻装箱成 error_code 的边界纪律、sys_call 模板的完整故事,以及工具层 expected、应用顶层 system_error 的双出口约定">错误处理范式:从 errno 到 expected</ChapterLink>
</ChapterNav>
