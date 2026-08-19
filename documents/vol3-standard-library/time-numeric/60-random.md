---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透 <random>——rand() 为什么该退役（RAND_MAX 小、跨平台不可重现、线程不安全、没法控制分布）、<random> 的引擎/分布/设备三件套、std::random_device{}() 一行种 mt19937 的正确姿势、mt19937 与 uniform_int_distribution 的均匀性实测，以及多线程用 thread_local 引擎避免竞争
difficulty: intermediate
order: 60
platform: host
prerequisites:
- 算法总览（上）：非修改式、修改式与查找
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 15
related:
- numeric 算法：累加、前缀和与 midpoint
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'random：为什么别再用 rand()'
---

# random：为什么别再用 rand()

几乎所有 C 教程教随机数都停在 `srand(time(NULL)); rand() % N;` 这一行。能跑，能出结果，于是大家就这么用下去了——直到有一天你的模拟结果在换了一台机器之后悄悄变了、你的多线程程序在压测下偶发崩溃、你想生成一个正态分布的噪声却不知道怎么从 `rand()` 那 0 到 `RAND_MAX` 的均匀整数里搞出来。

C++11 给的标准答案是一个全新的头文件 `<random>`，它把"产生随机数"这件事拆成了三件互相独立、可以自由拼装的东西：**引擎**（生成原始的均匀无符号整数）、**分布**（把原始整数映射成你真正想要的形状——均匀、正态、伯努利……）、**设备**（提供非确定的真随机种子）。这一篇我们就把这三件套拆开讲透，顺带把 `rand()` 到底差在哪里、为什么该退役这件事用实测说清楚。需要先说一句的范围限定：**密码学安全的随机数不在本篇讨论范围内**——`std::random_device` 也不是为密码学设计的，密钥生成请用专门的密码学库（OpenSSL、libsodium 那一类）。

## rand() 的几条罪状，逐条对照实测

先把靶子立起来。`rand()` 不是"质量差得没法用"，现代 glibc 的 `rand()` 实际上是个加法反馈发生器，单纯做分布统计时看起来还行。但它有几条结构性的毛病，每一条都会在真实工程里咬你一口。

### 罪状一：RAND_MAX 太小，且跨实现不一样

先看本机的 `RAND_MAX`：

```cpp
// Standard: C++20
#include <cstdio>
#include <cstdlib>

int main()
{
    std::printf("RAND_MAX = %d\n", RAND_MAX);
    return 0;
}
```

```text
RAND_MAX = 2147483647
```

`2147483647` 也就是 `2^31 - 1`，31 bit。这在本机（Linux，glibc）是这样，但 C 标准只保证 `RAND_MAX` 至少是 `32767`——`2^15 - 1`，**只有 15 bit**。也就是说同样一行 `rand()`，在某个符合标准的实现上最多只能产生 32768 个不同的值。你想从一个 `rand()` 拼出 64 bit 的种子（比如给一个状态空间巨大的 PRNG 做初始化），至少得调好几次再移位拼起来，而且每次调用之间还有相关性，写起来又丑又容易错。

`<random>` 里的引擎就没这个毛病：`std::mt19937` 直接产 32 bit 无符号整数，`min()` 是 `0`，`max()` 是 `4294967295`（`2^32 - 1`），完整 32 bit，白纸黑字写在类型里，跨平台一致。

### 罪状二：跨平台、跨实现不可重现

下面这段在两台不同编译器/标准库的机器上跑，结果会不一样：

```cpp
std::srand(12345);
// 前 5 个值
```

本机（GCC 16.1.1 / glibc）跑出来是：

```text
srand(12345) 前5: 383100999 858300821 357768173 455282511 133005921
```

C 标准里 `rand()` 用的算法是 **implementation-defined**——glibc 用一种加法反馈发生器，MSVC 的 CRT 用另一种 LCG，BSD 又是另一种。同样的种子 `12345`，换到 Windows 上 MSVC 跑就是完全不同的序列。这对"做模拟、做测试、做对局回放"是致命的：你没法把一个种子贴给别人让他重现你的随机序列，也就没法复现一个由随机数驱动的 bug。

