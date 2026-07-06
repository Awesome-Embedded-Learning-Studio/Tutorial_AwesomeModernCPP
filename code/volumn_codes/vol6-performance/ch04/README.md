# vol6 ch04 · 按瓶颈部位优化 — 代码示例

对应文章:`documents/vol6-performance/ch04-tuning-by-bottleneck/`

七个程序,对应 ch04 的四桶(TMA)对策。每个程序都是**本机实测**出来的真实数字,文章里贴的结果都能复现。**关心比例和趋势,绝对数字随 CPU/编译器/环境变。**

## 构建

```bash
# 单独编译(推荐)
g++ -O2 -std=c++17 backend_memory.cpp   -o backend_memory  && taskset -c 0 ./backend_memory
g++ -O2 -std=c++17 loop_opt.cpp         -o loop_opt        && taskset -c 0 ./loop_opt
g++ -O2 -std=c++17 arithmetic_cost.cpp  -o arithmetic_cost && taskset -c 0 ./arithmetic_cost
g++ -O2 -std=c++17 virtual_devirt.cpp   -o virtual_devirt  && taskset -c 0 ./virtual_devirt
g++ -O2 -std=c++17 branchless.cpp       -o branchless      && taskset -c 0 ./branchless

# SIMD:标量基线 vs 手写 AVX2(⚠️ 必须显式开 -mavx2 -mfma)
g++ -O2            simd.cpp -o simd_scalar && taskset -c 0 ./simd_scalar
g++ -O3 -mavx2 -mfma simd.cpp -o simd_avx2 && taskset -c 0 ./simd_avx2

# PGO:三阶段流程(见 pgo.sh,不是单次编译)
bash pgo.sh

# 或用 CMake 一次构建全部(CMakeLists 已配好各 target 的 flag)
cmake -B build && cmake --build build
```

## 程序对照(每个对应一桶/一篇)

| 程序 | 文章 | 桶 | 核心数字 |
|---|---|---|---|
| `backend_memory` | 04-01 | Backend Memory | AoS vs SoA:**9.79×** |
| `loop_opt` | 04-02 | Backend Core | code motion/mem-ref 微弱(编译器已做);多累加器见 ch02-03 的 2.92× |
| `arithmetic_cost` | 04-03 | Backend Core | 除法/乘法 **5.0×**;switch vs if-else 0.90×(switch 不总赢) |
| `virtual_devirt` | 04-04/06-01 | Backend Core/Frontend | virtual/CRTP **2.5×**;final 没自动去虚化 |
| `simd` | 04-05 | Backend Core | 手写 AVX2 vs 标量 **~20×**;FP 归约不自动向量化 |
| `branchless` | 04-06 | Bad Speculation | if≈cmov(1.07×,编译器已 branchless);真分支惩罚看 ch02-03 的 4.2× |
| `pgo_demo` | 04-07/07-02 | Frontend | PGO 对微基准**无收益**(~3.7 vs ~3.9 ms)——诚实 null 结果 |

## 几个诚实的「翻车/反直觉」结果(文章里都展开了)

- **switch 不总比 if-else 快**:case 少且均匀时,if-else 可能略快(间接跳转预测器)。
- **final 没自动触发去虚化**:去虚化要看编译器能否证明类型,不是 `final` 一标就行。
- **FP 归约默认不被自动向量化**(结合律),需 `-ffast-math` 或手写多累加器。
- **branchless 在 -O2 下和 if 一样快**:编译器已转 cmov,看汇编确认是不是真分支。
- **PGO 对微基准无收益**(那个一度出现的 4× 是仪器化开销,不是 PGO)。

这些「不漂亮」的结果是 ch04 的教学价值所在:**现代编译器 + 硬件替你做了大半,性能优化不是堆 trick,是测量驱动的精准手术**。
