// lto_main.cpp — vol6 ch07-02 LTO 演示主文件
// 编译对照:
//   无 LTO:  g++ -O2 lto_main.cpp lto_helper.cpp -o lto_nolto
//   有 LTO:  g++ -O2 -flto lto_main.cpp lto_helper.cpp -o lto_lto
// 跑: taskset -c 0 ./lto_nolto && taskset -c 0 ./lto_lto
// 看:LTO 版应略快(跨 TU 内联 helper,常量传播/消除冗余),且二进制可能更小(死代码消除)
#include <chrono>
#include <cstdint>
#include <cstdio>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

int helper(int x); // 定义在 lto_helper.cpp

int main() {
    constexpr int N = 100'000'000;
    volatile uint64_t sink = 0;
    auto t0 = clk::now();
    for (int i = 0; i < N; ++i)
        sink += helper(i & 0xFF);
    auto t1 = clk::now();
    do_not_optimize(sink);
    std::printf("helper 调 %d 次:%6.1f ms\n", N,
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    return 0;
}
