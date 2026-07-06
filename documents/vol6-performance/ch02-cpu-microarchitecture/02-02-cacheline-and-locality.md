---
chapter: 2
cpp_standard:
- 17
description: cache 不是按字节、也不是按你访问的数据大小工作的,而是以 64 字节的缓存行(cacheline)为最小搬运单位——哪怕只读 1 字节,硬件也会把整个
  64 字节拉进 cache。用步长扫描精确定位这 64 字节断崖,再用行优先 vs 列优先遍历 6 倍差距看清空间局部性的力量
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 存储层次与延迟阶梯:为什么顺序访问快 100 倍
reading_time_minutes: 12
related:
- 流水线、ILP 与分支预测
- 后端内存瓶颈:cache-friendly、AoS/SoA 与 prefetch
tags:
- host
- cpp-modern
- intermediate
- 优化
- 内存管理
title: 缓存行与局部性:64 字节的最小搬运单位
---
# 缓存行与局部性:64 字节的最小搬运单位

## 从上一张山的「坎」说起

上一篇的 memory mountain 里藏着一个细节,我们当时一笔带过了。把那张山的工作集 = 1024K(落在 L3)那一行单独拎出来:

```text
1024K   16.7  16.6  16.2  12.9   8.7  11.1   4.5
        8B    16B   32B   64B   128B  256B   512B   ← stride
```

stride 从 8B 涨到 32B,吞吐几乎不动(16 GB/s 上下);可一旦跨过 **64B**,数字就明显往下掉。这不是噪声,这个「坎」的位置是稳定的,它对应着 cache 一个硬邦邦的物理参数:**缓存行(cacheline)的大小**。这一篇我们就把这个 64 字节掰开讲清楚,它是理解一切「布局为什么影响性能」的钥匙。

## 缓存行:cache 的最小单位

很多人对 cache 有个直觉但错误的模型:我访问 `int`(4 字节),硬件就去内存搬 4 字节进来。**完全不是。** 真实情况是:cache 和主存之间的搬运,以一个固定大小的块为单位,这个块叫**缓存行**(也叫 cache line / cache block)。当代 x86 和 ARM 上,这个大小几乎清一色是 **64 字节**。

含义是这样的:无论你访问一个 1 字节的 `char`、一个 8 字节的 `double`,还是一次只读 `int`,**只要这次访问 miss 了 cache,硬件就会把「那个地址所在的整条 64 字节缓存行」整块拉进来。** 你只想花 4 字节的成本,实际花了搬运 64 字节的时间(当然,这 64 字节里后续的部分如果你接下来访问,就免费了,这正是空间局部性的机制)。

```bash
$ cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

`index0` 是 L1 数据 cache,`coherency_line_size` 就是缓存行大小。L1/L2/L3 全是 64 字节(可以挨个 `index1/2/3` 查,本机都是 64)。这个值在同代 CPU 上是固定的,所以性能文章里经常直接写死「64B」,但**第一次在一台陌生机器上调优时,先 `getconf` 确认一下**,有些低功耗 ARM 核或者老 Intel 是 32 字节。这条提醒看似啰嗦,真踩过 32B 行的坑就记住了。

讲清楚模型,接下来我们用行为实验亲手把这个 64 测出来。

## 上手跑一跑:步长扫描定位 64 字节断崖

思路很直接:把工作集固定在一个「大于 L1、落在 L3」的尺寸(这样每次 miss 有真实的成本),然后只变 stride,看吞吐在哪个步长出现断崖。如果断崖正好出现在 stride = 64B,那就反证了「cache 以 64 字节为单位」。

核心还是顺序环形遍历,但这次我们精细扫步长:

```cpp
double stride_throughput(long elems, long stride_elem) {
    const long ACCESSES = 64'000'000;
    long mask = elems - 1;            // elems 是 2 的幂
    int sink = 0; long idx = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < ACCESSES; ++i) {
        sink += g_data[idx];
        idx = (idx + stride_elem) & mask;
    }
    do_not_optimize(sink);            // 防 DCE,语义同 Google Benchmark DoNotOptimize
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return (double)ACCESSES / secs / 1e6; // M 访问/秒
}
```

工作集 2 MB(`int` 数组,512K 元素),`taskset -c 0` 绑核,扫一遍步长:

```text
===== A. 步长扫描(工作集 2MB,落 L3)=====
stride(B)     M访问/秒       说明
       4B       2017.5   < cacheline:同行的多次访问被摊薄
       8B       1956.3   < cacheline:同行的多次访问被摊薄
      16B       1986.7   < cacheline:同行的多次访问被摊薄
      32B       1962.3   < cacheline:同行的多次访问被摊薄
      48B       1820.1   < cacheline:同行的多次访问被摊薄
      56B       1543.5   < cacheline:同行的多次访问被摊薄
      64B       1422.1   = cacheline:每次访问正好换一行   ← 断崖
      72B       1181.8   > cacheline:每次访问都是新行
      96B       1034.8   > cacheline:每次访问都是新行
     128B        995.8   > cacheline:每次访问都是新行
     256B       1324.2   > cacheline:每次访问都是新行   ← 反常凸点,见下文(疑似预取器)
     512B        528.7   > cacheline:每次访问都是新行
