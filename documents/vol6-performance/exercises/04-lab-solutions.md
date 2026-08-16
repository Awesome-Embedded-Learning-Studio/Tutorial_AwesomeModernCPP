---
title: "卷六 Lab 实验参考"
description: "卷六 Lab（缓存与分支探针台）的实验参考：六个步骤加 L5 挑战的逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1，i7-14650HX）真实运行得到。stride 断崖平缓、伪共享 4× 等与教材不一致之处如实报告并解释。"
chapter: 6
order: 4
tags:
  - host
  - intermediate
  - cpp-modern
  - 优化
  - 内存管理
  - 调试
difficulty: intermediate
platform: host
reading_time_minutes: 14
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷六 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1，Intel Core i7-14650HX，24 线程）真实运行得到。计时数字随运行浮动，**秒数会抖、量级趋势稳定**；本 Lab 有两处与教材数字明显不一致的诚实结果（stride 断崖平缓、伪共享只有 4×），都在正文如实标注，别把它们当错误抹掉——它们正是「性能数字随机器变、趋势才可信」的活证据。

## 步骤 1：给「看起来很快」的基准做体检 {#lab-1}

**思路**：`fast_abs` 的速度是真的（分支预测命中 + 一条 `neg`），但它对 INT_MIN 是错的——`-x` 在有符号溢出下是 UB，本机回绕成它自己。UBSan 把它从「沉默的错」变成「点名到行的报告」。

1. 普通构建：1 亿次 50.8 ms，看起来是个漂亮数字；但 `fast_abs(INT_MIN) = -2147483648`——**绝对值返回了负数**，这个基准算的结果本身就是错的。→ 知识点：[从「先正确」到「再快」](../ch00-performance-mindset/02-from-correctness-to-performance.md)「第三类,也是最阴险的一类」一节（踩在错的内存上不算、**算出错的值**才算）
2. UBSan 构建：`fastabs.cpp:9:25: runtime error: negation of -2147483648 cannot be represented in type 'int'`，精确到行列；耗时 104.5 ms ≈ 普通的 2 倍——这 2 倍就是插桩检查的开销，也是「带 UB 的漂亮数字」的真实成本被摊开后的样子。→ 知识点：[Sanitizer 工具链全景](../ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md)「UBSan」一节

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra fastabs.cpp -o fastabs && ./fastabs
1 亿次 fast_abs: 50.841 ms, 总和=4572225624
fast_abs(INT_MIN) = -2147483648  (期望 abs = 2147483648,装不下!)
$ g++ -O2 -std=c++17 -fsanitize=undefined fastabs.cpp -o fastabs_ub && ./fastabs_ub
fastabs.cpp:9:25: runtime error: negation of -2147483648 cannot be represented in type 'int'; cast to an unsigned type to negate this value to itself
1 亿次 fast_abs: 104.499 ms, 总和=4572225624
fast_abs(INT_MIN) = -2147483648  (期望 abs = 2147483648,装不下!)
```

## 步骤 2：vector vs set——换一种查询模式 {#lab-2}

**思路**：全未命中查询下，二分查找和红黑树都必须走完整条路径——没有「命中即返」的捷径，两边的比较成本都最大化；缓存差异成为唯一的主导因素。

1. 实测曲线：N=1024 两边打平（1.0×），N=4096 起 `set` 被甩开（1.2×），到 N=1M 是 **3.7×**。对比教材「全命中」曲线（N=1024 时 set 反而略快 0.9×、N=65536 时 3.4×）：全未命中模式下 **set 更早**开始落后——教材全命中时小 N 下 set 靠「可预测的分支 + 更少的比较」反超，全未命中把这条优势抹掉了，剩下的只有缓存行为。→ 知识点：[性能思维](../ch00-performance-mindset/01-efficiency-vs-performance.md)「小 N 那个反常」一节（分支预测 vs 缓存的两段论）
2. 比值随 N 拉大的机制：`set` 的节点散落堆里，每往下一层都是不可预测的指针追逐，工作集一大就层层 cache miss；`vector` 的连续内存让二分跳到的点互相搭缓存行顺风车。查询模式换了、曲线形状换了，但「同 $O(\log n)$ 差几倍」的总命题不动。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)（连续 vs 节点分散）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra vs.cpp -o vs && ./vs
N              vector(ns/q)        set(ns/q) set/vector
1024                   39.9             38.6        1.0x
4096                   50.9             62.3        1.2x
16384                  61.9             86.7        1.4x
65536                  76.4            137.0        1.8x
262144                 95.3            272.3        2.9x
1048576               145.9            545.1        3.7x
global_sink=0 (防死代码消除)
```

