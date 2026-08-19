---
chapter: 7
cpp_standard:
- 17
- 20
description: 讲透 <execution> 四种策略（seq/par/par_unseq/unseq）各自允许的并行与向量化语义、reduce 为何要求结合律、以及并行算法「何时真变快、何时反而更慢」的工程判断——附本机 GCC 16.1.1 + libstdc++/TBB 的真实耗时实测，不编造加速数字
difficulty: advanced
order: 46
platform: host
prerequisites:
- 迭代器基础与 category
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- advanced
- 容器
title: 并行算法：execution 策略与何时真的变快
---

# 并行算法：<execution> 策略与何时真的变快

前面算法那几篇，`std::sort`、`std::accumulate`、`std::copy` 都是单线程跑的——一份工作从头干到尾。可现在的机器动辄十几二十个核，让 `sort` 在八个核上同时排，不是天经地义该更快吗？

C++17 给了这套机制：给标准库算法多塞一个「执行策略」参数，声明你允许多大的并行度，库自己决定怎么切分、怎么调度。一行 `std::sort(std::execution::par, v.begin(), v.end())`，理论上就把活儿摊到了多核。听起来很美，但这里有两个真实工程问题，恰恰是这一篇要拆透的：

第一，**并行不等于更快**。线程创建、任务切分、结果汇总这些开销不是免费的，数据量太小或者算法本身被内存带宽卡死（比如 `reduce` 一通累加），并行反而更慢。我们不写「并行就是好」的口号，而是拿本机的真实耗时数据，看清楚到底什么时候才值得加那个 `par`。

第二，**并行改变了算法对函数对象的要求**。`std::transform` 在单线程下，你传一个可交换但不满足交换律的 lambda 都没事；一旦切到 `par`，标准允许它以任意结合顺序跑，不满足结合律的算法结果就是错的。这一篇会把「哪些算法能 par、哪些不能」讲清楚，而不是盲目地把 `par` 塞进每一个算法。

## 四种执行策略：你允许库干多激进的事

`<execution>` 头文件里定义了四个策略对象，从保守到激进依次是 `seq`、`par`、`par_unseq`，外加 C++20 新增的 `unseq`。它们不是「指定用几号线程」的开关——你给不了那么细的控制——而是声明「允许库把元素访问函数以多自由的方式调度」。库拿到这个授权，才去决定要不要切线程、要不要向量化。

先看一个最小例子，四种策略都能编译通过（本机 GCC 16.1.1）：

```cpp
// Standard: C++20
#include <algorithm>
#include <execution>
#include <iostream>
#include <vector>

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

    std::sort(std::execution::seq, v.begin(), v.end());
    std::sort(std::execution::par, v.begin(), v.end());
    std::sort(std::execution::par_unseq, v.begin(), v.end());
    std::sort(std::execution::unseq, v.begin(), v.end());
    std::cout << "all four policies compiled and ran\n";
    return 0;
}
```

```text
all four policies compiled and ran
```

四个都能过编译。那它们到底差在哪？关键在「元素访问函数（element access function）」的调用之间，**允许什么样的重叠行为**。cppreference 把四种策略的语义讲得很精确，我们提炼成下面这张表：

| 策略 | 多线程？ | 向量化？ | 同一线程内调用之间的关系 | 能加锁吗 |
|------|---------|---------|--------------------------|---------|
| `seq`（C++17） | 否 | 否 | indeterminately sequenced（不可重叠，顺序不定） | 能 |
| `par`（C++17） | 是 | 否 | indeterminately sequenced（同线程内不可重叠） | 能（并行 forward progress 保证持有锁的线程会被再次调度） |
| `par_unseq`（C++17） | 是 | 是 | unsequenced（同一线程内可交错、可向量化） | **不能**（weakly parallel progress，线程不一定被再次调度） |
| `unseq`（C++20） | 否 | 是 | unsequenced（单线程内向量化、可交错） | **不能** |

这张表里最容易翻车的是最后两行——`par_unseq` 和 `unseq` 因为允许在单线程内把多次调用交错（unsequenced），所以你的函数对象里**不能调用任何 vectorization-unsafe 的操作**：加锁（`std::mutex::lock`）、非 lock-free 的 `std::atomic`、甚至 `new`/`delete` 都算。cppreference 给了一个直接的反例：

