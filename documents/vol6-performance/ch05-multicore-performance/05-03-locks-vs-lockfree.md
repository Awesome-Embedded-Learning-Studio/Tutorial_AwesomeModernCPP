---
chapter: 5
cpp_standard:
- 17
description: 本篇从成本视角看并发同步:无竞争 mutex 的 fast path 是纳秒级、约 3.6 倍于 atomic,有竞争时暴涨;无锁不是银弹(ABA、重试风暴、内存回收难题),很多场景分片锁比无锁更快更简单。「该不该用无锁」是成本/复杂度权衡,归
  vol6;「怎么写无锁」归 vol5
difficulty: advanced
order: 3
platform: host
prerequisites:
- NUMA、affinity 与扩展性曲线
- 伪共享:同一缓存行把多核拖回单核
reading_time_minutes: 6
related:
- std::atomic 与内存序(vol5)
tags:
- host
- cpp-modern
- advanced
- 优化
- 并发
- 无锁
title: 锁的开销与「无锁不是银弹」
---
# 锁的开销与「无锁不是银弹」

## 从成本视角看同步

ch05 前两篇讲了多核的物理瓶颈(伪共享、NUMA、扩展性)。这一篇换到**同步原语**的成本视角:`std::mutex`、`std::atomic`、无锁数据结构,各贵多少、什么时候值。

先划边界(这条最重要):**「怎么写正确的同步、原子操作的内存序语义、无锁数据结构的实现」归 vol5**。vol6 只回答一个问题——**「这些同步方式各贵多少纳秒、什么场景该选哪个」**。这是成本视角,不是机制教学。

## 无竞争锁:纳秒级,但有 3-4 倍于 atomic 的开销

很多人对 `std::mutex` 的印象是「慢」。**无竞争(uncontended)时其实不慢**——现代 mutex 的 fast path 是用户态 CAS(自旋几下),拿不到才陷入内核。我们测单线程(完全无竞争)自增一亿次:

```text
===== 同步开销(单线程自增 1 亿次,ns/op)=====
  非原子 int(基线):           0.00 ns
  atomic relaxed(无锁,弱序):  1.86 ns
  atomic seq_cst(默认强序):    1.84 ns
  mutex 无竞争(加解锁):       6.60 ns
  mutex/atomic_relaxed = 3.6x
```

读出几件事:

- **非原子 int 基线 0.00 ns**:编译器把 `++plain` 优化成寄存器自加,无内存写入,这是「无同步」的极限。
- **`atomic` 约 1.8-1.9 ns/op**:这是无锁原子操作的成本(`fetch_add` 在返回值被丢弃时常被 GCC 优化成 `lock addq`(常量原子加),返回值被使用时才是 `lock xadd`;都带 LOCK 前缀,缓存行对齐时走 cache lock 而非总线锁)。**`memory_order_relaxed` 和默认 `seq_cst` 在单线程下几乎一样**——它们的差别在跨线程 ordering,单线程自增没有跨线程可见性需求,差别显现不出来。
- **`mutex` 无竞争 6.6 ns/op**:**约 3.6 倍于 atomic**。这个倍数是无竞争 mutex 的典型开销——fast path 几条指令(CAS 拿锁、CAS 还锁),比单条 `lock xadd` 多几条。

所以**无竞争场景下,mutex 比 atomic 慢 3-4 倍,但都是纳秒级**。如果你的临界区只有一次原子自增,用 mutex 确实浪费(直接 atomic 更快);但如果临界区有几十条指令,mutex 那 6 纳秒开销相对临界区本身微不足道,**可读性和正确性远比这点开销重要**。

## 有竞争:mutex 代价暴涨

上面是**无竞争**。`mutex` 真正贵的是**有竞争(contended)**时:多个线程同时抢,fas path CAS 失败 → 自旋 → 还拿不到 → 陷入内核(`futex` 系统调用),线程被挂起、唤醒,涉及上下文切换(每个几微秒)。一个高度竞争的 mutex 可能让程序退化成「内核调度器在切换线程,业务几乎没跑」。

