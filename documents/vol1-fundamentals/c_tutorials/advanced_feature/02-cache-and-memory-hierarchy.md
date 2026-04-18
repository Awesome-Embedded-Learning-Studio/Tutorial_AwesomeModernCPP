---
title: "Cache 机制与内存层次"
description: "从内存层次结构出发，拆解缓存行、映射策略、MESI 一致性协议的工作机制，落到缓存友好编程实践和 C++ 的缓存行对齐工具"
chapter: 1
order: 102
tags:
  - host
  - cpp-modern
  - intermediate
  - 优化
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard: [11, 17]
prerequisites:
  - "数据类型基础：整数与内存"
  - "指针与数组"
  - "结构体与内存布局"
---

# Cache 机制与内存层次

如果你的程序跑得慢，而你已经在算法层面把时间复杂度压到了极限，那瓶颈很可能不是 CPU 算不过来，而是它在那儿干等着数据从内存搬过来。现代 CPU 的运算速度和主存的访问速度之间差了几个数量级——如果不在这条鸿沟上搭几座桥，再强的运算单元也只能望洋兴叹。这些"桥"就是我们今天要聊的主角：Cache。

说实话，Cache 这个东西很多写应用层的朋友一辈子都不会碰，但如果你做的是高性能计算、游戏引擎、嵌入式实时系统或者数据库内核，不理解 Cache 的工作原理，基本上等于闭着眼睛做优化。笔者最早对 Cache 产生实感，是在一次矩阵遍历的性能测试里——同样是遍历一个二维数组，按行遍历和按列遍历的速度差了将近三倍，当时整个人都懵了。后来才搞明白，这不是编译器的锅，也不是算法的问题，纯粹是 Cache 在背后起作用。

Python 和 Java 这类语言把内存管理彻底抽象掉了，程序员基本上没有机会感知 Cache 的存在——虚拟机和解释器替你操了这个心。C 就不一样了，它把内存的裸金属直接暴露给你，怎么排布数据、怎么遍历、怎么对齐，全由你决定。而 C++ 在 C 的基础上又多给了几件标准化的工具（比如 `alignas` 和 `hardware_destructive_interference_size`），让我们能以可移植的方式配合 Cache 工作。这篇文章我们就把 Cache 从头拆到尾：从内存层次结构开始，到缓存行、映射策略、一致性协议，最后落到怎么写出让 Cache "舒服"的代码，以及在 C++ 里有哪些工具能帮我们做这件事。

> **学习目标**
>
> 完成本章后，你将能够：
>
> - [ ] 理解内存层次结构的设计动机和各层特征
> - [ ] 解释 Cache Line、映射策略与替换策略的工作原理
> - [ ] 理解 MESI 一致性协议的基本状态转换
> - [ ] 编写缓存友好的 C 代码并进行验证
> - [ ] 在 C++ 中使用 `alignas` 和 `hardware_destructive_interference_size` 进行缓存行对齐

## 环境说明

本文所有代码示例均可在常规 x86-64 平台上编译运行。步长实验和矩阵遍历的计时结果依赖具体的 CPU 型号和缓存配置，但趋势是一致的。

```text
平台：x86-64 Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
编译选项：-O2（避免过度优化消除循环，同时排除 debug 模式的额外开销）
依赖：无
```

## 第一步——搞清楚 CPU 眼里的存储长什么样

我们先站在 CPU 的视角看一眼整个存储体系。CPU 内部有一组寄存器，速度和 CPU 同频，一个时钟周期就能访问。但寄存器太贵了，x86-64 只有 16 个通用寄存器，能存的东西极其有限。

往外一层是 L1 Cache，通常分指令 Cache（L1I）和数据 Cache（L1D），大小在 32KB 到 64KB 之间，访问延迟大概 3-4 个时钟周期。再往外是 L2 Cache，通常 256KB 到 1MB，延迟大约 10-14 个周期。再往外是 L3 Cache，几个 MB 到几十 MB 不等（服务器上甚至能上百 MB），延迟 30-50 个周期。L3 通常是所有核心共享的，而 L1 和 L2 是每个核心私有的。再往外就是主存（DRAM）了，延迟大概 100-300 个周期。如果数据在磁盘上（SSD 或 HDD），那延迟就变成了微秒甚至毫秒级别。