对比 `std::mt19937`：它是一个数学上完全确定的算法（Mersenne Twister，MT19937），标准规定死了，**同一种子在任何符合标准的实现上产生完全相同的序列**：

```cpp
// Standard: C++20
#include <cstdio>
#include <random>

int main()
{
    std::printf("mt19937(12345) 前5: ");
    std::mt19937 eng(12345);
    for (int i = 0; i < 5; ++i) std::printf("%u ", eng());
    std::printf("\n");
    return 0;
}
```

```text
mt19937(12345) 前5: 3992670690 3823185381 1358822685 561383553 789925284
```

这一串你贴给用 Clang+libc++ 的同事、贴给用 MSVC 的同事，跑出来分毫不差。这才是"可复现的随机"该有的样子。

### 罪状三：线程不安全

`rand()` 维护一个进程级的内部状态，每次调用都读写它。C 标准里这个状态**不是线程安全**的——多线程同时调 `rand()` 是数据竞争，属于未定义行为。POSIX 在此基础上给 glibc 的 `rand()` 加了锁，所以下面这段在本机 Linux 上"看起来能跑通"：

```cpp
// Standard: C++20
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main()
{
    std::atomic<int> total{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&]() {
            for (int i = 0; i < 100000; ++i) {
                total += std::rand() & 1;   // 0 或 1
            }
        });
    }
    for (auto& th : ts) th.join();
    std::printf("4 线程各取 100000 次奇偶，1 的总数 = %d\n", total.load());
    return 0;
}
```

```text
4 线程各取 100000 次奇偶，1 的总数 = 199624
```

能跑、结果看着也对。但别被骗了：这是 glibc 给你兜的底，不是 C 标准给你的保证。换一个不加锁的实现（比如某些嵌入式平台的精简 libc），这段直接就是数据竞争，轻则随机数质量塌掉，重则崩溃。"在我的机器上能跑"在这里是站不住脚的论据。

`<random>` 里每个引擎对象**自带状态**，你想要几个就开几个，线程之间只要不共享同一个对象，就天然没有竞争。后面我们会演示用 `thread_local` 让每线程一个引擎的标准写法。

### 罪状四：没法直接表达"分布"

`rand() % N` 只能给你 0 到 N-1 的**均匀整数**。那如果你要的是"均值为 0、标准差为 1 的正态分布噪声"呢？"以 0.7 概率为真的布尔值"呢？"[0, 1) 区间的浮点数"呢？

用 `rand()` 你得自己写——正态分布要 Box-Muller 变换、浮点要除以 `RAND_MAX` 还要小心精度、伯努利要 `rand() < p * RAND_MAX`。每一样都不难，但每一样都得自己实现、自己测、自己保证没写错，而且你写的版本换一个 PRNG 就得重来。

`<random>` 把这些"把均匀整数变成目标分布"的数学全部封成了**分布对象**，引擎是引擎、分布是分布，随便组合。下一节我们就把这套机制讲透。

::: warning 顺带说清"低 bit 不随机"这条老罪状
很多老资料会强调 `rand() % N` "低 bit 高度不随机、有周期性模式"。这条**在历史上、对某些实现是对的**——最朴素的线性同余发生器（LCG）的最低 bit 会严格交替 `0,1,0,1...`（周期 2），次低 bit 周期 4，以此类推，取模恰好把这些低 bit 暴露出来。但现代 glibc 的 `rand()` 早已不是简单 LCG，我们实测 `rand()%2` 在 10 万次采样里相邻相等的次数约一半（`49945 / 100000`），并没有"严格交替"的毛病。所以更准确的说法是：**`rand()` 的低 bit 质量依赖具体实现、且标准不保证**，你不该把自己的程序正确性建立在"我这个平台的 rand 低 bit 碰巧还行"上。用 `mt19937` + `uniform_int_distribution` 一开始就把这条不确定性消掉。
:::

## `<random>` 三件套：引擎、分布、设备

讲完痛点，来看正确的工具长什么样。`<random>` 的设计哲学一句话：**把"随机从哪来"和"随机是什么形状"分开**。

