// pgo_demo.cpp — vol6 ch04-07 / ch07-02 PGO 演示
// 一个「分支概率极度倾斜」的函数:99% 的输入走 same path,1% 走 rare path。
// PGO 能让编译器把 hot path 布局到一起(icache 友好)+ 优化分支预测。
// 编译(三阶段 PGO;完整脚本见 pgo.sh,这里只给要点):
//   ⚠️ 阶段1 和阶段3 的 -o 二进制名必须一致,否则 .gcda 文件名对不上、profile 不会生效。
//   阶段1 仪器化:  g++ -O2 -std=c++17 -fprofile-generate pgo_demo.cpp -o pgo_demo
//   阶段2 跑剖面:  ./pgo_demo        (生成 pgo_demo-pgo_demo.cpp.gcda,真实分布 99/1)
//   阶段3 用剖面:  g++ -O2 -std=c++17 -fprofile-use pgo_demo.cpp -o pgo_demo
//   对比:见 pgo.sh(纯 -O2 基线 pgo_plain vs 带 PGO 的 pgo_demo)
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

// 模拟一个分支倾斜的解析器:大多数 token 走 fast path,少数走慢路径
int process(int token) {
    if (token % 100 == 0) {
        // rare path:99% 不进
        int x = 0;
        for (int i = 0; i < 10; ++i)
            x += token * i;
        return x + 1;
    }
    return token * 2 + 1; // hot path
}

int main(int argc, char** argv) {
    constexpr int N = 1 << 23; // 800 万
    std::vector<int> data(N);
    // 真实分布:99% 是普通 token,1% 是特殊 token
    for (int i = 0; i < N; ++i)
        data[i] = (i % 100 == 0) ? 100 : (i * 7 + 1);

    const int REP = 20;
    process(1);
    process(100); // warmup
    auto t0 = clk::now();
    uint64_t s = 0;
    for (int r = 0; r < REP; ++r)
        for (int i = 0; i < N; ++i)
            s += process(data[i]);
    auto t1 = clk::now();
    do_not_optimize(s);
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / REP;
    std::printf("process %d tokens,%d 次平均:%.2f ms/次  (sum=%llu)\n", N, REP, ms,
                (unsigned long long)s);
    std::printf("(此二进制:%s)\n", argc > 1 ? argv[1] : "未标注");
    return 0;
}