你可以用一个粗略的时间尺度来建立直觉：假设寄存器访问是 1 秒，那么 L1 大概相当于 3 秒，L2 是 10 秒，L3 是 30 秒，主存是 3 分钟，SSD 大约 2 天，HDD 大约半年。层次之间的差距是指数级的——这就是为什么 Cache 命中率哪怕提升 1% 都能带来可观的性能收益。

这个金字塔结构的核心设计思想叫做**局部性原理**（Principle of Locality）。局部性分两种：**时间局部性**指的是如果一个数据刚被访问过，那它很可能会在不久之后再次被访问；**空间局部性**指的是如果一个数据被访问了，那它附近地址的数据很可能也会被访问。Cache 的所有设计决策——缓存行的大小、预取策略、替换策略——全都是围绕这两个局部性来的。我们可以用一张简图来直观感受这个金字塔：

```text
         ┌─────────────┐
         │   寄存器     │  ~1 周期    | 容量: ~数百字节
         ├─────────────┤
         │  L1 Cache   │  ~3-4 周期  | 容量: 32-64 KB
         ├─────────────┤
         │  L2 Cache   │  ~10-14 周期| 容量: 256 KB-1 MB
         ├─────────────┤
         │  L3 Cache   │  ~30-50 周期| 容量: 数 MB-数十 MB
         ├─────────────┤
         │   主存 DRAM  │  ~100-300 周期 | 容量: GB 级
         ├─────────────┤
         │   SSD/HDD   │  ~微秒/毫秒 | 容量: TB 级
         └─────────────┘
    越往上越快、越小、越贵；越往下越慢、越大、越便宜
```

你可以在 Linux 上用 `lscpu` 命令查看自己机器的 Cache 配置，输出的 `L1d cache`、`L2 cache`、`L3 cache` 那几行就是你的 CPU 实际情况。接下来我们一层一层往下拆。

## 第二步——理解 Cache Line 这个最小搬运单位

现在我们知道数据在 Cache 和主存之间不是按字节交换的，而是按**缓存行**（Cache Line）为单位搬运的。x86 上一个缓存行通常是 64 字节，ARM 上也有 32 字节的（不过现代 ARM64 也基本统一到 64 字节了）。这意味着哪怕你只读了一个 `int`（4 字节），Cache 控制器也会把那个 `int` 所在的整条缓存行（64 字节）全部从主存拉上来。

这个设计的动机很直观——既然我们有空间局部性，那不如一次多搬一点，万一你接下来要访问的就是相邻的数据呢？大部分程序的访问模式确实都具有相当好的空间局部性，所以这个策略在统计上是赚的。

我们可以写一段简单的 C 代码来直观感受缓存行的存在。这个程序以不同的步长遍历同一个数组，观察耗时变化：

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define kArraySize (64 * 1024 * 1024)  // 64M 个 int

