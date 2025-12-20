# AVX 指令集系列深度介绍：领域、意义、以及 AVX / AVX2 的基本用法与样例

## 前言

PS下，笔者不是专门做这一块的，是聊天的时候聊到这里，发现这个领域对我而言相当的陌生，打算好好的记录个笔记唠下，所以我没办法完全保证我搜集得到的内容百分百准确。看官自行评判。

------

## 为什么要关心 AVX？—— 向量化计算的领域与意义

我关心这个，有的时候是高清视频渲染（嗯，笔者参与的项目有涉及到这块，所以才知晓还有这个领域的），毕竟在现代计算任务中，无论是高清视频渲染、人工智能模型训练，还是复杂的科学仿真，数据量都在呈指数级增长。传统的 **SISD（单指令单数据）** 处理模式——即每次操作只处理一个数据项——已逐渐成为计算效率的瓶颈。

为了突破这一瓶颈，**SIMD（单指令多数据）** 概念应运而生。它允许 CPU 用一条指令同时处理一组数据，这种“成批处理”的技术被称为**向量化**。**AVX（Advanced Vector Extensions，高级向量扩展）** 正是 x86 架构中最重要的向量化指令集之一。

我们很自然的就要问了，那怎么优化的？在 CPU 内部，**寄存器**是数据参加运算前必须停留的“临时月台”。在早期的 SSE 技术时代，这个月台的宽度是 128 位。如果我们处理的是“单精度浮点数”（每个数据占 32 位），那么一个周期内只能并排排下 4 个数据进行计算。

而 AVX 技术将这个月台的宽度翻倍到了 **256 位**。这意味着 CPU 的硬件通道发生了质变：现在它可以在同一个瞬间，同时吞吐并处理 **8 个**单精度浮点数，或者 **4 个**更大、更精确的双精度浮点数。这种位宽的翻倍，本质上是为数据流动修建了更宽的高速公路，让计算的“胃口”增大了一倍。

在传统的计算指令中，CPU 的操作逻辑通常比较“粗糙”。比如要执行 A + B 的操作，计算结果必须强制覆盖掉原来的数据 A。这种设计被称为“两操作数”模式，它具有一定的破坏性——如果你后续还需要用到原始数据 A，就必须在计算前额外花时间把它备份到另一个地方。

AVX 引入了更先进的 **VEX 编码**，实现了“三操作数”模式。它允许程序下达更精细的指令：“取数据 A，取数据 B，计算结果存入 C”。这样一来，原始数据 A 和 B 都被完好地保留了下来。这种进化精简了大量的重复劳动，减少了数据在内存中反复搬运、备份的开销，使得整个程序的逻辑变得更加轻盈和高效。

AVX 带来的不仅仅是速度的微调，而是处理逻辑的底层进化。它将原本需要一个接一个排队执行的“串行”任务，转化为成批进行的“向量化”任务。在理想的计算密集型场景下（如科学模型计算或高画质渲染），这种转化能让 CPU 的工作效率产生数倍的飞跃。

这种进步意味着，在面对海量数字运算时，CPU 能够极大程度地释放其算术吞吐量。以前需要反复旋转的时钟周期，现在通过一次强有力的“向量化打击”即可完成，从而在不单纯依赖提高主频的情况下，实现了性能的跨越式提升。

### AVX2：整型运算与灵活性的飞跃

2013 年发布的 **AVX2** 进一步完善了这一体系。如果说 AVX 解决了“算得快”的问题，那么 AVX2 则解决了“算得广”的问题：

1. **全面整型化**：AVX2 将原有的 256 位并行计算能力从浮点数扩展到了**整数**领域。这对于数据压缩、图像处理以及数据库检索等依赖整数运算的场景至关重要。
2. **非连续数据处理（Gather/Permute）**：在实际应用中，数据往往零散地分布在内存中。AVX2 引入了“收集”（Gather）指令，允许 CPU 从不连续的内存地址批量抓取数据，显著增强了处理复杂数据结构的能力。

------

## 在代码中使用 AVX / AVX2

