---
title: "卷六 Project 参考实现"
description: "卷六综合项目（点积优化工作台 dotbench）的完整参考实现：分文件逐段讲解——自包含基准框架（中位数+cv+防 DCE）、多累加器的诚实结果（本机仅 1.2× 及归因）、四道质量门（警告/sanitizer/双编译器/方法论）、AVX2+FMA 与带宽墙缩放曲线，全部输出真实运行得到。"
chapter: 6
order: 6
tags:
  - host
  - advanced
  - cpp-modern
  - 优化
  - 测试
  - 工程实践
difficulty: advanced
platform: host
reading_time_minutes: 18
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷六 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8，Intel Core i7-14650HX，24 线程）真实运行得到。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。本参考实现的几个诚实结果先摆在明面上：dot4 只有 ~1.2×、缩放曲线上 AVX2 的加速比从 5.7× 衰减到 1.8×、clang 标量版比 gcc 慢——它们不是实现写得差，是这台机器的真实读数，解释都在正文里。

## 核心任务（L2）：能跑起来的基准框架 {#pj-core}

**思路**：先立正确性参照（`std::inner_product`），再搭测量框架——`do_not_optimize` 用空内联汇编钉结果、`bench` 多轮取中位数 + cv、数据运行期生成、环境快照落进输出。整个文件按层生长，最终形态（含 L5）长这样。

**`dotbench.cpp` 头部——正确性参照与核心基线**。→ 知识点：[RVO、NRVO 与 move 的真实成本](../ch06-cpp-abstraction-cost/06-05-rvo-move.md)（`inner_product` 的按值返回在 C++17 下零成本）、[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)（运行期数据的理由）

```cpp
// dotbench.cpp —— 点积优化工作台(核心 → 进阶 → 质量门 → 终极的最终形态)
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <fstream>
#include <thread>
#include <immintrin.h>

using Clock = std::chrono::steady_clock;

// ---------- 正确性参照 ----------
static float ref_dot(const float* a, const float* b, long n)
{
    return std::inner_product(a, a + n, b, 0.0f);
}

// ---------- 核心(L2):标量基线 ----------
static float dot_scalar(const float* a, const float* b, long n)
{
    float acc = 0.0f;
    for (long i = 0; i < n; ++i) acc += a[i] * b[i];
    return acc;
}
```

**`do_not_optimize` 与 `bench`——测量框架本体**。`do_not_optimize` 就是「结果被消费」的最小实现：编译器不能证明 `x` 没人用，整段计算必须真实发生；`bench` 把「多轮、中位数、cv」三件事包起来——对应 ch01-02 的 `Repetitions + ReportAggregatesOnly` 手工版。→ 知识点：[怎么写一个可信的 microbenchmark](../ch01-benchmark-methodology/02-credible-microbenchmark.md)「DoNotOptimize」与「重复几轮,报中位数」两节

```cpp
// ---------- 测量框架(核心):中位数 + cv + 防优化 ----------
static void do_not_optimize(float x)
{
    asm volatile("" : : "g"(x) : "memory");
}

struct BenchResult { double median_ms; double cv_pct; };

template <class F>
static BenchResult bench(F f, int rounds)
{
    std::vector<double> times;
    times.reserve((size_t)rounds);
    for (int r = 0; r < rounds; ++r) {
        auto t0 = Clock::now();
        float v = f();
        auto t1 = Clock::now();
        do_not_optimize(v);
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(times.begin(), times.end());
    double median = times[times.size() / 2];
    double mean = 0;
    for (double t : times) mean += t;
    mean /= (double)times.size();
    double sd = 0;
    for (double t : times) sd += (t - mean) * (t - mean);
    sd = std::sqrt(sd / (double)times.size());
    return {median, sd / mean * 100.0};
}
```

