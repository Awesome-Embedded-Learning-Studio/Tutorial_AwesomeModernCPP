---
id: "015"
title: "卷六：性能优化 TODO"
category: content
priority: P2
status: rebuild
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: large
---

# 卷六：性能优化 TODO

## Current Assets

- `documents/vol6-performance/`：3 篇文章 + index.md（骨架）。
  - `02-inline-and-compiler-optimization.md`（77 行，极薄）→ 改写为 03
  - `06-evaluating-performance-and-size.md`（1290 行，充实）→ 改写为 12
  - `avx-avx2-deep-dive.md`（167 行，笔记风格）→ 改写为 11
- `code/examples/compiler_explorer/`：14 个 Compiler Explorer 示例（host/ARM 双平台），项目中已有 `OnlineCompilerDemo` 组件可嵌入。
- `code/examples/vol34567/`：inline optimization、perf eval、compiler options 示例。
- 英文翻译 `documents/en/vol6-performance/` 4 文件已存在。

## 设计原则

1. **自包含优先**：每篇文章是完整阅读单元，读者不需要跳到其他卷就能理解。跨卷链接只出现在"延伸阅读"。
2. **读者路径**：为什么优化 → 如何测量 → 编译器做了什么 → 如何找到瓶颈 → 硬件感知优化 → 向量化 → 综合评估。
3. **可复现性能工程**：每篇文章必须回答"怎么测"和"不能得出什么结论"。不做玄学优化清单。
4. **打碎重建**：现有 3 篇文章保留核心实测数据，全部规范化改写、重新编号。

## 重建大纲（13 篇文章）

### Chapter 0：性能工程思维（1 篇）

#### 00 — `00-performance-mindset.md` — 性能工程：思维与方法

**定位**：卷入口，建立认知框架

**内容要点**：

- 性能优化不是玄学调优，是"测量 → 分析 → 优化 → 验证"的工程闭环
- 优化层次：算法 → 数据结构 → 内存访问 → 指令级 → 平台特定
- 过早优化 vs 过晚优化：什么时候该做、什么时候不该做
- C++ 性能迷思：零开销抽象的真实含义、C++ 不一定比 C 慢
- 本卷学习路线图

**篇幅**：2000-2500 字 | **难度**：beginner | **cpp_standard**：无特定

**参考资料**：

- CppCon 2025 — Performance and Efficiency for Experts: https://cppcon.org/class-2026-performance-and-efficiency/
- C++ Core Guidelines — Performance: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
- Fedor Pikus, *The Art of Writing Efficient Programs*, Packt, Ch.1
- Kurt Guntheroth, *Optimized C++*, O'Reilly, Ch.1

---

### Chapter 1：性能测量方法论（2 篇）

#### 01 — `01-benchmark-methodology.md` — 基准测试方法论

**定位**：核心概念

**内容要点**：

- 什么是好的 benchmark：可重复、统计显著、控制变量
- Warmup：cache 冷启动、分支预测器冷启动、CPU 频率爬升
- 重复次数与统计：均值、中位数、标准差、异常值剔除
- 常见陷阱：编译器把 benchmark 优化掉（DCE）、测量空循环而非目标代码、微基准不代表真实性能
- 如何读 benchmark 结果：不能得出什么结论（样本偏差、平台依赖、编译器版本依赖）
- 从 benchmark 到决策：A/B 对比、回归检测

**篇幅**：2500-3000 字 | **难度**：intermediate | **cpp_standard**：[17, 20]

**代码示例**：手写简单计时器 → 展示各种陷阱 → 引出为什么需要专业工具

**参考资料**：

- Abseil Performance Tip #75 — How to Microbenchmark: https://abseil.io/fast/75
- Google Benchmark User Guide: https://google.github.io/benchmark/user_guide.html
- C++ Stories — Micro Benchmarking Libraries: https://www.cppstories.com/2016/01/micro-benchmarking-libraries-for-c/
- nanobench Framework Comparison: https://nanobench.ankerl.com/comparison.html