- **引擎（engine）**：只负责吐原始的均匀分布无符号整数。`mt19937`、`minstd_rand`、`ranlux24` 这些都是引擎。它是一个有状态的对象，每次调用 `eng()` 推进一步状态、返回一个值。
- **分布（distribution）**：把引擎吐出来的原始整数，映射成你想要的目标分布。`uniform_int_distribution`、`normal_distribution`、`bernoulli_distribution`……它本身**无状态**（绝大多数如此），只是个函数对象：`dist(eng)`。
- **设备（device）**：`std::random_device`，访问外部熵源（Linux 上通常是 `/dev/urandom`），吐**非确定**的值，专门用来给引擎做种子。

三件套的分工很干净：引擎决定"随机数序列有多长周期、多均匀、多快"，分布决定"这些数要被塑造成什么形状"，设备决定"从哪弄一个不可预测的起点"。你想要正态分布，就是"设备种一个引擎、引擎喂给正态分布"，三步各自独立、可替换。

### 引擎：为什么 mt19937 是默认选择

标准库提供了一堆引擎，最常用的就一个：`std::mt19937`。它是 Mersenne Twister（梅森旋转）算法的 32 bit 版本，名字里的 19937 来自它的周期长度——`2^19937 - 1`，这是一个天文数字，你不可能在程序生命周期里跑到它重复。它的内部状态是 624 个 32 bit 字（`std::mt19937::state_size == 624`，本机实测），质量好、速度快、统计性质经过广泛检验，绝大多数场景用它就够了。

另外两个偶尔会撞见的引擎家族：

- `std::linear_congruential_engine`：线性同余，就是 `x = a*x + c mod m` 那套老办法，`minstd_rand0` / `minstd_rand` 是它的预设实例。状态小（一个整数）、快、但质量一般，只在"状态要极小、对统计质量要求不高"的场景（比如某些嵌入式约束）才考虑。
- `std::subtract_with_carry_engine`：带进位的减法发生器（Lagged Fibonacci），`ranlux24` / `ranlux48` 是预设实例。某些场合统计性质好，但默认还是选 mt19937。

一个很实用的细节：`mt19937` 直接构造只接一个 32 bit 种子，可它的状态空间是 624 个字、`2^19937` 那么大——只给一个 32 bit 种子，等于只在 `2^32` 个起始状态里挑，可重现的"起点"被大大压缩了。如果你在意这个（比如长时间跑模拟不想撞种子），标准库给了 `std::seed_seq`，可以把多个种子字填进去再喂给引擎，把初始状态铺满。日常用单种子足够，知道有这么个进阶选项就行。

### 分布：把均匀整数塑造成你要的形状

分布才是 `<random>` 真正省心的地方。引擎吐的是 `[0, 2^32-1]` 的均匀整数，你要的几乎从来都不是这个。分布对象负责做这个映射，而且**自己处理好了取模偏差（modulo bias）**——这是手写 `eng() % N` 会踩、但 `uniform_int_distribution` 自动避开的坑。

我们先看最常见的几种分布各跑一百万个样本：

```cpp
// Standard: C++20
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

int main()
{
    std::mt19937 eng(2024);

    // 正态分布: 均值 0, 标准差 1
    std::normal_distribution<double> norm(0.0, 1.0);
    constexpr int N = 1000000;
    double sum = 0, sum2 = 0;
    std::vector<long long> hist(10, 0);   // [-5, 5) 分 10 桶，每桶宽 1.0
    for (int i = 0; i < N; ++i) {
        double x = norm(eng);
        sum += x; sum2 += x * x;
        int b = int(x + 5.0);
        if (b >= 0 && b < 10) ++hist[b];
    }
    double mean = sum / N;
    double stddev = std::sqrt(sum2 / N - mean * mean);
    std::printf("normal(0,1) %d 样本: 均值=%.4f 标准差=%.4f\n", N, mean, stddev);
    std::printf("直方图(每桶宽1.0, [-5,5)):\n");
    for (int i = 0; i < 10; ++i) {
        std::printf("  [%+.0f,%+.0f) %lld\n", i - 5.0, i - 4.0, hist[i]);
    }

    // 伯努利: p=0.7
    std::bernoulli_distribution bern(0.7);
    long long trues = 0;
    for (int i = 0; i < N; ++i) if (bern(eng)) ++trues;
    std::printf("bernoulli(0.7) %d 样本: true 占比=%.4f\n", N, double(trues) / N);

    // 均匀实数: [0, 1)
    std::uniform_real_distribution<double> ureal(0.0, 1.0);
    double usum = 0;
    for (int i = 0; i < N; ++i) usum += ureal(eng);
    std::printf("uniform_real(0,1) %d 样本: 均值=%.4f (期望 0.5)\n", N, usum / N);

    return 0;
}
```