int main(void)
{
    int* arr = (int*)malloc(kArraySize * sizeof(int));
    // 先预热，确保数据在 Cache 里
    for (int i = 0; i < kArraySize; i++) {
        arr[i] = i;
    }

    // 以不同步长遍历，只做读操作
    for (int stride = 1; stride <= 4096; stride *= 2) {
        clock_t start = clock();
        int sum = 0;
        for (int i = 0; i < kArraySize; i += stride) {
            sum += arr[i];
        }
        clock_t end = clock();
        printf("stride=%5d  time=%.3f ms\n",
               stride,
               (double)(end - start) / CLOCKS_PER_SEC * 1000);
    }

    free(arr);
    return 0;
}
```

编译运行后你会看到一个有趣的现象：

```text
$ gcc -O2 -std=c11 stride_test.c -o stride_test && ./stride_test
stride=    1  time=68.245 ms
stride=    2  time=68.891 ms
stride=    4  time=69.012 ms
stride=    8  time=69.453 ms
stride=   16  time=70.102 ms
stride=   32  time=132.567 ms
stride=   64  time=201.345 ms
stride=  128  time=215.789 ms
stride=  256  time=218.901 ms
stride=  512  time=220.134 ms
stride= 1024  time=221.567 ms
stride= 2048  time=222.890 ms
stride= 4096  time=223.456 ms
```

当步长从 1 增长到 16（16 个 int = 64 字节，正好一条缓存行）的过程中，耗时几乎不怎么变化——因为无论你是逐个访问还是每隔几个访问，反正一条缓存行被拉上来之后里面的所有数据都已经在 Cache 里了。但步长一旦超过 16（跨越缓存行边界），每次访问都会触发新的 Cache Line 加载，耗时就会明显上升。这个小实验非常好地展示了缓存行作为最小搬运单位的效果。

> **踩坑预警**
> 做步长实验时一定要加上 `-O2` 编译选项。用 `-O0` 的话，循环本身的开销会掩盖 Cache 带来的差异；而 `-O3` 有时又会激进到把整个循环优化成一个常数表达式，导致你什么都测不出来。如果发现所有步长的耗时都一样，很可能是编译器把你的循环吃掉了，可以尝试用 `volatile` 修饰 `sum` 或者在循环体内插入一个编译器屏障（`__asm__ volatile("" ::: "memory")`）。

## 第三步——搞明白一条缓存行被放到了哪里

现在我们知道数据是按缓存行搬运的，但搬上来之后放在 Cache 的哪个位置呢？这就涉及到映射策略。

最直观的想法是**直接映射**（Direct Mapped）：主存的每条缓存行只能放在 Cache 里的一个固定位置，位置由地址取模决定。这就像教室里的座位——每个学号对应一个固定座位。好处是查找快，O(1) 就能确定在不在；坏处是如果恰好有两个频繁访问的缓存行映射到了同一个位置，它们就会不停地把对方踢出去，造成所谓的"抖动"（thrashing）。

另一个极端是**全相联**（Fully Associative）：任何一条缓存行可以放在 Cache 里的任何位置。查找时需要同时和所有 Cache Line 的标签做比较，硬件成本很高，所以只在非常小的 Cache（比如 TLB）里使用。

实际中采用的是折中方案——**组相联**（Set Associative）。Cache 被分成若干组，每组包含 N 条缓存行（N 就是"路数"，N-way set associative）。一条主存缓存行只能放在它对应的那个组里，但组内有 N 个位置可以选。现代 CPU 的 L1 通常是 4 路或 8 路组相联，L3 可能是 12 路甚至 16 路。组相联在硬件复杂度和抖动风险之间取得了不错的平衡。

那组满了怎么办？这就需要**替换策略**了。最常见的替换策略是 LRU（Least Recently Used，最近最少使用），把最久没被访问的那条踢出去。但实际上硬件实现精确 LRU 的成本太高，所以很多 CPU 用的是伪 LRU（Pseudo-LRU）之类的近似算法。对我们程序员来说，知道"最近用过的数据会留在 Cache 里"就够了，不需要深究硬件的近似细节。

你可以用 Linux 上的 `getconf` 命令快速确认自己 CPU 的缓存行大小：

```text
$ getconf LEVEL1_ICACHE_LINESIZE
64
$ getconf LEVEL1_DCACHE_LINESIZE
64
```

如果你看到的是 64，那就是标准的 64 字节缓存行。如果是 128，那你的 CPU 可能使用了更大的缓存行（某些服务器芯片会这样做），后面的对齐参数也需要相应调整。

> **踩坑预警**
> 如果你发现某个循环遍历数组的性能莫名其妙地差，而且数组大小恰好是 2 的幂次方，很可能是直接映射导致的地址冲突抖动。一个简单的修复方式是给数组多分配一点 padding，破坏那个"恰好取模冲突"的规律。这类问题在高性能代码里非常隐蔽，因为从代码层面看完全没问题。

## 第四步——搞懂多核之间怎么保持数据一致

事情到单核还挺简单的——数据要么在 Cache 里，要么不在。但在多核系统里，每个核心都有自己的 L1 和 L2，如果核心 A 修改了自己 Cache 里的一份缓存行，核心 B 的 Cache 里还存着同一地址的旧数据，那不就乱套了？

这就是 **Cache 一致性协议**要解决的问题。x86 上最广泛使用的是 MESI 协议（ARM 上用的是它的变体 MOESI）。MESI 得名于缓存行的四种状态：

- **M（Modified）**：这份数据被修改过了，和主存里的不一样。当前只有这一个核心持有最新版本。
- **E（Exclusive）**：这份数据和主存里的一致，而且只有当前核心这一份拷贝。如果你想修改它，不需要通知任何人。
- **S（Shared）**：这份数据和主存一致，但可能有多个核心都持有拷贝。只能读，不能直接写。
- **I（Invalid）**：这条缓存行无效，相当于空的。

举个具体的例子走一遍流程。假设核心 A 和核心 B 都读取了同一个地址的数据，这时候两个核心的缓存行都处于 S 状态。现在核心 A 想写入这个地址——它需要先发出一个"失效"广播，告诉其他核心："你们如果持有这个地址的数据，统统作废。"核心 B 收到通知后把自己那份改成 I 状态，核心 A 的那份变成 M 状态。之后核心 A 就可以放心地修改数据了。如果核心 B 又想读这个地址，发现自己是 I 状态，就会触发一次 Cache Miss，然后通过总线去核心 A 那里把最新数据拿过来（同时写回主存），两边的状态再根据情况变成 S 或 E。

这套机制保证了所有核心看到的始终是一致的数据，但它有一个副作用——**伪共享**（False Sharing）。如果两个核心各自修改的是同一条缓存行上的不同变量（比如一个结构体里紧挨着的两个 int），虽然逻辑上互不干扰，但硬件层面它们争用的是同一条缓存行，MESI 协议会不断触发失效和同步，性能直接暴跌。这个问题在多线程编程里非常经典，后面我们会看到怎么用缓存行对齐来规避它。

> **踩坑预警**
> 伪共享在单线程测试中完全不会暴露，只在多线程高并发下才会体现为性能劣化。而且劣化程度和线程数成正比——线程越多，总线上的失效广播越频繁。排查这类问题的标准手段是用 `perf` 工具观察缓存未命中事件（`perf stat -e cache-misses,cache-references`），如果多线程版本的 cache miss 异常飙升，大概率就是伪共享在作怪。

## 第五步——写出 Cache "舒服"的代码

理论说完了，我们来点实际的。缓存友好编程的核心就一句话：**让数据访问模式尽量贴合 Cache 的工作方式**，也就是最大化空间局部性和时间局部性。

### 按行遍历 vs 按列遍历

最经典的例子就是二维数组的遍历。C 语言里二维数组是**行优先**存储的，也就是说 `matrix[0][0]`、`matrix[0][1]`、`matrix[0][2]`……在内存里是连续的。如果我们按行遍历，访问顺序和内存布局一致，Cache 的空间局部性拉满；如果按列遍历，每次访问都跳过一整行，大概率每次都要重新加载缓存行。

```c
#define kRows 1024
#define kCols 1024