#### 02 — `02-benchmark-tools-practice.md` — 基准测试工具实战

**定位**：工具实战

**内容要点**：

- Google Benchmark：安装、KeepRunning/State、参数化、自定义计数器
- nanobench：header-only 快速上手、适用场景对比
- 编译器防御：`benchmark::DoNotOptimize`、`asm volatile("" ::: "memory")` 防 DCE
- CI 集成：如何在 CI 中检测性能回归
- 延伸阅读：并发场景 benchmark → vol5 `ch08/02-concurrency-benchmarks.md`

**篇幅**：2500-3000 字 | **难度**：intermediate | **cpp_standard**：[17, 20]

**代码示例**：同一个排序算法分别用 Google Benchmark 和 nanobench 测量

**参考资料**：

- google/benchmark GitHub: https://github.com/google/benchmark
- nanobench 官方文档: https://nanobench.ankerl.com/
- CodSpeed — How to Benchmark C++: https://codspeed.io/docs/guides/how-to-benchmark-cpp-with-google-benchmark
- Bencher — How to Benchmark C++ Code: https://bencher.dev/learn/benchmarking/cpp/google-benchmark/

---

### Chapter 2：理解编译器（2 篇）

#### 03 — `03-compiler-optimization-deep-dive.md` — 编译器优化深度剖析

**定位**：核心（改写现有 `02-inline-and-compiler-optimization.md`）

**内容要点**：

- **编译器选项速查（自包含）**：-O0/-O1/-O2/-O3/-Os/-Ofast 区别、-flto、-march=native、CMake 配置（延伸阅读：vol7 `02-compiler-options.md` 有完整指南）
- inline 关键字真实语义：ODR 豁免不是"请内联"，编译器内联启发式（函数体大小、调用频率、上下文）
- 去虚化（devirtualization）：final、已知动态类型、LTO 下全程序分析
- constexpr/consteval 优化：编译期计算 vs 运行期（延伸阅读：vol2 `04-compile-time-practice.md`）
- LTO 原理与实战：中间表示合并 → 全程序优化、对 inline 的影响、CMake 启用、编译时间代价
- PGO（Profile-Guided Optimization）：采样 → 反馈 → 重编译流程、实测对比

**篇幅**：3000-3500 字 | **难度**：intermediate | **cpp_standard**：[11, 14, 17, 20]

**代码示例**：同一虚函数调用在 -O0/-O2/-O2+LTO 下的汇编对比

**现有资产**：改写 `02-inline-and-compiler-optimization.md`（77 行 → 完整教程）；接入 `code/examples/vol34567/14_compiler_options.cpp`

**参考资料**：

- Johnny's Software Lab — LTO: https://johnnysswlab.com/link-time-optimizations-new-way-to-do-compiler-optimizations/
- Agner Fog — Optimizing Software in C++: https://www.agner.org/optimize/optimizing_cpp.pdf
- LLVM LTO Design: https://llvm.org/docs/LinkTimeOptimization.html
- Nicolò Valigi — LTO and PGO: https://nicolovaligi.com/talks/link-time-optimization-profile-guided-optimization/
- GCC LTO blog series: http://hubicka.blogspot.com/2014/04/linktime-optimization-in-gcc-1-brief.html

#### 04 — `04-reading-assembly-compiler-explorer.md` — 汇编阅读与 Compiler Explorer

**定位**：工具

**内容要点**：

- 为什么 C++ 开发者需要读汇编：验证优化、理解编译器行为、debug 性能回归
- x86_64 汇编最小知识集：寄存器（rax/rdi/rsi...）、常用指令（mov/add/cmp/jmp/call/ret/lea）、System V ABI 调用约定基础
- AT&T vs Intel 语法
- Compiler Explorer 使用：基本操作、多编译器对比、执行模式、颜色对应
- 常见 C++ 结构的汇编模式：if/else → cmp+jcc、for → cmp+jl 回跳、函数调用 → call+ret、虚函数 → vtable 间接调用、模板实例化 → 多份代码
- 如何判断优化是否生效：内联了没？向量化了没？死代码消除了没？
- 项目 `OnlineCompilerDemo` 组件使用方法

