---
chapter: 5
cpp_standard:
- 17
description: 伪共享(false sharing)是多核性能最隐蔽也最戏剧性的坑——两个不相关的变量碰巧挤在同一条 64 字节缓存行,两个核分别写,硬件一致性协议每次都让对方
  cacheline 失效,实质串行。本篇用两个线程自增计数器实测:伪共享比 alignas(64) 慢一个数量级(本机单次约 18×,倍数随运行浮动大),并讲清
  MESI 一致性协议的机制
difficulty: advanced
order: 1
platform: host
prerequisites:
- 缓存行与局部性:64 字节的最小搬运单位
- Benchmark 方法论参考卡
reading_time_minutes: 6
related:
- NUMA、affinity 与扩展性曲线
- 锁的开销与「无锁不是银弹」
tags:
- host
- cpp-modern
- advanced
- 优化
- 并发
title: 伪共享:同一缓存行把多核拖回单核
---
# 伪共享:同一缓存行把多核拖回单核

## ch02 埋的伏笔,这里兑现

ch02-02 讲缓存行时埋了一个伏笔:缓存行的「共享」在单核是红利(空间局部性),在多核可能变成税。这一篇就兑现它。

回忆:缓存行是 cache 的最小单位,64 字节,而且是**一致性(coherence)的最小单位**——硬件保证「同一条缓存行在整个系统里是一致的」。多核场景下,这个保证的代价就是**伪共享(false sharing)**:两个核分别频繁写**同一条缓存行上的不同变量**,硬件为了维持一致性,每次写都让对方核持有的这条 cacheline 失效,两个核**实质上被迫串行**——「看起来并行,实则互相踢 cache」。

这是多核性能最隐蔽的坑:代码层面两个线程操作的是**完全不相关的变量**,逻辑上没有共享,但性能上被一条看不见的缓存行绑在一起。它不会让程序出错(正确性没问题),只会让程序莫名其妙地慢。

## MESI 一致性:为什么 cacheline 是多核战场

理解伪共享要先理解**缓存一致性协议**(x86 用 MESI 及其变种)。每个 cacheline 在每个核的 cache 里有一个状态:

- **M(odified)**:只有我这个核有,且我改过(脏)。
- **E(xclusive)**:只有我这个核有,没改过(干净)。
- **S(hared)**:多个核都有同一份(干净)。
- **I(nvalid)**:失效,不能用。

当核 A 要写一条 S 状态(共享)的 cacheline 时,它必须先发一个「我要写了,你们把这条作废」的信号给其它核,其它核把这条 cacheline 标记为 I。下次核 B 要用这条 cacheline 时,发现自己那份是 I,得重新去拿(Cache-to-cache 或从内存)。**这个「让对方失效 + 重新获取」的往返,就是伪共享的开销来源。**

关键:**一致性粒度是 cacheline(64 字节),不是单个变量**。所以哪怕核 A 写 `counter_a`、核 B 写 `counter_b`,只要 `counter_a` 和 `counter_b` 在同一条 64 字节里,硬件就当成「同一条在两边被改」,触发完整的 invalidate 往返。变量逻辑上无关,物理上同船。

## 上手跑一跑:一个数量级的代价

经典场景:两个线程各有一个计数器,各自自增一亿次。逻辑上完全独立。

```cpp
// A. 伪共享:两个 atomic<long> 紧挨着,同一条 cacheline
struct BadCounters { std::atomic<long> a{0}; std::atomic<long> b{0}; };  // sizeof = 16B
// B. 无伪共享:每个 alignas(64) 独占 cacheline
struct alignas(64) PaddedCounter { std::atomic<long> v{0}; };
struct GoodCounters { PaddedCounter a; PaddedCounter b; };              // sizeof = 128B
```

两个线程分别只动自己的计数器(`a` 和 `b`),跑同样次数:

```text
===== 伪共享(2 线程各自自增 1 亿次)=====
  伪共享(同 cacheline):      467.0 ms
  alignas(64)(独占 cacheline):  26.0 ms
  伪共享/对齐 = 18.0x
  sizeof(BadCounters)=16  sizeof(GoodCounters)=128
```