```text
normal(0,1) 1000000 样本: 均值=-0.0005 标准差=1.0002
直方图(每桶宽1.0, [-5,5)):
  [-5,-4) 29
  [-4,-3) 1346
  [-3,-2) 21417
  [-2,-1) 136208
  [-1,+0) 340726
  [+0,+1) 341356
  [+1,+2) 136132
  [+2,+3) 21484
  [+3,+4) 1269
  [+4,+5) 33
bernoulli(0.7) 1000000 样本: true 占比=0.7008
uniform_real(0,1) 1000000 样本: 均值=0.5000 (期望 0.5)
```

想跑一遍看分布统计？点开下面这个在线示例（0.04 秒跑完）：

<OnlineCompilerDemo
  title="<random> 分布演示：normal / bernoulli / uniform"
  source-path="code/examples/vol3/60_random_distributions.cpp"
  description="mt19937 喂给三种分布各跑一百万样本：正态看钟形直方图、bernoulli(0.7) 看 true 占比、uniform_real(0,1) 看均值逼近 0.5——这些是 rand()%N 做不到的"
  allow-run
/>

几件事一眼就能看出来。正态分布的直方图是一根漂亮的钟形曲线，中间 `[-1,+1)` 两桶各 34 万、往外对称衰减，均值 `-0.0005`、标准差 `1.0002`，几乎就是标称的 `(0, 1)`。伯努利 `0.7` 的 true 占比 `0.7008`。均匀实数 `[0, 1)` 的均值 `0.5000`。这些都是你用 `rand()` 要自己实现、自己验证的活，`<random>` 替你做完了，而且做得对。

最关键的差别在 `uniform_int_distribution`。你大概会想：均匀整数不就是 `eng() % N` 吗？真这么写就踩了取模偏差。原因是：引擎吐的值域是 `[0, 2^32-1]`，一共 `2^32` 个值，而 `2^32` 不一定能被你的 `N` 整除。比如 `N=3`，`2^32 = 4294967296 = 3 * 1431655765 + 1`，余 1，那 bucket 0 会比 bucket 1、2 多一个候选值，分布就不是均匀的了（偏差很小，但客观存在）。`uniform_int_distribution` 内部用拒绝采样（rejection sampling）把这一段"多出来的尾巴"丢弃重抽，保证每个 bucket 概率严格相等。这件事在 `RAND_MAX` 只有 15 bit 的平台上偏差会很显眼，在 glibc 31 bit 的 `rand()` 上小到几乎测不出来——但"依赖平台给的面子"本来就是要退役 `rand()` 的理由之一。

我们实测 `rand()%3` 和 `uniform_int_distribution(0,2)` 各取 3 亿样本，bucket 命中：

```text
rand()%3 取 300000000 样本:
  bucket 0: 100009515
  bucket 1: 99993723
  bucket 2: 99996762
mt19937+uniform_int(0,2) 取 300000000 样本:
  bucket 0: 99999397
  bucket 1: 99992920
  bucket 2: 100007683
```

在 glibc 上两者都在噪声范围内（偏差万分之一量级），说明本机 `rand()` 的 modulo bias 确实很小。但结论不是"`rand()%3` 没问题"，而是"本机刚好没问题、换个平台就未必"——`uniform_int_distribution` 让你从一开始就不操这份心。

## 正确写法：一行干净的新手模板

把上面拼起来，"生成一个 1 到 100 的均匀整数"这件事，标准、可移植、无偏差的写法就这一行核心：