**篇幅**：3000-3500 字 | **难度**：intermediate | **cpp_standard**：[17, 20]

**代码示例**：5 个小例子（if/else/for/class/template），每个 -O0 vs -O2

**现有资产**：14 个 `code/examples/compiler_explorer/` 文件可直接嵌入

**参考资料**：

- The Faker's Guide to Reading x86 Assembly: https://www.timdbg.com/posts/fakers-guide-to-assembly/
- Learning to Read x86 Assembly — Pat Shaughnessy: https://patshaughnessy.net/2016/11/26/learning-to-read-x86-assembly-language
- Compiler Explorer: https://godbolt.org/
- Matt Godbolt — Features You Never Knew (CppCon 2025): https://www.youtube.com/watch?v=3W0vE_VKokY
- How Compiler Explorer Works in 2025: https://xania.org/202506/how-compiler-explorer-works
- UVA — Guide to x86 Assembly: https://www.cs.virginia.edu/~evans/cs216/guides/x86.html

---

### Chapter 3：Profiling 实战（2 篇）

#### 05 — `05-profiling-perf-flamegraph.md` — Linux perf 与火焰图

**定位**：核心工具

**内容要点**：

- 性能分析基本分类：采样（sampling）vs 插桩（instrumentation）
- `perf stat`：硬件事件计数（cycles, instructions, cache-misses, branch-misses）
- `perf record` / `perf report`：采样分析热点函数
- 火焰图：原理、生成流程（perf script | stackcollapse | flamegraph.pl）、如何读（x=采样数, y=调用栈深度, 颜色无含义）
- `perf top`：实时监控
- Off-CPU 分析简述：程序"慢"但不吃 CPU 的时候
- 实战案例：分析一个 C++ 程序，定位到 cache miss 还是分支预测失败

**篇幅**：3000-3500 字 | **难度**：intermediate | **cpp_standard**：[17] | **平台**：Linux (host)

**代码示例**：一个故意写慢的 C++ 程序（cache-unfriendly + 分支不可预测），用 perf 定位瓶颈

**参考资料**：

- Brendan Gregg — Linux perf Examples: https://www.brendangregg.com/perf.html
- Brendan Gregg — CPU Flame Graphs: https://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html
- Brendan Gregg — perf_events Tutorial: https://github.com/brendangregg/Misc/blob/master/perf_events/perf1.md
- Julia Evans — Profiling with perf: https://jvns.ca/blog/2014/05/13/profiling-with-perf/
- GitLab — How to Use Flamegraphs: https://gitlab.com/gitlab-com/runbooks/-/blob/v2.220.2/docs/tutorials/how_to_use_flamegraphs_for_perf_profiling.md

#### 06 — `06-valgrind-memory-profiling.md` — Valgrind 工具套件与内存分析

**定位**：工具

**内容要点**：

- Valgrind 概述：虚拟 CPU 架构，慢但精确
- Callgrind：调用图 + 缓存模拟 + 分支预测模拟，KCachegrind 可视化
- Cachegrind：缓存命中率分析（L1 miss / LL miss）
- Memcheck / DHAT / Massif：简要提及
- perf vs Valgrind 对比：什么时候用哪个
- 限制：只支持 Linux/x86、开销大（10-50x）、不支持 ARM

**篇幅**：2500-3000 字 | **难度**：intermediate | **cpp_standard**：[17] | **平台**：Linux (host)

**代码示例**：同一程序分别用 perf 和 Callgrind 分析，对比结果

**参考资料**：

