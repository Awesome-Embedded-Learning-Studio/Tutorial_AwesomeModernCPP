---
title: "卷六课后练习参考答案（Homework）"
description: "卷六课后练习的逐题详细解答：每道题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出（WSL Arch g++ 16.1.1 / clang++ 22.1.8 实跑，i7-14650HX）。计时题注明「秒数会抖、量级趋势稳定」，与教材表述不一致之处（mutex 与 atomic 的顺序翻转、__restrict 干净对照无收益等）如实报告并改写结论。"
chapter: 6
order: 2
tags:
  - host
  - intermediate
  - cpp-modern
  - 优化
  - 测试
  - 工程实践
difficulty: intermediate
platform: host
reading_time_minutes: 50
prerequisites: []
related: []
cpp_standard: [17, 20]
---

# 卷六课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8，Intel Core i7-14650HX，24 线程）下真实运行得到。**本卷是性能卷：所有计时数字都会抖，秒数会抖、量级趋势稳定。** 教材的数字大部分在 AMD Ryzen 7 5800H 上测得，你手上的机器不同，绝对数对不上正常，对趋势就对答案。凡实测结果与教材表述不一致的地方，解答里都如实标出并改写结论——这正是本卷「先测量再优化」的活教材。

## 6.0-A {#hw-6-0-a}

**难度 L1** · 题面见 [homework](01-homework.md#hw-6-0-a)

**思路**：Amdahl 定律只要求代公式；判断题考的是「复杂度不等于硬件表现」这一全卷总命题；sizeof 探针让你亲手摸到「连续内存 vs 节点分散」的物质基础。

1. 手算：$S(N) = \frac{1}{(1-p) + p/N}$，p=0.75：$S(2) = \frac{1}{0.25+0.375} = 1.6$，$S(8) = \frac{1}{0.25+0.09375} \approx 2.91$，$S(\infty) = \frac{1}{0.25} = 4$。p 提到 0.90 后 $S(\infty) = \frac{1}{0.1} = 10$——串行部分从 25% 压到 10%，上限从 4× 变 10×，这就是「打占比大的串行部分」的数学根据。→ 知识点：[性能思维:efficiency 与 performance 不是一回事](../ch00-performance-mindset/01-efficiency-vs-performance.md)「Amdahl:优化的天花板」一节
2. 判断题：a) 错——big-O 把所有硬件效应塞进常数 C，同 O(n) 的顺序遍历和随机访问可以差几十倍；b) 对——铁律二「先测量，再优化」，直觉在微架构层面经常是错的；c) 对——这正是 Amdahl 的推论，也是 profile 驱动优化的理论根据。→ 知识点：同上（两条铁律与 Amdahl 推论）
3. 探针实测：`vector` 24 字节 = 三指针，`set` 48 字节 = 树节点管理结构 + 每个节点还要单独 `new`。连续内存让二分查找跳到的点互相搭缓存行顺风车，`set` 的节点散落堆里、每次下跳都是不可预测的指针追逐——`sizeof` 解释了「为什么不同」，但「具体差几倍」还得靠 ch01 的方法实测。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)（缓存行与连续布局）、[性能思维](../ch00-performance-mindset/01-efficiency-vs-performance.md)「代码示例」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra sizeof_probe.cpp -o sizeof_probe && ./sizeof_probe
sizeof(std::vector<int>) = 24
sizeof(std::set<int>)    = 48
sizeof(int)              = 4
$ getconf LEVEL1_DCACHE_LINESIZE
64
$ nproc
24
$ grep -m1 "model name" /proc/cpuinfo
model name  : Intel(R) Core(TM) i7-14650HX
```

本机缓存行 64 字节，一条行装 16 个 `int`——连续数组遍历时一次 miss 搭车 16 个元素，`set` 节点一次 miss 只服务 1 个 key，这就是同 $O(\log n)$ 差几倍的物质基础。

## 6.0-B {#hw-6-0-b}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-0-b)

**思路**：`(x-1) < x` 是教材 `(x+1) > x` 的镜像变式。关键不是记住一个例子，而是看清「UB 假设在哪个阶段把减法删掉了」——这决定了 sanitizer 能不能抓到它。

1. `-O2` 下 `always_less` 被折叠成 `movl $1, %eax; ret`——函数体就两条指令，什么减法、什么比较都没有。喂 INT_MIN 也理直气壮返回 1。`-fwrapv` 把有符号溢出定义为回绕，减法被保留，INT_MIN-1 回绕成 INT_MAX，`INT_MAX < INT_MIN` 为假，返回 0。→ 知识点：[从「先正确」到「再快」](../ch00-performance-mindset/02-from-correctness-to-performance.md)「UB 是怎么把性能数字变成谎话的」一节
2. UBSan 在 -O0 和 -O2 下**都一声不吭**（都打印 f=1）。原因：GCC 的中间端在很早就识别出 `(x-1)<x` 这个惯用法并把它折叠掉——**这个折叠发生在 UBSan 插桩之前**，插桩时减法已经不存在了，UBSan 想检查都没有对象。教材 ch00-02 提醒过「当代 GCC 的中间端即便 -O0 也会识别这个惯用法」，本机实测坐实。→ 知识点：[Sanitizer 工具链全景](../ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md)「UBSan」一节、[从「先正确」到「再快」](../ch00-performance-mindset/02-from-correctness-to-performance.md)（那个 `-fwrapv` 对照的坑）
3. 拆成 `int y = x - 1; return y < x;` 之后，惯用法模式被打破、折叠不再发生，UBSan 在 -O0 和 -O2 下都抓到：`ub_less2.cpp:8:9: runtime error: signed integer overflow: -2147483648 - 1 cannot be represented in type 'int'`，结果 f2=0。→ 知识点：同上（UBSan 精确到 文件:行:列）
4. 三份结果合起来：同一份源码、同一个输入 INT_MIN，`-O2` 给 1、`-fwrapv` 给 0、UBSan 版报溢出。**一个带 UB 的 benchmark，编译选项一换，测的对象就不是同一个东西了**——你对着它测出的「快 30%」，可能一半是「UB 假设让编译器删掉了一半代码」省出来的。所以性能数字的地基是 sanitizer 跑干净，不是「看起来能跑」。→ 知识点：[从「先正确」到「再快」](../ch00-performance-mindset/02-from-correctness-to-performance.md)（三类假数字与「先正确再快」）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra ub_less.cpp -o ub_o2 && ./ub_o2 -2147483648
f(-2147483648) = 1          ← 折叠成 return true,什么减法都没有
$ g++ -O2 -fwrapv -std=c++17 ub_less.cpp -o ub_wrap && ./ub_wrap -2147483648
f(-2147483648) = 0          ← 老实回绕: INT_MIN-1 = INT_MAX, INT_MAX < INT_MIN 为假
$ g++ -O2 -fsanitize=undefined -std=c++17 ub_less.cpp -o ub_ubsan && ./ub_ubsan -2147483648
f(-2147483648) = 1          ← UBSan 没报:折叠先于插桩,减法已不存在
$ g++ -O0 -fsanitize=undefined -std=c++17 ub_less.cpp -o ub_ubsan_o0 && ./ub_ubsan_o0 -2147483648
f(-2147483648) = 1          ← -O0 也没报,实锤「-O0 也折叠」
$ g++ -O0 -fsanitize=undefined -std=c++17 ub_less2.cpp -o ub_ubsan2 && ./ub_ubsan2 -2147483648
ub_less2.cpp:8:9: runtime error: signed integer overflow: -2147483648 - 1 cannot be represented in type 'int'
f2(-2147483648) = 0         ← 拆成两行后,折叠被打破,UBSan 抓到
$ g++ -O2 -S -std=c++17 ub_less.cpp -o ub_o2.s
$ sed -n '/always_less/,/LFE/p' ub_o2.s
_Z11always_lessi:
    .cfi_startproc
    movl    $1, %eax
    ret                      ← 就两条指令
```