```cpp
// Standard: C++20
#include <cstdio>
#include <random>

int main()
{
    std::random_device rd;                       // 1. 设备：非确定种子
    std::mt19937 eng(rd());                      // 2. 引擎：用种子初始化
    std::uniform_int_distribution<int> dist(1, 100);  // 3. 分布：[1, 100] 闭区间

    std::printf("rd{}() 种 mt19937 + uniform(1,100) 10 个: ");
    for (int i = 0; i < 10; ++i) std::printf("%d ", dist(eng));
    std::printf("\n");
    return 0;
}
```

```text
rd{}() 种 mt19937 + uniform(1,100) 10 个: 97 60 100 11 25 17 57 16 43 86
```

三步对应三件套：`random_device` 取一个不可预测的种子、`mt19937` 用它初始化、`uniform_int_distribution` 把引擎的输出映射到你要的闭区间 `[1, 100]`。注意 `uniform_int_distribution` 的区间是**闭区间**（两个端点都取得到），这跟一堆语言的半开区间不一样，写 `dist(1, 100)` 是真能抽到 100 的。

一个更紧凑的写法是直接用临时对象取种子：`std::mt19937 eng(std::random_device{}());`。`random_device{}` 构造一个设备对象、`()` 调一次取一个值、整个表达式作为 `eng` 的构造参数。这一行在教程和真实代码里都极常见，背下来就行。

如果是要**可复现**（测试、模拟回放），就把 `random_device` 那步换成固定种子：`std::mt19937 eng(42);`，序列就锁死了，跨平台一致。要不要可复现，是这一行唯一的分叉点，其它都一样。

::: warning random_device 不是密码学安全的
`std::random_device` 大多数实现读 `/dev/urandom`，质量很好，但**标准允许它退化为一个确定性的伪随机数发生器**（历史上某些 MinGW 版本就这么干过，返回的就是 `rand()` 一类的东西），而且它**不是密码学安全的**。生成密钥、token、盐值这些安全敏感场景，用专门的密码学库（OpenSSL 的 `RAND_bytes`、libsodium 的 `randombytes_buf`），不要用 `<random>`。
:::

## 多线程随机：每个线程一个 thread_local 引擎

最后一个高频痛点。多线程程序里要随机数，最忌讳的是**多个线程共享同一个引擎对象**——引擎有状态，并发调用就是数据竞争，要么加锁（性能差）、要么 UB。

正解是给每个线程一个独立的引擎，用 `thread_local` 存储。每个线程进来自动有自己那份引擎、各自的状态，互不干扰，无需加锁：

```cpp
// Standard: C++20
#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

// 每个线程一个独立引擎，thread_local 保证线程私有
thread_local std::mt19937 tl_eng{std::random_device{}()};

int main()
{
    std::atomic<int> total{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&]() {
            std::uniform_int_distribution<int> dist(1, 100);
            for (int i = 0; i < 100000; ++i) total += dist(tl_eng);
        });
    }
    for (auto& th : ts) th.join();
    std::printf("4 线程各取 100000 次 uniform(1,100), 总和=%d\n", total.load());
    std::printf("期望均值约 50.5 * 400000 = %d\n", (int)(50.5 * 400000));
    return 0;
}
```

```text
4 线程各取 100000 次 uniform(1,100), 总和=20196410
期望均值约 50.5 * 400000 = 20200000
```

4 个线程各抽 10 万次、总和约 2020 万，跟期望的 `50.5 * 400000 = 20200000` 吻合。这里有几个细节值得点一下。

第一，`thread_local std::mt19937` 用 `random_device{}()` 初始化，意味着**每个线程第一次访问它时各自取一个真随机种子**，所以不同线程的引擎起点不同、序列不同，不会出现"所有线程走同一条随机序列"的尴尬。

第二，分布对象 `dist` 是在 lambda 内部、循环外面构造的——分布基本无状态，构造一次反复调用即可，别写在内层循环里每次都构造（虽然开销不大，但没必要）。这里 `dist` 是每个线程局部变量，也没共享问题。

第三，`thread_local` 不是免费的午餐：每个线程首次访问会触发引擎的构造（含 `random_device` 一次系统调用 + mt19937 的 624 字状态初始化），有一次性开销。所以它适合"这个线程会反复取随机数"的场景；如果某个线程只取一两次随机数就退出，开 thread_local 不划算，直接局部构造一个引擎就行。

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下，每条都是上面实测验证过的：