- Valgrind — Callgrind Manual: https://valgrind.org/docs/manual/cl-manual.html
- Callgrind Exhaustive Guide (Gist): https://gist.github.com/MangaD/3cc4144ea99ab2ac725fb3c2b9467858
- Stanford CS107 — Valgrind Callgrind: https://web.stanford.edu/class/archive/cs/cs107/cs107.1266/resources/callgrind.html
- Stack Overflow — How to Profile C++ on Linux: https://stackoverflow.com/questions/375913/how-do-i-profile-c-code-running-on-linux

---

### Chapter 4：CPU 与内存优化（3 篇）

#### 07 — `07-cache-friendly-data-layout.md` — Cache 友好的数据布局

**定位**：核心（自包含 cache 基础 + 数据布局优化）

**内容要点**：

- **Cache 基础（自包含）**：
  - CPU cache 层次（L1/L2/L3）、cache line（通常 64 字节）
  - 空间局部性 / 时间局部性：为什么顺序访问比随机访问快
  - cache hit vs cache miss 的性能差异（数量级）
  - 预取简述：编译器自动预取、`__builtin_prefetch`
- **数据布局优化**：
  - AoS vs SoA：什么时候 SoA 更快（批量处理同字段）、什么时候 AoS 更好（随机访问单对象）
  - 热冷数据分离：频繁访问字段放前面、冷数据移到子结构
  - `std::vector` vs `std::list` 的 cache 影响：指针追踪 vs 连续内存线性遍历
  - 实战：粒子系统 / ECS 场景的数据布局优化
- **延伸阅读**：并发场景的 false sharing 与 cache → vol5 `ch00/03-cpu-cache-and-os-threads.md`

**篇幅**：3500-4000 字 | **难度**：intermediate | **cpp_standard**：[17, 20]

**代码示例**：AoS vs SoA 遍历 benchmark；热冷分离前后 perf stat 对比

**现有资产**：接入 `struct_alignment_host.cpp`

**参考资料**：

- CppCon 2025 — Cache-Friendly C++ (Jonathan Müller): https://www.youtube.com/watch?v=g_X5g3xw43Q
- Johnny's Software Lab — Memory Layout: https://johnnysswlab.com/
- Johnny's Software Lab — Memory Subsystem Optimizations: https://johnnysswlab.com/optimizations-for-memory-subsystem/
- Algorithmica — Alignment and Packing: https://en.algorithmica.org/hpc/cpu-cache/alignment/
- CPU Cache Lines Interactive Simulator: https://www.abhik.ai/concepts/systems/cpu-cache-lines
- Fedor Pikus, *The Art of Writing Efficient Programs*, memory chapters

#### 08 — `08-memory-alignment-struct-packing.md` — 内存对齐与结构体布局

**定位**：核心

**内容要点**：

- `alignof` / `sizeof`：自然对齐要求、编译器自动 padding
- 结构体布局规则：成员按声明顺序排列、尾部 padding
- 优化方法：按对齐要求降序排列成员 → 减少 padding
- `alignas`：显式控制对齐（64 字节 cache line 对齐、SIMD 16/32 字节对齐）
- `#pragma pack` / `__attribute__((packed))`：什么时候用（网络协议、文件格式）、性能代价
- `std::hardware_destructive_interference_size`（C++17）：平台无关的 cache line 大小
- `[[no_unique_address]]`（C++20）：空成员零开销（延伸阅读：EBO → vol8 embedded）

**篇幅**：2500-3000 字 | **难度**：intermediate | **cpp_standard**：[11, 17, 20]

**代码示例**：同一 struct 不同成员顺序的 sizeof 对比；`alignas(64)` 对 cache 的影响

**现有资产**：接入 `struct_alignment_host.cpp`、`ebo_arm.cpp`/`ebo_host.cpp`

**参考资料**：

