---
sidebar_order: 2
title: "共享与同步"
description: "mutex 与 RAII 守卫、死锁现场诊断、mutex 家族其他锁、thread_local、condition_variable 与阻塞队列正源、同步原语工具箱：共享数据的协调机制一章配齐"
---

# 共享与同步

第 1 章 咱们解决的是线程本身：怎么开、怎么传参、怎么收尾。可两条线程要碰的数据若是同一份，咱们就得回答三个问题：读和写怎么排队、谁等谁、错了怎么查。卷首的 `先锁再无锁` 说的就是这道顺序：咱们把锁用对、用熟，第 4 章 再谈无锁的事。咱们从锁讲起，把共享数据的协调机制从底到面走一遍。

六篇的走法咱们排好了。第一篇讲的是 mutex 与 RAII 守卫，什么时候上锁、怎么交给守卫管，全卷的锁用法都从它起步。第二篇对付的是死锁：gdb 三命令查谁在等谁，咱们把总锁序、std::lock、try_lock 回退三条防线配齐，层级锁咱们也一并配上。第三篇清点 mutex 家族里的其他锁：recursive_mutex 为什么多数时候是设计绕了弯的信号、timed_mutex 的时限拿法、shared_mutex 的读写分档，咱们按场景认识它们，也认清它们的坑。第四篇讲的是 thread_local，有些数据天生就该是每线程一份的，咱们根本不用抢。第五篇接手的是 mutex 和 gdb 都接不住的等待：condition_variable 与阻塞队列就是这一篇的正题，也在全卷的六大正源之列。咱们把等待配上谓词、把唤醒管住不丢，全用一个能塞能堵的队列把这些功夫练熟。第六篇把工具箱收了进来：latch、barrier、semaphore、call_once 这些零碎咱们按场景取用，篇末还给了一张选型表。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-mutex-and-raii-guards">mutex 与 RAII 守卫</ChapterLink>
  <ChapterLink href="02-deadlock-and-gdb">死锁与现场诊断</ChapterLink>
  <ChapterLink href="03-mutex-family">mutex 家族：标准库里的其他锁</ChapterLink>
  <ChapterLink href="04-thread-local">thread_local：每线程一份的世界</ChapterLink>
  <ChapterLink href="05-condition-variable-and-bounded-queue">condition_variable 与阻塞队列</ChapterLink>
  <ChapterLink href="06-sync-primitives-toolkit">同步原语工具箱</ChapterLink>
</ChapterNav>
