// 行主序 vs 列主序的 cache 实测
// 配套 documents/vol8-domains/ai/tiny_ml/stage2/02-weight-shape.md
// 点"运行"在 Compiler Explorer 云端执行,绝对数字因机器而异,
// 但"行序远快于列序"的趋势一定成立。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    constexpr int N = 1024;
    std::vector<float> m((size_t)N * N);
    for (int i = 0; i < N * N; ++i)
        m[i] = float(i % 7);
    volatile float sink = 0; // 防止累加循环被优化掉

    double row_t[5], col_t[5];
    for (int it = 0; it < 5; ++it) {
        float acc;
        // 行序遍历:内层走 c,顺着一行连续读 —— cache 命中
        auto a = std::chrono::high_resolution_clock::now();
        acc = 0;
        for (int r = 0; r < N; ++r)
            for (int c = 0; c < N; ++c)
                acc += m[(size_t)r * N + c];
        auto b = std::chrono::high_resolution_clock::now();
        sink = acc;
        row_t[it] = std::chrono::duration<double, std::milli>(b - a).count();

        // 列序遍历:内层走 r,跨行读同一列 —— cache miss
        auto c1 = std::chrono::high_resolution_clock::now();
        acc = 0;
        for (int c = 0; c < N; ++c)
            for (int r = 0; r < N; ++r)
                acc += m[(size_t)r * N + c];
        auto d = std::chrono::high_resolution_clock::now();
        sink = acc;
        col_t[it] = std::chrono::duration<double, std::milli>(d - c1).count();
    }

    std::sort(row_t, row_t + 5);
    std::sort(col_t, col_t + 5);
    std::printf("N=%d, -O2, 5 次取中位数:\n", N);
    std::printf("  行序(row-major): %.2f ms\n", row_t[2]);
    std::printf("  列序(col-major): %.2f ms\n", col_t[2]);
    std::printf("  列序 / 行序 = %.1f 倍\n", col_t[2] / row_t[2]);
    return 0;
}
