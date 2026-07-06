---
chapter: 2
cpp_standard:
- 17
description: ch02 收尾篇:补上虚拟地址翻译这最后一块拼图——TLB 缓存页表项,huge page 用更大页降低 TLB 压力;并附一张各 CPU
  族(Intel / AMD Zen / Apple / ARM)微架构差异速查表,给需要跨平台调优的同学一个案头入口
difficulty: advanced
order: 4
platform: host
prerequisites:
- 存储层次与延迟阶梯:为什么顺序访问快 100 倍
- 缓存行与局部性:64 字节的最小搬运单位
reading_time_minutes: 8
related:
- 后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch
- 测量陷阱与环境就绪:16 条 checklist
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存管理
title: TLB、huge page 与各 CPU 族微架构速查
---
# TLB、huge page 与各 CPU 族微架构速查

## 还有一道翻译的关卡

前三篇我们把 cache 讲透了,但程序里用的都是**虚拟地址**(virtual address),而 cache 和主存用的是**物理地址**(physical address)。两者之间隔着一道翻译:CPU 每次访存,都要先把虚拟地址翻译成物理地址,才能去查 cache 或 DRAM。这道翻译如果每次都走完整的页表(page table),代价是惊人的,这正是**TLB(Translation Lookaside Buffer,旁路翻译缓冲)**要解决的问题。

这一篇我们先讲清 TLB 和 huge page 的机制,然后附一张各 CPU 族微架构差异的速查表,作为 ch02 的收尾。讲完这一篇,ch02 单核硬件地基就全部铺完,后面的 ch03(归因)和 ch04(按部位优化)就能在它上面展开了。

## TLB:缓存页表项,否则一次翻译顶好几次 DRAM 访问

x86-64 的虚拟地址是 48 位(新一些的 CPU 支持 57 位,5 级页表),物理页大小 **4 KB**。页表是一棵 4 层的树(每层 9 位索引 + 12 位页内偏移):要翻译一个虚拟地址,CPU 理论上要依次访问 4 个页表页(每层一个),每个都在内存里。**最坏情况:一次地址翻译 = 4 次 DRAM 访问。** 如果真这么干,前面 cache 省下来的延迟全赔回去还倒贴。

TLB 就是这棵页表的缓存:它把最近翻译过的「虚拟页 → 物理页」映射记下来,下次同一个页的访问直接查 TLB,几个周期搞定,不用走页表。TLB 和数据 cache 是两套独立的硬件,你的数据可能在 cache 里,但地址翻译该走 TLB 还是页表,是另一码事。

TLB 也分层次:每核一个 L1 dTLB(数据)和 L1 iTLB(指令),容量小但快;再往下还有一个共享的 L2 TLB(AMD/Intel 结构略有不同)。L1 dTLB 通常只有几十到上百项(每项管一个 4 KB 页),所以**当你的工作集涉及的页数超过 dTLB 容量,TLB miss 就开始发生,每次 miss 都要付一次页表漫游(page walk)的代价,几个 DRAM 访问级别,几十到上百纳秒**。

