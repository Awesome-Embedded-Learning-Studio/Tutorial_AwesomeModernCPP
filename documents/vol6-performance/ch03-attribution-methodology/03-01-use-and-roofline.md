---
chapter: 3
cpp_standard:
- 17
description: 测出慢之后要回答为什么慢。先学两套互补的高层归因框架:Brendan Gregg 的 USE method(对每个资源查利用率/饱和度/错误,先看全局排除系统性瓶颈),和
  Roofline 模型(用算术强度一眼判断你的代码是该减访存还是该加 SIMD)
difficulty: advanced
order: 1
platform: host
prerequisites:
- 存储层次与延迟阶梯:为什么顺序访问快 100 倍
- Benchmark 方法论参考卡
reading_time_minutes: 7
related:
- TMAM 四桶与硬件采样:LBR / PEBS / Intel PT
- 火焰图、perf 工作流与 COZ / eBPF
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: USE 方法与 Roofline 模型:先看全局,再判算力还是带宽
---
# USE 方法与 Roofline 模型:先看全局,再判算力还是带宽

## 测出「慢」之后,先别急着改代码

ch01 教我们怎么把性能测准,ch02 给了硬件底座。但真正上手优化一个「慢」的程序时,你会立刻撞上两个问题:**它到底慢在哪?** 是 CPU 算不过来,还是数据搬不过来,还是根本没在算(在等锁、等 IO)?第二个问题更阴险:**这瓶颈值不值得改?** 你花一天把某个函数提速 3 倍,可如果它只占总耗时 2%,用户根本感知不到。

这一章(归因方法论)就是回答这两个问题的。它不直接提速,而是给你一套**从「慢」到「瓶颈在哪儿、占多大比例」的定位流程**。定位准了,ch04 的「按部位优化」才能对症下药;定位错了,就是瞎忙。

我们分三篇讲三套互补的工具,再加一篇综合实战:本篇讲两套**高层框架**(USE 看系统全局、Roofline 判算力 vs 带宽),03-02 讲 Intel 的 **TMAM 四桶**(把瓶颈归到流水线哪一段),03-03 讲 **火焰图**(定位到具体代码行),03-04 把它们串成一个完整工作流。三套工具的关系是:**USE 先排除系统性问题 → Roofline 一眼判算力/带宽 → TMAM 下钻到流水线部位 → 火焰图定位到代码**。

> 本章的工具大多是 `perf` / `toplev` / 火焰图脚本这类系统级 profiler。我写文章这台机器是 WSL2,`perf` 没装、`toplev` 也跑不了。所以本章的命令和输出我**引自权威资料(Brendan Gregg、Bakhvalov、easyperf.net)并标注**,而不是假装我在这台机器上跑过。命令本身是 Linux 性能分析的标准动作,换到一台装好 perf 的裸机 Linux 上照样能用。这种「环境制约下老实交代」的纪律,本身也是 ch01 测量方法论的一部分。

## USE 方法:对每个资源查三件事

USE 是 Brendan Gregg 提出的一套**系统级体检**框架,名字是三件事的缩写——对系统里**每一个资源**,分别检查:

- **U**tilization(利用率):忙的时间占比。
- **S**aturation(饱和度):排队/等待的长度(已经有活干不完)。
- **E**rrors(错误):错误计数(硬件错、丢包、重传……)。

资源包括:CPU、内存、磁盘、网络、总线、互斥锁、线程池、连接池……任何可能成为瓶颈的东西。USE 的价值在于它是**穷举式的早期排查**——你不用猜瓶颈在哪,把每个资源的 U/S/E 都扫一遍,哪个饱和先治哪个,避免「盯着一处优化,真瓶颈在别处」。

举几个资源到指标的对应(完整表见 brendangregg.com/usemethod.html):

| 资源 | 利用率 | 饱和度 | 错误 |
|---|---|---|---|
| CPU | `vmstat 1` 的 `%us`+`%sy` | `vmstat` 的 `r` 列(运行队列长度)> 核数 | — |
| 内存 | `free -m` / `sar -B` | `vmstat` 的 `si`/`so`(换入换出)> 0 | `dmesg` 的 OOM |
| 网络 | `sar -n DEV` 的 `rxkB/s` | `ifconfig` 的 drops / `netstat -L` 溢出 | `ifconfig` 的 errors |
| 磁盘 | `iostat -xz 1` 的 `%util` | `iostat` 的 `avgqu-sz` / `await` | `dmesg` / smart |

USE 有个**反直觉**的点要特意记住:**平均利用率低也可能饱和**。5 分钟均值 80% 的 CPU,可能藏着秒级 100% 的尖刺;所以看饱和度(队列长度)比看平均利用率更早暴露问题。这条推论在 ch01-03「测量陷阱」里也出现过(均值被长尾拉偏)——统计上是一回事。

USE 在性能调查的**最早期**用,几分钟扫一遍系统,排除掉「内存换页了」「磁盘打满」「网络丢包」这类明显问题,再下沉到 ch02 讲的微架构层面。它不回答「哪行代码慢」,但能让你**别一开始就钻进代码的死胡同**。