**`env_snapshot` 与 `main` 的正确性门**——环境快照让「这组数字是谁测的」可追溯；正确性门在每次基准前先验三版与参照的最大差，把铁律一焊死在流程里。数据用固定种子随机生成：可复现（换种子对比时除种子外一切相同）、且运行期才存在（编译器算不穿）。→ 知识点：[统计与报告:把分布变成结论](../ch01-benchmark-methodology/04-statistics-and-reporting.md)「必报环境快照」、[从「先正确」到「再快」](../ch00-performance-mindset/02-from-correctness-to-performance.md)（正确性地基）

```cpp
static std::string env_snapshot()
{
    std::string model = "unknown";
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("model name", 0) == 0) {
            size_t p = line.find(':');
            if (p != std::string::npos) model = line.substr(p + 2);
            break;
        }
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s | hardware_concurrency=%u",
                  model.c_str(), std::thread::hardware_concurrency());
    return buf;
}

int main(int argc, char** argv)
{
    long n = (argc > 1) ? std::atol(argv[1]) : 1'000'000;
    int rounds = (argc > 2) ? std::atoi(argv[2]) : 9;

    // 运行期数据(防常量折叠): 固定种子保证可复现
    std::vector<float> a((size_t)n), b((size_t)n);
    std::mt19937 rng(2026);
    for (long i = 0; i < n; ++i) {
        a[i] = (float)(rng() % 1000) / 1000.0f;
        b[i] = (float)(rng() % 1000) / 1000.0f;
    }

    // 正确性门: 三版与参照的最大差
    float ref = ref_dot(a.data(), b.data(), n);
    float r1 = dot_scalar(a.data(), b.data(), n);
    std::printf("正确性: ref=%.6f | scalar 差=%.3e\n",
                (double)ref, (double)std::fabs(r1 - ref));
    ...
}
```

**验证输出**（L1 热身 + 核心，N=1M）：

```text
$ g++ -O2 -mavx2 -mfma -std=c++17 -Wall -Wextra dotbench.cpp -o dotbench
$ ./dotbench 1048576 9
正确性: ref=261867.296875 | scalar 差=0.000e+00
N=1048576 rounds=9
dot_scalar: 中位数 0.473 ms, cv=18.6%, 17.7 GB/s
...
环境快照: Intel(R) Core(TM) i7-14650HX | hardware_concurrency=24
```

`scalar 差 = 0` 不是巧合：两者都是同一顺序的单链求和（`acc += a[i]*b[i]` 从左到右），浮点运算逐位相同。17.7 GB/s 与 6.3-A 的带宽墙（14.7~15.7 GB/s）同量级——标量点积贴着带宽斜线跑，这是后面 L5 一切故事的起点。

## 进阶任务（L3）：多累加器——以及一个诚实的结果 {#pj-adv}

**思路**：dot4 把一条 RAW 长链拆成四条独立链。先加代码，再读两行数据——一行是对的（正确性差不是 0），一行是「诚实」的（加速比只有 1.2×）。

**`dot4`——四条独立链**。→ 知识点：[循环与计算优化:code motion、展开与多累加器](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)「多累加器」一节

```cpp
// ---------- 进阶(L3):4 累加器打破依赖链 ----------
static float dot4(const float* a, const float* b, long n)
{
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    long i = 0;
    for (; i + 3 < n; i += 4) {
        a0 += a[i] * b[i];
        a1 += a[i + 1] * b[i + 1];
        a2 += a[i + 2] * b[i + 2];
        a3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; ++i) a0 += a[i] * b[i];
    return (a0 + a1) + (a2 + a3);
}
```

**①正确性差**：`dot4` 与参照差 **1.692e+03**（N=4M，相对 ~0.17%）——不再是 0，因为四条链的求和顺序和单链不同，浮点加法不满足结合律，尾数舍入路径不一样。这是「拆链的代价」，程序员（不是编译器）承担了它。→ 知识点：[SIMD 与向量化](../ch04-tuning-by-bottleneck/04-05-simd.md)（FP 归约的结合律问题）

**②实测**：N=1M：dot_scalar 0.473 ms vs dot4 0.388 ms = **1.22×**；N=4M：2.060 vs 1.785 = **1.15×**。cv 都在 12~19% 之间（WSL2 噪声，比「可信」阈值 5% 高——如实报，不粉饰）。→ 知识点：[统计与报告](../ch01-benchmark-methodology/04-statistics-and-reporting.md)「cv 大的 mean 没有意义」

