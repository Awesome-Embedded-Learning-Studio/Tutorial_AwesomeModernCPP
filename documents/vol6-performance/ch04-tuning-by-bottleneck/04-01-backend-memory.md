---
chapter: 4
cpp_standard:
- 17
description: Backend Memory Bound 是单线程性能最大的杠杆。本篇用粒子系统的实测讲透三件事:为什么连续+顺序就是快(cache-friendly
  回顾)、AoS 改 SoA 在「只更新部分字段」场景下快近 10 倍、结构体对齐/padding 如何影响 cacheline 利用率,以及软件 prefetch
  何时有用
difficulty: advanced
order: 1
platform: host
prerequisites:
- 存储层次与延迟阶梯:为什么顺序访问快 100 倍
- 缓存行与局部性:64 字节的最小搬运单位
- 归因实战:从一个慢程序到定位瓶颈
reading_time_minutes: 7
related:
- 循环与计算优化:code motion、展开与多累加器
- SIMD 与向量化:自动向量化条件、intrinsics 与 CPU 分发
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存管理
title: 后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch
---
# 后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch

## 单线程性能最大的杠杆

ch03 的归因方法告诉我们:绝大多数 C++ 程序的瓶颈落在 **Backend Memory Bound** 这个桶——执行单元在等数据。这其实是个好消息,因为「等数据」这一类是单线程优化里**杠杆最大**的:数据布局改一改,经常能换来几倍甚至十几倍的提升,而改算法/加 SIMD 通常只能换百分之几十。

这一篇专治 Backend Memory。我们把 ch02 讲过的存储层次和缓存行知识,落到三个具体的 C++ 改写手法上:**cache-friendly(复习,把它变成肌肉记忆)、AoS → SoA(数据导向设计的核心改写)、结构体对齐与 padding(让 cacheline 不浪费)**,最后讲软件 prefetch 什么时候有用。

## cache-friendly:三条铁律复习

ch02 已经讲透了,这里压成三条可背诵的铁律,作为后续所有改写的出发点:

1. **数据连续**:`vector`/`array`/原生数组 > `list`/`set`/链式桶。连续才能让一个 cacheline 服务多次访问。
2. **访问顺序**:遍历沿内存连续方向走(行优先),预取器帮你把后续数据提前拉进 cache。
3. **热数据集小**:反复扫的数据要装得进 cache(尤其 L3),否则在容量边界颠簸(回看 ch02-01:工作集 = L3 大小时延迟从 ~12 ns 飙到 ~96 ns)。

这三条是「免费」的——不改变算法、只调整布局和访问顺序,就能吃满 cache。但当你有一个「对象包含很多字段、但每次循环只用其中几个」的场景时,光连续还不够,这时就要上 AoS → SoA。

## AoS → SoA:数据导向设计的核心改写

这是 ch04 里最值钱的一招。看一个典型的「面向对象」粒子结构:

```cpp
// AoS:Array of Structures —— 每个粒子的所有字段挨在一起
struct Particle { float x, y, z;        // 位置
                  float vx, vy, vz; };  // 速度
Particle ps[N];   // 6 个 float = 24 字节/粒子
```

看上去很自然,每个粒子是一个对象,字段紧凑。**问题出在你只更新位置时**:

```cpp
for (int i = 0; i < N; ++i)
    ps[i].x += ps[i].vx * 0.016f;   // 只动 x,用 vx
```

每次循环只碰 `x` 和 `vx`,但因为是 AoS,你把 `ps[i]` 整条(连同没用的 `y`、`z`、`vy`、`vz`)都拉进了 cache。一个 24 字节的粒子,你只用了 8 字节(`x`+`vx`),**cacheline 利用率只有 1/3**,带宽白浪费 2/3。

**SoA(Structure of Arrays)** 把同一个字段的所有值连续排:

```cpp
struct Particles {
    std::vector<float> x, y, z, vx, vy, vz;   // 6 个独立数组
};
Particles ps;
for (int i = 0; i < N; ++i)
    ps.x[i] += ps.vx[i] * 0.016f;   // 只碰 x 数组和 vx 数组
```

现在你只把 `x` 数组和 `vx` 数组拉进 cache,cacheline 利用率接近 100%。我们实测(N = 100 万粒子,只更新 `x`):

```text
===== A. AoS vs SoA(更新 1048576 粒子的 x,20 次平均)=====
  AoS:  1.74 ms/次(每行把不更新的 y/z/vy/vz 也拉进 cache,带宽浪费)
  SoA:  0.18 ms/次(只碰 x、vx 数组,cacheline 利用率近 100%)
  SoA 快 9.79x
```

**接近 10 倍。** 这是单线程优化里最戏剧性的改写之一,而且没动算法、没加 SIMD——纯粹是把数据重新摆了摆。这就是**数据导向设计(DOD,Data-Oriented Design)** 的核心思想:**按「怎么被访问」组织数据,而不是按「现实世界是什么」组织**。Fabian 的《Data-Oriented Design》和 Mike Acton 的演讲是这条思路的源头(演讲留 vol10,这里只讲结论)。