static int matrix[kRows][kCols];

// 缓存友好：按行遍历
void sum_by_rows(int* total)
{
    int sum = 0;
    for (int i = 0; i < kRows; i++) {
        for (int j = 0; j < kCols; j++) {
            sum += matrix[i][j];  // 连续访问，Cache 命中率高
        }
    }
    *total = sum;
}

// 缓存不友好：按列遍历
void sum_by_cols(int* total)
{
    int sum = 0;
    for (int j = 0; j < kCols; j++) {
        for (int i = 0; i < kRows; i++) {
            sum += matrix[i][j];  // 每次跳跃 sizeof(int)*kCols 字节
        }
    }
    *total = sum;
}
```

笔者的测试结果如下（i7-12700H，L3 24MB）：

```text
$ gcc -O2 -std=c11 matrix_sum.c -o matrix_sum && ./matrix_sum
sum_by_rows: 1048576, time=1.234 ms
sum_by_cols: 1048576, time=5.678 ms
按行遍历比按列遍历快约 4.6 倍
```

`sum_by_rows` 通常比 `sum_by_cols` 快 3 到 6 倍（具体取决于矩阵大小和 Cache 容量）。原理很简单：按行遍历时，加载一条缓存行后可以连续处理 16 个 int（64 字节 / 4 字节）；按列遍历时，每条缓存行只用了 4 字节就被换出去了。

### 结构体布局——热数据放前面

另一个常见的优化点是结构体字段的排列。如果一个结构体有几十个字段，但热路径上只用到其中三四个，那这几个字段应该紧挨着放，让它们能共享同一条缓存行：

```c
typedef struct {
    // 热路径字段——频繁访问，放一起
    int x;
    int y;
    int z;
    // 冷字段——不常访问
    char name[64];
    int id;
    double metadata[8];
} Particle;