## 6.1-A {#hw-6-1-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-1-a)

**思路**：三骗的辨识靠场景特征——「结果没人消费」对应 DCE、「空闲机器独占 cache」对应假热、「Turbo/调频」对应噪声。实验部分亲手把 DCE 变成 0.000 ms。

1. 三骗：①编译器把你的 benchmark 优化成空；②缓存总是热的、真实负载不是；③系统噪声（DVFS/Turbo、文件缓存、布局偏置）淹没信号。场景对应：空闲机器独占 cache → ②；结果没人消费 → ①；Turbo 进出 → ③。→ 知识点：[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)（三集结构）
2. 实测：`-O0` 下两个循环都 ~169 ms（编译和循环都在）；`-O2` 下 `dead_loop` 变成 **0.000 ms**——汇编里它只剩 `xorl %eax, %eax; ret` 两条指令，整个循环连同 200 万次 `vector` 分配全被 DCE 删除。`live_loop` 保留（12.9 ms），汇编里能看到每轮 `call _Znwm`（`operator new`）。→ 知识点：[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)「第一集」一节
3. `volatile` 强制每次真读写内存，把结果「钉」住，所以救回了 `live_loop`。但它不是正解：volatile 同时禁掉了对那段数据的一切优化，你测的是「带 volatile 的代码」而不是你的代码。正规解法是 Google Benchmark 的 `DoNotOptimize` + `ClobberMemory`——语义更精准（只钉结果、只强制写落内存）。→ 知识点：[怎么写一个可信的 microbenchmark](../ch01-benchmark-methodology/02-credible-microbenchmark.md)「DoNotOptimize:它救你,但救不到底」一节

**验证输出**：

```text
$ g++ -O0 -std=c++17 dce.cpp -o dce_o0 && g++ -O2 -std=c++17 dce.cpp -o dce_o2
$ ./dce_o0 2000000
dead_loop: 168.416 ms (r=0)
live_loop: 169.226 ms (r=1999999000000)
$ ./dce_o2 2000000
dead_loop: 0.000 ms (r=0)      ← 整个循环被 DCE 删除
live_loop: 12.892 ms (r=1999999000000)
$ g++ -O2 -S -std=c++17 dce.cpp -o dce.s
$ sed -n '/dead_loop/,/LFE/p' dce.s
_Z9dead_loopi:
    .cfi_startproc
    xorl    %eax, %eax
    ret                      ← 循环连同 200 万次分配,蒸发
```

## 6.1-B {#hw-6-1-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-6-1-b)

**思路**：没有框架时，「多轮交替 + 排序取中位数 + 算 cv」就是 ch01-04 方法的手工版。reserve 与否的差别来自扩容：不 reserve 时 push_back 触发约 20 次倍增扩容、每次整块搬移，reserve 一次分配到位。

1. 实测（N=1e6，15 轮）：reserve 中位数 1.016 ms vs 不 reserve 1.486 ms，1.46×；两个 cv 都**大于 5%**（8.2% 和 28.3%）——按教材 ch01-02 的判据，noreserve 版这组数据「不可信」级别：本机是 WSL2 虚拟机，ch01-03 的 16 条里「锁 governor、关 Turbo、绑核、关 ASLR」都做不到或没做，分配器噪声（malloc 的状态）也在轮间漂移。结论只能取到「reserve 更快、约 1.4~1.5×」这个方向，不能报出精确到小数点的倍数。→ 知识点：[统计与报告](../ch01-benchmark-methodology/04-statistics-and-reporting.md)「报什么、不报什么」、[测量陷阱与环境就绪](../ch01-benchmark-methodology/03-pitfalls-and-env.md)
2. N 从命令行读入是防「常量传播算穿」：写死 constexpr 的话，编译器能在编译期把整段循环算完，你测的是空。这跟 `DoNotOptimize` 不防「表达式自身被算掉」是同一条纪律。→ 知识点：[怎么写一个可信的 microbenchmark](../ch01-benchmark-methodology/02-credible-microbenchmark.md)「DoNotOptimize」一节的警告
3. 性能数据右偏——偶尔一次调度走、一次 malloc 慢，就拖出长尾，均值被长尾拉高、中位数岿然不动。→ 知识点：[统计与报告](../ch01-benchmark-methodology/04-statistics-and-reporting.md)「为什么中位数比均值靠谱」
4. 轮数 15→31 的对照实测：noreserve 的 cv 从 28.3% 降到 **13.1%**、reserve 从 8.2% 降到 **4.6%**——两个 cv 都约减半，reserve 版跨过了 5% 的「可信」线、noreserve 版还在 13%。样本多了置信区间收窄，但没关掉的噪声源（WSL2 的调度、频率）还在，cv 只会**收窄不会归零**；真正治 cv 的办法是 ch01-03 的环境控制，不是无限加轮。→ 知识点：[测量陷阱与环境就绪](../ch01-benchmark-methodology/03-pitfalls-and-env.md)（16 条 checklist 的适用边界）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra reserve_bench.cpp -o reserve_bench
$ ./reserve_bench 1000000 15
n=1000000 rounds=15 sink=29999970
noreserve: median=1.486 ms mean=1.624 ms cv=28.3%
reserve:   median=1.016 ms mean=1.046 ms cv=8.2%
noreserve/reserve (median) = 1.46x
$ ./reserve_bench 1000000 31
n=1000000 rounds=31 sink=61999938
noreserve: median=1.393 ms mean=1.455 ms cv=13.1%
reserve:   median=1.009 ms mean=1.013 ms cv=4.6%
noreserve/reserve (median) = 1.38x
```

（15 轮与 31 轮是同一次会话里连跑的，可比；上一场会话里 15 轮曾测出 1.853/1.221、cv 40.5%/24.0%——跨会话的漂移本身也是「秒数会抖」的证据。）

## 6.2-A {#hw-6-2-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-2-a)

**思路**：缓存行计算是死知识，探针是活知识——指针追逐测出来的四级延迟就是 ch02-01 那张「延迟阶梯」在你这台机器上的复刻。

1. 64 字节缓存行装 16 个 `int`、8 个 `double`。模型：无论你访问 1 字节还是 8 字节，一旦 miss，硬件把「该地址所在的整条 64 字节缓存行」整块搬进来——你付了 64 字节的搬运成本，之后访问同一行里的其它元素免费。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)「缓存行:cache 的最小单位」
2. 实测四级：4 KB=1.37 ns（L1d）、256 KB=4.82 ns（L2）、8 MB=49.98 ns（L3）、64 MB=110.58 ns（DRAM）。L1 与 DRAM 差约 **81 倍**，和教材「两个数量级」的命题一致（教材机器是 1.2 vs 120 ns，本机 1.4 vs 111 ns——量级相同、绝对数不同）。本机 L3 约 30 MB（i7-14650HX），所以 8 MB 还在 L3 里、64 MB 肯定出局。→ 知识点：[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)「上手跑一跑」一节
3. 关键词：**真依赖**。下一个地址藏在当前数据里（`idx = nxt[idx]`），本次 load 的结果决定下次 load 的地址，硬件预取器猜不出下一个访问哪，只能干等——所以测出来的是裸访存延迟。→ 知识点：[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)「指针追逐」的原理说明

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra chase4.cpp -o chase4
$ taskset -c 0 ./chase4
工作集     ns/access
4096               1.37      ← L1d
262144             4.82      ← L2
8388608           49.98      ← L3
67108864         110.58      ← DRAM
```