```cpp
int x = 0;
std::mutex m;
int a[] = {1, 2};
std::for_each(std::execution::par_unseq, std::begin(a), std::end(a), [&](int) {
    std::lock_guard<std::mutex> guard(m);   // 错误：构造里调 m.lock()，vectorization-unsafe
    ++x;
});
```

为什么 `par_unseq` 不让加锁？因为「unsequenced」意味着同一个线程内的两次元素访问可以交错执行——指令流水线随时可能从函数 A 跳到函数 B 再跳回来。一旦允许这种交错，加锁/解锁就不再能保证成对配对，互斥量语义直接崩塌。所以标准干脆规定：用了 unsequenced 策略，就别想同步。需要加锁的并行场景，最多用 `par`（它保证同线程内调用不交错、且持有锁的线程会被重新调度）。

至于 `seq` 和 `par` 的区别就直观多了：`seq` 永远单线程、库不许切；`par` 允许库开线程，但同一线程上的多次调用仍然是「顺序不可重叠」的，所以你照样可以加锁——这也是 `par` 在实战里用得最多的原因，它够快又没那么挑食。

::: warning 别把策略当成「指定线程数」
这四个策略里没有一个是让你写「给我开 8 个线程」的。要不要并行、开几个线程，是库（在 libstdc++ 里就是底下的 TBB）决定的，你只是授权。想精细控制并发度，得直接上 vol5 并发卷讲的 `std::thread`/`std::async`/线程池，而不是靠执行策略。
:::

## 怎么用：给算法塞一个策略参数

用法本身很省心——几乎所有 `<algorithm>` 算法都有一个带执行策略的重载，策略是**第一个参数**，插在迭代器之前。`<numeric>` 里 C++17 新增的几个算法（`reduce`、`transform_reduce`、各类 `scan`）也有并行版本。

```cpp
// Standard: C++20
#include <algorithm>
#include <execution>
#include <numeric>
#include <vector>

void demo(std::vector<int>& v) {
    // 排序：允许并行
    std::sort(std::execution::par, v.begin(), v.end());

    // 累加：reduce 是 accumulate 的并行友好版（要求结合律，后面详谈）
    long sum = std::reduce(std::execution::par, v.begin(), v.end(), 0L);

    // 逐元素改写
    std::transform(std::execution::par, v.begin(), v.end(), v.begin(),
                   [](int x) { return x * 2; });

    // 对每个元素执行一个操作（注意：不保证顺序）
    std::for_each(std::execution::par, v.begin(), v.end(),
                  [](int x) { /* 用 x */ });
}
```

关键就一句话：**策略是额外的参数，加了它，你就授权库用对应方式调度；不加它（用老的 `std::sort(beg, end)` 形式），等价于 `seq`**。所以从老代码迁到并行版，最小改动就是函数最前面塞一个 `std::execution::par`。

但「能加」不代表「该加」。接下来这一节是我们这一篇最较真的地方——**拿真实数据看看到底加得值不值**。

## 实测：并行什么时候真变快，什么时候反而更慢

这一节所有数字都是本机跑出来的，环境是 AMD Ryzen 7 5800H（8 核 16 线程）、GCC 16.1.1、libstdc++ 的并行后端是 TBB（这一点稍后会专门讲，是个真坑）。编译命令统一是 `g++ -std=c++20 -O2 bench.cpp -ltbb`，每个程序连跑两次取一次代表。

### 先看大数据量：par 确实显著变快

我们用两个典型算法测——`reduce`（纯算术，受内存带宽限制）和 `sort`（计算密集，需要大量比较和搬动）。数据量都开得足够大。