::: warning mt19937 单种子只覆盖 2^32 个起点
`std::mt19937 eng(seed)` 只接受一个 32 bit 种子，但它的状态空间是 `2^19937`。只用单种子，意味着 `2^32` 个程序实例各自走 `2^19937` 状态空间里不相交的一条轨道——对绝大多数应用够用，但如果你要同时跑海量的独立模拟、又担心起点碰撞，用 `std::seed_seq` 填多个种子字初始化，把起点空间铺满。
:::

::: warning uniform_int_distribution 是闭区间
`uniform_int_distribution<int>(1, 100)` 的范围是 `[1, 100]`，**两端都闭**，100 抽得到。这跟 Python `random.randint` 一样、但跟一堆半开区间 API（`randint` 的反面、`std::uniform_real_distribution` 的半开 `[a, b)`）相反，用之前确认清楚你要的是闭还是半开，别照着别的语言的习惯写。
:::

::: warning 别在循环里反复构造分布或引擎
引擎构造贵（mt19937 要初始化 624 字状态），分布构造虽便宜也非零。把它们提到循环外面，引擎尽量提到函数/类的生命周期，分布按需构造一次反复用。把 `std::mt19937 eng(...)` 写进热路径的内层循环里，是新手常见的性能坑。
:::

::: warning 多线程共享引擎就是数据竞争
引擎有状态，多个线程不加锁地调同一个引擎对象是 UB。要么 `thread_local` 每线程一个，要么用互斥保护（但会成瓶颈）。默认就上 `thread_local`。
:::

::: warning random_device 可能退化为伪随机
标准允许 `std::random_device` 在没有真熵源时退化为确定性发生器，且它不是密码学安全的。种子用途一般没问题，安全敏感场景换密码学库。
:::

## 小结

`<random>` 的思路一句话：**把"随机从哪来"（引擎）、"随机是什么形状"（分布）、"起点从哪取"（设备）三者解耦**。几条关键结论收一下：

- `rand()` 该退役的真正理由不是"在现代 glibc 上分布有多差"（实测其实还行），而是 **`RAND_MAX` 小且跨实现不一致、算法 implementation-defined 导致跨平台不可重现、标准层面线程不安全、没法直接表达分布**——每一条都是真实工程会咬人的结构问题。
- 三件套分工：引擎（`mt19937` 最常用，周期 `2^19937-1`、状态 624 字）吐均匀无符号整数、分布（`uniform_int`/`uniform_real`/`normal`/`bernoulli` 等）把它塑形成目标形状、设备（`random_device`）提供非确定种子。
- 正确写法一行核心：`std::random_device rd; std::mt19937 eng(rd()); std::uniform_int_distribution<int> dist(1, 100);` —— 要可复现就把 `rd()` 换成固定整数种子。
- `uniform_int_distribution` 内部用拒绝采样消除了 `eng() % N` 的取模偏差，且区间是**闭区间**——这两点是手写最容易错的。
- 多线程用 `thread_local std::mt19937` 每线程一个引擎，避免共享状态的数据竞争；别在热路径里反复构造引擎。
- `random_device` 不是密码学安全的，密钥/token 走专门密码学库。

下一篇我们换个话题——看 `<random>` 之外、标准库提供的另一类"把数据变个样"的设施。

## 参考资源

- [cppreference: `<random>`](https://en.cppreference.com/w/cpp/numeric/random) —— 引擎/分布/设备总览与完整目录
- [cppreference: std::mt19937](https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine) —— Mersenne Twister 引擎、`state_size` 与周期
- [cppreference: std::uniform_int_distribution](https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution) —— 闭区间与拒绝采样消除取模偏差
- [cppreference: std::random_device](https://en.cppreference.com/w/cpp/numeric/random/random_device) —— 非确定熵源及其实现退化注意事项
- [cppreference: std::rand](https://en.cppreference.com/w/cpp/numeric/random/rand) —— `rand()` 的线程安全性与跨实现差异说明
