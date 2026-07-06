// pipeline_branch_ilp.cpp — vol6 ch02-03 用
//   A. 分支预测:已排序 vs 未排序数组的条件累加
//   B. ILP:单累加器(长依赖链)vs 4 累加器(独立并行链)
// 编译(关键 flag,故意关掉向量化/分支消除,才能演示标量分支与标量 ILP):
//   g++ -O2 -std=c++17 -fno-tree-vectorize -fno-slp-vectorize -fno-if-conversion \
//       pipeline_branch_ilp.cpp -o pipeline_branch_ilp
// 跑: taskset -c 0 ./pipeline_branch_ilp
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}
inline void do_not_optimize_f(float v) {
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    asm volatile("" : "+r"(u)::"memory");
}

constexpr int N = 32'768;

// ---------- A. 分支预测 ----------
// 最朴素写法:if 累加。配合 -fno-if-conversion -fno-tree-vectorize,汇编里会是真 jcc。
uint64_t sum_gt128_scalar(const std::vector<uint8_t>& d) {
    uint64_t s = 0;
    for (int i = 0; i < N; ++i) {
        if (d[i] >= 128)
            s += d[i];
    }
    return s;
}

// ---------- B. ILP ----------
float dot1(const float* a, const float* b) {
    float acc = 0.0f;
    for (int i = 0; i < N; ++i)
        acc += a[i] * b[i];
    return acc;
}
float dot4(const float* a, const float* b) {
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    for (int i = 0; i < N; i += 4) {
        a0 += a[i] * b[i];
        a1 += a[i + 1] * b[i + 1];
        a2 += a[i + 2] * b[i + 2];
        a3 += a[i + 3] * b[i + 3];
    }
    return a0 + a1 + a2 + a3;
}

int main() {
    // ===== A. 分支预测 =====
    std::vector<uint8_t> data(N);
    for (int i = 0; i < N; ++i)
        data[i] = (uint8_t)(i * 17 + 7);
    std::vector<uint8_t> shuffled = data;
    for (int i = N - 1; i > 0; --i) {
        int j = (i * 7 + 3) % N;
        std::swap(shuffled[i], shuffled[j]);
    }
    std::vector<uint8_t> sorted = data;
    std::sort(sorted.begin(), sorted.end());

    const int REPEAT = 3000;
    uint64_t sink = 0;
    sum_gt128_scalar(shuffled);
    sum_gt128_scalar(sorted); // warmup
    auto t0 = clk::now();
    for (int r = 0; r < REPEAT; ++r)
        sink += sum_gt128_scalar(shuffled);
    do_not_optimize(sink);
    auto t1 = clk::now();
    double t_shuffled = std::chrono::duration<double, std::milli>(t1 - t0).count() / REPEAT;

    t0 = clk::now();
    sink = 0;
    for (int r = 0; r < REPEAT; ++r)
        sink += sum_gt128_scalar(sorted);
    do_not_optimize(sink);
    auto t2 = clk::now();
    double t_sorted = std::chrono::duration<double, std::milli>(t2 - t0).count() / REPEAT;

    std::printf("===== A. 分支预测(条件累加 %d 元素,%d 次平均)=====\n", N, REPEAT);
    std::printf("   打乱(随机分支,预测器猜不中):%7.3f ms/次\n", t_shuffled);
    std::printf("   排序(模式清晰,几乎不预测失败):%7.3f ms/次\n", t_sorted);
    std::printf("   差 %.1fx\n", t_shuffled / t_sorted);

    // ===== B. ILP(每轮扰动输入 a[0],防循环不变量提升整体消除)=====
    std::vector<float> a(N), b(N);
    for (int i = 0; i < N; ++i) {
        a[i] = (float)i * 0.001f;
        b[i] = (float)(N - i) * 0.001f;
    }

    const int R2 = 4000;
    dot1(a.data(), b.data());
    dot4(a.data(), b.data());
    float fs = 0;
    t0 = clk::now();
    for (int r = 0; r < R2; ++r) {
        a[0] = (float)r;
        fs += dot1(a.data(), b.data());
    }
    do_not_optimize_f(fs);
    auto tb1 = clk::now();
    double t_dot1 = std::chrono::duration<double, std::micro>(tb1 - t0).count() / R2;

    fs = 0;
    t0 = clk::now();
    for (int r = 0; r < R2; ++r) {
        a[0] = (float)r;
        fs += dot4(a.data(), b.data());
    }
    do_not_optimize_f(fs);
    auto tb2 = clk::now();
    double t_dot4 = std::chrono::duration<double, std::micro>(tb2 - t0).count() / R2;

    std::printf("\n===== B. ILP(点积 %d float,%d 次平均,标量无向量化)=====\n", N, R2);
    std::printf("   单累加器 dot1:%7.1f us/次  (一条长依赖链,CPU 只能等上次加完)\n", t_dot1);
    std::printf("   4 累加器 dot4:%7.1f us/次  (4 条独立链,CPU 并行填满执行端口)\n", t_dot4);
    std::printf("   差 %.2fx\n", t_dot1 / t_dot4);
    return 0;
}
