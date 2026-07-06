---
title: "多核性能"
description: "ch05 承接 vol5(同步原语/无锁的正确性归 vol5),只讲多核带来的性能衰减与成本:伪共享(同 cacheline 把多核拖回单核,实测一个数量级,单次约 18× 浮动大)、NUMA 与扩展性曲线(1→8 线程亚线性 2.53×)、锁的开销与「无锁不是银弹」(无竞争 mutex 3.6 倍于 atomic)"
---

# 多核性能

vol5 把**同步原语、内存序、无锁数据结构的正确性**讲透了。这一章不重复那些机制,只回答一个性能问题:**多核带来的性能衰减怎么测、怎么改,各种同步方式各贵多少纳秒。**

三篇:

- **05-01 伪共享**:两个核频繁写同一条 64 字节 cacheline 上的不同变量,触发 MESI 一致性 invalidate 往返,实测慢**一个数量级**(单次约 18×,倍数随运行浮动大)。对策是 `alignas(64)`。
- **05-02 NUMA 与扩展性曲线**:多 socket 跨节点访存延迟翻 2-4 倍;扩展性曲线(1→8 线程实测 2.53×,亚线性)诊断「加核能买到多少性能」;Amdahl vs Gustafson;线程绑核 affinity;线程创建/栈成本。
- **05-03 锁 vs 无锁成本**:无竞争 mutex 纳秒级(约 3.6 倍于 atomic),有竞争暴涨;无锁不是银弹(ABA、重试风暴、内存回收),分片锁常胜无锁。

边界:**「怎么写正确的同步、原子操作内存序、无锁实现」归 vol5**;vol6 只答「各贵多少、什么场景选哪个」。

> 本机 WSL2 单 socket 单 NUMA 节点,NUMA 跨节点惩罚测不了(05-02 如实标注,内容引自多 socket 服务器实践)。伪共享、扩展性、锁开销都是本机实测。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="05-01-false-sharing">伪共享:同一缓存行把多核拖回单核</ChapterLink>
  <ChapterLink href="05-02-numa-scaling">NUMA、affinity 与扩展性曲线</ChapterLink>
  <ChapterLink href="05-03-locks-vs-lockfree">锁的开销与「无锁不是银弹」</ChapterLink>
</ChapterNav>