```

读这张表的姿势:盯住「**stride = 64B 是分水岭**」。

- **stride < 64B(4 到 32)**:连续几次访问落在同一条 64 字节缓存行里。第一次访问 miss,把整行拉进来;之后几次访问全都命中这条已经搬来的行,几乎免费。所以吞吐高且平坦(~2000 M/秒),这条 64 字节里没被你访问到的部分,被你「免费搭车」了。
- **stride ≥ 64B(64、72、96…)**:每一次访问都踩进一条**新的**缓存行,「搭车」福利消失,每次都要付一次搬行的成本。吞吐应声跌到 ~1000–1400 M/秒。

这个从 ~2000 跌到 ~1000 的坎,精确出现在 stride = 64B,就是我们用行为给「缓存行 = 64 字节」做的交叉验证。

> 笔者要特意点一下 256B 那一行:它反而比 128B 高(1324 vs 996),看起来「不科学」。**最可能的嫌疑是硬件预取器**,当代 CPU 的预取器能识别「固定等步长」的访问流(相当大的步长也在它可学习的窗口内),提前把后面的行拉进来,部分藏住延迟。**但这是基于机制的推测,不是实测定论**:本机 WSL2 没条件关掉预取器做对照(关预取要写 MSR,WSL2 拿不到),且 Zen 预取器的精确可跟踪步长上限/流数没有公开文档;256B 那一行同时也吃了「同数组遍历周期变短、预热更充分」的几何红利(见代码里 `mask = elems-1` 的环形遍历)。512B 时数字继续往下掉,主导因素更可能是 cacheline 利用率(每次访问只消费 64B 行里的一小部分,见 02-01 stride 大步长带宽下降)。这件事提醒我们:**测 cache 行为时,预取器是个经常出来捣乱的变量**,看到不合常理的「凸点」先怀疑它,但也要承认,没做对照实验之前,「怀疑」不等于「证明」(这正是 vol6 ch00-01 反复警告的「听起来像解释」的伪因果陷阱)。

## 空间局部性:连续布局是「双赢」

把上一节的结论翻译成 C++ 设计原则,就是大家都听过但未必想透的那条:**数据要连续**。连续布局吃到的是双重红利:

1. **摊薄缓存行加载**:一条 64 字节搬进来,连续访问能把这 64 字节里的每个元素都用上,不浪费。同样一次 miss,你访问 16 个 `int`(64B)和访问 1 个 `int`,搬的成本一样。
2. **喂饱预取器**:预取器最爱「顺序等步长」的流。你连续遍历 `vector`,它检测到 stride = 4B 的流,提前把后面几条缓存行拉进 cache,等你用到时早已命中,**连 miss 都帮你省了**。

这就是为什么 `std::vector` / `std::array` / 原生数组遍历起来飞快,而 `std::list` / `std::set` / `std::unordered_map`(链式桶)的节点遍历慢:后者的节点分散在堆各处,既不能摊薄(每个节点占用一条缓存行但只读一小部分),预取器也跟不上(下一个节点的地址不可预测)。

这条原理有一个特别经典、每个 C++ 程序员都该亲手踩一次的坑:二维数组的遍历顺序。

## 行优先 vs 列优先:6 倍差距从哪来

C 和 C++ 的二维数组(无论原生 `a[N][N]` 还是用一维模拟的 `a[i*N+j]`)在内存里都是**行优先(row-major)**存储的,第一行存完存第二行。也就是说,`a[i][j]` 和 `a[i][j+1]` 在内存里挨着(差 4 字节),但 `a[i][j]` 和 `a[i+1][j]` 隔了一整行(差 `N*4` 字节)。

这意味着:双层循环遍历矩阵时,**内层循环沿「行」走是顺序访问,沿「列」走是大步长跳跃**。我们用 2048×2048 的 `int` 矩阵(16 MB,正好 = 本机 L3 大小,放大差异)实测:

```cpp
void walk_2d(int* a, int N, bool row_major) {
    volatile int sink = 0; int s = 0;
    auto t0 = std::chrono::steady_clock::now();
    if (row_major)
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) s += a[i * N + j]; // 沿行:顺序,stride=4B
    else
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) s += a[j * N + i]; // 沿列:跳跃,stride=N*4B=8KB
    sink = s;
    auto t1 = std::chrono::steady_clock::now();
    /* 打印耗时和吞吐 */
}
```

```text
===== B. 2D 遍历(N=2048,矩阵 16 MB = L3 大小)=====
   行优先 row-major:     1.0 ms,  16.7 GB/s
   列优先 col-major:     6.3 ms,   2.7 GB/s
