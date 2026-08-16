---
title: "卷五 Project 参考实现"
description: "卷五综合项目（并行图像卷积工作台）的完整参考实现：分层任务逐步讲解，每步标注知识点链接，含确定性校验、缩放曲线、sanitizer 质量门、行交错与分离卷积的完整代码与真实运行输出（WSL Arch，g++ 16.1.1，C++20），如实报告「分离卷积反而更慢」及其内存流量分析。"
chapter: 5
order: 15
tags:
  - host
  - cpp-modern
  - advanced
  - 并发
  - 优化
  - 测试
difficulty: advanced
platform: host
reading_time_minutes: 12
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷五 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1，C++20，24 核）真实运行得到。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。性能数字因机器而异——重点是你的分析方法和结论方向，不是和本文件逐毫秒对齐。

## 核心任务（L2）：能跑起来的模糊器 {#pj-core}

**思路**：`Image` 用一维 `vector<float>` 存像素（行优先），边界 clamp 用一个 `std::clamp` 搞定；并行版按**输出行区间**分块，每线程写自己的行——互不重叠，天然无竞争。

**`Image` 与串行版**——类型 + 随机图 + 3×3 盒式模糊。→ 知识点：[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)「基本模式：派生线程，作用域退出时 join」一节（分区并行）；[CPU cache 与 OS 线程](../ch00-concurrency-fundamentals/03-cpu-cache-and-os-threads.md)（行优先布局的空间局部性）

```cpp
struct Image {
    int width{0};
    int height{0};
    std::vector<float> pixels;

    float at(int x, int y) const
    {
        return pixels[static_cast<size_t>(y) * width + x];
    }

    float& at(int x, int y)
    {
        return pixels[static_cast<size_t>(y) * width + x];
    }
};

Image make_random_image(int w, int h, uint32_t seed)
{
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& p : img.pixels) {
        p = dist(rng);
    }
    return img;
}

int clamp_coord(int v, int lo, int hi)
{
    return std::clamp(v, lo, hi);
}

Image blur_serial(const Image& src)
{
    Image dst = src;
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            float sum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int sx = clamp_coord(x + dx, 0, src.width - 1);
                    int sy = clamp_coord(y + dy, 0, src.height - 1);
                    sum += src.at(sx, sy);
                }
            }
            dst.at(x, y) = sum / 9.0f;
        }
    }
    return dst;
}
```

**并行分块版**——行区间 $[height\times w/workers, height\times (w+1)/workers)$ 各交给一个线程，`w` **值捕获**。→ 知识点：[线程参数与生命周期](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime.md)（值捕获循环变量——引用捕获会重演 5.C-2 的 TSan 报告）

```cpp
Image blur_parallel_chunk(const Image& src, int workers)
{
    Image dst = src;
    std::vector<std::thread> ts;
    for (int w = 0; w < workers; ++w) {
        ts.emplace_back([&src, &dst, w, workers]() {
            int y_begin = src.height * w / workers;
            int y_end = src.height * (w + 1) / workers;
            for (int y = y_begin; y < y_end; ++y) {
                for (int x = 0; x < src.width; ++x) {
                    float sum = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int sx = clamp_coord(x + dx, 0, src.width - 1);
                            int sy = clamp_coord(y + dy, 0, src.height - 1);
                            sum += src.at(sx, sy);
                        }
                    }
                    dst.at(x, y) = sum / 9.0f;
                }
            }
        });
    }
    for (auto& t : ts) {
        t.join();
    }
    return dst;
}
```

**验证输出**（4096×4096，64MB 图）：

```text
$ g++ -std=c++20 -O2 -pthread blur.cpp -o blur && ./blur
image 4096x4096 (64 MB)
max_diff(chunk8, serial)   = 0
max_diff(striped8, serial) = 0
max_diff(sep8, serial)     = 1.78814e-07
```

## 进阶任务（L3）：确定性校验与缩放曲线 {#pj-adv}

**思路**：校验就是 `max_diff == 0`；缩放曲线按 ch08-02 的方法论（预热 + 中位数）测；并行版不需要同步的根子是「分区并行」。