**③诚实归因——为什么不是教材的 2.9×**。两个机制叠加：a) **GCC 在 -O2 已经对 dot_scalar 做了部分 SLP 向量化与轻量展开**（`-O2` 开 `-ftree-slp-vectorize`），标量版的链已经不是教科书里最惨的那条单链——手写 dot4 抢到手的 ILP 空间变小了；b) **这个负载在 N≥1M 时主要带宽受限**：dot 读两个数组，N=1M 就有 8 MB 流量、N=4M 有 32 MB 超出 30 MB L3，17~19 GB/s 的吞吐贴着带宽墙——链再短，数据跟不上也没用。所以「多累加器 2~4×」是链受限场景的红利（教材 ch02-03 的 2.9× 是在故意关掉向量化、小工作集的条件下测的），搬到「带宽受限 + 编译器已部分代劳」的真实负载上，就只剩 1.2×。**改之前先确认真瓶颈**——本层用 1.2× 给你上了这一课。→ 知识点：[流水线、ILP 与分支预测](../ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md)（教材 2.9× 的测量条件）、[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)（带宽受限则 ILP 是空转）

**验证输出**：

```text
$ ./dotbench 1048576 9
正确性: ref=261867.296875 | scalar 差=0.000e+00 | dot4 差=4.256e+01 | avx2 差=4.470e+01
N=1048576 rounds=9
dot_scalar: 中位数 0.473 ms, cv=18.6%, 17.7 GB/s
dot4      : 中位数 0.388 ms, cv=17.6%, 21.6 GB/s (1.22x)
$ ./dotbench 4194304 9
正确性: ref=1045158.125000 | scalar 差=0.000e+00 | dot4 差=1.834e+03 | avx2 差=1.992e+03
N=4194304 rounds=9
dot_scalar: 中位数 2.060 ms, cv=7.4%, 16.3 GB/s
dot4      : 中位数 1.785 ms, cv=19.4%, 18.8 GB/s (1.15x)
```

## 再进阶任务（L4）：把门装上 {#pj-gates}

**思路**：四道门各自独立、全部可自动化——它们把「先正确」从口号变成 CI 里的硬检查。

**①警告门**：`-Wall -Wextra` 下整个文件（含 AVX2 intrinsics）零警告——上面的每次编译输出里没有任何 warning 行，即达标。**②sanitizer 门**：`-O1 -g -fsanitize=address,undefined` 构建小规模跑一遍，输出干净、无任何 `runtime error`/`ERROR: AddressSanitizer` 行。→ 知识点：[Sanitizer 工具链全景](../ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md)（ASan+UBSan 的组合与开销）

**验证输出**：

```text
$ g++ -O1 -g -fsanitize=address,undefined -mavx2 -mfma -std=c++17 -Wall -Wextra \
      dotbench.cpp -o dotbench_san && ./dotbench_san 131072 5
正确性: ref=32631.873047 | scalar 差=0.000e+00 | dot4 差=3.027e-01 | avx2 差=1.992e-01
N=131072 rounds=5
dot_scalar: 中位数 0.220 ms, cv=1.2%, 4.8 GB/s
dot4      : 中位数 0.222 ms, cv=7.4%, 4.7 GB/s (0.99x)
dot_avx2  : 中位数 0.0427 ms, cv=0.4%, 24.6 GB/s (5.1x vs scalar)
环境快照: Intel(R) Core(TM) i7-14650HX | hardware_concurrency=24
```

小规模（1 MB 工作集）下 AVX2 的 5.1× 与 N=4M 的 2.3× 对照——工作集在 cache 内时 SIMD 的真面目，出了 L3 就被带宽墙稀释。这就是为什么 L5 要求画整条缩放曲线。