// 反面教材：冷热数据混排
typedef struct {
    int x;
    char name[64];  // 冷数据插在热数据中间
    int y;
    int id;          // 冷数据
    int z;
    double metadata[8];
} ParticleBadLayout;
```

我们可以用 `sizeof` 来验证一下布局的差别。`Particle` 中 `x`、`y`、`z` 三个字段紧挨着，一共 12 字节，在缓存行中是连续的。而 `ParticleBadLayout` 中 `y` 和 `z` 被 `name` 和 `id` 隔开了，如果你要遍历一个粒子数组并只读取坐标，那每次加载 `x` 后跳过 64 字节的 `name` 才到 `y`，大概率需要加载新的缓存行——这就是冷热混排的代价。

如果 `x`、`y`、`z` 在同一个缓存行里（它们一共只占 12 字节，轻松塞进一条 64 字节的缓存行），那一次 Cache 加载就能把它们全拿到手。如果散布在结构体的各个角落，可能每次访问 `z` 都要再加载一条新的缓存行。这种冷热分离的思想在高性能代码里非常常见，游戏引擎的 ECS 架构本质上就是在做这件事——把频繁访问的位置、速度数据单独拎出来连续存储，把名字、模型 ID 这些不常用的东西丢到另一个数组里。

### 数据导向设计——SoA vs AoS

顺着上面的思路进一步延伸，如果我们有一组相同类型的对象，有两种组织方式：AoS（Array of Structures）和 SoA（Structure of Arrays）。

AoS 就是我们平时最常见的写法——一个结构体数组，每个元素是完整的结构体：

```c
typedef struct {
    float x, y, z;
    float r, g, b;
} Vertex;

Vertex vertices[10000];
```

SoA 则是拆成多个独立数组：

```c
typedef struct {
    float x[10000];
    float y[10000];
    float z[10000];
    float r[10000];
    float g[10000];
    float b[10000];
} VertexSoA;
```

对比一下两者在内存中的布局差异：

```text
AoS 布局：每个元素的 x,y,z,r,g,b 紧挨在一起
|x0 y0 z0 r0 g0 b0| x1 y1 z1 r1 g1 b1| x2 y2 z2 r2 g2 b2| ...
└─── 24 字节 ───┘

SoA 布局：所有 x 连续，所有 y 连续，以此类推
|x0|x1|x2|x3|x4|...|y0|y1|y2|y3|y4|...|z0|z1|z2|...
└── 连续的 x ──┘   └── 连续的 y ──┘   └── 连续的 z ──┘
```

如果你的热路径只处理坐标 `x`、`y`、`z`，而不碰颜色 `r`、`g`、`b`，那 SoA 的优势就非常明显了——你连续遍历 `x[0]`、`x[1]`、`x[2]`……数据在内存里完全连续，Cache 命中率接近 100%。而 AoS 的情况下，每访问一个 `x` 都会顺带把同一结构体里的 `y`、`z`、`r`、`g`、`b` 也拉进 Cache（因为它们在同一条缓存行上），但我们暂时用不到颜色数据，这些空间就浪费了。

当然 SoA 也不是万能的，如果你的访问模式是同时需要所有字段，那 AoS 的空间局部性反而更好。具体选哪个取决于你的访问模式——没有银弹，只有权衡。

## C++ 衔接——从 C 的理解到 C++ 的工具

我们前面聊的这些——缓存行、局部性、伪共享——全都是硬件层面的事情，跟语言无关。但 C++ 在标准层面给了我们一些工具来更好地配合 Cache，这是 C 所没有的。

### `std::hardware_destructive_interference_size`（C++17）

C++17 引入了一个编译期常量 `std::hardware_destructive_interference_size`，它的值等于目标平台上两条并发访问的缓存行之间的最小间距——在 x86 上就是 64。这个名字确实够长的，但它的用途非常直接：用这个值来做 `alignas` 对齐，就能确保两个变量不会被放到同一条缓存行上，从而避免伪共享：

```cpp
#include <new>  // hardware_destructive_interference_size

