---
title: "Linux 时间与定时器"
sidebar_order: 50
description: "时钟源与 POSIX 时间 API、C++20 chrono 的日历时区、定时器四代 API 的全景与漂移对照"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
---

# Linux 时间与定时器

时间这一章咱们从「机器里有几把钟、它们差在哪」问起:第一篇把九个 clockid 逐个量过去,分辨率与精度分开看,vDSO 的免票通道也补上实测。第二篇转进标准库,看 std::chrono 的时钟怎么落在 OS 时钟上,闰秒与时区的坑在类型系统里长什么样。第三篇把定时器的四代 API 摆开,周期任务的漂移用三种写法对照收尾。全系列公共工具沿用[思维基石](../../thinking/)两篇的定义。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-clock-sources" desc="CLOCK_MONOTONIC 这串常量背后有几把尺子、差在哪、怎么选:九个 clockid 普查(TAI−UTC=37s 的闰秒累计、CLK_TCK=100 与内核 HZ=250 两套口径、COARSE 分辨率 4ms),分辨率≠精度(getres 报 1ns 而背靠背连读中位 20ns、COARSE 百万次只 3 个不同值、读取代价 16.2ns 对 2.0ns),连续性实测(200 万次零回退、RAW−MONOTONIC 冻结在 −106.4ms 的历史频率调整),vDSO 兑现总纲的免票承诺(16.9ns 对真陷入 168.9ns 十倍、strace 零系统调用对两百万条),CPU 时钟三口径对表(纳秒 298.0/getrusage 298.1/tick 290 的粒度差),墙上时间呈现链(TZ 换规则不换时间)给 chrono 篇搭桥">时钟源与 POSIX 时间 API</ChapterLink>
  <ChapterLink num="2" href="02-chrono-calendar" desc="std::chrono 的时钟与日历怎么落在 OS 上、闰秒与时区的坑在类型系统里长什么样:时钟落点对拍(system/steady 背靠背即测、utc−system=+27s/TAI−UTC=+37s/GPS−UTC=+18s 的三偏移,与内核 37s 同一份闰秒数据两种读法),闰秒时间轴(2016-12-31 的 23:59:60 真在轴上、get_leap_second_info、27 条表),时区硬案例(纽约歧义与不存在时刻抛异常、中国 1988 夏令时在 tzdata 里完整可查),tzdb 落 OS(strace 证 openat tzdata.zi、首次 1.751ms 对再次同引用),日历类型的 ok() 校验,四列打点收束测量持久化审计呈现的分工">C++20 std::chrono 深度</ChapterLink>
  <ChapterLink num="3" href="03-timer-family" desc="定时器四代 API 怎么选、周期任务为什么不能拿 sleep 糊:alarm 的整秒粒度与剩余向上取整(1.70s 报 2/0.40s 报 1 的三点判别),setitimer 三种计时口径(REAL 200/VIRTUAL 101/PROF 96 次)、撤销的 new_value 传 NULL 是 Linux 私有 misfeature,timer_create 四通知形态(SIGEV_THREAD 每次派新线程的 tid 实证、THREAD_ID 定向零打扰)与到期合并 getoverrun,存亡矩阵(fork 三件信号形态全零、exec 侧 alarm 与 itimer 按文档与实测都保留、处置复位、timerfd 活过 fork+exec),招牌漂移对照:相对 sleep 循环漂约 78ms 对绝对到期 0.08ms 以下对 timerfd 0.03ms,周期调度器的正确写法">定时器全景</ChapterLink>
</ChapterNav>

时间章的三篇到此齐了,下一站终端,路线跟[总纲](../../00-overview.md)的学习路线对齐。
