---
chapter: 2
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 6
tags:
- cpp-modern
- host
- intermediate
title: 'In-Depth Introduction to the AVX Instruction Set Series: Scope, Significance,
  and Basic Usage with Examples for AVX and AVX2'
description: ''
translation:
  source: documents/vol6-performance/avx-avx2-deep-dive.md
  source_hash: d341ba0e6c0726e775647342afe12bd486dcbbc43b83486cbf4aa3b3bdd567ec
  translated_at: '2026-06-16T04:07:38.687428+00:00'
  engine: anthropic
  token_count: 1211
---
# In-Depth Introduction to the AVX Instruction Set Family: Scope, Significance, and Basic Usage with Examples for AVX/AVX2

## Preface

PS: I am not a specialist in this field. The topic came up during a chat, and I realized how unfamiliar this area was to me, so I decided to write a proper note to sort it out. Therefore, I cannot guarantee that the information I have gathered is 100% accurate. Reader discretion is advised.

------

## Why Care About AVX? — The Scope and Significance of Vectorized Computing

I care about this partly because of high-definition video rendering (yes, the projects I participate in involve this, which is how I became aware of this field). In modern computing tasks, whether it is HD video rendering, AI model training, or complex scientific simulations, data volumes are growing exponentially. The traditional **SISD (Single Instruction, Single Data)** processing model—where a single data item is processed per operation—has increasingly become a bottleneck for computational efficiency.

To break through this bottleneck, the concept of **SIMD (Single Instruction, Multiple Data)** emerged. It allows the CPU to process a set of data with a single instruction. This "batch processing" technique is known as **vectorization**. **AVX (Advanced Vector Extensions)** is one of the most important vector instruction sets in the x86 architecture.

We naturally ask, how is it optimized? Inside the CPU, **registers** are the "temporary platforms" where data must reside before participating in operations. In the era of early SSE technology, this platform was 128 bits wide. If we were processing "single-precision floating-point numbers" (32 bits per data item), only 4 data items could be arranged side-by-side for calculation in one cycle.

AVX technology doubles the width of this platform to **256 bits**. This means a qualitative change in the CPU's hardware channels: it can now ingest and process **8** single-precision floating-point numbers, or **4** larger, more precise double-precision floating-point numbers in the same instant. This doubling of bit width essentially builds a wider highway for data flow, doubling the computational "appetite."

In traditional computing instructions, the CPU's operation logic is usually quite "coarse." For example, to execute A + B, the calculation result must forcibly overwrite the original data A. This design is known as the "two-operand" mode, which is somewhat destructive—if you need the original data A later, you must spend extra time backing it up elsewhere before the calculation.

AVX introduces the more advanced **VEX encoding**, implementing a "three-operand" mode. It allows programs to issue more fine-grained instructions: "Take data A, take data B, store the result in C." This way, the original data A and B are preserved intact. This evolution streamlines a significant amount of repetitive labor, reducing the overhead of moving and backing up data in memory, making the entire program logic lighter and more efficient.

AVX brings not just a speed tweak, but an underlying evolution in processing logic. It transforms "serial" tasks that used to execute one by one into batch "vectorized" tasks. In ideal compute-intensive scenarios (like scientific modeling or high-quality rendering), this transformation can leapfrog CPU efficiency by several times.

This progress means that when facing massive numerical computations, the CPU can maximize its arithmetic throughput. Clock cycles that previously required repeated spinning can now be completed in one powerful "vectorized strike," achieving a leap in performance without solely relying on increasing the clock frequency.

### AVX2: The Leap in Integer Operations and Flexibility

Released in 2013, **AVX2** further refined this system. If AVX solved the problem of "calculating fast," then AVX2 solved the problem of "calculating broadly":

1. **Full Integer Support**: AVX2 extends the existing 256-bit parallel computing capability from floating-point numbers to the **integer** domain. This is crucial for scenarios relying on integer arithmetic, such as data compression, image processing, and database retrieval.
2. **Non-Contiguous Data Handling (Gather/Permute)**: In practical applications, data is often scattered in memory. AVX2 introduced "Gather" instructions, allowing the CPU to fetch data from non-contiguous memory addresses in batches, significantly enhancing the ability to handle complex data structures.

------

## Using AVX / AVX2 in Code

#### Compiler Switches

- GCC/Clang:
  - AVX: `-mavx`
  - AVX2: `-mavx2`
  - FMA (if needed): `-mfma`
  - To optimize for the target CPU: `-march=native` (but this generates code dependent on the current CPU)