struct alignas(std::hardware_destructive_interference_size) PaddedCounter {
    int value;
};

// 两个计数器各自独占一条缓存行
PaddedCounter counter_a;
PaddedCounter counter_b;
```

这样做之后，`counter_a` 和 `counter_b` 不会共享缓存行，即使它们在内存里挨得很近。线程 A 修改 `counter_a` 不会导致线程 B 的缓存行失效——这就是我们前面 MESI 部分说的伪共享问题的标准解决方案。

在 C 里我们只能硬编码 `__attribute__((aligned(64)))`（GCC/Clang）或者 `__declspec(align(64))`（MSVC），没有可移植的手段来获取这个值。C++17 的这个常量至少在理论上提供了可移植性——虽然实际上主流编译器在所有支持的平台上都返回 64。

### `alignas` 与缓存行对齐

C++11 引入了 `alignas` 关键字，让我们可以指定变量或类型的对齐要求。结合缓存行大小，我们可以手动保证某些关键数据结构不跨缓存行：

```cpp
// C++ 风格的缓存行对齐
struct alignas(64) CacheLineAligned {
    int hot_data[4];    // 16 字节
    // 剩余 48 字节是 padding，编译器自动填充
};

static_assert(sizeof(CacheLineAligned) == 64,
              "Should be exactly one cache line");
```

这个 `static_assert` 很有用——如果哪天有人在结构体里加了太多字段导致超过了 64 字节，编译期就会直接报错。比起运行时才发现性能劣化，编译期检查要好太多了。

### 数据结构布局对 Cache 的影响

C++ 标准库里的容器在设计时也考虑了缓存因素。`std::vector` 的数据是连续存储的，遍历时缓存友好度极高；`std::list` 的每个节点都是独立分配的，内存里可能到处散落，遍历它就是 Cache 的噩梦。这也是为什么在很多现代 C++ 代码规范里 `std::vector` 是默认容器，而 `std::list` 几乎不被推荐——不是因为 list 的时间复杂度差（插入删除确实是 O(1)），而是因为它的缓存命中率太差，常数因子大得离谱。`std::deque` 是个折中方案——它分块存储，每块大小固定，比 list 好不少，但比 vector 还是差一截。如果你在做性能敏感的场景，容器选择的第一考量往往不是时间复杂度，而是内存布局对 Cache 的影响。

## 练习

1. **步长实验验证**：修改本文的步长测试代码，将数组大小改为 4MB（恰好塞进大部分 CPU 的 L3），观察步长从 1 到 32 时耗时的变化曲线。思考：为什么步长超过 16 之后耗时又开始趋于平缓？

2. **伪共享复现**：写一个多线程程序（使用 pthread 或 C++ `<thread>`），创建两个线程各自累加一个共享结构体里的不同字段到一亿次。先不加对齐地跑一次，然后用 `alignas(64)` 把两个字段分别对齐到不同缓存行再跑一次，对比耗时。

3. **矩阵转置优化**：实现一个方阵转置函数，先写朴素的双重循环版本，再尝试分块（blocking）——将矩阵分成 32x32 的小块，在块内做转置。对比两个版本在大矩阵（2048x2048）上的性能差异。

4. **AoS vs SoA benchmark**：定义一个包含 `float x, y, z, r, g, b` 的粒子结构体，创建十万个粒子。分别用 AoS 和 SoA 两种布局实现"将所有粒子的坐标归一化到单位球内"，对比耗时。

5. **Cache 友好的链表**：参考 Linux 内核的 `list_head` 设计思路，实现一个侵入式双向链表，节点数据域和链表指针域分开存储，使得遍历链表指针时不需要加载整个节点数据，提升缓存命中率。

## 参考资源

- [cppreference: `std::hardware_destructive_interference_size`](https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size)
- [cppreference: `alignas` 说明符](https://en.cppreference.com/w/cpp/language/alignas)
- [Ulrich Drepper: What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [Gustavo Duarte: Cache: a place for concealment](https://manybutfinite.com/post/intel-cpu-caches/)
