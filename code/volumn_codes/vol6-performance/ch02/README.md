# vol6 ch02 · CPU 微架构与存储层次 — 代码示例

对应文章:`documents/vol6-performance/ch02-cpu-microarchitecture/`

四个程序,分别对应 ch02 的四篇文章,用本机实测数据佐证每一篇的核心结论。所有数字随 CPU / 编译器 / 运行环境(尤其是虚拟机/容器)变化,**关心比例和趋势,不要把某个绝对数当普适结论**。

## 构建

```bash
# 单独编译(最快,推荐一篇一篇跑)
g++ -O2 -std=c++17 memory_mountain.cpp -o memory_mountain && taskset -c 0 ./memory_mountain
g++ -O2 -std=c++17 cacheline_locality.cpp -o cacheline_locality && taskset -c 0 ./cacheline_locality
g++ -O2 -std=c++17 tlb_hugepage.cpp -o tlb_hugepage && taskset -c 0 ./tlb_hugepage

# ⚠️ pipeline_branch_ilp 必须带反优化 flag,否则 demo 失效(见下)
g++ -O2 -std=c++17 -fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion \
    pipeline_branch_ilp.cpp -o pipeline_branch_ilp && taskset -c 0 ./pipeline_branch_ilp

# 或用 CMake 一次构建全部(CMakeLists 已为 pipeline 配好 flag)
cmake -B build && cmake --build build
```

> `taskset -c 0` 把进程绑到 0 号核,避免核间迁移把 cache 弄冷、给测量加噪声。这是 vol6 ch01 *测量方法论* 的标准动作。

## 程序对照

### memory_mountain.cpp —— 02-01 存储层次

两个实验:(A)经典 CSAPP memory mountain,读吞吐随 size × stride 变化;(B)指针追逐随机读延迟,画出 L1/L2/L3/DRAM 的延迟阶梯。要点:L1 ~1 ns、DRAM ~120 ns,差 100 倍。

### cacheline_locality.cpp —— 02-02 缓存行与局部性

(A)步长扫描:工作集固定,扫 stride,吞吐在 stride=64B 处断崖,反证 cacheline=64 字节;(B)行优先 vs 列优先 2D 遍历,~6 倍差距。

### pipeline_branch_ilp.cpp —— 02-03 流水线 / ILP / 分支预测

(A)分支预测:已排序 vs 打乱数组的条件累加,~4 倍差距(预测失败的冲刷代价);(B)ILP:单累加器(长依赖链)vs 4 累加器(并行链),~3 倍差距。

**这个程序必须用 `-fno-tree-vectorize -fno-tree-slp-vectorize -fno-if-conversion` 编译。** 默认 `-O2` 下 GCC 会把 `if` 条件累加自动向量化成 SIMD / 转成 `cmov`(分支消失,打乱=排序),并把单累加器点积当循环不变量整体消除(ILP demo 失真)。这组 flag 是文章论证成立的前提——也是文章里那个「你看到的分支开销取决于编译器把代码编译成了什么」教学点的直接证据。

### tlb_hugepage.cpp —— 02-04 TLB / huge page

同 256 MB 工作集,4 KB 页 vs `madvise(MADV_HUGEPAGE)` 请求 2 MB 大页,指针追逐比延迟。**注意**:本机(WSL2)实测两者无差别(`AnonHugePages: 0`,内核没实际兑现 THP)——这是个诚实的否定结果,演示「你以为开了的优化可能根本没生效」。换裸机 + 预分配 hugetlb 池或全局 THP=always,对 TLB 受限负载能测出提升。

## 怎么读结果

- **比例 > 绝对数**:L1/DRAM 的 100 倍、行/列的 6 倍、ILP 的 3 倍、分支的 4 倍——这些比例是硬件机制决定的,稳。绝对 ns / GB/s 随环境抖动。
- **WSL2 / 虚拟机噪声**:频率被宿主管、TLB/THP 行为可能和裸机不同、`perf` 可能没装。看到不合常理的数字,先怀疑环境,再怀疑结论。
- **汇编是最终裁判**:尤其 pipeline 那个程序,`-O2 -S` 看 `dot1` 是不是真单链、`sum_gt128` 是不是真有 `jcc`,能直接验证你测的是不是你想测的。
