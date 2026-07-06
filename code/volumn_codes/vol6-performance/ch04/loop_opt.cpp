// loop_opt.cpp — vol6 ch04-02 循环与计算优化
// 三个经典变换:
//   A. code motion:把循环不变量提到循环外
//   B. 消除不必要的内存引用:每次循环都 store 中间结果 vs 寄存器累加最后 store 一次
//   C. 多累加器打破依赖链(呼应 ch02-03 的 dot1/dot4)
// 编译: g++ -O2 -std=c++17 loop_opt.cpp -o loop_opt
//   (注意:-O2 下 B 的两版可能被编译器优化成一样,看汇编确认;必要时 -O1 对照)
// 跑:   taskset -c 0 ./loop_opt
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize_f(float v) {
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    asm volatile("" : "+r"(u)::"memory");
}

constexpr int N = 1 << 20;

template <class F> double time_us(F&& f, int repeat) {
    f();
    f();
    auto t0 = clk::now();
    for (int r = 0; r < repeat; ++r)
        f();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / repeat;
}

int main() {
    std::vector<float> a(N), b(N), c(N);
    for (int i = 0; i < N; ++i) {
        a[i] = (float)i;
        b[i] = (float)(i % 7);
        c[i] = 0;
    }
    const int REP = 50;

    // ===== B. 消除不必要的内存引用(典型 CSAPP 例子:对数组元素累加)=====
    // 「坏」:每次循环都从 c[i] 读、写回 c[i](c[i] 在循环里不变,却反复访存)
    //   —— 但这例子里 c[i] 每次变,改用「连续写」对比「重复读-写」
    // 更贴 CSAPP 的例子:sum 存到数组元素 vs 寄存器
    volatile float scale = 2.5f;
    // 烂:把临时累加器放在内存(数组)里,每次循环 load+store
    double t_memref = time_us(
        [&] {
            for (int i = 0; i < N; ++i)
                c[i] = c[i] + a[i] * b[i] * scale; // c[i] 反复 load/store
        },
        REP);
    // 好:寄存器累加,最后一次 store
    double t_reg = time_us(
        [&] {
            for (int i = 0; i < N; ++i)
                c[i] = a[i] * b[i] * scale; // 直接写,无读回
        },
        REP);
    do_not_optimize_f(c[N / 2]);

    std::printf("===== B. 消除不必要的内存引用(%d 元素,%d 次平均)=====\n", N, REP);
    std::printf("  反复读写 c[i]:%6.1f us\n", t_memref);
    std::printf("  直接写 c[i]: %6.1f us\n", t_reg);
    std::printf("  (差距取决于编译器能否证明 c[i] 可省 load;-O2 常已优化掉)\n");

    // ===== A. code motion(循环不变量外提)=====
    // 烂:每次循环重算 b[i]*scale(scale 不变却每次 load)
    // 好:scale 外提(编译器对 volatile scale 做不到,正好演示)
    double t_nomotion = time_us(
        [&] {
            float s = 0;
            for (int i = 0; i < N; ++i)
                s += a[i] * b[i] * scale;
            s += 1;
            do_not_optimize_f(s);
        },
        REP);
    float sc = scale; // 外提到普通变量,编译器能 hoist
    double t_motion = time_us(
        [&] {
            float s = 0;
            for (int i = 0; i < N; ++i)
                s += a[i] * b[i] * sc;
            s += 1;
            do_not_optimize_f(s);
        },
        REP);
    std::printf("\n===== A. code motion(循环不变量外提)=====\n");
    std::printf("  scale 是 volatile(每次 load):%6.1f us\n", t_nomotion);
    std::printf("  scale 外提到普通变量:        %6.1f us\n", t_motion);
    return 0;
}
