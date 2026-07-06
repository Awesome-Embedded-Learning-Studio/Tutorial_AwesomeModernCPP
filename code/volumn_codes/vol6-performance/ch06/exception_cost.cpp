// exception_cost.cpp — vol6 ch06-02 异常的零成本模型
// 零成本 = 「正常路径无额外指令,异常路径靠 EH 表查找(很贵)」。
// 测三条路径:① 不抛(正常路径,应最快)② 返回错误码 ③ 抛+捕获异常
// 编译: g++ -O2 -std=c++17 exception_cost.cpp -o exception_cost
//        对照 -fno-exceptions 看正常路径有没有变(应该一样,这就是"零成本")
// 跑:   taskset -c 0 ./exception_cost
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

constexpr int N = 10'000'000; // 注意:throw 版每轮 N 次,会很慢,控制总量

// ① 正常路径:从不抛
int normal_path(int i) {
    return i + 1;
}
// ② 错误码路径:用返回值传错误
int error_code_path(int i, int& err) {
    if (i < 0) {
        err = -1;
        return 0;
    }
    return i + 1;
}
// ③ 异常路径:从不抛(正常路径,但函数有 throw 能力)
int exception_path_normal(int i) {
    return i + 1;
}
// ③b 真的抛(每次都抛,测 throw+catch 成本)
int exception_path_throw(int i) {
    if (true)
        throw std::runtime_error("x");
    return i + 1;
}

int main() {
    const int REP = 5;
    volatile uint64_t sink = 0;

    // ① 纯正常路径
    {
        auto t0 = clk::now();
        for (int r = 0; r < REP; ++r)
            for (int i = 0; i < N; ++i)
                sink += normal_path(i);
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / REP / N;
        std::printf("  正常路径(从不抛):           %6.3f ns/op\n", ns);
    }
    // ② 错误码
    {
        auto t0 = clk::now();
        for (int r = 0; r < REP; ++r)
            for (int i = 0; i < N; ++i) {
                int err;
                sink += error_code_path(i, err);
            }
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / REP / N;
        std::printf("  错误码(返回值传 err):       %6.3f ns/op\n", ns);
    }
    // ③ 有 throw 能力但从不抛(零成本模型:应和 ① 一样快)
    {
        auto t0 = clk::now();
        for (int r = 0; r < REP; ++r)
            for (int i = 0; i < N; ++i)
                sink += exception_path_normal(i);
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / REP / N;
        std::printf("  有 throw 能力但从不抛(零成本):%6.3f ns/op\n", ns);
    }
    // ④ 真的每次抛+捕获(慢,只跑少量轮)
    {
        const int REP2 = 1;
        const int M = 100'000; // 只跑 10 万次,throw 很慢
        auto t0 = clk::now();
        for (int r = 0; r < REP2; ++r)
            for (int i = 0; i < M; ++i) {
                try {
                    exception_path_throw(i);
                } catch (const std::exception&) {
                    sink += 1;
                }
            }
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / REP2 / M;
        std::printf("  每次都 throw+catch:          %6.1f ns/op  ← 异常路径很贵\n", ns);
    }
    return 0;
}