**③双编译器门**：clang++ 22.1.8 同旗标同数据，如实对照——g++ 标量 1.843 ms vs clang 3.479 ms（clang 标量更慢，它没做同样的部分向量化），dot4 3.06× vs g++ 1.13×（clang 的标量版链更长、dot4 抢到的空间更大），avx2 4.5× vs g++ 2.2×。**哪个「更好」随代码随版本变**——教材 ch07-03 的结论：「别为了听说 X 编译器更快换工具链」，交叉编译的价值是**发现你的基准对编译器敏感**，不是选边站。→ 知识点：[链接性能、多编译器对比与编译期元编程](../ch07-compiler-and-size/07-03-linking-and-compilers.md)（三大编译器差距小节）

**验证输出**：

```text
$ clang++ -O2 -mavx2 -mfma -std=c++17 -Wall -Wextra dotbench.cpp -o dotbench_clang
$ ./dotbench_clang 4000000 9
正确性: ref=996827.750000 | scalar 差=0.000e+00 | dot4 差=1.692e+03 | avx2 差=1.834e+03
N=4000000 rounds=9
dot_scalar: 中位数 3.479 ms, cv=4.0%
dot4      : 中位数 1.136 ms, cv=5.2% (3.06x)
dot_avx2  : 中位数 0.7802 ms, cv=19.9% (4.5x vs scalar)
```

**④方法论报告**：规程 = 每次基准前正确性门先过 → 每档 rounds=9 轮、排序取**中位数**（性能数据右偏，均值被长尾拉高）→ 报 cv（本机 WSL2 下 5~24% 常见，超过 ~5% 就只谈方向不谈小数）→ 输出环境快照 → 关键对照用同一二进制同一数据。为什么不能用单次数字：性能是分布不是数（ch01-01），单次运行撞上调度、调频、缓存冷热就全变了——你报的必须是一个分布的代表值（中位数）加它的离散度（cv），外加它来自哪台机器（快照）。→ 知识点：[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)（性能是分布）、[统计与报告](../ch01-benchmark-methodology/04-statistics-and-reporting.md)（必报/禁报清单）

## 终极挑战（L5）：手写 AVX2 与带宽墙 {#pj-l5}

**思路**：intrinsics 四件套——`__m256` 宽向量、`_mm256_loadu_ps` 非对齐加载、`_mm256_fmadd_ps` 乘加合一、4 条累加器拆依赖链；最后 horizontal 合并。缩放曲线是这道题的主角：它把「带宽墙」画成了看得见的衰减。

**`dot_avx2`**——每轮 32 个 float，4 条 FMA 链并行。→ 知识点：[SIMD 与向量化](../ch04-tuning-by-bottleneck/04-05-simd.md)「手写 intrinsics」一节

```cpp
// ---------- 终极(L5):AVX2 + FMA,4 条向量累加器 ----------
static float dot_avx2(const float* a, const float* b, long n)
{
    __m256 v0 = _mm256_setzero_ps(), v1 = _mm256_setzero_ps(),
           v2 = _mm256_setzero_ps(), v3 = _mm256_setzero_ps();
    long i = 0;
    for (; i + 31 < n; i += 32) {
        v0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i),      _mm256_loadu_ps(b + i),      v0);
        v1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8),  _mm256_loadu_ps(b + i + 8),  v1);
        v2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 16), _mm256_loadu_ps(b + i + 16), v2);
        v3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 24), _mm256_loadu_ps(b + i + 24), v3);
    }
    __m256 s = _mm256_add_ps(_mm256_add_ps(v0, v1), _mm256_add_ps(v2, v3));
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, s);
    float acc = 0;
    for (int k = 0; k < 8; ++k) acc += tmp[k];
    for (; i < n; ++i) acc += a[i] * b[i];
    return acc;
}
```

**①为什么是 4 条**：1 条 `__m256` 累加器自身就是一条链——每条 FMA 的延迟（~4 周期）串成依赖链，8 lane 的宽度救不了它；4 条互不依赖的向量链才能同时填满 FMA 端口。这跟 dot4 是同一句话在 256 位宽度上的重演。→ 知识点：[循环与计算优化](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)（多累加器）、[流水线、ILP 与分支预测](../ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md)

