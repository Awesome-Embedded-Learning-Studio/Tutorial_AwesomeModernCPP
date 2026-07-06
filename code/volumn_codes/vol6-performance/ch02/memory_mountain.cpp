// memory_mountain.cpp — vol6 ch02-01 用
// 两个实验:
//   A. 经典 CSAPP memory mountain(读吞吐, size × stride 网格)
//   B. 指针追逐随机读延迟(真实访问延迟随工作集跨过 L1/L2/L3/DRAM 的断崖)
// 编译: g++ -O2 -std=c++17 memory_mountain.cpp -o memory_mountain
// 跑:   taskset -c 0 ./memory_mountain   (绑核降噪)
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using clk = std::chrono::steady_clock;

// ---------- A. memory mountain: 顺序步长读吞吐 ----------
// data 大小 = elems * 8 字节。以 stride(单位:元素个数)环形遍历。
// 固定总访问次数 ACCESSES,测吞吐。
static std::vector<int64_t> g_data;

// 把 v 强制「被消费」+ 内存屏障,等同 Google Benchmark 的 DoNotOptimize 语义
inline void do_not_optimize(int64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

double throughput(long elems, long stride) {
    const long ACCESSES = 64'000'000L; // 总访问次数(够大以摊薄计时噪声)
    long mask = elems - 1;             // elems 必须是 2 的幂
    int64_t sink = 0;
    long idx = 0;
    auto t0 = clk::now();
    for (long i = 0; i < ACCESSES; ++i) {
        sink += g_data[idx];         // g_data 非 const,load 无法被消除
        idx = (idx + stride) & mask; // 环形,始终落在 [0, elems)
    }
    do_not_optimize(sink); // sink 必须求值
    auto t1 = clk::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    // 每次访问读 8 字节
    return (double)ACCESSES * 8.0 / secs / 1e9; // GB/s
}

// ---------- B. 指针追逐真实随机读延迟 ----------
// 构造一条贯穿全部节点的环形链,idx_next = perm 置乱,硬件预取器无法预测
// (下一个地址依赖本次 load 的结果),测出真实访存延迟。
double chase_latency(long elems) {
    // 构造置乱单环
    std::vector<long> perm(elems);
    for (long i = 0; i < elems; ++i)
        perm[i] = i;
    std::mt19937_64 rng(0xC0FFEEull);
    for (long i = elems - 1; i > 0; --i) {
        long j = (long)(rng() % (i + 1));
        std::swap(perm[i], perm[j]);
    }
    std::vector<long> nxt(elems);
    for (long i = 0; i < elems; ++i)
        nxt[perm[i]] = perm[(i + 1) % elems];

    const long ROUNDS = 8; // 每个元素访问 ROUNDS 次
    long total_steps = elems * ROUNDS;
    volatile int64_t sink = 0;
    long idx = 0;
    auto t0 = clk::now();
    for (long s = 0; s < total_steps; ++s) {
        idx = nxt[idx];
        sink = sink + g_data[idx]; // 读 data[idx] 制造真依赖
    }
    auto t1 = clk::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    if (sink == 0x12345)
        std::printf("");
    return secs / total_steps * 1e9; // ns/访问
}

int main() {
    // 给 data 分配到足够大(最大覆盖到 DRAM 区:64M 元素 = 512MB)
    // 用非常量填充:g_data[i] = i,确保每次 load 无法被编译器折叠成常量
    const long MAX_ELEMS = 1L << 26; // 64M * 8B = 512 MB
    g_data.resize(MAX_ELEMS);
    for (long i = 0; i < MAX_ELEMS; ++i)
        g_data[i] = i;

    // ===== A. memory mountain =====
    std::printf("===== A. memory mountain: 读吞吐 (GB/s) =====\n");
    std::printf("size\\stride(B)");
    long strides_elem[] = {1, 2, 4, 8, 16, 32, 64};    // 元素步长
    long strides_B[] = {8, 16, 32, 64, 128, 256, 512}; // 字节步长
    int ns = (int)(sizeof(strides_elem) / sizeof(strides_elem[0]));
    for (int k = 0; k < ns; ++k)
        std::printf("%8ldB", strides_B[k]);
    std::printf("\n");

    long size_KB[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
    int nsize = (int)(sizeof(size_KB) / sizeof(size_KB[0]));
    for (int s = 0; s < nsize; ++s) {
        long elems = (size_KB[s] * 1024L) / 8; // 元素个数
        if (elems > MAX_ELEMS)
            break;
        std::printf("%7ldK", size_KB[s]);
        for (int k = 0; k < ns; ++k) {
            // warmup 一次
            throughput(elems, strides_elem[k]);
            double gb = throughput(elems, strides_elem[k]);
            std::printf("%8.1f", gb);
            std::fflush(stdout);
        }
        std::printf("\n");
    }

    // ===== B. 指针追逐延迟阶梯 =====
    std::printf("\n===== B. 指针追逐随机读延迟 (ns/访问) =====\n");
    std::printf("%10s %10s %10s %12s\n", "size", "elems", "ns/access", "level(推断)");
    long lat_KB[] = {4,    8,    16,   32,   64,    128,   256,   512,
                     1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072};
    int nl = (int)(sizeof(lat_KB) / sizeof(lat_KB[0]));
    for (int s = 0; s < nl; ++s) {
        long elems = (lat_KB[s] * 1024L) / 8;
        if (elems > MAX_ELEMS)
            break;
        // warmup
        chase_latency(elems);
        double ns = chase_latency(elems);
        const char* lvl = "";
        if (lat_KB[s] <= 32)
            lvl = "L1d"; // <=32K
        else if (lat_KB[s] <= 512)
            lvl = "L2"; // <=512K
        else if (lat_KB[s] <= 16384)
            lvl = "L3"; // <=16M
        else
            lvl = "DRAM";
        std::printf("%8ldK %10ld %10.2f %12s\n", lat_KB[s], elems, ns, lvl);
        std::fflush(stdout);
    }
    return 0;
}