## 步骤 3：亲手画出本机延迟阶梯 {#lab-3}

**思路**：置乱单环 + 真依赖 = 预取器束手无策，测出纯访存延迟；16 档工作集横跨四级存储，平台的拐点就是各级容量。

1. 实测阶梯：4K~32K 在 **1.76~2.74 ns**（L1d，本机 L1d 48 KB）；64K~1M 在 **4.13~12.42 ns**（L2，P 核 2 MB）；2M~32M 在 **23.0~104.7 ns**（L3，约 30 MB，越接近容量越颠簸）；64M~128M 在 **123.8~151.8 ns**（DRAM）。L1 与 DRAM 差 **约 60~85 倍**，和教材「两个数量级」的量级一致。→ 知识点：[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)「上手跑一跑」一节
2. 边界识别：L1d 的 48 KB 对应 4K~32K 档（64K 档已经 4.13 ns、开始溢出到 L2）；L2 为 2 MB（P 核），所以 64K~1M 档都还在 L2 内、延迟随工作集逼近容量缓慢爬升（6.52 → 12.42 ns）；2M 档起进入 L3（约 30 MB），到 32M 档已贴着 L3 容量边缘剧烈颠簸（104.7 ns）；64M 起明确是 DRAM。→ 知识点：同上（工作集 = 各级容量的临界点）

**验证输出**：

```text
$ taskset -c 0 ./ladder
工作集     ns/access 级(推断)
4096               1.76        L1d
8192               2.11        L1d
16384              2.09        L1d
32768              2.74        L1d
65536              4.13         L2
131072             5.27         L2
262144             6.52         L2
524288             9.59         L2
1048576           12.42         L2
2097152           23.04         L3
4194304           39.42         L3
8388608           99.67         L3
16777216         113.77         L3
33554432         104.70         L3
67108864         151.80       DRAM
134217728        123.81       DRAM
```

## 步骤 4：用行为测出 64 字节缓存行 {#lab-4}

**思路**：stride 小于缓存行时同行搭车、吞吐高；大于缓存行时每次踩新行、吞吐掉一档——断崖位置就是缓存行大小。但**断崖的清晰度取决于预取器的激进程度**，这一步要如实贴曲线。

1. 实测曲线：stride 4~32B 在 **~2000~2190 M/秒** 的平台上；stride 64~72B 掉到 **1790~1900**；stride 128~512B 再掉到 **~1200~1340**。`getconf LEVEL1_DCACHE_LINESIZE = 64` 实锤。本机曲线**没有教材 5800H 那种「2000→1000」的整齐断崖**——Raptor Lake 的预取器对大步长流的容忍度更高（教材 ch02-02 的 256B 凸点在本机变成了 512B 的高位 1341），断崖是「渐变的坡」而不是「台阶」。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)「步长扫描定位 64 字节断崖」一节
2. 两个「不合常理」的点：stride 48B 的 997 M/秒（低于两侧 2085/1946）是单次测量的离群值嫌疑——重测大概率消失；stride 512B 反而高于 256B（1341 vs 1207）符合教材「预取器出来捣乱」的观察。教材的纪律：看到不合常理的凸点先怀疑预取器，但没做对照实验之前，「怀疑」不等于「证明」。→ 知识点：同上（256B 凸点警告框的纪律）
3. 机制落点：64B 以下「同一行的多次访问被摊薄」体现为 4~32B 的高平台（每行搭车 8~16 次）；64B 以上「每次踩新行」体现为 64B 起的一档下坡。断崖不够陡，但**趋势的方向和位置**都指向 64。→ 知识点：同上（行为级验证的判读）

**验证输出**：

