// cacheline_locality.cpp — vol6 ch02-02 用
// 两个实验:
//   A. 步长扫描:固定工作集,扫步长,精确定位 cacheline 边界(吞吐在 stride=64 处断崖)
//   B. 行优先 vs 列优先 2D 遍历:空间局部性最经典的对照
// 编译: g++ -O2 -std=c++17 cacheline_locality.cpp -o cacheline_locality
// 跑:   taskset -c 0 ./cacheline_locality
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
static std::vector<int> g_data;

inline void do_not_optimize(int v) {
    asm volatile("" : "+r"(v)::"memory");
}

// ---------- A. 步长扫描:固定 size,扫 stride,测吞吐(元素/ns)----------
// 思路:工作集固定在 L3 区(够大,使每次新 cacheline 加载有成本)。
// stride < 64B 时,一个 cacheline 内的多次访问被摊薄 → 高吞吐;
// stride >= 64B 时,每次访问都触发新 cacheline → 吞吐断崖。
double stride_throughput(long elems, long stride_elem) {
    const long ACCESSES = 64'000'000L;
    long mask = elems - 1;
    int sink = 0;
    long idx = 0;
    auto t0 = clk::now();
    for (long i = 0; i < ACCESSES; ++i) {
        sink += g_data[idx];
        idx = (idx + stride_elem) & mask;
    }
    do_not_optimize(sink);
    auto t1 = clk::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return (double)ACCESSES / secs / 1e6; // M 访问/秒
}

// ---------- B. 行优先 vs 列优先 2D 遍历 ----------
void walk_2d(int* a, int N, bool row_major) {
    volatile int sink = 0;
    int s = 0;
    auto t0 = clk::now();
    if (row_major) {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                s += a[i * N + j]; // 顺序
    } else {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                s += a[j * N + i]; // 跨 N 个 int 跳
    }
    sink = s;
    do_not_optimize(sink);
    auto t1 = clk::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    long bytes = (long)N * N * sizeof(int);
    std::printf("   %s: %7.1f ms,  %.1f GB/s\n",
                row_major ? "行优先 row-major" : "列优先 col-major", secs * 1e3,
                bytes / secs / 1e9);
}

int main() {
    // ===== A. 步长扫描 =====
    // 工作集 2 MB(落在 L3,且远大于 L1/L2,使 cacheline miss 成本显形)
    const long ELEMS = (2L * 1024 * 1024) / sizeof(int); // 2MB / 4B = 512K 元素
    g_data.resize(ELEMS);
    for (long i = 0; i < ELEMS; ++i)
        g_data[i] = (int)i;

    std::printf("===== A. 步长扫描(工作集 2MB,落 L3)=====\n");
    std::printf("%-12s %12s %12s\n", "stride(B)", "M访问/秒", "说明");
    long strides_B[] = {4, 8, 16, 32, 48, 56, 64, 72, 96, 128, 256, 512};
    for (long sb : strides_B) {
        long stride_elem = sb / sizeof(int); // int=4B
        // warmup
        stride_throughput(ELEMS, stride_elem);
        double m = stride_throughput(ELEMS, stride_elem);
        const char* note = "";
        if (sb < 64)
            note = "< cacheline:同行的多次访问被摊薄";
        else if (sb == 64)
            note = "= cacheline:每次访问正好换一行";
        else
            note = "> cacheline:每次访问都是新行";
        std::printf("%8ldB   %10.1f   %s\n", sb, m, note);
    }

    // ===== B. 行优先 vs 列优先 =====
    const int N = 2048; // 2048*2048*4B = 16MB = L3 大小,放大局部性差异
    std::vector<int> a((long)N * N);
    for (long i = 0; i < (long)N * N; ++i)
        a[i] = (int)i;
    std::printf("\n===== B. 2D 遍历(N=%d,矩阵 %d MB = L3 大小)=====\n", N,
                N * N * (int)sizeof(int) / (1024 * 1024));
    // warmup
    walk_2d(a.data(), N, true);
    walk_2d(a.data(), N, true);
    walk_2d(a.data(), N, false);
    walk_2d(a.data(), N, false);
    return 0;
}