```

**同样的矩阵、同样的计算量、同样的命中 L3,列优先比行优先慢 6 倍多。**

拆开看:行优先时内层 `j` 每次前进 4 字节,完全顺序,一条缓存行(64B)能服务 16 次 `int` 访问,预取器还提前拉数据,吞吐 16.7 GB/s 跑满。列优先时内层 `j` 让地址每次跳 `N*4 = 8192` 字节,每一次访问都踩进一条全新的缓存行,而且步长 8 KB 大到预取器追不上(参考上一节 512B 那行的惨状),于是退化成 L3 的随机访问吞吐,只剩 2.7 GB/s。

这就是「内层循环必须沿内存连续方向」的全部理由。写矩阵乘法、图像卷积、网格类模拟的朋友,这个方向写反了就是白送几倍性能,而且编译器**不会**帮你自动翻转(它不能证明交换循环次序不影响结果,大多数情况其实不影响,但编译器不敢)。

> 顺带一提:这个「交换循环次序」叫做 **loop interchange**,是后面 ch04-02 循环优化会专门讲的编译器优化之一。这里你先记住它的物理动机:让最内层循环沿着内存连续方向走。

## 结构体字段排序:让热数据挤进同一条行

缓存行还直接决定了一件 C++ 特有的布局决策:**结构体字段该怎么排**。看这两个结构体:

```cpp
struct Bad {
    int    id;          // 热:每帧都要查
    char   debug_tag;   // 冷:只有打日志才读
    double values[6];   // 热:每帧都算
    void*  parent;      // 冷:只有遍历树时才用
};

struct Good {
    int    id;          // 热
    double values[6];   // 热    ← 热字段聚一起
    char   debug_tag;   // 冷
    void*  parent;      // 冷    ← 冷字段分开
};
```

`Bad` 里冷热字段交错,遍历时每碰一下热字段,顺带把同一条缓存行里的冷字段也搬进了 cache,白白占掉宝贵的 cache 空间,本来能多缓存几个热对象的。`Good` 把热字段集中,一条缓存行尽可能多的装「真的会被用」的数据。这叫**热/冷字段分离(hot/cold splitting)**,本质就是给缓存行排兵布阵,让它每一格都花在刀刃上。

这条经验有个更激进的版本叫 **AoS → SoA**(Array of Structs 改 Struct of Arrays),当你有一大堆对象、但每次只处理它们的某个字段时(比如物理模拟里只更新位置),把「同一字段连续排」比「同一对象字段挨着排」快得多。这件事比字段排序更深、更有讲究,我们留到 ch04-01「后端内存瓶颈」专章讲,这里只种下这个念头。

> 边界提醒:结构体**对齐、padding、`#pragma pack`** 这些「为什么 `sizeof` 会比你以为的大」的机制(比如 `char` 后面编译器会插填充让 `double` 对齐到 8 字节),属于 ABI / 布局规则,vol4 讲类布局时会涉及。vol6 这里只关心「热字段聚类对 cache 友好」这层性能含义。

## 一个伏笔:同行的代价(false sharing)

缓存行还有一面我们故意还没展开。既然「相邻地址共享同一条缓存行」,那两个**不相关**的变量如果碰巧挤在同一条 64 字节里,会发生什么?会互相把对方踢出 cache,甚至更糟,在多线程下引发**伪共享(false sharing)**:线程 A 写变量 x、线程 B 写变量 y,虽然 x、y 逻辑上无关,但它们在同一条缓存行,硬件的一致性协议会反复让这条行在两个核之间来回 invalidate,性能塌方。

这件事单核上看不出来,是多核专属的坑,而且坑极深。所以完整的 false sharing 实测和 `alignas(64)` 解法,我们留到 ch05 多核性能那章专门拆,这里先埋个伏笔:缓存行的「共享」在单核是红利,在多核可能变成税。

## 留给后面的线头

到这一篇为止,我们确认了 cache 的最小搬运单位是 **64 字节的缓存行**,并用两个实测看清它的后果:**步长扫描**精确在 stride = 64B 处看到吞吐断崖(行为级证明),**行优先 vs 列优先遍历差 6 倍**(内层循环方向决定了你访问是顺序还是大步长跳跃),再加上**热字段聚类**让宝贵的缓存行只装会被用到的数据(AoS→SoA 留到 ch04-01),以及多核下的暗面 **false sharing**(留到 ch05)。

但程序的性能不只取决于数据搬运,CPU 算指令的方式本身(流水线、指令级并行、分支预测)也能让同样的数据跑出几倍的差距。下一篇我们就走进 CPU 的执行核心。

## 参考资源

- Agner Fog《The microarchitecture of Intel, AMD and VIA CPUs》§22.16 *Cache and memory access*:Zen 族的缓存参数(64 B 行、各级路数/组数)、硬件预取行为。本地:`.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Bryant & O'Hallaron《CSAPP》第 6 章 *The Memory Hierarchy*:缓存行、空间/时间局部性的形式化定义与 memory mountain
- Drepper, U.《What Every Programmer Should Know About Memory》:缓存行、对齐、预取的工程细节(经典长文)
- 本篇实测代码:`code/volumn_codes/vol6-performance/ch02/cacheline_locality.cpp`
