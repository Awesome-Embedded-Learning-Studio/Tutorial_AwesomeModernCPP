---
title: 'An In-Depth Introduction to the AVX Instruction Set Family: Domains, Significance,
  and Basic Usage and Examples of AVX / AVX2'
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 2
order: 3
---
# An In-Depth Look at the AVX Instruction Set Family: Domains, Significance, and Basic Usage & Examples of AVX / AVX2

## Preface

As a side note, I don't specialize in this field. The topic came up in a conversation, and I realized how unfamiliar this domain was to me, so I decided to put together some notes to talk it through. Because of this, I can't guarantee that the information I've gathered is 100% accurate. Readers should exercise their own judgment.

------

## Why Care About AVX? — The Domain and Significance of Vectorized Computing

I became interested in this partly because of high-definition video rendering (a project I worked on involved this area, which is how I learned this domain even existed). After all, in modern computing tasks—whether it's HD video rendering, AI model training, or complex scientific simulations—data volumes are growing exponentially. The traditional **SISD (Single Instruction, Single Data)** processing model, where each operation handles only one data item, has gradually become a bottleneck for computational efficiency.

To break through this bottleneck, the concept of **SIMD (Single Instruction, Multiple Data)** emerged. It allows the CPU to process a set of data with a single instruction. This "batch processing" technique is known as **vectorization**. **AVX (Advanced Vector Extensions)** is one of the most important vectorization instruction sets in the x86 architecture.

We naturally have to ask: how exactly does it optimize things? Inside a CPU, **registers** act as the "temporary staging areas" where data must reside before participating in calculations. In the early SSE era, the width of this staging area was 128 bits. If we were processing "single-precision floating-point numbers" (each taking up 32 bits), we could only fit four of them side-by-side for calculation in a single cycle.

AVX technology doubled this staging area's width to **256 bits**. This means a qualitative change occurred in the CPU's hardware channels: it can now吞吐吞吐 and process **eight** single-precision floating-point numbers, or **four** larger, more precise double-precision floating-point numbers in the exact same instant. This doubling of bit width essentially builds a wider highway for data flow, doubling the computational "appetite" of the processor.

In traditional computing instructions, the CPU's operational logic is usually quite "coarse." For example, to perform an A + B operation, the result must forcibly overwrite the original data A. This design is known as the "two-operand" mode, which is somewhat destructive—if you need the original data A later, you must spend extra time backing it up somewhere else before performing the calculation.

AVX introduced the more advanced **VEX encoding**, enabling a "three-operand" mode. It allows programs to issue more fine-grained instructions: "take data A, take data B, and store the result in C." This way, the original data A and B are both perfectly preserved. This evolution eliminates a massive amount of redundant work, reducing the overhead of repeatedly moving and backing up data in memory, making the overall program logic much lighter and more efficient.

AVX brings more than just minor speed tweaks; it represents a fundamental evolution in processing logic. It transforms "serial" tasks that originally had to execute one by one into batched "vectorized" tasks. In ideal compute-intensive scenarios (such as scientific model calculations or high-quality rendering), this transformation can yield a manifold leap in CPU work efficiency.

This progress means that when facing massive numerical operations, the CPU can unleash its arithmetic throughput to a tremendous degree. Clock cycles that previously required repetitive spinning can now be completed in one powerful "vectorized strike," achieving a leap in performance without solely relying on increasing the clock frequency.

### AVX2: A Leap in Integer Operations and Flexibility

**AVX2**, released in 2013, further refined this system. If AVX solved the problem of "computing fast," then AVX2 solved the problem of "computing broadly":

1. **Comprehensive Integer Support**: AVX2 extended the existing 256-bit parallel computing capabilities from floating-point numbers to the **integer** domain. This is crucial for scenarios relying on integer operations, such as data compression, image processing, and database searches.
2. **Non-Contiguous Data Processing (Gather/Permute)**: In practical applications, data is often scattered across memory. AVX2 introduced "Gather" instructions, allowing the CPU to fetch data in bulk from non-contiguous memory addresses, significantly enhancing the ability to handle complex data structures.

------

## Using AVX / AVX2 in Code

#### Compiler Flags

- GCC/Clang:
  - AVX: `-mavx`
  - AVX2: `-mavx2`
  - FMA (if needed): `-mfma`
  - For optimal targeting of the current CPU: `-march=native` (but this generates code dependent on the current CPU)
- MSVC:
  - `/arch:AVX` or `/arch:AVX2` (depending on the VS version)
- Recommended practice: We can generate dedicated files with AVX/AVX2 at compile time, or compile multiple versions and select at runtime (runtime dispatch).

#### Intrinsics (Example APIs)

- Floating-point (AVX): `__m256` (float32 ×8) and `__m256d` (double ×4)
  - load/store: `_mm256_loadu_ps`, `_mm256_storeu_ps` (unaligned)
  - add/mul: `_mm256_add_ps`, `_mm256_mul_ps`
  - fused: `_mm256_fmadd_ps` (requires FMA)
- Integer (AVX2): `__m256i`
  - add: `_mm256_add_epi32`
  - gather: `_mm256_i32gather_epi32` (gather from int indices)
  - shift/and/or: `_mm256_slli_epi32`, `_mm256_and_si256`, etc.

------

## Basic Examples (C/C++ Intrinsics)

The small examples below give a fairly intuitive feel for AVX.

#### Floating-Point Array Addition (AVX)

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

Compilation:

```cpp

g++ -O3 -mavx -std=c++17 avx_samples.cpp -o avx_samples

```

#### Floating-Point Dot Product (AVX + Reduction)

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

#### Trying It Out: AVX2 Integer Parallel Addition and Gather Example

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

Compilation:

```cpp

g++ -O3 -mavx2 -std=c++17 avx_samples.cpp -o avx2_samples

```