## 6.2-B {#hw-6-2-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-6-2-b)

**思路**：行优先 vs 列优先的比值不是常数，它取决于工作集落在哪一级。教材在「矩阵 = L3 大小」的 2048 上测出 6×，双尺寸变式把这条曲线两头都露出来。

1. 实测：N=1024（4 MB，L3 内）列/行 ≈ **8.0×**（行 0.35 ms、列 2.79 ms 的中位数比值）；N=4096（64 MB，超出 30 MB L3）≈ **22.0×**（行 5.67 ms、列 124.9 ms）。预测「尺寸越大比值越大」命中：N=1024 时列优先踩的还是 L3（~50 ns/次），N=4096 时列优先的每次访问都在 DRAM（~110 ns/次）且步长 16 KB 大到预取器完全追不上，比值被放大近 3 倍。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)「行优先 vs 列优先」、[存储层次](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)（工作集跨 L3 边界的代价）
2. 交换循环次序叫 loop interchange，编译器**不敢**自动做：它必须证明交换后语义不变，而对大多数循环它证明不了（浮点累加次序、可能的别名、可能的越界提前暴露），「大多数情况其实不影响」不等于「能证明不影响」。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)（loop interchange 的物理动机）
3. 改法：把内层循环变量从「列下标」换成「行内连续方向」——`for i: for j: s += a[i*N+j]`（行优先）。验证：改完耗时回到行优先水平（0.3~5.7 ms 那一档），因为内层沿 4 字节步长顺序扫，一条缓存行服务 16 次访问。→ 知识点：同上（内层循环必须沿内存连续方向）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra rowcol.cpp -o rowcol
$ ./rowcol 1024
N=1024 第1轮: 行优先 0.24 ms (17.5 GB/s) | 列优先 2.77 ms (1.5 GB/s) | 列/行=11.54x
N=1024 第2轮: 行优先 0.44 ms (9.4 GB/s)  | 列优先 2.95 ms (1.4 GB/s) | 列/行=6.64x
N=1024 第3轮: 行优先 0.35 ms (12.1 GB/s) | 列优先 2.79 ms (1.5 GB/s) | 列/行=8.05x
$ ./rowcol 4096
N=4096 第1轮: 行优先 5.67 ms (11.8 GB/s) | 列优先 128.04 ms (0.5 GB/s) | 列/行=22.57x
N=4096 第2轮: 行优先 5.67 ms (11.8 GB/s) | 列优先 124.89 ms (0.5 GB/s) | 列/行=22.01x
N=4096 第3轮: 行优先 4.75 ms (14.1 GB/s) | 列优先 123.12 ms (0.5 GB/s) | 列/行=25.91x
```

N=1024 的三轮里行优先在 0.24~0.44 ms 之间抖（WSL2 噪声），列优先稳定在 2.8 ms——比值 8× 量级；N=4096 时比值稳定在 22× 量级。注意别拿单轮的行优先数字做精确除法，报中位数：8.0× 和 22.0×。

## 6.3-A {#hw-6-3-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-3-a)

**思路**：算术强度是「一支笔」就能算的归因工具，它直接决定优化方向，比任何 profiler 都快。

1. 手算：dot 每次迭代 2 FLOP（1 乘 1 加）、读 2 个 float = 8 字节 → AI = 2/8 = **0.25 FLOP/byte**；AXPY 每次迭代 2 FLOP（乘、加）、读 2 写 1 = 12 字节 → AI = 2/12 ≈ **0.17 FLOP/byte**。→ 知识点：[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)「手算两个例子」一节
2. Roofline：横轴算术强度（ops/byte），纵轴可达算力（ops/s）；水平线是峰值算力、斜线是峰值带宽（$\frac{ops}{s} = \frac{bytes}{s} \times AI$）。贴着斜线 = 带宽受限，加 SIMD 没用，要减访存；贴着水平线 = 算力受限，减访存没用，要加算力。两线交点叫脊点。→ 知识点：同上「Roofline 模型」一节
3. 探针实测：8M float 点积单遍 4.348 ms、流量 64 MB、吞吐 14.72 GB/s。AI=0.25 远低于脊点（~20 FLOP/byte 量级），铁定落在带宽斜线上——「加 SIMD 突破不了带宽墙」的意思是：SIMD 能帮你更快地**发** load，但字节还是那些字节，DRAM 带宽就那么多；所以带宽受限内核的第一刀永远是减访存（改布局、改算法），SIMD 是算力受限内核的事。→ 知识点：同上（Roofline 判读与优化方向）、[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)（减访存是单线程最大杠杆）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra dotbw.cpp -o dotbw && ./dotbw
N=8000000 每次 4.348 ms, 内存流量 64.0 MB, 吞吐 14.72 GB/s
sink=379623520.000000
```

14.7 GB/s 与 6.3-B 里 128 MB 数组扫描的 15.7 GB/s 基本一致——同一台机器、同一种内存状态下是同一个 DRAM 带宽墙（**两个数字能不能合拢取决于你的带宽与 L3 状态**：同款 CPU 上也实测过 56~59 GB/s 的点积与 28.8 GB/s 的扫描，不是一个墙，机器/内存态一变，墙的位置和数量都会变）。本机 WSL2 的有效带宽低于裸机 Linux 的 ~40 GB/s，这正是「你的数字和教材对不上量级也正常」的又一个例子；但「点积贴着带宽斜线跑」这个定性结论跨机器成立。

## 6.3-B {#hw-6-3-b}

