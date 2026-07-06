---
chapter: 3
cpp_standard:
- 17
description: TMAM 答的是「哪类瓶颈」,火焰图答的是「哪段代码」。本篇讲 perf record 的标准采样工作流、怎么生成并读懂 on-CPU /
  off-CPU 火焰图,以及两个进阶工具——COZ(因果 profiler,告诉你优化哪个函数最值)和 eBPF(现代可编程 tracing)
difficulty: advanced
order: 3
platform: host
prerequisites:
- TMAM 四桶与硬件采样
- Benchmark 方法论参考卡
reading_time_minutes: 8
related:
- USE 方法与 Roofline 模型
- 归因实战:从一个慢程序到定位瓶颈
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: 火焰图、perf 工作流与 COZ / eBPF
---
# 火焰图、perf 工作流与 COZ / eBPF

## 从「哪类瓶颈」到「哪段代码」

上一篇的 TMAM 把瓶颈归到流水线的某一桶(Frontend / Backend / Bad Spec),但「Backend Memory Bound 60%」这个结论还落不到代码行上,你得知道是哪个函数、哪段循环在制造那些 cache miss,才能动手。这就需要能按代码位置聚合的 profiler。

Brendan Gregg 发明的**火焰图(Flame Graph)**是这类工具里最好读的:它把采样的调用栈画成一张「层层叠叠的盒子图」,一眼就能看出时间花在哪条调用链上。配合 Linux 自带的 `perf`,这套组合是日常 profiling 的绝对主力。本篇讲 perf 工作流 + 火焰图读法,再介绍两个进阶工具(COZ 因果 profiling、eBPF 可编程 tracing)。

## perf record 工作流

整个流程是「采样 → 折叠栈 → 画图」三步。采样用 `perf record`:

```bash
# 标准 on-CPU 采样:99Hz、用 DWARF 调用栈(不依赖 frame pointer)
perf record -F 99 --call-graph dwarf -- ./app

# 采样完后导出
perf script > out.perf

# 折叠 + 画图(Brendan Gregg 的 FlameGraph 仓库脚本)
./stackcollapse-perf.pl out.perf > out.folded
./flamegraph.pl out.folded > out.svg
# 用浏览器打开 out.svg,鼠标可悬停/点击放大
```

几个**关键参数和坑**(都是 ch01-03「测量陷阱」的延伸):

- **`-F 99`(采样频率 99Hz)**:为什么不是 100Hz?因为 100 是很多内置计时器的「整点」,容易和系统其它周期性事件锁相共振,采样出现规律性偏差;99 是个奇数(而且不整除 100),采样点不容易和系统整点周期事件对齐。这是性能分析里一个常见的小约定。**注意 99 不是素数**(99 = 9 × 11),选它只是为了取一个不与 100 整除的奇数,不是因为它有素数性质(Brendan Gregg 的 perf 文档里只说「99 Hertz」,从未解释成「素数」)。
- **`--call-graph dwarf`(用 DWARF 调试信息重建栈)**:GCC 默认 `-fomit-frame-pointer`(省掉帧指针,多一点可用寄存器),这会让 `perf` 没法用「回溯帧指针」的方式重建调用栈,栈是断的。两个解法:**编译时加 `-fno-omit-frame-pointer`**(推荐,几乎零成本,profiling 时的标准做法),或者**采样时用 `--call-graph dwarf`**(靠 DWARF 调试信息重建栈,慢一点但不用改编译)。release 二进制如果要 profile,务必加 `-fno-omit-frame-pointer`。
- **采样是统计性的**:`perf record` 是采样,不是 trace。99Hz 跑 10 秒只采 ~990 个样本/核,短函数可能一个样本都没有。要看稀有热点,提高频率或延长运行;要看一次性的启动开销,改用 trace 工具(perf c2c / Intel PT / ftrace)。

## 怎么读火焰图

火焰图的结构:

- **y 轴(垂直)是调用栈深度**:底部是入口(`main`),上面每一层是被调用的函数。一个盒子架在另一个盒子上 = 「下面那个调用了上面那个」。
- **x 轴(水平)是样本数(不是时间顺序!)**:盒子越宽,这个函数(及其子调用)在 on-CPU 采样里占比越大。**x 轴左右顺序不代表执行先后**,只是按字母序排的聚合。
- **读法**:找**最宽的盒子**,那是最耗 on-CPU 时间的函数。但它不一定是该优化的函数,要看它架在谁上面(调用栈上下文)。

两个常见误读要避开:

1. **「最宽的盒子就一定要优化」**:不对。一个「宽而平」的盒子(没有上面的子盒子)是真热点,值得优化;一个「宽但上面摞着一大摞」的盒子,只是因为它调用的子函数耗时,优化它自己没用,要优化它顶上那些子盒子里最宽的。
2. **「x 轴是时间」**:不是。火焰图是聚合,不是 timeline。想看「按时间顺序哪段时间忙」,要用 timeline / chrometrace 那类工具。

### 两个重要变种:on-CPU vs off-CPU

标准火焰图是 **on-CPU**(CPU 上在跑什么),回答「算的时间花在哪」。但它看不到「**在等什么**」。如果程序慢是因为在等锁、等 IO、等 sleep,on-CPU 火焰图会是空的(因为采样时 CPU 根本没在跑你的程序)。