#### 编译器开关

- GCC/Clang:
  - AVX: `-mavx`
  - AVX2: `-mavx2`
  - FMA（若需要）: `-mfma`
  - 若希望对目标 CPU 最佳化：`-march=native`（但会生成依赖当前 CPU 的代码）
- MSVC:
  - `/arch:AVX` 或 `/arch:AVX2`（视 VS 版本）
- 推荐做法：编译时可以生成带 AVX/AVX2 的专门文件，或编译多版本并在运行时选择（runtime dispatch）。

#### Intrinsics（示例 API）

- 浮点（AVX）：`__m256`（float32 ×8）和 `__m256d`（double ×4）
  - load/store: `_mm256_loadu_ps`, `_mm256_storeu_ps` （unaligned）
  - add/mul: `_mm256_add_ps`, `_mm256_mul_ps`
  - fused: `_mm256_fmadd_ps`（需要 FMA）
- 整数（AVX2）：`__m256i`
  - add: `_mm256_add_epi32`
  - gather: `_mm256_i32gather_epi32`（从 int 索引收集）
  - shift/and/or: `_mm256_slli_epi32`, `_mm256_and_si256` 等

------

## 基本样例（C/C++ intrinsics）

下面这些小样例可以比较直观的体验下AVX。

#### 浮点数组相加（AVX）

```cpp
#include <immintrin.h>
#include <stddef.h>

void add_float_arrays_avx(const float* a, const float* b, float* out, size_t n) {
    size_t i = 0;
    const size_t stride = 8; // 8 floats per __m256
    for (; i + stride <= n; i += stride) {
        __m256 va = _mm256_loadu_ps(a + i); // unaligned load
        __m256 vb = _mm256_loadu_ps(b + i);
        __m256 vr = _mm256_add_ps(va, vb);
        _mm256_storeu_ps(out + i, vr);
    }
    // tail
    for (; i < n; ++i) out[i] = a[i] + b[i];
}
```

编译：

```
g++ -O3 -mavx -std=c++17 avx_samples.cpp -o avx_samples
```

#### 浮点点积（AVX + reduction）

```cpp
#include <immintrin.h>
#include <stddef.h>

float dot_product_avx(const float* a, const float* b, size_t n) {
    size_t i = 0;
    const size_t stride = 8;
    __m256 acc = _mm256_setzero_ps();
    for (; i + stride <= n; i += stride) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        __m256 prod = _mm256_mul_ps(va, vb);
        acc = _mm256_add_ps(acc, prod);
    }
    // horizontal sum of acc
    __attribute__((aligned(32))) float tmp[8];
    _mm256_store_ps(tmp, acc);
    float sum = tmp[0] + tmp[1] + tmp[2] + tmp[3] + tmp[4] + tmp[5] + tmp[6] + tmp[7];
    for (; i < n; ++i) sum += a[i] * b[i];
    return sum;
}
```

#### 试一下：AVX2：整型并行加法与 gather 示例

```cpp
#include <immintrin.h>
#include <stddef.h>

// add 8 32-bit ints in parallel
void add_int32_avx2(const int32_t* a, const int32_t* b, int32_t* out, size_t n) {
    size_t i = 0;
    const size_t stride = 8; // 8 x int32 in 256 bits
    for (; i + stride <= n; i += stride) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
        __m256i vr = _mm256_add_epi32(va, vb);
        _mm256_storeu_si256((__m256i*)(out + i), vr);
    }
    for (; i < n; ++i) out[i] = a[i] + b[i];
}

// gather example: gather int32_t at indices array idx from base pointer base
void gather_example(const int32_t* base, const int32_t* idx, int32_t* out) {
    __m256i vindex = _mm256_loadu_si256((const __m256i*)idx); // indices
    __m256i gathered = _mm256_i32gather_epi32(base, vindex, 4);
    _mm256_storeu_si256((__m256i*)out, gathered);
}
```

编译：

```
g++ -O3 -mavx2 -std=c++17 avx_samples.cpp -o avx2_samples
```