```cpp
// Standard: C++20
#include <algorithm>
#include <chrono>
#include <execution>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
    const std::size_t kReduceN = 50'000'000;
    const std::size_t kSortN = 5'000'000;

    std::vector<long> v(kReduceN);
    for (std::size_t i = 0; i < kReduceN; ++i) v[i] = static_cast<long>(i % 1000);

    auto t0 = Clock::now();
    long s_seq = std::reduce(std::execution::seq, v.begin(), v.end(), 0L);
    double dt_seq = ms_since(t0);

    auto t1 = Clock::now();
    long s_par = std::reduce(std::execution::par, v.begin(), v.end(), 0L);
    double dt_par = ms_since(t1);

    std::cout << "=== reduce N=" << kReduceN << " ===\n";
    std::cout << "seq: " << dt_seq << " ms\n";
    std::cout << "par: " << dt_par << " ms  (speedup " << (dt_seq / dt_par) << "x)\n\n";

    std::mt19937 rng(42);
    std::vector<int> a(kSortN), b(kSortN);
    for (std::size_t i = 0; i < kSortN; ++i) {
        int x = static_cast<int>(rng());
        a[i] = x; b[i] = x;
    }

    auto t2 = Clock::now();
    std::sort(std::execution::seq, a.begin(), a.end());
    double dt_sort_seq = ms_since(t2);

    auto t3 = Clock::now();
    std::sort(std::execution::par, b.begin(), b.end());
    double dt_sort_par = ms_since(t3);

    std::cout << "=== sort N=" << kSortN << " ===\n";
    std::cout << "seq: " << dt_sort_seq << " ms\n";
    std::cout << "par: " << dt_sort_par << " ms  (speedup " << (dt_sort_seq / dt_sort_par) << "x)\n";
    return 0;
}
```

```text
=== reduce N=50000000 ===
seq: 24.3248 ms
par: 16.2351 ms  (speedup 1.49829x)

=== sort N=5000000 ===
seq: 341.455 ms
par: 62.773 ms  (speedup 5.43952x)
```

两个算法的加速差异巨大，这恰好说明了一个核心道理：**并行加速好不好，取决于这个算法在单线程时是被什么卡住的**。

`sort` 加速 5 倍多，因为排序是计算密集型——大量的比较、搬动、随机内存访问，CPU 算力是瓶颈。把活儿切到 8 个核上，每个核都能把算力打满，加速比自然接近核数（这里不到 8 是因为还有任务切分、合并的开销）。

`reduce` 只加速 1.5 倍，看起来「不上不下」，原因是它被**内存带宽**卡住了。`reduce` 的计算就一个加法，单核早就算得比内存供数据还快——瓶颈在「把 5000 万个 long 从内存搬到 CPU」这一步，这一步是 8 个核共享同一条内存总线的数据通路的，多开几个核也搬不动更多数据。这就是典型的 memory-bound 场景，并行能榨出的收益天然有限。

换句话说：**判断一个算法并行划不划算，先问它单线程时是 compute-bound 还是 memory-bound**。计算密集（`sort`、`transform` 配重计算）就划算，内存带宽密集（`reduce` 这种轻量逐元素归并）就天花板很低。这个判断比盲目加 `par` 重要得多。

### 再看小数据量：par 反而慢 60 倍

把数据量缩到 1000 个元素，同样的 `reduce`，对比 `seq` 和 `par` 的耗时（取 5 次里最好的一次）：

```cpp
// Standard: C++20
#include <chrono>
#include <execution>
#include <iostream>
#include <numeric>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
    const std::size_t kSmallN = 1000;
    std::vector<int> v(kSmallN, 1);
    double best_seq = 1e9, best_par = 1e9;
    for (int i = 0; i < 5; ++i) {
        auto t0 = Clock::now();
        volatile long s1 = std::reduce(std::execution::seq, v.begin(), v.end(), 0L);
        (void)s1;
        best_seq = std::min(best_seq, ms_since(t0));
        auto t1 = Clock::now();
        volatile long s2 = std::reduce(std::execution::par, v.begin(), v.end(), 0L);
        (void)s2;
        best_par = std::min(best_par, ms_since(t1));
    }
    std::cout << "seq: " << best_seq << " ms\n";
    std::cout << "par: " << best_par << " ms\n";
    std::cout << "par/seq ratio: " << (best_par / best_seq) << "  (>1 means par slower)\n";
    return 0;
}
```

```text
seq: 0.00014 ms
par: 0.008376 ms
par/seq ratio: 59.8286  (>1 means par slower)
```

`par` 慢了将近 **60 倍**。原因再直白不过：1000 个元素的加法，单线程几个微秒就干完了；可 `par` 要为这次调用启动 TBB 调度、切任务、派线程、汇总结果——这些**固定开销**本身就比整个顺序计算还贵。数据量越小，固定开销占的比重越大，并行就越亏。