- The Lost Art of Structure Packing — Eric S. Raymond: http://www.catb.org/esr/structure-packing/
- C++ Weekly Ep 410 — Padding and Alignment: https://www.youtube.com/watch?v=E0QhZ6tNoRg
- Algorithmica — Alignment and Packing: https://en.algorithmica.org/hpc/cpu-cache/alignment/
- GeeksforGeeks — Structure Alignment, Padding: https://www.geeksforgeeks.org/c/structure-member-alignment-padding-and-data-packing/

#### 09 — `09-branch-prediction-patterns.md` — 分支预测与 branchless 编程

**定位**：核心

**内容要点**：

- 为什么分支预测影响性能：流水线、预测失败代价（15-20 周期）
- 可预测分支 vs 不可预测分支：排序数据 vs 随机数据的 if 条件性能差异
- C++20 `[[likely]]` / `[[unlikely]]`：语义（影响代码布局不是硬件预测器）、适用场景（错误处理）
- `__builtin_expect`：`[[likely]]` 的底层实现
- 查表替代分支：switch/if-else → 数组索引，什么时候值得
- Branchless 技巧：条件传送（cmov，编译器自动生成）、位运算 `(cond) & mask`
- 实测对比 + 注意：不要过度优化

**篇幅**：2500-3000 字 | **难度**：intermediate | **cpp_standard**：[17, 20]

**代码示例**：排序 vs 未排序 if 条件遍历 benchmark；查表 vs switch benchmark

**参考资料**：

- Johnny's Software Lab — How Branches Influence Performance: https://johnnysswlab.com/how-branches-influence-the-performance-of-your-code-and-what-can-you-do-about-it/
- Branch Prediction Definitive Guide — John Farrier: https://johnfarrier.com/branch-prediction-the-definitive-guide-for-high-performance-c/
- Core C++ 2022 — Branch Prediction Talk: https://www.youtube.com/watch?v=T84swS6DCRo
- Aussie AI — Ch.4 Branch Prediction: https://www.aussieai.com/book/memory-book-4-branch-predicction
- P0479R0 — likely/unlikely proposal: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0479r0.html

---

### Chapter 5：SIMD 与向量化（2 篇）

#### 10 — `10-simd-concepts-auto-vectorization.md` — SIMD 基础与自动向量化

**定位**：核心

**内容要点**：

- SIMD 原理：一条指令处理多个数据（SISD → SIMD）
- x86 SIMD 演进：MMX → SSE/SSE2(128-bit) → AVX/AVX2(256-bit) → AVX-512(512-bit)
- ARM NEON(128-bit) / SVE 简要提及
- 自动向量化条件：循环无数据依赖、连续内存访问、已知迭代次数
- 阻碍自动向量化的原因：指针别名（`__restrict`）、循环内函数调用、条件分支、非连续访问
- 编译器标志：`-O3 -march=native`、`-fopt-info-vec`（向量化报告）
- `#pragma omp simd` 手动向量化提示
- 验证向量化：Compiler Explorer 看 xmm/ymm/zmm 寄存器
- **C++26 `std::simd` 展望**：P1928 已进入 C++26，便携式 SIMD 抽象，编译到 AVX2/AVX-512/NEON/SVE（编译器支持尚早，标记 needs standard-status verification）

**篇幅**：3000-3500 字 | **难度**：intermediate | **cpp_standard**：[17, 20, 26]

**代码示例**：简单循环 → 自动向量化 vs 阻止向量化 → 汇编和性能对比

**参考资料**：

- Intel — Vectorization Guide (PDF): https://www.intel.com/content/dam/develop/external/us/en/documents/31848-compilerautovectorizationguide.pdf
- Red Hat — Vectorization in GCC: https://developers.redhat.com/articles/2023/12/08/vectorization-optimization-gcc
- LLVM — Auto-Vectorization: https://llvm.org/docs/Vectorizers.html
- C++26 std::simd (P1928): https://lucisqr.substack.com/p/c26-shipped-a-simd-library-nobody
- VcDevel/std-simd reference impl: https://github.com/VcDevel/std-simd
- C++Now 2025 — SIMD Wrappers to SIMD Ranges: https://www.youtube.com/watch?v=CRe20RdU_5Q