**②缩放曲线（g++ -O2，每档 9 轮取中位数）**：

| N | dot_scalar | dot4 | dot_avx2 | avx2/scalar |
|---|---:|---:|---:|---:|
| 65,536（512 KB） | 0.028 ms, 18.6 GB/s | 0.023 ms（1.23×） | 0.0050 ms, **105.3 GB/s** | **5.7×** |
| 262,144（2 MB） | 0.107 ms, 19.7 GB/s | 0.091 ms（1.17×） | 0.0246 ms, 85.4 GB/s | 4.3× |
| 1,048,576（8 MB） | 0.473 ms, 17.7 GB/s | 0.388 ms（1.22×） | 0.1621 ms, 51.8 GB/s | 2.9× |
| 4,194,304（32 MB） | 2.060 ms, 16.3 GB/s | 1.785 ms（1.15×） | 0.8783 ms, 38.2 GB/s | 2.3× |
| 16,777,216（128 MB） | 8.288 ms, 16.2 GB/s | 7.097 ms（1.17×） | 4.5255 ms, 29.7 GB/s | **1.8×** |

标量吞吐全程钉在 16~20 GB/s——**带宽墙**；AVX2 的加速比从 512 KB 的 5.7× 一路衰减到 128 MB 的 1.8×。→ 知识点：[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)（带宽受限的判读）、[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)（工作集跨 L3）

**③Roofline 解释**：点积 AI = 2 FLOP / 8 B = 0.25 FLOP/byte，远低于脊点（~20 量级）——程序点钉在**带宽斜线**上，屋顶高度 = 带宽 × AI。SIMD 让你更快地**发出** 8 路 load，但字节数不变，DRAM/内存子系统每秒能供的字节数不变：小 N 时数据在 L2/L3，屋顶高，SIMD 的 5.7× 拿满；N 一超 L3，屋顶被 DRAM 带宽锁死，AVX2 的吞吐也被压到 29.7 GB/s，只剩 1.8× 的余量。**「先减访存、再提 ILP、最后 SIMD」的顺序在表里一目了然：减访存（换布局）在这条曲线上永远比加 SIMD 更先值得做。**→ 知识点：[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)「斜线和水平线」、[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)（减访存是最大杠杆）

**④fast-math 对照**：`-O3 -mavx2 -mfma -ffast-math` 下 **dot_scalar = 0.1466 ms，比手写 dot_avx2 的 0.1579 ms 还略快（0.93×）**，且两者结果**逐位一致**（249757.75）——`-ffast-math` 告诉编译器「浮点可以重排」，它就把标量版自己拆成了 4 条向量 FMA 链（和手写版同构），手写 intrinsics 的存在意义被抹平。**如实报告**：在这个负载上，编译器 + 一个旗标就追平了手写 SIMD——这就是教材 ch04-05「自动向量化能做到就别手写」的本机实锤；手写 intrinsics 的舞台在「自动向量化做不到」的访问模式上。→ 知识点：[SIMD 与向量化](../ch04-tuning-by-bottleneck/04-05-simd.md)（`-ffast-math` 让编译器敢拆通道、手写的边界）

**验证输出**（fast-math 对照）：

```text
$ g++ -O3 -mavx2 -mfma -ffast-math -std=c++17 -Wall -Wextra dotbench_ffm.cpp -o dotbench_ffm
$ ./dotbench_ffm 1000000
N=1000000  dot_scalar(-ffast-math 自动优化): 0.1466 ms | dot_avx2: 0.1579 ms | 比值 0.93x
结果: scalar=249757.750000 avx2=249757.750000
```

到这里，`dotbench` 的四层全部盖完：一把校准过的尺子（中位数+cv+正确性门+快照），一次只有 1.2× 的诚实优化（多累加器撞上带宽墙），四道把「先正确」焊进流程的门，和一条把「带宽墙」画成曲线的缩放实验。这个项目没有炫技——它每一步都在验证本卷的那句总命题：**别只看 big-O，要看数据在硬件上怎么流；先正确，再测量，按瓶颈优化。**