```text
$ taskset -c 0 ./stride
stride(B)    M访问/秒
4                2030.3
8                2186.6
16               2162.2
32               2085.2
48                997.0      ← 单次测量离群值嫌疑,见正文
56               1946.0
64               1790.0      ← 分水岭:从此每次访问踩新行
72               1899.2
96               1707.2
128              1314.2
256              1207.5
512              1341.2      ← 预取器凸点,见教材 02-02 的纪律
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

## 步骤 5：AoS → SoA 与 Roofline 手算 {#lab-5}

**思路**：先一支笔判方向（带宽受限 → 减访存），再动手把布局改掉——这是 ch03→ch04 的标准顺序，别反过来。

1. Roofline 手算：每次迭代 1 乘 1 加 = 2 FLOP；读 `x`、`vx`（8 字节）+ 写 `x`（4 字节）= 12 字节 → AI = 2/12 ≈ **0.17 FLOP/byte**，远低于脊点——**带宽受限**，优化方向是减访存（只碰需要的字段），不是加算力。→ 知识点：[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)「手算两个例子」一节
2. 实测：AoS 0.947 ms vs SoA 0.288 ms = **3.29×**；两版更新后的 x 数组最大差 **0.00e+00**（同一份乘加、同一顺序）。教材 5800H 测出 9.79×，本机 3.29×——**如实报告**：AoS 每粒子流量 24 字节、SoA 只碰 x/vx 共 8 字节，流量比 3：1 是这道题的理论下限，本机 3.29× 基本贴着流量比（教材的 9.79× 里还叠加了那台机器上 AoS 向量化受阻的额外惩罚，本机 GCC 对 AoS 的 stride-24 访问处理得更好）。倍数差这么多，但「布局改写白捡几倍」的结论一致。→ 知识点：[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)「AoS → SoA」一节（9.79× 的出处与机制）
3. 「只碰 8 字节却按 24 字节搬数据」是题眼：AoS 里 `y/z/vy/vz` 四个字段跟着 `x/vx` 一起被缓存行搬进 cache，却一次都没被读——cacheline 利用率只有 1/3，另外 2/3 的带宽喂给了空气。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)（空间局部性的反面）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra particle.cpp -o particle && ./particle
N=1000000  AoS 更新 x: 0.947 ms | SoA 更新 x: 0.288 ms | SoA 快 3.29x
两版 x 的最大差 = 0.000000e+00
```

## 步骤 6：伪共享——对齐之前的惨状 {#lab-6}

**思路**：两个计数器逻辑无关、物理同船——一致性协议的粒度是缓存行不是变量，于是每次写都触发 RFO 把对方的行抢过来，实质串行。