#### 11 — `11-avx-avx2-practical.md` — AVX/AVX2 intrinsics 实战

**定位**：实战（改写现有 `avx-avx2-deep-dive.md`）

**内容要点**：

- AVX 寄存器：`ymm0`-`ymm15`（256-bit）、VEX 三操作数编码
- 浮点 intrinsics：`_mm256_add_ps`、`_mm256_mul_ps`、`_mm256_load_ps` / `_mm256_store_ps`
- AVX2 整数 intrinsics：`_mm256_add_epi32`、`_mm256_mullo_epi16`、gather `_mm256_i32gather_ps`
- 对齐要求：`_mm256_load_ps`（32 字节对齐）vs `_mm256_loadu_ps`（不对齐）
- 编译：GCC/Clang `-mavx2`、MSVC `/arch:AVX2`
- 实战案例：向量点积 / 矩阵乘法（选 1-2 个）
- 性能陷阱：AVX-SSE 转换惩罚、AVX-512 频率降频
- 延伸阅读：ARM NEON → vol8 嵌入式

**篇幅**：3000-3500 字 | **难度**：advanced | **cpp_standard**：[17]

**代码示例**：向量点积标量版 vs AVX2 版，带 benchmark

**现有资产**：改写 `avx-avx2-deep-dive.md`（167 行笔记 → 规范化教程 + 独立代码文件）

**参考资料**：

- Intel Intrinsics Guide: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/
- Agner Fog — Optimizing Software in C++, SIMD section: https://www.agner.org/optimize/optimizing_cpp.pdf

---

### Appendix（2 篇）

#### 12 — `12-performance-size-evaluation.md` — 性能与代码大小综合评估

**定位**：参考（改写现有 `06-evaluating-performance-and-size.md`）

**内容要点**：

- 保留现有文章的 C vs C++ 核心实测（GPIO/环形缓冲区/状态机对比）
- 补充代码膨胀来源：模板实例化、异常/RTTI、内联展开
- 模板膨胀量化：`nm` / `size` / `bloaty` 分析二进制大小
- 缓解手段：显式实例化（`extern template`）、thin template、LTO 去重、`-fvisibility=hidden`
- 嵌入式视角：`-Os`、`-flto`、strip、`-ffunction-sections` + `--gc-sections`
- **C++26 饱和算术简述**：`std::add_sat` 等，信号处理场景（编译器支持尚早）

**篇幅**：3000-3500 字 | **难度**：intermediate | **cpp_standard**：[11, 14, 17, 20, 26]

**现有资产**：改写 `06-evaluating-performance-and-size.md`（保留核心实测，补充代码膨胀分析）

**参考资料**：

- Sandor Dargo — Binary Sizes and Templates: https://www.sandordargo.com/blog/2023/04/05/binary-size-and-templates
- C++ on Sea 2024 — How to Keep C++ Binaries Small: https://www.youtube.com/watch?v=7QNtiH5wTAs
- ThePhD — Binary Size Reductions for sol3: https://thephd.dev/sol3-compile-times-binary-sizes
- Where C++ Code Bloat Occurs (ykiko): https://www.ykiko.me/en/articles/686296374/

#### 13 — `13-code-examples-guide.md` — 卷六代码示例索引与使用指南

**定位**：导航参考

**内容要点**：

- 全部代码示例的索引表：文件路径 → 对应文章 → Compiler Explorer 链接
- 如何在本地构建和运行卷六代码示例
- `OnlineCompilerDemo` 组件嵌入方法（供贡献者参考）
- Compiler Explorer 示例的 ARM 版本使用说明

**篇幅**：1000-1500 字 | **难度**：beginner

**现有资产**：梳理现有 14 个 `compiler_explorer` 文件 + `vol34567` 文件，建立索引

---

## 编写顺序