1. `max_diff` 就是逐像素最大绝对差。注意 `sep8` 与串行版差 `1.78814e-07`——分离卷积把加法顺序换了，浮点舍入不满足结合律，这是**预期内的舍入量级**（float 精度 ~1e-7），验收口径用 `max_diff < 1e-6` 而不是 `== 0`。→ 知识点：[atomic 操作](../ch03-atomic-memory-model/01-atomic-operations.md)「浮点原子操作的注意事项」（浮点不结合，结果依赖顺序——普通浮点计算同理）
2. 缩放曲线（预热 1 轮 + 5 轮取中位数）：serial 99ms；chunk 1/2/4/8 = 104/60/40/34ms，加速比 0.95/1.65/2.48/2.91。**如实说明**：24 核机器上 8 线程只有 2.9 倍——本负载是**内存带宽受限**（64MB 图读写一遍 ≈ 128MB+ 流量），不是计算受限，线程再多也抢不到更多带宽；这与 Amdahl 无关，是另一种天花板。→ 知识点：[并发性能测试与基准](../ch08-debug-testing-perf/02-concurrency-benchmarks.md)「并发 benchmark 设计陷阱」（预热、多轮、中位数）；[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)（吞吐与延迟的权衡）
3. 不需要同步的依据：每个输出行只被一个线程写、读的输入是共享的**只读**数据——「多个线程读同一内存没问题，只要没写」，这正是 ch01 分区并行的原则。→ 知识点：[std::thread 基础](../ch01-thread-lifecycle-raii/01-std-thread.md)「基本模式」一节（输出分片互不重叠）

**验证输出**：

```text
serial:            99 ms
chunk(1):      104 ms  speedup=0.951923
chunk(2):      60 ms   speedup=1.65
chunk(4):      40 ms   speedup=2.475
chunk(8):      34 ms   speedup=2.91176
```

## 再进阶任务（L4）：把门装上 {#pj-gates}

**思路**：sanitizer 构建用小图快跑；固定种子保证「同一输入 → 同一输出」的可复现回归。

1. 质量门：512×512、4 线程的 chunk 版分别用 TSan 与 ASan+UBSan 构建，均 `max_diff = 0`、零报告。→ 知识点：[并发程序调试技巧](../ch08-debug-testing-perf/01-debugging-concurrency.md)「ThreadSanitizer」一节（TSan 是编译期选项，运行不加旗标）
2. 换种子（seed=7）再校验仍为 0：固定种子让 bug 报告「哪张图、哪个像素」可复述可复现，并发回归才有意义。→ 知识点：同上「系统性诊断流程」（第一步：稳定复现）
3. 方法论：并发基准数字随调度、缓存温度剧烈波动，单次运行是噪声；预热 + 多轮取中位数、不取平均。→ 知识点：[并发性能测试与基准](../ch08-debug-testing-perf/02-concurrency-benchmarks.md)「预热：冷启动与稳态」一节

**验证输出**：

```text
$ g++ -std=c++20 -O1 -g -fsanitize=thread -pthread blur_small.cpp -o blur_small_tsan
$ ./blur_small_tsan
max_diff = 0

$ g++ -std=c++20 -O1 -g -fsanitize=address,undefined -pthread blur_small.cpp -o blur_small_asan
$ ./blur_small_asan
max_diff = 0
```

## 终极挑战（L5）：性能攻坚 {#pj-l5}

**思路**：三种策略都用「相同输出、不同调度」来对答案；分离卷积的失败要拿**内存流量**算账，而不是空喊带宽瓶颈。

**策略一：行交错（striped）**——worker w 处理 `y % workers == w` 的行。→ 知识点：[SPSC 与 MPMC 队列](../ch04-concurrent-data-structures/04-lock-free-queues.md)「生产者-消费者批量处理」一节（分派策略影响局部性的同类权衡）

