---
title: "第四单元：从管理线程走向组织任务"
description: "从 std::async 的发起与取结果，到启动策略与析构等待，再到自行传递结果与包装可分发的任务"
---

# 第四单元：从管理线程走向组织任务

前三单元里，咱们一直亲手抓着线程：启动它、等它、停它，数据怎么共享、等待怎么结束，都由咱们自己安排。第四单元把视角抬高一层，管理的单位从线程换成任务，谁去执行、结果怎么回来、失败怎么传，都交给任务对象来表达。

`std::async` 是单元的入口：它替咱们起线程、搬结果、传异常，咱们只管拿着 future 等。任务什么时候开工，由启动策略决定，策略是单元里最容易看走眼的地方，值得单独一篇。自行供值的 promise 已经在第三篇到场，把失败往上传的正路走通。往后还剩包装任务的 packaged_task。线程池放进 cookbook 选读，不进正文。单元内容按节分篇，已落定的篇目列在下面。

## 本单元内容

<ChapterNav variant="sub">
  <ChapterLink href="01-launch-with-async">用 std::async 执行计算</ChapterLink>
  <ChapterLink href="02-launch-policies">任务什么时候执行：启动策略与等待行为</ChapterLink>
  <ChapterLink href="03-promise-and-future">自行传递结果：`std::promise` 与 `std::future`</ChapterLink>
</ChapterNav>