- MSVC:
  - `/arch:AVX` or `/arch:AVX2` (depending on VS version)
- Recommended practice: You can generate dedicated files with AVX/AVX2 during compilation, or compile multiple versions and select at runtime (runtime dispatch).

#### Intrinsics (Example APIs)

- Floating Point (AVX): `__m256` (float32 ×8) and `__m256d` (double ×4)
  - load/store: `_mm256_load_ps`, `_mm256_loadu_ps` (unaligned)
  - add/mul: `_mm256_add_ps`, `_mm256_mul_ps`
  - fused: `_mm256_fmadd_ps` (requires FMA)
- Integer (AVX2): `__m256i`
  - add: `_mm256_add_epi32`
  - gather: `_mm256_i32gather_epi32` (gather from int indices)
  - shift/and/or: `_mm256_slli_epi32`, `_mm256_and_si256`, etc.

------

## Basic Examples (C/C++ intrinsics)

The following small examples provide an intuitive experience of AVX.

#### Floating-Point Array Addition (AVX)

```cpp
#include <immintrin.h>
#include <stdio.h>

int main() {
    // Prepare source data (must be aligned or use loadu)
    float a[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    float b[8] = {8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f};
    float c[8];

    // Load data into 256-bit registers
    __m256 va = _mm256_load_ps(a); // Assumes 32-byte alignment
    __m256 vb = _mm256_load_ps(b);

    // Perform parallel addition
    __m256 vc = _mm256_add_ps(va, vb);

    // Store result back to memory
    _mm256_store_ps(c, vc);

    // Print result
    for(int i = 0; i < 8; i++) {
        printf("%.1f ", c[i]);
    }
    // Output: 9.0 9.0 9.0 9.0 9.0 9.0 9.0 9.0
    return 0;
}
```

Compile:

```bash
gcc -mavx example.c -o example
```

#### Floating-Point Dot Product (AVX + reduction)

```cpp
#include <immintrin.h>
#include <stdio.h>

float dot_product_avx(const float* x, const float* y, size_t size) {
    __m256 sum_vec = _mm256_setzero_ps();

    // Process 8 floats at a time
    for (size_t i = 0; i < size; i += 8) {
        __m256 vx = _mm256_loadu_ps(x + i); // Use loadu if alignment is uncertain
        __m256 vy = _mm256_loadu_ps(y + i);

        // Multiply and accumulate
        __m256 v_mul = _mm256_mul_ps(vx, vy);
        sum_vec = _mm256_add_ps(sum_vec, v_mul);
    }

    // Horizontal reduction (extract elements from the vector and sum them)
    // Note: AVX horizontal operations are slightly complex, here is a simple method
    alignas(32) float temp[8];
    _mm256_store_ps(temp, sum_vec);

    float result = 0.0f;
    for (int i = 0; i < 8; i++) {
        result += temp[i];
    }

    // Handle remaining tail elements (if size is not a multiple of 8)
    for (size_t i = (size / 8) * 8; i < size; i++) {
        result += x[i] * y[i];
    }

    return result;
}

int main() {
    float a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    float b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    printf("Dot Product: %f\n", dot_product_avx(a, b, 8));
    return 0;
}
```

#### Try It: AVX2: Integer Parallel Addition and Gather Example

```cpp
#include <immintrin.h>
#include <stdio.h>
#include <stdint.h>

int main() {
    // Define a sparse array (indices)
    int32_t indices[8] = {0, 10, 20, 30, 40, 50, 60, 70};
    // Define a large data array
    int32_t data[100];

    // Initialize data: data[i] = i
    for(int i=0; i<100; i++) data[i] = i;

    // 1. Gather: Load 8 integers from non-contiguous addresses based on indices
    // This is much faster than loading individually in a loop
    __m256i v_data = _mm256_i32gather_epi32(data, indices, 4); // Scale 4 (sizeof int)

    // 2. Vector Addition: Add a constant vector (e.g., 100) to the gathered data
    __m256i v_offset = _mm256_set1_epi32(100);
    __m256i v_result = _mm256_add_epi32(v_data, v_offset);

    // 3. Store result
    int32_t result[8];
    _mm256_storeu_si256((__m256i*)result, v_result);

    // Print result
    printf("Gathered and Added Result:\n");
    for(int i=0; i<8; i++) {
        printf("%d ", result[i]); // Expected: 100, 110, 120...
    }
    printf("\n");

    return 0;
}
```

Compile:

```bash
gcc -mavx2 avx2_gather_example.c -o avx2_example
```
