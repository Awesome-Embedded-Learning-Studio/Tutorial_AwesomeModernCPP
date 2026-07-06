---
chapter: 5
cpp_standard:
- 17
description: 多核性能不只是「加核就快」。本篇讲扩展性曲线(1/2/4/8 核跑同一任务,实测 1→2.53× 亚线性,为什么远不到理想线性)、Amdahl(固定规模)vs
  Gustafson(规模随核扩大)、NUMA 跨节点访存延迟翻倍、线程绑核 affinity,以及线程创建/栈的成本。WSL2 单 NUMA 节点的限制如实标注
difficulty: advanced
order: 2
platform: host
prerequisites:
- 伪共享:同一缓存行把多核拖回单核
- Amdahl 定律:优化的天花板(ch00)
reading_time_minutes: 5
related:
- 锁的开销与「无锁不是银弹」
tags:
- host
- cpp-modern
- advanced
- 优化
- 并发
title: NUMA、affinity 与扩展性曲线
---
# NUMA、affinity 与扩展性曲线

## 加核不等于线性加速

很多人的直觉是「4 核就比 1 核快 4 倍」,并行化的工作就是「把任务分成 N 份给 N 个核」。真上手会发现:**几乎没有程序能线性加速**。我们用一个最简单的并行任务(分块累加一亿元素)实测,1/2/4/8 线程的加速比:

```text
===== 扩展性曲线(并行累加 1 亿元素)=====
线程数   耗时(ms)    加速比
1              29.3         1.00x
2              17.2         1.70x
4              12.6         2.33x
8              11.6         2.53x
```

**8 线程只换来 2.53 倍加速,远不是理想的 8 倍。** 为什么?三个原因,每一个都是后面要讲的:

1. **Amdahl 定律**:程序里有串行段(汇总结果、同步),它占比再小也锁死了加速上限。ch00-01 讲过 `S = 1/(s + (1-s)/N)`,10% 串行就把无限核加速锁死在 10×。
2. **共享资源争用**:这个例子里,所有核读同一份 DRAM,**内存带宽是共享的**。累加是 memory-bound(回看 ch03-01:dot 的算术强度极低,带宽受限),核一多,带宽先打满,加核没用。**memory-bound 任务扩展性天然差**,compute-bound 任务才好扩展。
3. **NUMA 跨节点**:多 socket 机器上,核访问「远端 socket 的内存」延迟翻 2-4 倍。

扩展性曲线是诊断多核程序的金标准:**跑一遍 1/2/4/8 核,画出来**。理想是一条 45° 斜率的直线;弯下去拐平,就说明撞上了上面三个瓶颈之一。拐点在哪、拐得多狠,告诉你还有多少「加核能买到的性能」。

## Amdahl vs Gustafson:强扩展 vs 弱扩展

扩展性有两个不同的衡量口径,别混:

- **Amdahl(强扩展,strong scaling)**:**固定问题规模**,加核看加速比。上限被串行段锁死。这是大多数「我要把这个程序跑快」场景关心的。
- **Gustafson(弱扩展,weak scaling)**:**问题规模随核数等比扩大**,看「核数翻倍 + 数据翻倍,时间不变」能不能做到。这是 HPC / 大数据场景关心的,数据涨了,加核顶住。

推论:「这个程序扩展性不好」在 Amdahl 语境下是硬伤,在 Gustafson 语境下可能无所谓。取决于你的问题是「固定大小跑快」还是「数据在涨别崩」,先想清楚你关心哪个。

## NUMA:多 socket 的隐藏延迟

**NUMA(Non-Uniform Memory Access)** 是多 socket 服务器的现实:每个 CPU socket 有「自己的」本地内存,访问别的 socket 的内存要走互联总线(QPI/UPI),**延迟翻 2-4 倍**。于是「内存带宽」测成了「互联带宽」,线程跑在 socket 0,数据却在 socket 1 的内存,每次访存都交互联罚金。

NUMA 的对策是把「线程」和「它操作的数据」绑在同一个 socket:

```bash
# 把线程和内存都绑到 NUMA 节点 0
numactl --cpunodebind=0 --membind=0 ./your_app
# 或交织分配(让两节点平均承担,避免一边打满)——但有跨节点惩罚
numactl --interleave=all ./your_app
```

程序层面:线程池按 NUMA 拓扑分组(每 socket 一个池,只处理本 socket 内存上的数据)、数据按 socket 分区。这些是 HPC 和高性能后端的标配。

> **本机局限**:WSL2 在一台单 socket 笔记本上(5800H 单 NUMA 节点,`numactl --hardware` 只列出 node0),**测不了 NUMA 跨节点惩罚**。NUMA 的命令(`numactl`)和数据引自 Bakhvalov §11 + 多 socket 服务器实践。你要测 NUMA,得找一台双路服务器。本节诚实标注「本机测不了」,把它当测量环境的教学点,而不是含糊带过。

## affinity:线程绑核,减少迁移

哪怕单 socket,线程被 OS 在核间**迁移**也有成本:迁移后它的 L1/L2 cache 全冷,要重新预热。`taskset`(临时)/ `pthread_setaffinity_np`(程序内)把线程**绑到固定核**,消除迁移开销:

```bash
# 命令行:把进程绑到核 0-3
taskset -c 0-3 ./your_app
```

```cpp
// 程序内:把线程绑到特定核
cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(core_id, &cpuset);
pthread_setaffinity_np(thread.native_handle(), sizeof(cpuset), &cpuset);
```

绑核对**长运行、cache 敏感**的负载(数据库、流处理)很重要;对短任务意义不大(本来就来不及暖 cache)。绑核还能配合 NUMA,让线程只在本 socket 的核上跑。

> ch01-03「测量陷阱」里第 5/8 条(绑核、NUMA)就是这一节的内容,这里展开讲它们的原理。绑核既是**性能测量**的标准动作(避免迁移噪声),也是**生产部署**的标准动作(稳定 cache 行为)。

## 线程创建与栈的成本

一个容易被忽略的多核成本:**线程本身不免费**。创建一个线程要分配栈(默认 8MB 虚拟地址空间,Linux 触碰多少分配多少)、内核数据结构,耗时几十到上百微秒。所以:

- **别在热路径 `new` 线程**:`std::thread t(...)` 每次都付创建+销毁成本。用**线程池**复用。
- **栈大小可调**:8MB 默认对大多数线程过多,`pthread_attr_setstacksize` 调小(比如 256KB-1MB)能省虚拟内存、改善 TLB 压力(栈也是内存,占 TLB 项)。嵌入式/超高并发场景常见。
- **`std::async` + 默认策略**:可能有隐式线程创建,且和 `std::launch::async` 的语义有坑(vol5 详讲)。

线程池是「正确性归 vol5、成本归 vol6」的典型:怎么写一个无 UB 的线程池是 vol5 的事,这里只讲「为什么你应该用池而不是裸 `std::thread`」的成本动机。

一句话收口:扩展性曲线就是 1/2/4/8 核跑同一任务,看加速比拐不拐平(理想线性,弯了说明撞了 Amdahl / 共享资源 / NUMA);memory-bound 任务因共享内存带宽先打满,扩展性天然差,compute-bound 才好扩展;Amdahl(固定规模)vs Gustafson(规模随核扩大),先想清你关心哪个;NUMA 上线程和数据要绑同 socket(`numactl`),本机 WSL2 单节点测不了;affinity 绑核减少迁移,测量和生产都该用;线程本身不免费,热路径用线程池,栈大小可调。

## 参考资源

- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 11 章 *Multithreaded Apps》(Mark Dawson 撰写,含 NUMA/affinity/扩展性)
- Drepper《What Every Programmer Should About Memory》——NUMA、cache 一致性的工程视角
- `numactl` / `taskset` / `pthread_setaffinity_np` 文档
- ch00-01 性能思维(本卷,Amdahl 定律的出处)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch05/scalability.cpp`
