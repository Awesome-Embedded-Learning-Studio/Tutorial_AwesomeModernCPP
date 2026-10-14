---
title: "volatile 到底管什么：它禁的是编译器，不是数据竞争"
description: "volatile 是最常被误解的关键字之一。这个主题只讲它真正管的那件事：约束编译器对访存指令的合并、删除与重排，数据竞争它一点也管不了，边界在哪拿反汇编划出来。"
sidebar_order: 7
---

# volatile 到底管什么：它禁的是编译器，不是数据竞争

volatile 是最常被误解的关键字之一。这个主题只讲它真正管的那件事：约束编译器对访存指令的合并、删除与重排，数据竞争它一点也管不了，边界在哪拿反汇编划出来。

<ChapterNav variant="sub">
  <ChapterLink href="01-volatile-bans-in-one-thread">volatile 在单线程里禁掉的三件事：合并、消除与重排</ChapterLink>
  <ChapterLink href="02-volatile-race-and-lock-prefix">volatile 拦不住数据竞争：丢更新现场与 lock 前缀的分界</ChapterLink>
  <ChapterLink href="03-standard-deprecation-mmio-and-verdict">volatile 的标准边界：弃用清单、设备寄存器与判断法</ChapterLink>
</ChapterNav>