**难度 L4** · 题面见 [homework](01-homework.md#hw-6-3-b)

**思路**：四桶的判读靠「谁占比异常高」；场景归类靠各桶的成因特征；实验部分用一个「工作集大小 vs 吞吐」的对照给 Memory Bound 提供直接证据。

1. 四桶：Retiring（slot 退休成有效指令，**好桶**，越高越好）、Frontend Bound（取指/译码跟不上）、Backend Bound（执行单元等数据或等端口，再分 Memory/Core 两支）、Bad Speculation（投机错误路径被冲刷）。→ 知识点：[TMAM 四桶与硬件采样](../ch03-attribution-methodology/03-02-tmam-and-hw-sampling.md)「四个桶」一节
2. 场景归类：a) Backend **Memory** Bound——每次访问踩新缓存行、执行单元在等 DRAM；b) **Bad Speculation**——数据相关的随机分支，预测器猜不中；c) Backend **Core** Bound——长依赖链让执行端口闲着（这是 ILP 问题的 TMAM 叫法）；d) **Frontend** Bound——代码膨胀导致 icache/iTLB miss。→ 知识点：同上（各桶的典型对策对应 ch04 各篇）
3. 瓶颈会迁移：流水线的短板是相对的，修好最宽的桶，第二宽的桶就成了新的主桶。修好 Backend Memory 之后，最常抬头的是 **Bad Speculation**（原来被内存等待掩盖的分支冲刷现在暴露了）或 Frontend（代码布局变了）。所以 TMAM 是迭代流程：每轮治当前最大桶、改完重测。→ 知识点：[TMAM 四桶与硬件采样](../ch03-attribution-methodology/03-02-tmam-and-hw-sampling.md)「瓶颈会迁移」一节、[归因实战](../ch03-attribution-methodology/03-04-walkthrough.md)「别高兴太早」
4. 实验：8 MB 工作集扫描吞吐 34.27 GB/s，128 MB 只有 15.73 GB/s——**2.2 倍**。8 MB 装得进 30 MB 的 L3，128 MB 只能走 DRAM；同样的代码、同样的求和，慢的那份就是「Backend Memory Bound」的直接证据：执行单元大部分时间在等数据从 DRAM 回来。→ 知识点：[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)（工作集跨 L3 边界的吞吐代价）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra scansum.cpp -o scansum && ./scansum
工作集    8 MB, 100 遍, 24.5 ms, 吞吐 34.27 GB/s
工作集  128 MB,  10 遍, 85.3 ms, 吞吐 15.73 GB/s
```

## 6.4-A {#hw-6-4-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-4-a)

**思路**：除法瓶颈的测量有一个先决条件——被除数必须逐元素变化。这道题的题面已经埋了这个坑（第④问），参考答案作者确实在上面翻过车。

1. 实测六个数（d=19）：`/8` 0.77 ns、`>>3` 0.66 ns、`/7` 1.02 ns、`/d` 2.55 ns、`*3` 0.82 ns、`%d` 2.62 ns。变量除是乘法的 **3.1×**。教材在 Zen 3 上测出 5.0×，本机 Raptor Lake 是 3.1×——倍数随机器变，但「变量除法贵几倍」的方向不变。→ 知识点：[数据类型与算术](../ch04-tuning-by-bottleneck/04-03-types-and-arithmetic.md)「除法瓶颈」一节
2. `/8` 与 `>>3` 几乎一样快：GCC 认出 $8 = 2^3$ 自动把除法编译成算术右移，你写哪个都行。`/7` 比变量除法快：常量除数让编译器换成「乘以 7 的乘法逆元 + 移位修正」，不真做 `idiv`（1.02 ns 比乘法 0.82 ns 略高，多出的就是逆元法的几条修正指令）。→ 知识点：同上（常量除数 vs 变量除数）
3. 哈希表 `& (size-1)` 省掉的是那条 `idiv` 指令（size 是 2 的幂时取模等价于掩码）；前提是桶数必须是 2 的幂——`absl::flat_hash_map` 等现代哈希表为此把桶数取 2 的幂，而 `std::unordered_map`（libstdc++）反过来把桶数取素数、宁可付真除法，是「散列质量 vs 除法成本」的另一面权衡。→ 知识点：同上（实战推论与两种哈希表的取舍）
4. 陷阱：如果测 `x/d` 而 x、d 都是循环外变量，那 `x/d` 是**循环不变量**，LICM 会把它提到循环外只算一次，你六个循环测的全是「存一遍常量」——参考答案作者第一版实测六个全 0.28 ns（比值 1.0×），就是这个坑。改法就是题面写的：对 `a[i]/d` 逐元素做，被除数每轮变、除法必须真做。→ 知识点：[循环与计算优化](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)「code motion」一节（编译器替你做的 vs 你的测量意图）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra divcost.cpp -o divcost
$ taskset -c 0 ./divcost 19
d=19
x/8     (2 的幂,编译器换位移): 0.77 ns
x>>3    (手写位移)           : 0.66 ns
x/7     (常量除数,换乘法逆元) : 1.02 ns
x/d     (运行期变量除数,idiv) : 2.55 ns
x*3     (乘法)               : 0.82 ns
x%d     (取模,和除法同价)    : 2.62 ns
变量除/乘法 = 3.1x
```

（坑的实锤：第一版把 `x/d` 写成循环不变量时，六个数全在 0.28~0.29 ns、比值 1.0×——LICM 把除法提出去了。别删，这正是你要解释的现象。）

## 6.4-B {#hw-6-4-b}