1. `sizeof(BadCounters)=16`（两个 atomic 挤一条 64 字节行）、`sizeof(GoodCounters)=128`（各占一行）。→ 知识点：[伪共享](../ch05-multicore-performance/05-01-false-sharing.md)「MESI 一致性」一节
2. 实测 3 轮：伪共享 821/805/817 ms，对齐版 201/207/207 ms，比值 **3.9~4.1×**。**如实报告**：教材机器（5800H）是 15~48×，本机只有约 4×——解释：本机 Raptor Lake 上 `lock` 前缀指令本身延迟就高（6.5-A 测过单线程 fetch_add ≈ 3.9 ns/op，对齐版两线程并行 5000 万次恰好 ≈ 4 ns/op，说明对齐版已经贴着原子指令的硬件下限跑），RFO 往返的**增量**（~12 ns）相对这个高基线的比例就被稀释成 4×；5800H 上 lock add 快得多，RFO 增量相对更大、倍数就飙到两位数。**倍数随机器变，两个结论跨机器不变**：①伪共享让「完美并行」实质串行化；②`alignas(64)` 让每核频繁写的变量独占缓存行即可根治。→ 知识点：[伪共享](../ch05-multicore-performance/05-01-false-sharing.md)「上手跑一跑」一节（含「倍数随运行浮动大」的原文提醒）、[锁的开销与「无锁不是银弹」](../ch05-multicore-performance/05-03-locks-vs-lockfree.md)（atomic 单条操作的成本）
3. 为什么 TSan/ASan 查不出伪共享：它不是数据竞争（两个 atomic 各写各的、内存序合法）、不是 UB——它是**纯性能问题**，只有性能 profiler（`perf c2c` 的 HITM 计数）看得见。→ 知识点：[伪共享](../ch05-multicore-performance/05-01-false-sharing.md)「对策」一节

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra fshare.cpp -o fshare -pthread && ./fshare
sizeof(BadCounters)=16 sizeof(GoodCounters)=128
第 1 轮: 伪共享 821.1 ms | alignas(64) 201.4 ms | 比值 4.1x
第 2 轮: 伪共享 804.7 ms | alignas(64) 207.4 ms | 比值 3.9x
第 3 轮: 伪共享 817.2 ms | alignas(64) 206.5 ms | 比值 4.0x
最快轮: 伪共享 804.7 ms | alignas(64) 201.4 ms | 伪共享/对齐 = 4.0x
```

## 附加挑战（L5）：排序 vs 打乱——把分支预测罚金量到底 {#lab-l5}

**思路**：这题的完整图景分两层：默认 -O2 下「分支」早被编译器无分支化/向量化了（差距消失）；只有把三条优化通路全关掉，真分支和它的预测罚金才现形——然后手写 branchless 把它按下去。

1. 默认 `-O2`：打乱 0.0078~0.0084 ms/次 vs 排序 0.0078~0.0082，比值 **1.02×**——差距消失。原因：GCC 把循环向量化成 SIMD 掩码比较-加法（或 if-conversion 成 cmov），**分支根本不存在了**。这不是「分支预测免费」的证据，是「你的 if 不一定是分支」的证据——教材 ch04-06 的诚实结论，本机复现。→ 知识点：[分支:branchless、predication 与「别盲目无分支」](../ch04-tuning-by-bottleneck/04-06-branch-branchless.md)「上手跑一跑（以及一个诚实的结果）」
2. 三个旗标各挡一条通路：`-fno-tree-vectorize` 挡循环级向量化、`-fno-tree-slp-vectorize` 挡基本块 SLP 向量化、`-fno-if-conversion` 挡标量 cmov 化——真分支留下。实测：打乱 0.0923 ms vs 排序 0.0070 ms = **13.19×**（教材 5800H 是 4.2×，本机罚得更狠——每两次预测失败冲刷一次十几级流水线，代价随流水线深度放大）。→ 知识点：[流水线、ILP 与分支预测](../ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md)「分支预测」一节（4.2× 的出处与机制）
3. 手写 branchless 掩码：`d[i]-128` 在 <128 时为负（算术右移 63 位得全 1、取反得全 0），≥128 时非负（右移得 0、取反得全 1）——`d[i] & m` 完成「条件保留」。实测：打乱 0.0126 vs 排序 0.0128 = **0.99×**——无论顺序，稳定在 0.013 ms 附近。它比「排序 + 可预测分支」（0.0070 ms）**略慢**：验证了教材那句话——**可预测的分支几乎免费**，branchless 反而多算了几条指令；branchless 真正的战场是「数据相关、不可预测」的分支。→ 知识点：[分支:branchless](../ch04-tuning-by-bottleneck/04-06-branch-branchless.md)「cmov 的代价」与「四条纪律」
4. 结果一致：三版对同一份打乱数据都算出 3103205。**参考答案作者的初版掩码写反了（没取反），branchless 算出 1041184——先修正确性、再谈性能**，这正是本卷铁律一的现场。→ 知识点：[性能思维](../ch00-performance-mindset/01-efficiency-vs-performance.md)（铁律一）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra branch.cpp -o branch_default && ./branch_default
分支版   打乱: 0.0084 ms/次 | 排序: 0.0082 ms/次 | 打乱/排序 = 1.02x
无分支版 打乱: 0.0104 ms/次 | 排序: 0.0098 ms/次 | 打乱/排序 = 1.06x
结果一致: 分支=3103205 无分支=3103205
$ g++ -O2 -fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion \
      -std=c++17 -Wall -Wextra branch.cpp -o branch_real && ./branch_real
分支版   打乱: 0.0923 ms/次 | 排序: 0.0070 ms/次 | 打乱/排序 = 13.19x
无分支版 打乱: 0.0126 ms/次 | 排序: 0.0128 ms/次 | 打乱/排序 = 0.99x
结果一致: 分支=3103205 无分支=3103205
```

到这里，探针台的六个探针 + 一道挑战全部量完：UB 会让基准算错还继续跑（50.8 → 104.5 ms 的体检单）、查询模式换一下曲线就换（1.0× → 3.7×）、延迟阶梯 60~85 倍（1.76 → 123.8 ns）、缓存行用行为测得出（64B 起下坡）、布局改写 3.29×（纯减访存）、伪共享 4×（MESI 的税）、分支罚金 13.19×（可被 branchless 按到 0.99×）。每一格的数字都是这台机器这一天的真实读数——明天再跑会抖，但阶梯的形状、断崖的方向、倍数的量级，稳。