## Roofline 模型:算力屋顶 vs 带宽屋顶

USE 看完系统全局、确认瓶颈在 CPU 计算上之后,下一步要分清:**是算力不够(Core Bound),还是数据喂不上(Backend Memory Bound)?** 这两种瓶颈的对策完全相反——算力受限要加 SIMD、减指令;带宽受限要减访存、改数据布局。判断反了,优化白做。

Roofline 模型(Williams et al., CACM 2009)给了一个极简的判据。把程序画在一张二维图上:

- **横轴**:算术强度(arithmetic intensity,AI)= 每字节内存流量做了多少次运算,**ops/byte**。
- **纵轴**:可达算力,**ops/s**(或 FLOPS/s)。
- **两条屋顶线**:水平线 = CPU 峰值算力;斜线 = 峰值内存带宽(`ops/s = bytes/s × AI`,所以是一条过原点的斜线)。

你的程序按它的算术强度落在图上某个点,**这个点能到达的「屋顶」高度,就是它的理论峰值性能**。关键判读:

- 若程序点**贴着斜线**(带宽线)→ 它是**内存带宽受限**,加再多 SIMD 也没用,得减访存。
- 若程序点**贴着水平线**(算力线)→ 它是**算力受限**,减访存没用,得加算力(SIMD、更少指令)。

斜线和水平线的交点叫**脊点(roofline point)**,对应的算术强度是「刚好吃满带宽又能转向算力」的分界。AI 低于脊点的程序全是带宽受限;高于的才可能算力受限。

### 手算两个例子:dot 和 axpy

Roofline 的好处是**算术强度可以手算**,不需要任何 profiler。我们拿两个经典 BLAS 内核算一下:

**点积 `dot = Σ a[i]*b[i]`**:

- 每次迭代:2 次浮点运算(一次乘、一次加),读 2 个 float(8 字节)。
- 算术强度 = `2 FLOP / 8 B = 0.25 FLOP/byte`。

**AXPY `y[i] = α*x[i] + y[i]`**:

- 每次迭代:2 次浮点运算(乘、加),读 2 个 float + 写 1 个 float(12 字节)。
- 算术强度 = `2 FLOP / 12 B ≈ 0.17 FLOP/byte`。

5800H 这类芯片的脊点算术强度大概在 **~20-25 FLOP/byte** 量级(峰值 FP32 算力约 900 GFLOPS,峰值 DDR 带宽约三四十 GB/s,相除)。dot 的 0.25 和 axpy 的 0.17 **远低于脊点**——所以这两个内核**铁定是内存带宽受限**,贴着带宽斜线跑。

这个结论直接指导优化方向:对 dot 和 axpy,**别去抠 SIMD 通道利用率**(算力方向),**要去减访存**(带宽方向)——比如把多个数组合成 SoA 一次加载、用更宽的 load、或者根本换算法减少数据移动。这就是 ch04-01「后端内存」要讲的事。

反过来,一个矩阵乘 `C += A·B` 的算术强度高得多(每个元素被复用很多次),AI 能到几十,落在算力线上——所以矩阵乘优化的重心是 SIMD / 分块榨算力,不是减访存。**同一份代码,优化方向完全不同,全看 AI 落在哪**。

> 量级说明:上面 5800H 的峰值算力和带宽我给的是**量级近似**,不写死精确数——因为它们随 turbo 频率、AVX 模式、内存条配置浮动,写死了反而误导。Roofline 的教学价值在「AI 落在斜线还是水平线」这个定性判断,不在于某台机器的精确峰值。需要精确峰值时,查 CPU 规格页 + 实测内存带宽(如 STREAM benchmark)。

## 小结:先全局,再定性

USE 和 Roofline 都是**高层、快速、不需要深 profiler 就能上手**的框架:

- **USE**:调查最早期,穷举系统资源的利用率/饱和度/错误,排除「问题根本不在 CPU」的情况。
- **Roofline**:确认瓶颈在计算后,用算术强度一眼区分「算力受限」(加 SIMD)还是「带宽受限」(减访存)。

它们能帮我们把战场缩小到「某个资源 / 某类瓶颈」,但还**没下钻到「流水线的哪一段」**。这正是 TMAM 四桶要做的事——下一篇我们走进 CPU 流水线,把瓶颈归到 Frontend / Backend / Bad Speculation / Retiring 四个桶里。

## 参考资源

- Brendan Gregg, *The USE Method*, brendangregg.com/usemethod.html——USE 框架原文与各资源的完整指标表
- Williams, Waterman, Patterson, *Roofline: An Insightful Visual Performance Model for Multicore Architectures*, CACM 2009——Roofline 模型原始论文
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 6 章 *Analysis Approaches*——USE 与 Roofline 的工程化讲法
- Ofenbeck et al. *Applying the Roofline Model》——算术强度的工程计算视角