**难度 L4** · 题面见 [homework](01-homework.md#hw-6-4-b)

**思路**：平方和与点积同构——单累加器是一条 RAW 依赖长链，4 累加器是四条独立链；编译器默认不敢帮你拆，因为拆了要改浮点结合顺序。

1. 实测 `-O2`：sos1 1570.7 us vs sos4 406.4 us = **3.86×**；`-O3`：1703.6 vs 440.7 = **3.87×**（**本机** -O3 对这两版没有额外帮助，因为两版都是手写标量；同款 CPU 上也实测过 -O3 快约 10% 的读数——这个胜负本身属机器/内存态依赖）。两个版本的结果差 **-3.27e3**（约 0.25% 相对误差）——这就是「拆链改结合顺序」的代价：浮点加法不满足结合律，$(a+b)+c \ne a+(b+c)$，尾数舍入路径不同。→ 知识点：[循环与计算优化](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)「多累加器」一节、[流水线、ILP 与分支预测](../ch02-cpu-microarchitecture/02-03-pipeline-ilp-branch.md)（依赖链与 ILP）
2. `-O3 -ffast-math`：sos1 428.8 us ≈ sos4 437.5 us（0.98×），**两个版本结果逐位一致**（差 0.00e+00）——编译器自己把单累加器重排成 4 通道了，手写版没有了存在意义。→ 知识点：[SIMD 与向量化](../ch04-tuning-by-bottleneck/04-05-simd.md)「一个关于 FP 归约的常见误解」一节
3. 编译器默认不敢拆的理由是标准里的**结合律**：FP 加法不满足结合律，自动重排会改变结果、违反「as-if」语义；`-ffast-math` 明确告诉编译器「我不在乎严格 FP 语义，随便重排」，它就敢拆多通道（汇编里能看到 4 个向量累加器）。→ 知识点：同上
4. `-ffast-math` 的代价：假设没有 NaN/Inf、允许 flush-to-zero（次正规数被清零）、假设有限精度数学——任何依赖「NaN 传播」或「Inf 正确性」的代码（数值求解器的收敛判断、物理模拟的特殊值语义）都可能算出不一样的东西，而且是**全局开关**。工程上划界：数值敏感的库不开；确信「只是求和/点积、数据有界」的局部热点开，或者干脆手写多累加器把重排的范围锁在你自己看得见的那几行里。→ 知识点：同上（`-ffast-math` 的取舍）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra sos.cpp -o sos_o2 && ./sos_o2 4000000
N=4000000 单累加器 sos1: 1570.7 us | 4 累加器 sos4: 406.4 us | sos1/sos4 = 3.86x
sos1(结果)=1327507.500000  sos4(结果)=1330782.000000  绝对差=-3.27e+03
$ g++ -O3 -std=c++17 -Wall -Wextra sos.cpp -o sos_o3 && ./sos_o3 4000000
N=4000000 单累加器 sos1: 1703.6 us | 4 累加器 sos4: 440.7 us | sos1/sos4 = 3.87x
$ g++ -O3 -ffast-math -std=c++17 sos.cpp -o sos_ffm && ./sos_ffm 4000000
N=4000000 单累加器 sos1: 428.8 us | 4 累加器 sos4: 437.5 us | sos1/sos4 = 0.98x
sos1(结果)=1330808.875000  sos4(结果)=1330808.875000  绝对差=0.00e+00
```

## 6.5-A {#hw-6-5-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-5-a)

**思路**：这题是全卷最该「如实报告」的一题——教材在 Zen 3 上测出 mutex 是 atomic 的 3.6 倍，本机 Raptor Lake 上顺序**正好反过来**。

1. 实测五数：plain **0.00 ns**、atomic relaxed **3.94 ns**、atomic seq_cst **3.90 ns**、mutex 加解锁 **2.66 ns**、mutex+100 次自增（每次加解锁口径）**2.67 ns**。mutex/atomic = **0.7×**——本机无竞争 mutex 反而比 `fetch_add` 快。如实解释两件事：第一，教材的数字是 Zen 3 的（那里 `lock xadd` 只要 ~8 周期、mutex 快速路径相对长）；本机 Intel Raptor Lake 上 `lock` 前缀指令延迟更高（~4 ns ≈ 17 周期），而 glibc futex 快速路径的 CAS+store 更短，于是 3.6× 翻成了 0.7×。第二，**顺序翻了，两个结论没翻**：①两者都是纳秒级（2.7 与 3.9）；②plain 0.00 不是「无同步免费」，是自增被优化成寄存器操作、根本没有内存写入。这就是教材「绝对倍数随机器浮动、量级和结论才可信」的活例——顺手记住：**拿本教材的倍数去别人的机器上当常数用，是另一种『猜』**。→ 知识点：[锁的开销与「无锁不是银弹」](../ch05-multicore-performance/05-03-locks-vs-lockfree.md)「无竞争锁」一节、[性能思维](../ch00-performance-mindset/01-efficiency-vs-performance.md)「别把这张表当普适结论」警告框
2. ⑤与④几乎一样（2.67 vs 2.66 ns/op）——**注意口径**：题面 ⑤ 要的是「每元素摊薄成本」，而 2.67 ns/op 是**每次加解锁**的成本；按每元素摊薄口径是 2.67/100 ≈ **0.027 ns/元素**（实测 ~0.03）。差这两个量级的原因是**临界区里那 100 次 plain 自增被编译器折叠**：它们改的是无人观察的普通变量、无内存副作用，-O2 整段折叠成寄存器加法甚至一次 +100，压根没有 100 次真实自增。所以 ⑤ 和 ④ 实测的是同一个东西：锁的**每次加解锁固定成本**，临界区里干 1 件还是 100 件（可折叠的轻活），锁的那 2.7 ns 都不变——临界区越大，锁成本占比越小。这正是决策表「复杂共享结构、竞争不强 → mutex + RAII」的依据：可读性和正确性远比这点纳秒差值钱。→ 知识点：[锁的开销与「无锁不是银弹」](../ch05-multicore-performance/05-03-locks-vs-lockfree.md)（决策框架表）
3. plain 自增 1 亿次 0.00 ns：GCC 看穿循环体只改一个没人观察的局部变量，把整段换成了寄存器自加甚至直接算闭式值。它测的是「编译器优化后的空转」，不是「无同步的极限」——想做对照需要 `DoNotOptimize` 或写内存。→ 知识点：[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)（第一集）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra synccost.cpp -o synccost -pthread && ./synccost
plain ++              : 0.00 ns/op
atomic relaxed        : 3.94 ns/op
atomic seq_cst        : 3.90 ns/op
mutex + 1 次 ++       : 2.66 ns/op
mutex + 100 次 ++(每次): 2.67 ns/op
mutex/atomic_relaxed = 0.7x      ← 与教材 3.6x 相反,如实报告
```

## 6.5-B {#hw-6-5-b}

**难度 L4** · 题面见 [homework](01-homework.md#hw-6-5-b)

**思路**：同一套并行框架、两个负载，一条曲线被带宽压平、一条逼近线性——扩展性曲线的形状就是瓶颈的指纹。

1. 实测（8 线程时）：带宽受限的求和 **4.39×**，算力受限的乘加混叠 **6.59×**。→ 知识点：[NUMA、affinity 与扩展性曲线](../ch05-multicore-performance/05-02-numa-scaling.md)「加核不等于线性加速」
2. A（求和）的天花板来得早：所有核读同一份 DRAM，**内存带宽是共享资源**——核数还没到 8，带宽先打满，再加核只是多几个核排队等数据。ch03-01 的 Roofline 早已预告：求和的算术强度趋近于零，铁定带宽受限。→ 知识点：同上（共享资源争用）、[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)
3. B（乘加混叠）8 线程 6.59× 更接近线性：每个元素 1000 次乘加几乎不碰内存，数据在寄存器里流转，各核的 ALU 是**私有**的——它躲开了共享内存带宽这个瓶颈，只剩 Amdahl 的串行尾巴（分块调度、汇总）和线程迁移噪声。教材 ch05-02 的 8 线程只有 2.53×，因为那是 memory-bound 的求和负载。→ 知识点：[NUMA、affinity 与扩展性曲线](../ch05-multicore-performance/05-02-numa-scaling.md)（memory-bound vs compute-bound）
4. 反推：$S = 1/((1-p) + p/N)$，代入 S=6.59、N=8 → $(1-p)+\frac{p}{8} = 0.152$ → $p \approx 0.97$、串行比例 ~3%；代入 A 的 S=4.39 → 串行比例 ~12%。注意这个「串行比例」里混着共享带宽的贡献，不是纯代码串行——这正是 Amdahl 只给上限的体现。→ 知识点：[性能思维](../ch00-performance-mindset/01-efficiency-vs-performance.md)「Amdahl」一节
5. 计时要放在**线程创建之前、join 之后**（像参考答案那样把整个 `emplace_back + join` 包进去）：你测的是「从开始到完成」的墙钟，线程创建（几十微秒级、要分配栈）也是并行程序的真实成本——把它剔出去，8 线程的小任务会显得比实际快，那是作弊。→ 知识点：[NUMA、affinity 与扩展性曲线](../ch05-multicore-performance/05-02-numa-scaling.md)「线程创建与栈的成本」一节

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra scaling.cpp -o scaling -pthread && ./scaling
workers    mem(ms)  mem 加速   comp(ms)  comp 加速
1            22.6      1.00x      267.0      1.00x
2             8.5      2.67x      134.0      1.99x
4             6.8      3.33x       69.1      3.86x
8             5.1      4.39x       40.5      6.59x
```

（数字会抖；稳定的是两条曲线的形状：mem 早早拐平、comp 一路往上。）

## 6.6-A {#hw-6-6-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-6-a)

**思路**：计时法测 RVO 会被编译器跨迭代优化打穿，计数法编译器打不穿——拷贝/移动构造的计数器是「数出来的事实」。

1. 四行计数：URVO **0 拷贝 0 移动**、NRVO **0/0**、`std::move(t)` **0/1**、`g_global` **1/0**。反模式是 `return std::move(t)`：它把返回值强转成右值，**禁用了 NRVO**（NRVO 要求左值），逼编译器走一次移动构造——它永远只会更慢、不会更快。编译警告原文：`warning: moving a local object in a return statement prevents copy elision [-Wpessimizing-move]`。→ 知识点：[RVO、NRVO 与 move 的真实成本](../ch06-cpp-abstraction-cost/06-05-rvo-move.md)「用 copy/move 计数器看清」一节
2. `-fno-elide-constructors` 后：URVO 仍是 0/0（**C++17 强制拷贝消除**，`return Tracked(1)` 的 prvalue 直接在调用方构造，关不掉）；NRVO 退化 0/1（它是「事实优化」，关掉后退化为一次便宜 move）；`std::move` 0/1 不变；全局 1/0 不变。→ 知识点：同上「用 -fno-elide-constructors 看 RVO 关掉的样子」
3. copy 176.3 µs vs move 7.5 ns = **23513×**。move 是 O(1)：`vector` 的移动构造只是把源的三指针偷过来、把源置空，不碰元素本身；copy 是 O(n) 深拷贝，4 MB 的分配 + memcpy 全都要。→ 知识点：同上「move 比 copy 便宜多少」
4. 旧教条「按值返回大对象要用指针/引用」在现代 C++ 过时：C++17 的 URVO 是强制免费的，NRVO 事实上免费，最坏情况兜底的也是 O(1) 移动——值语义写法既自然又零成本，输出参数、手动 new 反而把生命周期管得满手血。→ 知识点：[RVO、NRVO 与 move 的真实成本](../ch06-cpp-abstraction-cost/06-05-rvo-move.md)「实战规则」

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra rvo.cpp -o rvo
rvo.cpp:20:79: warning: moving a local object in a return statement prevents copy elision [-Wpessimizing-move]
$ ./rvo
URVO  return Tracked(1)  : copies=0 moves=0
NRVO  return t(局部)     : copies=0 moves=0
return std::move(t)      : copies=0 moves=1
return g_global(lvalue)  : copies=1 moves=0
$ g++ -O2 -fno-elide-constructors -std=c++17 rvo.cpp -o rvo_noelide && ./rvo_noelide
URVO  return Tracked(1)  : copies=0 moves=0    ← C++17 强制,关不掉
NRVO  return t(局部)     : copies=0 moves=1    ← 退化成一次 move
return std::move(t)      : copies=0 moves=1
return g_global(lvalue)  : copies=1 moves=0
$ g++ -O2 -std=c++17 -Wall -Wextra movecopy.cpp -o movecopy && ./movecopy
vector<int>(1000000): copy   176266.7 ns/次 | move      7.5 ns/次 | copy/move = 23513x
```

## 6.6-B {#hw-6-6-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-6-6-b)

**思路**：类型擦除的两个成本（间接调用、可能堆分配）都要在「防编译器看穿」的条件下才测得出来——这题的三个坑（局部累加、逃逸数组）就是测量纪律本身。

1. `sizeof(std::function<int(int)>) = 32`（libstdc++；libc++ 是 48，MSVC 又不同——实现定义）。→ 知识点：[std::function 的小缓冲区优化](../ch06-cpp-abstraction-cost/06-03-std-function-sbo.md)（sizeof 因实现而异）
2. 实测：函数指针调用 0.46 ns、直接 lambda 0.23 ns（被内联）、`std::function` 调用 1.35 ns = lambda 的 **5.9×**（教材 6×，本机吻合）。类型擦除贵的理由：调用走函数指针/虚函数表的间接跳转，**内联被挡在门外**——0.23 ns 那档是内联后只剩一条 `lea`，1.35 ns 那档每次都是真间接调用。**坑的实锤**：如果累加直接写进 `volatile long long sink`，每次迭代都要对 sink 做读改写，依赖链把三档全压成 ~1.5 ns、比值 0.8×——参考答案第一版就这么翻的车，改成「局部累加、最后一次性喂给 volatile」才测出 5.9×。→ 知识点：同上「调用是间接的」一节、[为什么 microbenchmark 会骗你](../ch01-benchmark-methodology/01-why-microbenchmarks-lie.md)（测量装置自己变成瓶颈）
3. 构造（逃逸进 `vector<std::function>` 防折叠）：小捕获 11.36 ns/次，大捕获 43.69 ns/次 = **3.8×**。5 个 int 的捕获体是 20 字节，超过 libstdc++ `std::function` 的 SBO 缓冲（32 字节对象里约 16 字节可用），只能 `new` 一块堆——43.69 ns 里大半是堆分配 + 后续析构的 free。**坑的实锤**：不逃逸的话，小捕获版会被 GCC 看穿整个生命周期、整段折叠成 0.00 ns（参考答案实测过）。热路径三个对策：模板参数（编译期多态，消除擦除）、固定签名函数指针（无捕获时零开销）、循环外构造一次复用对象（或减少捕获让它命中 SBO）。→ 知识点：同上「这两个代价什么时候咬人」、[C++ 抽象的成本速查表](../ch06-cpp-abstraction-cost/06-04-abstraction-cost-cheatsheet.md)（速查表两行）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra sfbo.cpp -o sfbo && ./sfbo
sizeof(std::function<int(int)>) = 32
函数指针调用(noinline)   : 0.46 ns/次
直接 lambda(内联)        : 0.23 ns/次
std::function 调用       : 1.35 ns/次 (5.9x of lambda)
$ g++ -O2 -std=c++17 -Wall -Wextra sfbo_ctor.cpp -o sfbo_ctor && ./sfbo_ctor
sizeof(std::function<int(int)>) = 32
构造 小捕获(1×int,SBO): 11.36 ns/次
构造 大捕获(5×int,堆) : 43.69 ns/次 (3.8x)
```

## 6.7-A {#hw-6-7-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-6-7-a)

**思路**：`-O` 级别是「编译器替你做什么」的档位表；text 段是「它实际留下了什么」的账本；两个死函数是 `--gc-sections` 的猎物。

1. 实测 gcc：-O0 6.86 ms、-O2 2.70 ms、-O3 2.61 ms——`-O0→-O2` 约 **2.5×**（教材机器 4×，本机循环体瓶颈比例不同，量级一致）；**-O3 并没有稳定更快**（2.61 vs 2.70 在噪声内，教材 ch07-01 的诚实结论「-O3 不总比 -O2 快」这里平着验证）。clang 同机对比：7.29 / 2.16 / **1.84** ms——clang 的 -O2 比 gcc 快 25%，-O3 更快到 1.84。这不是「clang 更好」的普适结论（教材 ch07-03：差距随代码变、随版本变），但如实贴出。→ 知识点：[-O 级别与 optimization blockers](../ch07-compiler-and-size/07-01-opt-levels-and-blockers.md)「-O 级别」一节、[链接性能、多编译器对比与编译期元编程](../ch07-compiler-and-size/07-03-linking-and-compilers.md)
2. `size` 的 text 段（gcc 16）：-O0 **8601**、-O2 **4037**、-O3 **3931**、带 `-ffunction-sections` 但不带 `--gc-sections` **4037**、带全链接回收 **3967**。用 `ls -l` 会错：整个 ELF 含头部、对齐、调试信息，跟「代码多大」不是一回事，教材 ch07-04 专门提醒过。`--gc-sections` 回收了 70 字节 = 那两个 `dead_fn` 的机器码——它们从未被调用，编译器（-Wall 都警告了 unused）不能自己删掉外部可见的东西，链接器可以。→ 知识点：[体积优化](../ch07-compiler-and-size/07-04-size-optimization.md)「第二招」与量体积用 `size` 的警告框
3. 体积影响性能的主路径是 **icache**：代码体积大 → 指令缓存装不下 → 取指频繁 miss → Frontend Bound。`--gc-sections` 几乎零成本回收死代码、改善 icache，所以是「release 免费午餐」；配套的 `-ffunction-sections -fdata-sections` 是它的前置。→ 知识点：[前端优化](../ch04-tuning-by-bottleneck/04-07-frontend-pgo.md)（icache miss）、[体积优化](../ch07-compiler-and-size/07-04-size-optimization.md)（取舍清单）

**验证输出**：

```text
$ ./olevels_o0 4000000
work(4000000) = 6.86 ms, r=287633184.000000
$ ./olevels_o2 4000000
work(4000000) = 2.70 ms, r=287633184.000000
$ ./olevels_o3 4000000
work(4000000) = 2.61 ms, r=287633184.000000
$ size olevels_o0 olevels_o2 olevels_o3 olevels_nogc olevels_gc
   text    data     bss     dec     hex filename
   8601     720       8    9329    2471 olevels_o0
   4037     704       8    4749    128d olevels_o2
   3931     704       8    4643    1223 olevels_o3
   4037     704       8    4749    128d olevels_nogc
   3967     696       8    4671    123f olevels_gc      ← 回收 70 字节死代码
$ ./olevels_clang_o0 4000000
work(4000000) = 7.29 ms, r=287633184.000000
$ ./olevels_clang_o2 4000000
work(4000000) = 2.16 ms, r=287633184.000000
$ ./olevels_clang_o3 4000000
work(4000000) = 1.84 ms, r=287633184.000000
```

## 6.7-B {#hw-6-7-b}

**难度 L4** · 题面见 [homework](01-homework.md#hw-6-7-b)

**思路**：两个实验合起来正好是「跨 TU blocker」的正反两面：LTO 让它看得见实现（大收益），`__restrict` 解决的是另一类 blocker（别名），而后者在干净对照下可能毫无收益——两个诚实结果都要如实写。

1. LTO 常量传播实测：无 LTO 14.0~14.8 ms，有 LTO **3.6~3.9 ms ≈ 3.9×**（与教材 3.9× 恰好一致）。机制链：无 LTO 时 `scale_factor()` 在另一个翻译单元，编译器看不见实现，不敢假设它循环不变，于是每次迭代一次真调用 + 一次乘法、循环无法向量化；LTO 链接期合并 IR，函数内联成常量 3.0f，循环立即向量化（`-fopt-info-vec` 实锤 `loop vectorized using 16 byte vectors and unroll factor 4`），`nm` 实锤符号消失。→ 知识点：[LTO、ThinLTO 与 PGO 的工程接入](../ch07-compiler-and-size/07-02-lto-pgo.md)「LTO」一节、[-O 级别与 optimization blockers](../ch07-compiler-and-size/07-01-opt-levels-and-blockers.md)（跨 TU blocker）
2. `__restrict` 干净对照（循环体是写内存的 `a[i] += b[i] * *scale`，教材 ch07-01 的 scale_add 形）：别名版 2.969 ms vs `__restrict` 版 3.265 ms（**0.91×，无收益**）。原因在诊断里：别名版的 GCC 生成了**版本化循环**——`loop versioned for vectorization because of possible aliasing`，即先运行时检查 a/b 是否重叠，不重叠走向量化体、重叠走标量体；这道「运行时检查」几乎免费，所以 `__restrict` 省下的那点检查在 8M 规模上根本测不出来。`__restrict` 真正值钱的是「编译器连版本化都做不了」的场景（循环结构太复杂、检查代码本身打断优化），以及它在热路径上帮你省掉版本化的分支；撒谎的代价是 **UB**——你承诺无别名、实际有别名，编译器按无别名做的重排就是错的，没有任何诊断。→ 知识点：[-O 级别与 optimization blockers](../ch07-compiler-and-size/07-01-opt-levels-and-blockers.md)「指针别名」一节（含那个「教学 demo 归因不干净」的警告框——本题的干净对照正是对它的回应）

**验证输出**：

```text
$ g++ -O2 -std=c++17 lto_bench.cpp config.cpp -o lto_no
$ g++ -O2 -flto -std=c++17 lto_bench.cpp config.cpp -o lto_yes
$ ./lto_no
scale 循环 (N=8000000, 10 轮取最快): 14.795 ms
$ ./lto_yes
scale 循环 (N=8000000, 10 轮取最快): 3.931 ms
$ ./lto_no && ./lto_yes
scale 循环 (N=8000000, 10 轮取最快): 14.044 ms
scale 循环 (N=8000000, 10 轮取最快): 3.634 ms
$ nm lto_yes | grep -i scale || echo "(LTO 后 scale_factor 被内联消除)"
(LTO 后 scale_factor 被内联消除)
$ g++ -O2 -flto -std=c++17 -fopt-info-vec lto_bench.cpp config.cpp -o lto_yes 2>&1 | grep -i vectoriz
lto_bench.cpp:12:23: optimized: loop vectorized using 16 byte vectors and unroll factor 4

$ g++ -O3 -std=c++17 -Wall -Wextra -fopt-info-vec restrict.cpp -o restrict_o3
$ ./restrict_o3
别名版(默认)  : 2.969 ms
__restrict 版 : 3.265 ms (0.91x)      ← 干净对照:无收益,如实报告
$ g++ -O3 -std=c++17 -fopt-info-vec restrict.cpp -o restrict_o3 2>&1 | grep -i "versioned"
restrict.cpp:9:23: optimized:  loop versioned for vectorization because of possible aliasing
```

## 6.C-1 {#hw-6-c-1}

**难度 L3** · 题面见 [homework](01-homework.md#hw-6-c-1)

**思路**：这道题是 ch04-01「AoS→SoA」在真实业务结构上的重演——10 倍的差距不需要改一行算法，只需要把数据按「怎么被访问」重新摆。

1. 实测：A（AoS 120 字节/订单）4.25 ms；B（SoA 两个数组）0.40 ms = **10.6×**；C（冷热分离）0.45 ms = **9.5×**；三版结果逐位一致（287994538.0）。A 的浪费在哪一级都狠，但最狠在**缓存行利用率**：热循环每订单只碰 12 字节，却把 120 字节的整段拉进 cache，一条 64 字节缓存行里真正有用的不到 8 字节——相当于用 10 倍的内存流量买同一个结果。→ 知识点：[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)「AoS → SoA」一节、[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)
2. B 和 C 几乎一样快：B 每订单流量 12 字节（8 字节 price + 4 字节 quant），C 是 16 字节（Hot 结构体对齐到 8，`sizeof(Hot)=16`）——两者都把「搬进来却用不上的字节」压到了最低，所以都逼近带宽下界。→ 知识点：同上（SoA 天然 SIMD 友好）
3. 工程权衡：C 保留了「按订单对象思考」的入口（Hot 数组 + 按 id 索引冷字段），代码可读性和维护性接近 AoS；代价是冷字段要另存一张按 id 查的表，写路径要维护两份数据的同步。选 B 而不是 C 的场景：热字段就是全部字段、或者性能是压倒性的第一优先级（比如物理引擎的粒子数组）；选 C 的场景：业务对象字段多、热字段少、团队不想把代码改得太「数据导向」。→ 知识点：同上（SoA 的工程权衡）
4. 换成「按 name 前缀筛选」后 B 不成立：name 是冷字段，B 里根本没有——你得回到 A 或 C 的冷表上扫。所以「按**怎么被访问**组织数据」的重音在「怎么被访问」：访问模式一换，最优布局就换。这也是 ch04-01 反复强调「数据导向设计」而不是「面向对象设计」的含义——对象是按现实世界组织的，数据要按访问模式组织。→ 知识点：[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)（DOD 的核心思想）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra orders.cpp -o orders && ./orders
sizeof(Order)=120, sizeof(Hot)=16
A AoS(120B/订单,只碰 12B): 4.25 ms
B SoA(只读 price+quant)   : 0.40 ms (10.6x of A)
C 冷热分离(热字段独立数组) : 0.45 ms (9.5x of A)
结果一致: A=287994538.000000 B=287994538.000000 C=287994538.000000
```

## 6.C-2 {#hw-6-c-2}

**难度 L4** · 题面见 [homework](01-homework.md#hw-6-c-2)

**思路**：这是一次完整的 ch03-04 漏斗实践——一支笔判方向（Roofline）、一次改写换 1.7×（AoS→SoA）、再试一刀（多累加器）并诚实评估它还值不值。

1. Roofline 手算：每次迭代 2 乘 1 加 = 3 FLOP；AoS 读 `w,x,y` 三字段，但 `pad` 被同一缓存行顺路带进，按有用流量算 12 字节 → AI = 3/12 = **0.25 FLOP/byte**，远低于脊点（~20 量级）——铁定带宽受限，优化方向是**减访存**，不是加 SIMD。→ 知识点：[归因实战](../ch03-attribution-methodology/03-04-walkthrough.md)「第 2 步」一节、[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)
2. 实测：AoS 3.289 ms vs SoA 1.928 ms = **1.71×**（另一次运行 1.74×，抖动内一致）；两版结果逐位一致（394405.9，SoA4 差 0.1% 属浮点重排）。→ 知识点：[后端内存瓶颈](../ch04-tuning-by-bottleneck/04-01-backend-memory.md)「AoS → SoA」一节
3. 理论预期：AoS 每粒子 16 字节、SoA 12 字节，流量比 16/12 = 1.33×；实测 1.71× 多出来的部分来自 SoA 的连续布局喂饱了预取器与更宽的向量化（AoS 三字段 stride-16 的 gather 式访问编译器处理得更差）。所以「pad 占 1/4 带宽」是下限估计，实测只会更大不会更小。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)（预取器与连续流）
4. 瓶颈会迁移：修好 AoS→SoA 之后，同一个程序里的「分支密集过滤」可能成为新主桶（Backend Memory 下去了、Bad Speculation 抬头）。下一步是重测整个程序（不是只看这个内核），看 TMAM 四桶里新的最大桶是谁。→ 知识点：[TMAM 四桶与硬件采样](../ch03-attribution-methodology/03-02-tmam-and-hw-sampling.md)「瓶颈会迁移」、[归因实战](../ch03-attribution-methodology/03-04-walkthrough.md)「别高兴太早」
5. SoA + 4 累加器：1.308 ms，比 SoA 再快 **1.47×**——本机这个内核的链还有可压榨的 ILP 空间（3 个 load + 2 乘 + 1 加的链比点积长）。但注意和 6.3-A 的 Roofline 结论的关系：带宽受限的内核再提 ILP 是在「更快地排队等带宽」，收益会随 N 逼近带宽墙而衰减——本卷 Project 的缩放曲线会亲眼看一次（1.8×~5.7× 的衰减曲线）。所以顺序永远是「先减访存、再提 ILP、最后 SIMD」，且每步都回测。→ 知识点：[循环与计算优化](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)（多累加器）、[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)（带宽墙）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra wdot2.cpp -o wdot2 && ./wdot2 4000000
N=4000000 AoS: 3.289 ms | SoA: 1.928 ms | SoA+4累加器: 1.308 ms
SoA/AoS=1.71x  SoA4/SoA=1.47x
结果: AoS=394405.906250 SoA=394405.906250 SoA4=394831.562500
```

## 6.C-3 {#hw-6-c-3}

**难度 L5** · 题面见 [homework](01-homework.md#hw-6-c-3)

**思路**：矩阵乘是「算力受限」家族的代言人（算术强度 O(n)），而它的朴素写法却把 cache 用成了灾难——循环次序 + 分块就是「不换算法、只换数据流向」的全部内容。改编自 CSAPP 第 6 章 cache blocking 系列练习（6.45–6.49），按竞赛挑战强化为「实测三种写法并解释倍数随 n 放大的机制」。

1. n=512 实测：ijk **165.5 ms（1.62 GFLOPS）**、ikj **56.5 ms（4.75）**、ikj+分块 **39.1 ms（6.87）**；三个版本的 C 两两最大差 **0.00e+00**——三种写法对同一个 (i,j) 的 k 求和顺序一致，浮点运算逐位相同，所以结果逐位相同（这也是「改次序不改语义」的最好证据）。ijk 慢的机制：内层 k 循环里 `B[k*N+j]` 每次前进 N×8 = 4 KB——固定 j、变 k，访问的是 B 的**同一列**，每个元素踩一条新缓存行，而且步长 4 KB 大到预取器追不上，于是每个内层迭代都是一次 L3/DRAM 级 miss。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)「行优先 vs 列优先」一节（B 的列访问 = 大步长跳跃）
2. n=1024 实测：ijk **2943.1 ms（0.73 GFLOPS）**、ikj **245.6 ms（8.74）**、分块 **171.7 ms（12.50）**（**分块 vs ikj 的胜负属机器/内存态依赖**：同款 CPU 上也实测过两者打平 ~1.0×，本组里分块赢 1.43×，两个方向都见过，别把「分块必赢」当常数；同理，若你重跑时三版 C 的两两最大差出现 1e-12 级微差也别慌——那是编译器对不同版本用了不同的归约实现、浮点求和顺序不同，纯浮点噪声，不是分块写错）。ijk 与分块版差距从 n=512 的 **4.2×** 扩大到 n=1024 的 **17.1×**：n=512 时一个矩阵 2 MB、三个共 6 MB，全在 L3（本机 30 MB）里，ijk 付的是「L3 随机访问延迟」；n=1024 时三个矩阵 24 MB 贴到 L3 上限，B 的列访问开始把 L3 打穿到 DRAM，每次 miss 从 ~50 ns 涨到 ~110 ns，差距被放大 4 倍。**ijk 在 n=1024 上 2.9 秒才跑完——这就是 cache 灾难的实感，别在更大的 n 上试它。**→ 知识点：[存储层次与延迟阶梯](../ch02-cpu-microarchitecture/02-01-memory-hierarchy.md)（工作集跨 L3 边界）、[归因实战](../ch03-attribution-methodology/03-04-walkthrough.md)（L3 颠簸）
3. 块大小约束：bs=64 时一个 $bs×bs$ 的 B 子块 = 64×64×8 = 32 KB；分块要保证「内层循环反复重访的 C 子块 + B 子块」装得进 L2（本机 P 核 L2 2 MB），bs 的上限由 $2×bs^2×8 ≤ L2$ 反推，bs 太大块装不进 cache 分块白做、太小则块循环开销占比上升——bs=64 是 32 KB 级的甜点区。→ 知识点：[缓存行与局部性](../ch02-cpu-microarchitecture/02-02-cacheline-and-locality.md)、[循环与计算优化](../ch04-tuning-by-bottleneck/04-02-loop-and-compute.md)（分块是「控热数据集」在矩阵上的形式）
4. 矩阵乘算术强度 ~n（每个 A、B 元素被复用 n 次，AI = 2n³ / (3n²×8) ≈ n/12 FLOP/byte），n=1024 时约 85——远超脊点 20，落在**算力线**上：所以它的优化方向是榨算力（SIMD/FMA/分块保重用），与全卷「点积减访存」的主线正好相反。这也解释了②：n 越大、每个元素复用次数越多、AI 越高，分块版 GFLOPS 随 n 上升（6.87 → 12.50），因为大 n 下「算力受限」的本色越来越纯粹。→ 知识点：[USE 方法与 Roofline 模型](../ch03-attribution-methodology/03-01-use-and-roofline.md)（矩阵乘 vs 点积的 AI 对照）

**验证输出**：

```text
$ g++ -O2 -std=c++17 -Wall -Wextra matmul.cpp -o matmul
$ ./matmul 512 64
n=512 bs=64
ijk 朴素   :    165.5 ms ( 1.62 GFLOPS)
ikj 交换次序:     56.5 ms ( 4.75 GFLOPS)
ikj+分块   :     39.1 ms ( 6.87 GFLOPS)
max_diff(ijk,ikj)=0.00e+00  max_diff(ikj,blocked)=0.00e+00
$ ./matmul2 1024 2
n=1024 mode=2: 245.6 ms (8.74 GFLOPS)
$ ./matmul2 1024 3
n=1024 mode=3: 171.7 ms (12.50 GFLOPS)
$ timeout 60 ./matmul2 1024 1
n=1024 mode=1: 2943.1 ms (0.73 GFLOPS)
```

（n=1024 的 ijk 这次在 60 秒 timeout 内跑完了（2.9 秒），但它在 n=2048 上会是分钟级——越大越惨，这就是「cache 灾难」的指数味。）