这条结论收一下：**并行的固定开销不是零，存在一个盈亏平衡点**。对 `reduce` 这种轻量操作，这个平衡点可能在十万、百万级；对 `sort` 这种计算密集操作，平衡点更低。实战中别无脑加 `par`，对拿不准的数据量，要么自己测一下，要么干脆别加——顺序算法在小数据上永远是最优的。

::: warning 别因为「显得现代」就加 par
一个常见误区是看到标准库支持并行版，就无脑把所有 `std::sort` 都改成 `std::sort(std::execution::par, ...)`。对几百几千元素的容器，这一改大概率让代码变慢几十倍，还白白占用了线程池。`par` 是给「数据量大到值得并行」的场景准备的，不是装饰品。
:::

## 并行的代价：算法对函数对象的要求变了

并行能加速，代价是它对函数对象的要求比顺序版**严苛**得多。核心两条：**可结合**（associative）和（对 unsequenced 策略）**vectorization-safe**。不满足这两条的算法，要么结果错，要么直接编译/运行出问题。

### reduce 要求结合律：accumulate 不要求，reduce 要求

最典型的对比是 `std::accumulate` 和 `std::reduce`。两个都是「把一串元素归并成一个值」，长得几乎一样，但语义要求天差地别：

- `std::accumulate` 是严格的**左折叠**——从左到右一个一个算，结合顺序固定。所以它**不要求**二元操作可结合。
- `std::reduce` 允许库以**任意结合顺序**计算（这样才能切分到多核上各算各的），所以它**要求**二元操作可结合；默认的 `+` 满足，自定义操作得你自己保证。

这个差异在浮点数上立刻现形——浮点加法**不满足结合律**：`(a+b)+c` 和 `a+(b+c)` 在浮点下结果可能不同。所以同一组 float 喂给 `accumulate`、`reduce(seq)`、`reduce(par)`，三个结果会不一样：

```cpp
// Standard: C++20
#include <execution>
#include <iostream>
#include <numeric>
#include <vector>

int main() {
    std::vector<float> v;
    for (int i = 0; i < 100000; ++i) v.push_back(0.1f);

    float acc     = std::accumulate(v.begin(), v.end(), 0.0f);
    float red_seq = std::reduce(std::execution::seq, v.begin(), v.end(), 0.0f);
    float red_par = std::reduce(std::execution::par, v.begin(), v.end(), 0.0f);

    std::cout.precision(12);
    std::cout << "accumulate (left fold): " << acc << "\n";
    std::cout << "reduce seq           : " << red_seq << "\n";
    std::cout << "reduce par           : " << red_par << "\n";
    return 0;
}
```

```text
accumulate (left fold): 9998.55664062
reduce seq           : 10000.3525391
reduce par           : 10000.3349609
```

三个结果都不同。数学上「正确答案」是 10000（0.1 加十万次），但浮点误差让它偏离，而且偏离多少取决于结合顺序——`accumulate` 一直把小数累加到一个越来越大的累积值上，误差累积最狠（差了 1.4）；`reduce` 把序列切成块、块内累加再合并，块内的值小、误差小，反而更接近真值。`reduce seq` 和 `reduce par` 也不一样，因为切分方式不同。

这件事的本质是：**浮点加法不满足结合律，所以从数学上它就不该并行**。标准库允许你这么做（不报错），代价是结果和顺序版不同、甚至每次跑都不同（取决于线程调度）。如果你的程序对浮点结果的**可复现性**有要求（比如金融、科学计算要逐位一致），用 `reduce(par)` 就是埋雷。要么用 `accumulate` 牺牲并行，要么上 Kahan 求和之类的补偿算法。

反过来，整数加法、位运算、逻辑与或这些都天然满足结合律，`reduce` 并行起来安全。所以判断「我这个 reduce 能不能 par」时，先问**你的二元操作结合律成不成立**——而不是问数据类型。

::: warning reduce(init, op) 形式还要求 op 对 init 可交换
`std::reduce(first, last, init, op)` 这个四参数重载，除了要求 `op` 可结合，还要求 `op(init, x)` 和 `op(x, init)` 都合法且结果一致——也就是说 init 和元素在 op 下要可交换。原因还是并行切分：库可能把 init 跟任意一块组合。自定义 op 时如果 init 是个特殊「单位元」类型，要确保 op 在两边位置都正确。
:::

### par 下异常会 std::terminate