| 批次 | 文章 | 理由 |
|------|------|------|
| Batch 1 | 00 → 01 → 02 | 测量基础，后续文章均依赖 benchmark 能力 |
| Batch 2 | 03 → 04 | 编译器理解是性能优化的前提 |
| Batch 3 | 07 → 08 → 09 | CPU/内存优化是卷六最核心内容 |
| Batch 4 | 05 → 06 | 有了优化知识后更容易理解 profiling 场景 |
| Batch 5 | 10 → 11 | SIMD 需要前面全部知识铺垫 |
| Batch 6 | 12 → 13 | 附录和索引 |

## 现有资产改写映射

| 现有文章 | 行数 | 去向 | 处理 |
|----------|------|------|------|
| `02-inline-and-compiler-optimization.md` | 77 | → `03-compiler-optimization-deep-dive.md` | 保留核心内容，大幅扩充 LTO/PGO/去虚化 |
| `06-evaluating-performance-and-size.md` | 1290 | → `12-performance-size-evaluation.md` | 保留 C vs C++ 实测，补充代码膨胀分析 |
| `avx-avx2-deep-dive.md` | 167 | → `11-avx-avx2-practical.md` | 规范化改写 + 提取独立代码文件 |
| `index.md` | 25 | → 重写 | 建立卷首页导航 |
| 英文翻译 4 文件 | — | 同步更新 | 中文内容稳定后统一更新 |

## 交叉卷链接（仅"延伸阅读"，非前置必读）

| 本卷文章 | 延伸方向 | 目标 |
|----------|----------|------|
| 02 benchmark tools | 并发场景 benchmark | vol5 `ch08/02-concurrency-benchmarks.md` |
| 03 compiler optimization | constexpr 实战 | vol2 `ch02-constexpr/04-compile-time-practice.md` |
| 03 compiler optimization | 编译器选项完整指南 | vol7 `02-compiler-options.md` |
| 04 assembly | RVO/NRVO 汇编分析 | vol2 `ch00-move-semantics/03-rvo-nrvo.md` |
| 07 data layout | 并发 false sharing | vol5 `ch00/03-cpu-cache-and-os-threads.md` |
| 08 alignment | EBO / 零开销抽象 | vol8 embedded |
| 11 AVX intrinsics | ARM NEON | vol8 embedded |

## 通用参考资料

| 资料 | 类型 | 覆盖范围 |
|------|------|----------|
| Agner Fog — Optimizing Software in C++: https://www.agner.org/optimize/optimizing_cpp.pdf | 在线 PDF | 编译器优化、CPU 微架构、指令选择 |
| Fedor Pikus, *The Art of Writing Efficient Programs*, Packt | 书籍 | CPU/内存/编译器/并发 |
| Kurt Guntheroth, *Optimized C++*, O'Reilly | 书籍 | 测量、字符串、内存、算法 |
| AwesomePerfCpp: https://github.com/fenbf/AwesomePerfCpp | 资源索引 | 工具、文章、演讲、库 |
| Johnny's Software Lab: https://johnnysswlab.com/ | 博客 | 内存子系统、数据布局、LTO |
| Compiler Explorer: https://godbolt.org/ | 在线工具 | 全卷贯穿 |
| Brendan Gregg — perf/FlameGraph: https://www.brendangregg.com/perf.html | 参考 | Profiling 章节 |

## Old TODO Merge

- `205-vol6-performance-outline.md`（旧 8 章 18-22 篇大纲）→ 本文件替代
- `036-dsp-basics.md`（嵌入式 DSP）→ 迁移到 vol8 TODO，不进入卷六

## Acceptance

- [ ] 13 篇文章全部落地，每篇有代码示例和 benchmark
- [ ] 现有 3 篇文章改写完成，旧文件清理
- [ ] index.md 重写为完整卷首页
- [ ] Compiler Explorer 示例全部被索引到卷六文章
- [ ] 每篇文章自包含，读者不需要跳到其他卷即可理解
- [ ] C++26 内容标记 needs standard-status verification