```cpp
Image blur_parallel_striped(const Image& src, int workers)
{
    Image dst = src;
    std::vector<std::thread> ts;
    for (int w = 0; w < workers; ++w) {
        ts.emplace_back([&src, &dst, w, workers]() {
            for (int y = w; y < src.height; y += workers) {
                for (int x = 0; x < src.width; ++x) {
                    float sum = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int sx = clamp_coord(x + dx, 0, src.width - 1);
                            int sy = clamp_coord(y + dy, 0, src.height - 1);
                            sum += src.at(sx, sy);
                        }
                    }
                    dst.at(x, y) = sum / 9.0f;
                }
            }
        });
    }
    for (auto& t : ts) {
        t.join();
    }
    return dst;
}
```

**策略二：分离卷积**——两遍各并行（先水平 3 点均值、再垂直 3 点均值）。→ 知识点：[性能思维:efficiency 与 performance 不是一回事](../../vol6-performance/ch00-performance-mindset/01-efficiency-vs-performance.md)「efficiency 与 performance,到底差在哪」一节（「算法复杂度不等于墙钟性能」为转述该篇观点，与 5.C-2 的锁粒度教训同源）

```cpp
Image blur_separable_parallel(const Image& src, int workers)
{
    Image tmp = src;
    {
        std::vector<std::thread> ts;
        for (int w = 0; w < workers; ++w) {
            ts.emplace_back([&src, &tmp, w, workers]() {
                for (int y = w; y < src.height; y += workers) {
                    for (int x = 0; x < src.width; ++x) {
                        int x0 = clamp_coord(x - 1, 0, src.width - 1);
                        int x1 = clamp_coord(x + 1, 0, src.width - 1);
                        tmp.at(x, y) =
                            (src.at(x0, y) + src.at(x, y) + src.at(x1, y)) / 3.0f;
                    }
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }
    }
    Image dst = tmp;
    {
        std::vector<std::thread> ts;
        for (int w = 0; w < workers; ++w) {
            ts.emplace_back([&tmp, &dst, w, workers]() {
                for (int y = w; y < tmp.height; y += workers) {
                    for (int x = 0; x < tmp.width; ++x) {
                        int y0 = clamp_coord(y - 1, 0, tmp.height - 1);
                        int y1 = clamp_coord(y + 1, 0, tmp.height - 1);
                        dst.at(x, y) =
                            (tmp.at(x, y0) + tmp.at(x, y) + tmp.at(x, y1)) / 3.0f;
                    }
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }
    }
    return dst;
}
```

**数值等价性**：边界 clamp 下分离卷积与 2D 盒式**严格等价**（手算角落像素即可验证：$(2(2p_{00}+p_{10})/3 + (2p_{01}+p_{11})/3)/3 = (4p_{00}+2p_{10}+2p_{01}+p_{11})/9$），实测最大差 1.79e-07 仅为浮点舍入——先证明等价、再上并行，是数值程序并行的基本纪律。→ 知识点：[为什么需要并发](../ch00-concurrency-fundamentals/01-why-concurrency.md)（先正确性、再性能）

**性能报告（200 字内，本机真实）**：

```text
serial:            99 ms
chunk(8):          34 ms   speedup=2.91
striped(8):        35 ms   speedup=2.83
separable(8):      46 ms   speedup=2.15
```

报告：24 核 WSL2、g++ 16 `-O2`、4096² 图、5 轮中位数。chunk 与 striped 打平（35 vs 34ms，噪声内）——行交错的分派优势被本负载的带宽瓶颈盖住了。**separable 反而慢**：单遍盒式 = 读 64MB + 写 64MB；分离两遍 = (读 64 + 写 64) × 2 = 256MB 流量，虽把每像素加法从 9 降到 6，但本负载是**带宽受限**而非计算受限，流量翻倍直接抵消了算力节省——算法复杂度下降 ≠ 更快，先量再优化。

**验证输出**：

```text
$ ./blur
image 4096x4096 (64 MB)
max_diff(chunk8, serial)   = 0
max_diff(striped8, serial) = 0
max_diff(sep8, serial)     = 1.78814e-07
serial:            99 ms
chunk(1):      104 ms  speedup=0.951923
chunk(2):      60 ms   speedup=1.65
chunk(4):      40 ms   speedup=2.475
chunk(8):      34 ms   speedup=2.91176
striped(8):        35 ms  speedup=2.82857
separable(8):      46 ms  speedup=2.15217
```
