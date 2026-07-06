// simd.cpp — vol6 ch04-05 SIMD 与向量化
// 同一个数组点积的三种实现:标量 / 编译器自动向量化(-O3 -ftree-vectorize)/ 手写 AVX2 intrinsics
// 编译:
//   g++ -O2 -std=c++17 simd.cpp -o simd_o2 -mavx2 -mfma      (标量 SSE 向量化,单累加器)
//   g++ -O3 -std=c++17 simd.cpp -o simd_o3 -mavx2 -mfma      (标量 AVX2 ordered 向量化,单累加器)
//   g++ -O3 -std=c++17 simd.cpp -o simd_fm -mavx2 -mfma -ffast-math (允许重结合;本例标量无显著收益)
//   都跑,看 dot_scalar 三档都远慢于 dot_avx2 —— 差距主要来自累加器数(ILP),不是向量化与否
// 跑:   taskset -c 0 ./simd_o2 && taskset -c 0 ./simd_o3
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(__AVX2__)
#    include <immintrin.h>
#endif

using clk = std::chrono::steady_clock;
inline void do_not_optimize_f(float v) {
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    asm volatile("" : "+r"(u)::"memory");
}

constexpr int N = 1 << 22; // 4M float = 16MB

// 标量点积(单累加器,长依赖链)
float dot_scalar(const float* a, const float* b) {
    float acc = 0.0f;
    for (int i = 0; i < N; ++i)
        acc += a[i] * b[i];
    return acc;
}

// 手写 AVX2(8 float/向量,多累加器)
#if defined(__AVX2__)
float dot_avx2(const float* a, const float* b) {
    __m256 v0 = _mm256_setzero_ps(), v1 = _mm256_setzero_ps();
    __m256 v2 = _mm256_setzero_ps(), v3 = _mm256_setzero_ps();
    for (int i = 0; i < N; i += 32) { // 每次 32 个 float = 4 条 AVX2 向量
        v0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), v0);
        v1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8), v1);
        v2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 16), _mm256_loadu_ps(b + i + 16), v2);
        v3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + i + 24), _mm256_loadu_ps(b + i + 24), v3);
    }
    __m256 lo12 = _mm256_add_ps(v0, v1);
    __m256 hi12 = _mm256_add_ps(v2, v3);
    __m256 s = _mm256_add_ps(lo12, hi12);
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, s);
    float acc = 0;
    for (int i = 0; i < 8; ++i)
        acc += tmp[i];
    return acc;
}
#endif

int main() {
    std::vector<float> a(N), b(N);
    for (int i = 0; i < N; ++i) {
        a[i] = (float)i * 1e-7f;
        b[i] = (float)(N - i) * 1e-7f;
    }
    const int REP = 30;

    auto bench = [&](auto fn, const char* name) {
        fn(a.data(), b.data());
        fn(a.data(), b.data());
        auto t0 = clk::now();
        float s = 0;
        for (int r = 0; r < REP; ++r)
            s += fn(a.data(), b.data());
        auto t1 = clk::now();
        do_not_optimize_f(s);
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / REP;
        std::printf("  %-30s %7.1f us\n", name, us);
        return us;
    };

    std::printf("===== SIMD 点积(%d float = %d MB,%d 次平均)=====\n", N, N * 4 / (1024 * 1024),
                REP);
    double t_scalar = bench(dot_scalar, "标量 dot_scalar");
#if defined(__AVX2__)
    double t_avx2 = bench(dot_avx2, "手写 AVX2 dot_avx2");
    std::printf("  AVX2/标量 = %.1fx\n", t_scalar / t_avx2);
#else
    std::printf("  (本编译未开 -mavx2,跳过 intrinsics)\n");
#endif
    std::printf("  注:dot_scalar 在 -O3 -mavx2 下其实已被向量化(-fopt-info-vec-optimized 可证),\n");
    std::printf("      但单累加器依赖链没打破,远慢于 4 累加器的 "
                "dot_avx2(差距主要来自累加器数,不是向量化与否)。\n");
    return 0;
}