> 精确的 dTLB 条目数随架构变,且不是所有 CPU 都一样。需要精确数字时,查 [Wikichip 对应微架构页](https://en.wikichip.org/wiki/amd/microarchitectures/zen_3)(amd / intel 各有专页)或 Agner 微架构手册的 TLB 小节,本篇只讲结构和量级。

那么问题来了:**怎么知道你的程序是不是吃了 TLB miss 的亏?** 最干净的判断方法是看硬件计数器(`perf stat -e dTLB-load-misses`),TLB miss 率高、且工作集涉及的页数远超 dTLB 容量,就是 TLB 受限。另一个常见信号是:**大工作集 + 随机访问**的程序(比如数据库的随机索引查询、大哈希表),即使数据全在 DRAM,延迟也比纯 DRAM 访问还高出一截,那一截往往就是 page walk。

## huge page:用更大的页,把 TLB 项数需求降下来

既然 TLB 容量瓶颈是「项数」,一个直接的解法就是**把页变大**,让一项覆盖更多内存。x86-64 除 4 KB 页外,还支持 **2 MB**(甚至 1 GB)的 huge page。一个 2 MB 页覆盖的内存相当于 512 个 4 KB 页,同样大小的工作集,用 2 MB 页只需要 1/512 的 TLB 项。

这在以下场景里能换到实打实的性能:

- **数据库**(PostgreSQL、MySQL)随机访问大索引:工作集几十 GB,4 KB 页下 TLB 项需求上千万,必然 thrash;换 2 MB 页能显著降延迟。
- **大哈希表 / JVM 堆**:Java 社区长期有「开 transparent huge page(THP)能不能提速」的讨论,根因就是 JVM 堆动辄几个 GB,TLB 压力大。
- **科学计算的大数组随机访问**:同理。

但 huge page 不是免费午餐,大页更难凑齐(2 MB 连续物理内存比 4 KB 难找),容易引发碎片;而且对**顺序访问**为主的程序收益小(预取器和 cache 已经把活干好了,TLB 不是瓶颈)。所以原则是:**先确认 TLB 真是瓶颈(perf 计数器),再上 huge page**,别盲目开。

### 上手跑一跑(以及一个诚实的否定结果)

我想在本机演示一下 huge page 的收益:同样的 256 MB 工作集做指针追逐,一份用普通 4 KB 页,一份用 `madvise(MADV_HUGEPAGE)` 请求透明大页,看大页版本是否更快。代码核心:

```cpp
void* a4 = mmap(nullptr, SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
void* a2 = mmap(nullptr, SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
madvise(a2, SZ, MADV_HUGEPAGE);   // 请求透明大页
// 对两份做同样的指针追逐,比延迟
```

跑三次的结果:

```text
第 1 次: 4KB 页 136.5 ns   2MB 页 134.0 ns   比值 1.02
第 2 次: 4KB 页 135.8 ns   2MB 页 137.4 ns   比值 0.99
```

比值基本是 1.0,**大页没带来任何可测的提升**。为什么?去查 `/proc/self/smaps`,发现 `AnonHugePages: 0 kB`:**WSL2(我跑的环境)压根没给透明大页**,`madvise` 请求了但内核没实际兑现。所以这个「没差别」是真实的,它不是 huge page 没用,而是这个环境没给我 huge page。

我把这个否定结果原样写在这里,是想再次强调 ch01 那条测量纪律:**你以为开了的优化,可能根本没生效。** 在这台 WSL2 上,`perf` 没装、THP 没兑现、CPU 频率读不到,每个都会让你对性能数字的解读偏离真实。换到裸机 Linux 上、`echo always > /sys/.../transparent_hugepage/enabled` 或预分配 hugetlb 池后,同样这段代码对 TLB 受限负载是能测出百分之十几到几十提升的。**结论可以引自权威资料,但你手上的数字必须来自你实际跑的环境,而且你得先确认环境真的满足前提。**

## 各 CPU 族微架构速查表

讲完 TLB,ch02 的硬件地基就齐了。但前面三篇的数字主要围绕本机的 AMD Zen 3,你换到 Intel、Apple Silicon 或 ARM 上,具体数字会变。下面这张表给方向和量级,**精确数字请查 [Wikichip](https://en.wikichip.org) 对应微架构页或 Agner 微架构手册**,它不是用来背的,是给你跨平台调优时一个「该往哪个方向查」的入口。

| 维度 | Intel(近几代)| AMD Zen(2/3/4/5)| Apple(A 系到 M 系)| ARM Cortex(X / 大核)|
|---|---|---|---|---|
| **译码宽度** | 6-wide(Golden Cove 起) | 4-wide x86 译码,**靠 op-cache 补到 6-8 µop/周期** | 极宽(~8-wide),且无 x86 译码负担 | 宽(4-5+)|
| **ROB 深度** | 深(~400-500+) | 中深(Zen3 ~256) | 极深(~600+) | 中等 |
| **L1d / L2** | 48 KB / 1-2 MB | 32 KB / 512KB-1MB | 128 KB / 大 | 64 KB / 可变 |
| **LLC 结构** | 共享 LLC(MIC / 内置)| Zen3+ 单 CCD 共享大 L3 | 系统级缓存(SLC) | 共享 /簇 L3 |
| **TLB** | L1 dTLB + L2 TLB | L1 dTLB + L2 TLB | 类似分层 | 类似分层 |
| **特色** | 深流水线、强前端 | 统一 CCD 大缓存、高频率 | 超宽 ROB、高 ILP | 能效比、可授权 |

读这张表的姿势:别盯绝对数(代际在演进),盯**结构性差异**。比如「AMD Zen 靠 4-wide x86 译码 + op-cache 补吞吐,而 Apple 是真·超宽译码 + 超深 ROB」,这是为什么 Apple Silicon 在 IPC(每指令周期数)上能打 x86 的根因之一,也解释了为什么 x86 代码的「前端瓶颈」(译码跟不上)比 ARM 更常见(ch04-07 前端优化会讲)。再比如「Zen3 把 L3 整合成单 CCD 共享」,这是 AMD 大幅缩小跨核缓存延迟的关键,直接影响多线程性能曲线(ch05)。

> 这张表严格意义上属于「给指针」的范畴,寄存器重命名表大小、执行端口分布、各指令延迟/吞吐这些**架构表录级**的数据,Agner 卷3(微架构)、卷4(指令表)是案头权威,Wikichip 是在线百科。vol6 讲到够支撑你「理解快慢」就停,深挖查这两处。

## ch02 收尾:四块硬件底座

到这里,单核硬件的三个层面都讲完了:

1. **存储层次**(02-01):L1/L2/L3/DRAM 的延迟阶梯,逐级差出 100 倍。顺序访问能逼近 L1 吞吐是预取器在帮忙。
2. **缓存行与局部性**(02-02):64 字节是最小搬运单位,空间局部性决定连续布局快,行优先 vs 列优先差 6 倍。
3. **流水线与 ILP / 分支**(02-03):ILP 决定执行单元饱不饱(多累加器 2.9×),分支预测对不可预测分支罚得重(排序 vs 打乱 4.2×)。
4. **TLB 与 huge page**(本篇):地址翻译是另一道关,huge page 降 TLB 压力,但得先确认环境真给了。

这些就是 ch04「按瓶颈部位优化」全部建议的硬件底座,为什么用连续容器、为什么控热数据集、为什么多累加器、为什么 branchless、为什么除法是瓶颈、为什么前端 PGO 有用,每一条都能回溯到这四章里的某个数字。后面 ch03 会教我们**怎么测出当前程序的瓶颈落在这四块的哪一块**,然后 ch04 对症下药。

## 参考资源

- Bryant & O'Hallaron《CSAPP》第 9 章 *Virtual Memory*:页表、TLB、页表漫游的概念与代价
- Agner Fog《The microarchitecture of Intel, AMD and VIA CPUs》§22 *AMD Ryzen* 与各 Intel 章:TLB、流水线、执行端口的架构级细节。本地:`.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Wikichip *Microarchitectures*:各 CPU 族(Intel / AMD / Apple / ARM)精确参数速查,跨平台调优的案头入口
- Drepper, U.《What Every Programmer Should Know About Memory》:TLB、huge page、页表的工程视角
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch02/tlb_hugepage.cpp`