顺序算法（包括 `seq` 策略）里，函数对象抛异常会正常往上传播，你可以 `catch`。但所有并行策略下——`par`、`par_unseq`、`unseq`——一旦元素访问函数抛了未捕获异常，标准规定直接调 `std::terminate`，程序挂掉。我们实测验证：

```cpp
// Standard: C++20
#include <algorithm>
#include <csignal>
#include <execution>
#include <iostream>
#include <stdexcept>
#include <vector>

void on_term() {
    std::cout << "std::terminate called (exception escaped par algorithm)" << std::endl;
    std::_Exit(1);
}

int main() {
    std::set_terminate(on_term);
    std::vector<int> v(10, 1);
    std::size_t i = 0;
    try {
        std::for_each(std::execution::par, v.begin(), v.end(), [&i](int& x) {
            if (i++ == 3) throw std::runtime_error("boom");
            x = 2;
        });
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";   // 这行不会被走到
    }
    return 0;
}
```

```text
std::terminate called (exception escaped par algorithm)
```

注意看：外面的 `try/catch` 没接到异常——异常根本没传播出来，`terminate` 直接被调用，进程退出了。这是并行算法和顺序算法一个很不直观的差异：**`par` 下函数对象必须实质上不抛异常**，要么保证逻辑上不抛，要么标 `noexcept` 并在内部消化掉。需要错误处理路径的算法，要么留在 `seq`，要么用 `std::expected`/返回值这种不依赖异常的通道。

为什么会这样？想象异常在八个线程里同时抛，谁来汇总？哪个异常该传播出去？标准干脆规定不传播、直接终止，把复杂性一刀切掉。这也是为什么并行算法的函数对象应当尽量简单、`noexcept`。

## 一个绕不开的真坑：libstdc++ 的并行后端是 TBB，得手动链接

前面所有例子编译时都带了 `-ltbb`。这不是可有可无的——它是 libstdc++ 并行算法能不能链接成功的关键，也是新手最容易撞上的墙。

libstdc++ 的并行算法（PSTL）底层依赖 Intel TBB 做线程调度。所以只要你代码里**用到了** `std::execution::par`（哪怕只是 `reduce(par, ...)`），编译能过，但链接会失败，报一长串 `undefined reference to tbb::...`。把开头那个最小 par 例子不带 `-ltbb` 编一下：