这就是无锁数据结构的动机:**避免陷入内核、避免上下文切换**。`std::atomic` 的 `lock xadd` 始终在用户态、始终无锁(不陷入内核),无论多竞争。对**极高竞争 + 极小临界区**(比如全局计数器、简单队列)的场景,无锁确实赢。

但——

## 无锁不是银弹

「无锁」听起来像银弹,实际坑极深:

1. **ABA 问题**:无锁的「比较-交换(CAS)」循环里,值从 A→B→A,CAS 以为「没变过」而成功,实际中间被改过。经典无锁栈 `pop` 反复 CAS,节点被释放又重用就中招。解法(tagged pointer、hazard pointer、epoch-based reclamation)都复杂且易错。
2. **重试风暴**:多个线程同时 CAS 同一个变量,只有一个赢,其它全失败重试,高竞争下 CPU 全耗在重试上,比 mutex 还惨(叫「thundering herd」或「live-lock」)。
3. **内存回收难**:无锁数据结构里,「这个节点还能不能释放」本身就是个并发难题(别的线程可能还拿着指针)。这是无锁编程最硬核的部分。
4. **写对极难**:无锁代码的正确性靠内存序(`acquire/release/seq_cst`)精细配合,错一个就是数据竞争 UB,且 TSan 都不一定抓得全。

**结论:无锁是高门槛、高复杂度的工具,不是「更快」的同义词**。很多场景,**分片锁(sharded locks)** 比无锁更快更简单:把一个共享结构切成 N 片,每片一把锁,线程大概率各操作不同片、不竞争。比如分片哈希表:N 个桶组、每组一把 mutex,实际竞争被摊薄到接近无竞争。这种「分片 + 锁」在工程上经常打败无锁,因为:

- 无竞争 mutex fast path 极快(纳秒级,前面实测);
- 分片把竞争降到无竞争,享受 fast path;
- 代码简单、正确性容易保证。

## 决策框架:什么时候用什么

| 场景 | 推荐 | 理由 |
|---|---|---|
| 单纯计数器 / 简单统计 | `std::atomic` | 一条 `lock xadd`,无临界区,最便宜 |
| 复杂共享结构,竞争不强 | `std::mutex` + RAII | 可读、正确、无竞争 fast path 够快 |
| 高竞争共享结构 | **分片锁** | 摊薄竞争,简单可靠,常胜无锁 |
| 极端高并发 + 极小临界区 + 团队能驾驭 | 无锁数据结构 | 有它的场景,但要付复杂度税 |
| 跨线程只读共享 | `std::shared_ptr` / 直接共享 const | 读不竞争,无需同步 |

**「该不该用无锁」是成本/复杂度权衡,归 vol6;「怎么写正确的无锁」归 vol5。** 先问「mutex + 分片够不够」,大多数时候够了;真不够再上无锁,且配合严谨的 TSan 验证。

## 小结

- **无竞争 mutex** 纳秒级、约 3.6 倍于 atomic;**有竞争**暴涨(陷入内核、上下文切换)。
- **atomic 单条操作始终用户态、始终无锁**,适合简单计数器。
- **无锁不是银弹**:ABA、重试风暴、内存回收、写对极难。
- **分片锁经常打败无锁**:摊薄竞争 + 简单可靠。先考虑分片,再考虑无锁。
- vol6 只答「各贵多少、选哪个」;「怎么写对」归 vol5。

ch05 多核性能到这里就讲完了——伪共享、NUMA/扩展性、同步成本。下一篇我们换到 ch06,从「C++ 抽象的性能成本」角度,看那些 C++ 特有特性(虚函数、异常、std::function、optional/variant)各贵多少。

## 参考资源

- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 11 章 *Multithreaded Apps》
- Pikus《The Art of Writing Efficient Programs》——并发性能、分片 vs 无锁的权衡(本地)
- `std::mutex` / `std::atomic` / 内存序:cppreference;深度归 vol5
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch05/lock_cost.cpp`