这种情况下用 **off-CPU 火焰图**:它采样的是「**线程离开 CPU 那一刻的调用栈**」,即「在等的时候,你等在哪个函数」。off-CPU 图上最宽的盒子,就是你最该减少的等待。Brendan Gregg 把 on-CPU 和 off-CPU 比作硬币两面,on-CPU 看算、off-CPU 看等,两者一起才完整。生产环境里很多「慢」其实是等(等数据库、等锁、等网络),off-CPU 才是关键。

生成 off-CPU 图需要 bcc / bpftrace(eBPF 工具,下面讲),比 on-CPU 麻烦,但治「卡顿」类问题不可替代。

## COZ:因果 profiler,告诉你「优化哪个函数最值」

普通 profiler(火焰图、perf)有个根本局限:它告诉你「时间花在哪」,但不告诉你「优化哪里收益最大」。一个函数占 50% 时间,你把它快 2 倍,总时间能减多少?直觉是减 25%,但 Charlie Curtsinger 团队的 COZ 论文(*COZ: Finding Code that Counts with Causal Profiling*, SOSP 2015)指出,这假设了「优化这个函数不影响其它函数的耗时」,而实际上函数之间有共享资源(锁、cache),优化一处可能让别处变慢。

COZ 用**因果 profiling**解决这个问题:它在程序运行时虚拟地「提速」某个目标函数,但做法不是真的让那个函数变快,而是反过来,**给所有其它并发运行的线程插入 pause,把它们拖慢**(Bakhvalov《Performance Analysis and Tuning on Modern CPUs》§11.5;Curtsinger & Berger, SOSP 2015)。把别人拖慢,在数学上就等效于把目标函数被提速了。然后它测量整体提速多少,因为控制住了「其它线程的时间」这个变量,测到的整体变化才能干净地归因到目标函数,这正是「因果」二字的含义。这样它能直接回答「如果我把函数 X 提速 10%,整个程序快多少」,这才是「值不值得优化」的真正判据。

COZ 的输出是「每个函数的『虚拟提速 → 整体提速』曲线」,斜率最高的函数就是收益最大的优化目标,哪怕它自己占的 CPU 时间不是最多。这跟火焰图「找最宽盒子」是不同的视角,对优化优先级更准。COZ 在 Linux 上开源(curtsinger.cc/coz),用法是链接 COZ 运行时、跑程序、看 profile。

## eBPF:现代可编程 tracing 底座

**eBPF**(extended Berkeley Packet Filter)是近年 Linux 性能工具的最大变量。简单说,它让你在内核里安全地跑小程序,挂到各种 hook 点(系统调用、内核函数、tracepoint、USDT……),按需采集数据。它不需要改内核、不需要重启、开销可控。

为什么性能分析要关心 eBPF?因为前面一堆工具都建立在它上面:

- **off-CPU 火焰图**:bcc 的 `profile` 或 bpftrace 能采 off-CPU 栈。
- **`perf` 的很多功能**:新一代基于 BPF 的工具(`bpftrace` 一行命令)更灵活。
- **系统级追踪**:`biosnoop`(块 IO 延迟)、`execsnoop`(新进程)、`tcplife`(TCP 连接生命周期)……Brendan Gregg 的 bcc 工具集几十个,全是 eBPF 写的。

对 C++ 后端开发,最实用的是 **bpftrace**,一种「性能分析专用的一行 DSL」,比如一行命令追某个用户态函数被调用时的延迟分布:

```bash
# 追踪 mysqld 的某函数延迟(us),直方图
bpftrace -e 'uprobe:/path/to/bin:my_func { @start[tid] = nsecs; }
             uretprobe:/path/to/bin:my_func /@start[tid]/ {
               @lat = hist((nsecs - @start[tid]) / 1000); delete(@start[tid]);
             }'
```

eBPF 的深度(怎么写 BPF 程序)超出 vol6 范围,这里只让你建立一个认知:**现代 Linux 性能工具的底座是 eBPF,off-CPU 和系统级 tracing 都靠它**。需要时,brendangregg.com 有完整教程。

把这一篇压成一张速查:日常 on-CPU profiling 的主力是 perf record + 火焰图,务必 `-fno-omit-frame-pointer` 或 `--call-graph dwarf`,否则栈断;读火焰图找宽盒子,但要区分「自己宽(真热点)」和「上面摞的宽(子调用耗时)」,x 轴是聚合不是时间;off-CPU 火焰图看「在等什么」,治卡顿类问题,靠 eBPF(bcc/bpftrace);COZ 是因果 profiler,直接告诉你优化哪个函数整体收益最大,比「找最宽盒子」更准;eBPF 是现代 Linux 性能工具的底座,off-CPU、系统级 tracing 都建立在它上面。

到这里,ch03 的四套工具(USE / Roofline / TMAM / 火焰图+COZ+eBPF)就讲完了。下一篇我们用它们串一个完整工作流:拿到一个慢程序,从「它慢」一路定位到「这里就是瓶颈」。

## 参考资源

- Brendan Gregg, *Flame Graphs*, brendangregg.com/FlameGraphs/cpuflamegraphs.html——火焰图原文、生成脚本(FlameGraph 仓库)、读法
- Gregg, *Brendan Gregg's perf Examples* / *perf one-liners*——perf 工作流速查
- Curtsinger et al. *COZ: Finding Code that Counts with Causal Profiling*, SOSP 2015——因果 profiling,curtsinger.cc/coz 开源实现
- Gregg, *BPF Performance Tools*(书)/ bcc / bpftrace 文档——eBPF 工具集
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 11.5–11.6 节——COZ 与 eBPF 在 CPU 调优里的应用