SoA 还有一个红利:它**天然适合 SIMD 向量化**(`x` 数组连续,SIMD 一次 load 8 个 float),ch04-05 会展开。代价是代码不再「面向对象」——这是工程权衡,在性能热点上值得,在非热点上不必。

> 边界提醒:SoA 是**布局变换**,讲「为什么快」是 vol6 的事(就是这篇);「vector 内部为什么连续」是 vol3 的事。这里只关心布局对 cache 的影响。

## 结构体对齐与 padding:cacheline 别浪费

AoS 还有一个隐形成本——**对齐和 padding**。看这两个结构:

```cpp
struct ParticleAoS  { float x, y, z, vx, vy, vz; };          // 6 float = 24 B
struct ParticleAoS8 { float x, y, z, vx, vy, vz, pad1, pad2; }; // 8 float = 32 B
```

`sizeof` 实测:第一种 24 字节,第二种 32 字节(故意填到 8 的倍数)。一个 64 字节的 cacheline 能装:**两个** ParticleAoS(2×24=48B,剩 16B 装不下第三个,浪费),或**两个** ParticleAoS8(2×32=64B 正好,无浪费)。

```text
sizeof(ParticleAoS)  = 24 B
sizeof(ParticleAoS8) = 32 B(pad 到 32)
64B cacheline 能装:AoS=2 个,AoS8=2 个
```

这里 AoS 因为不是 2 的幂大小,cacheline 末尾会浪费;填成 32B 反而能整数装满。这背后的规则是编译器的**对齐 padding**:`struct` 里编译器会按成员的对齐要求插入填充(比如 `char` 后跟 `double`,中间补 7 字节让 `double` 对齐到 8)。`sizeof` 比你以为的「字段之和」大,就是这个原因。

实用推论:**热路径上的结构体,字段按大小降序排**(大的 double/指针在前,小的 int/char在后),能减少 padding;或用 `alignas(64)` 让关键结构独占 cacheline(这个手法在 ch05-01 治伪共享时是主角)。`#pragma pack` 能强制紧凑,但会破坏对齐、可能触发未对齐访问惩罚,**别在性能敏感路径乱用**——对齐/padding 的机制(为什么 sizeof 这么算)归 vol4 类布局,vol6 只讲它对 cacheline 的影响。

## 软件 prefetch:大多数时候不用

最后一个手法是**软件预取** `__builtin_prefetch`。它的想法是:你知道过一会儿要访问 `data[i+stride]`,就提前告诉 CPU「帮我从内存拉进 cache」,等真访问时已经命中,掩盖延迟。

```cpp
for (int i = 0; i < N; ++i) {
    __builtin_prefetch(&data[i + PREFETCH_DIST]);  // 提前 PREFETCH_DIST 步预取
    process(data[i]);
}
```

听起来很美,但**现代 CPU 自带硬件预取器,对规律访问(顺序、固定步长)极其在行**(回看 ch02-01:顺序遍历在 DRAM 上都能跑出 L1 的吞吐,就是硬件预取的功劳)。所以:**规律访问根本不用软件 prefetch,硬件已经做了**。

软件 prefetch 真正有用的场景是**不规则但可预测的访问**:比如链表遍历(下一个节点的地址你提前知道)、B-tree 查找、图遍历——硬件预取器学不会这种模式,你手工 prefetch 能拿到收益。但即使在这些场景,prefetch 也很容易**帮倒忙**(预取了不用的数据,污染 cache、浪费带宽)。**永远 benchmark 对照**——这是 ch04-06 会反复强调的「别盲目手优化」纪律的一个具体实例。

## 小结

- **Backend Memory Bound 是单线程最大杠杆**:改数据布局换来的是几倍十几倍,比加算力划算。
- **cache-friendly 三铁律**:连续、顺序、热数据集小。免费的。
- **AoS → SoA**:「只更新部分字段」场景下快近 10 倍(实测 9.79×),且天然适合 SIMD。是 DOD 的核心改写。
- **对齐与 padding**:热路径结构体按大小降序排减少 padding;`alignas(64)` 治伪共享(ch05)。机制归 vol4。
- **软件 prefetch**:规律访问硬件已做;不规则但可预测的访问才用,且必须 benchmark。

下一篇我们把战场从「数据布局」转到「计算本身」——循环怎么写能让 ILP 和编译器都帮上忙。

## 参考资源

- Fabian, R.《Data-Oriented Design》——DOD 思想源头,本地 `.claude/drafts/books/`
- Agner Fog《Optimizing software in C++》§7 *Making containers/objects efficient*——AoS/SoA、对齐、padding 的工程讲法。本地
- Bakhvalov《Performance Analysis and Tuning on Modern CPUs》第 9 章 *Memory Optimizations》
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch04/backend_memory.cpp`
