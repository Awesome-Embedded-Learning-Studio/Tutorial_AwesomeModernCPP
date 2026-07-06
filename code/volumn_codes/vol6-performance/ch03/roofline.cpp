// roofline.cpp — vol6 ch03-01 用
// 解析地计算几个经典内核的算术强度(arithmetic intensity),
// 标注每个内核落在 Roofline 的哪一侧(带宽受限 vs 算力受限)。
// 纯算术,无需 perf / 无需硬件计数器,任何机器都能跑。
// 编译: g++ -O2 -std=c++17 roofline.cpp -o roofline
#include <cstdio>

// 每个内核:名字、每次迭代的浮点运算数、每次迭代的内存流量(字节)
struct Kernel {
    const char* name;
    double flops_per_iter;
    double bytes_per_iter;
};

int main() {
    Kernel kernels[] = {
        // dot = Σ a[i]*b[i]:2 FLOP(乘+加),读 2 float = 8 B
        {"dot   (a·b)", 2.0, 2 * 4.0},
        // axpy y = αx+y:2 FLOP,读 2 float + 写 1 float = 12 B
        {"axpy  (y=αx+y)", 2.0, 3 * 4.0},
        // 加权点积 w*x*y:3 FLOP(两乘一加),读 3 float = 12 B
        {"wdot  (w·x·y)", 3.0, 3 * 4.0},
        // 矩阵乘 C+=A·B(每输出元素):2 FLOP,但 A/B 元素被复用 —— 这里给「分块后」的有效 AI
        // 用一个高 AI 代表(compute-bound 典型)
        {"matmul (分块后)", 2.0 * 64.0, 3 * 4.0}, // 假设每元素被复用 ~64 次
    };

    // Roofline 脊点(rough,给量级):峰值算力 / 峰值带宽
    // 5800H 量级:峰值 FP32 ~256 GFLOPS(2 FMA×8 float×2×~4GHz 量级),
    //            DDR4 峰值带宽 ~40 GB/s。脊点 AI ≈ 256/40 ≈ 6.4 FLOP/byte
    // 注意:这俩数随 turbo/AVX 模式/内存条浮动,这里只给量级做判读。
    const double PEAK_FLOPS = 256e9;              // FLOP/s(量级近似)
    const double PEAK_BW = 40e9;                  // B/s(量级近似)
    const double ridge_ai = PEAK_FLOPS / PEAK_BW; // 脊点算术强度

    std::printf("===== Roofline 解析判读 =====\n");
    std::printf("本机量级:峰值 %.0f GFLOPS,峰值带宽 %.0f GB/s,脊点 AI ≈ %.1f FLOP/byte\n",
                PEAK_FLOPS / 1e9, PEAK_BW / 1e9, ridge_ai);
    std::printf("\n%-22s %12s %12s %s\n", "内核", "AI(FLOP/B)", "vs 脊点", "判读");
    std::printf("------------------------------------------------------------\n");
    for (const auto& k : kernels) {
        double ai = k.flops_per_iter / k.bytes_per_iter;
        const char* verdict = (ai < ridge_ai) ? "带宽受限(memory-bound)" : "算力受限(core-bound)";
        const char* cmp = (ai < ridge_ai) ? "< 脊点" : "≥ 脊点";
        std::printf("%-22s %12.3f %12s  %s\n", k.name, ai, cmp, verdict);
    }

    std::printf("\n判读含义:\n");
    std::printf("  带宽受限 → 优化方向是减访存(SoA、更宽 load、减数据移动),加 SIMD 无用\n");
    std::printf("  算力受限 → 优化方向是加算力(SIMD、减指令、打破依赖链),减访存无用\n");
    return 0;
}