```text
/usr/bin/ld: ... undefined reference to `tbb::detail::r1::initialize(tbb::detail::d1::task_group_context&)'
... (几十行 TBB 符号未定义)
collect2: error: ld returned 1 exit status
```

想看 `par` 为什么依赖 TBB？只编译（不链接）看汇编——`par` 版（`sum_par`）是一串 `call __gnu_parallel`/`tbb` 运行时符号，`seq` 版（`sum_seq`）就是普通标量循环：

<OnlineCompilerDemo
  title="reduce seq vs par 的汇编：TBB 运行时调用"
  source-path="code/examples/vol3/46_parallel_asm.cpp"
  description="只编译看汇编（不运行，故无需 -ltbb）：seq 版是标量累加循环，par 版是一串 call __gnu_parallel/tbb——这就是「并行后端是 TBB」在汇编层的证据"
  allow-x86-asm
/>

解决方法就一行：编译命令加上 `-ltbb`（系统得装了 TBB，本机是 `libtbb.so.12`）。CMake 工程里对应的是 `find_package(TBB REQUIRED)` 然后 `target_link_libraries(... TBB::tbb)`。

::: warning 不带 -ltbb 就链接不过
只要用 `par`/`par_unseq`，libstdc++ 必须链接 TBB。`seq` 和 `unseq` 不走 TBB（顺序和纯向量化不需要线程池），单独用这两个不带 `-ltbb` 也能链接。但实战中策略常常换来换去，工程里直接固定链 `-ltbb` 最省心。这也是为什么本篇所有含 `par` 的例子编译命令都写 `g++ -std=c++20 -O2 xxx.cpp -ltbb`。
:::

补充一个对比：另一个主流标准库实现 libc++（Clang 那套）默认并行后端不一样，不依赖 TBB；而 MSVC 的并行算法是开箱即用的、不需要额外链接。所以「要不要链 TBB」是 libstdc++ 特有的问题，迁移代码时要注意。

## C++17 引入背景与 C++20 的 unseq

并行算法是 C++17 才进标准的（源自更早的 Parallelism TS）。在那之前，想并行排序只能自己手搓线程或者上第三方库（TBB、OpenMP）。C++17 一次性把 60 多个 `<algorithm>` 算法和 `<numeric>` 的归并/扫描算法都加了带执行策略的重载，外加 `reduce`、`transform_reduce`、`inclusive_scan` 等一批天生为并行设计的新算法（老的 `accumulate` 不要求结合律、没法安全并行，所以另起炉灶）。

C++20 又补了第四个策略 `unseq`——单线程向量化（纯 SIMD）。它的设计动机是：有些场景你不想开多线程（比如嵌入式单核、或者数据量小到开线程不划算），但仍然想让编译器把循环向量化、用上 SIMD 指令。`par_unseq` 也能向量化，但它同时会开线程；`unseq` 把「向量化」单独拆出来，给你一个「不要线程、只要 SIMD」的选项。

但 `unseq` 在 libstdc++ 上的实际效果常常让人失望。cppreference 自己的例子（g++ -std=c++23 -O3 -ltbb）跑 100 万元素的 sort，四种策略是：seq 165ms、unseq 163ms、par_unseq 30ms、par 27ms。看清楚没？**`unseq` 几乎没比 `seq` 快**——163 vs 165，基本在误差内。原因还是向量化对整数比较这种操作的收益有限，SIMD 通道喂不饱。`unseq` 真正能拉开差距的是那种高度规则、无分支、计算密集的逐元素操作（比如浮点数组的逐元素数学运算），实战中要按场景测了才知道。

所以对 `unseq` 的正确预期是：**它是一个「请求向量化」的提示，不保证提速**。和 `par` 一样，值不值要测，别迷信。

## 小结

并行算法这一块，最容易学的就是「怎么加策略参数」，最容易踩的是「以为加了 `par` 就一定快」。把几条关键结论收一下：

- 四种策略从保守到激进：`seq`（单线程顺序）、`par`（可多线程，同线程内不交错，能加锁）、`par_unseq`（可多线程 + 向量化，单线程内可交错，**不能加锁**）、`unseq`（C++20，单线程向量化，同样不能加锁）。
- `par_unseq` 和 `unseq` 因为允许单线程内调用交错，函数对象里禁止任何 vectorization-unsafe 操作：加锁、非 lock-free 的 atomic、甚至 `new`/`delete`。需要加锁的并行场景最多用 `par`。
- **并行何时真变快**：算法 compute-bound（如 sort）时加速显著（本机 5x+），memory-bound（如 reduce）时受内存带宽限制加速有限（本机 1.5x）；数据量小到固定开销占主导时，`par` 反而慢几十倍。
- **reduce 要求结合律，accumulate 不要求**。浮点加法不满足结合律，所以浮点 `reduce(par)` 的结果和顺序版不同、甚至每次跑都不同——对可复现性有要求的场景禁用。
- `par` 及以下策略里函数对象抛异常会直接 `std::terminate`，不会被外层 `try/catch` 接到——并行算法的函数对象应当实质 `noexcept`。
- **libstdc++ 的并行后端是 TBB**：只要用到 `par`/`par_unseq` 就得在编译命令加 `-ltbb`，否则链接失败；libc++ 和 MSVC 无此要求。
- `unseq` 是「请求向量化」的提示，对整数比较这类操作常常几乎没加速，别迷信。

下一篇我们换一个角度看标准库——`<chrono>` 和时间处理，那是另一套「看着简单、坑很多」的设施。

## 参考资源

- [cppreference: Execution policy tags](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag) —— `seq`/`par`/`par_unseq`/`unseq` 四个策略对象的定义
- [cppreference: Execution policy types](https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t) —— 各策略类型对元素访问函数调度方式的精确语义（indeterminately sequenced vs unsequenced、能否加锁、异常行为）
- [cppreference: std::reduce](https://en.cppreference.com/w/cpp/numeric/reduce) —— `reduce` 的结合律要求、与 `accumulate` 的关系
- [cppreference: Parallel algorithms](https://en.cppreference.com/w/cpp/algorithm) —— C++17 起所有带执行策略重载的算法总览
- [GCC libstdc++ manual: Parallel algorithms](https://gcc.gnu.org/onlinedocs/libstdc++/manual/parallel.html) —— libstdc++ 并行后端依赖 TBB 的说明
