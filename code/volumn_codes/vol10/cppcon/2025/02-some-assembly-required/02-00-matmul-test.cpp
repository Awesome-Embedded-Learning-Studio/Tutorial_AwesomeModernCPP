/*
 * 验证：AVX2/FMA 矩阵乘法 — 手动 SIMD vs 编译器自动向量化
 *
 * 背景：文章 01-personal-journey-and-from-assembly-to-cpp.md 声称手动 AVX2/FMA 内联函数
 *       比编译器自动向量化快约 25%。需要验证代码正确性和加速比。
 *
 * 预期结果：
 *   - 标量版本与 AVX2 版本结果一致（最大误差 < 1e-4）
 *   - AVX2 版本快数倍
 *
 * 编译命令：
 *   g++ -std=c++17 -O3 -mavx2 -mfma -o /tmp/matmul_test 02-00-matmul-test.cpp
 *
 * 运行：
 *   /tmp/matmul_test
 *
 * 参考资料：
 *   - Intel Intrinsics Guide: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/
 *
 * 编译器：GCC 16.1.1 20260430
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>

// 纯 C++ 标量版本
void matmul_scalar(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            for (int j = 0; j < N; j++) {
                C[i * N + j] += aik * B[k * N + j];
            }
        }
    }
}

// 用 AVX2/FMA 内联函数版本
void matmul_avx2(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            __m256 vaik = _mm256_set1_ps(aik);
            for (int j = 0; j < N; j += 8) {  // N=256 是 8 的倍数，无需 tail 处理
                __m256 vb = _mm256_loadu_ps(&B[k * N + j]);
                __m256 vc = _mm256_loadu_ps(&C[i * N + j]);
                vc = _mm256_fmadd_ps(vaik, vb, vc);
                _mm256_storeu_ps(&C[i * N + j], vc);
            }
        }
    }
}

#include <chrono>
using Clock = std::chrono::high_resolution_clock;

int main() {
    const int N = 256;
    float* A = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* B = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C1 = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C2 = (float*)_mm_malloc(N * N * sizeof(float), 32);

    for (int i = 0; i < N * N; i++) {
        A[i] = static_cast<float>(rand()) / RAND_MAX;
        B[i] = static_cast<float>(rand()) / RAND_MAX;
    }

    auto t1 = Clock::now();
    matmul_scalar(A, B, C1, N);
    auto t2 = Clock::now();
    auto scalar_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    t1 = Clock::now();
    matmul_avx2(A, B, C2, N);
    t2 = Clock::now();
    auto avx2_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    printf("scalar: %.2f ms\n", scalar_ms);
    printf("avx2/fma: %.2f ms\n", avx2_ms);
    printf("speedup: %.2fx\n", scalar_ms / avx2_ms);

    float max_diff = 0.0f;
    for (int i = 0; i < N * N; i++) {
        float diff = C1[i] - C2[i];
        if (diff < 0) diff = -diff;
        if (diff > max_diff) max_diff = diff;
    }
    printf("max_diff: %e\n", max_diff);

    _mm_free(A); _mm_free(B); _mm_free(C1); _mm_free(C2);
    return 0;
}