**接近 20 倍(量级)。** 同样的计算量、同样的线程数,只因为两个计数器有没有挤在同一条 cacheline,差出一个数量级。**注意绝对倍数随运行浮动很大**——本机同硬件多次复现,倍数在 15×–48× 之间都见过(上面这次是 18×),WSL2 的调度噪声会放大抖动;但「差一个数量级」这个结论是稳定的。`BadCounters` 是 16 字节(两个 `atomic<long>` 挤一条 64B cacheline);`GoodCounters` 用 `alignas(64)` 让每个计数器独占一条,共 128 字节,伪共享消失。

这就是伪共享的杀伤力:**它能让一个「看起来完美并行」的程序,慢到接近单线程**。更阴险的是,正确的 TSan/ASan 都查不出它(因为不是数据竞争、不是 UB,逻辑正确),只有**面向性能的 profiler** 能抓:

```bash
# perf 的伪共享专用工具(本机 WSL2 无 perf,命令引自 KDAB/Brendan Gregg):
perf c2c record -- ./your_app
perf c2c report
# 看 HITM(Hit Modified)计数,高的地方就是伪共享重灾区
```

## 对策:alignas(64) 让热变量独占 cacheline

解法直接得几乎粗暴——**让会被不同核频繁写的变量,各自独占一条 cacheline**。C++ 里用 `alignas(64)`:

```cpp
struct alignas(64) AlignedCounter { std::atomic<long> v{0}; };
```

`alignas(64)` 强制这个结构的地址 64 字节对齐,且 `sizeof` 也补到 64 的倍数,于是它独占整条 cacheline,别人挤不进来。常见用法:

- **每线程统计计数器**:线程 `i` 写 `counters[i]`,如果 `counters` 是 `atomic<long>[]` 就伪共享;改成 `alignas(64)` 的结构数组就不。
- **无锁数据结构的 per-thread 数据**:ring buffer 的每线程槽位。
- **线程池的 per-worker 状态**。

注意 `alignas(64)` 会**浪费内存**(每个计数器占 64B 而不是 8B),但对热变量这笔买卖划算——省下的 cacheline 往返远比浪费的内存值钱。别对冷变量乱用。

C++17 起还有个更优雅的写法——把 per-thread 数据放到 `thread_local`,编译器自然给每个线程独立实例,从根上避免共享。但 `thread_local` 有它自己的坑(初始化开销、和线程池配合),具体取舍看场景。

> 边界提醒:伪共享是**性能**问题,归 vol6;「怎么写正确的多线程同步、原子操作的内存序语义」归 vol5。本篇只讲「多核下缓存行的性能代价」。

## 小结

- **伪共享**:多个核频繁写同一条 cacheline 上的不同变量,触发 MESI invalidate 往返,实质串行。
- **实测差一个数量级**:两个独立计数器同 cacheline vs `alignas(64)` 独占(本机单次约 18×,倍数随运行浮动 15×–48×;「一个数量级」是稳定结论)。
- **对策**:`alignas(64)` 让每核频繁写的变量独占 cacheline;或 per-thread 数据用 `thread_local`。
- **查伪共享**:`perf c2c`(看 HITM 计数);TSan/ASan 查不出(它不是正确性问题)。
- 一致性协议的**深度**(MESI 状态机、MOESI/MESIF 变种、cache-to-cache transfer)归体系结构课,vol6 只讲「cacheline 是一致性单位」这一层足以指导改代码。

下一篇讲多核的另一个放大器——NUMA,以及怎么用扩展性曲线判断你的并行程序「扩展得好不好」。

## 参考资源

- CppCoreGuidelines CP.3 *false sharing*——伪共享的定义与对策
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》§11.7 *Detecting Coherence Issues》(Mark Dawson 撰写)
- Drepper《What Every Programmer Should Know About Memory》——MESI 与多核 cache 一致性
- perf c2c 文档(KDAB 有详细教程)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch05/false_sharing.cpp`
