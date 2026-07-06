// arithmetic_cost.cpp — vol6 ch04-03 数据类型与算术
// A. 除法是瓶颈:整数除法 vs 乘法 vs 位运算(除数是 2 的幂)
// B. 整数除法 vs 浮点乘法的「换算」trick(x/常数 → x*(1/常数))
// C. switch vs if-else 链(本例 case 连续,-O2 折成算术,无跳转表;case 稀疏才生成跳转表)
// 编译: g++ -O2 -std=c++17 arithmetic_cost.cpp -o arithmetic_cost
//        看 codegen: g++ -O2 -S arithmetic_cost.cpp(本例 -O2 下 switch/ife 都被折成算术,jmp * 数=0)
// 跑:   taskset -c 0 ./arithmetic_cost
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

constexpr int N = 1 << 20;

template <class F> double time_ns(F&& f, int repeat) {
    f();
    f();
    auto t0 = clk::now();
    for (int r = 0; r < repeat; ++r)
        f();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / repeat / N;
}

int main() {
    std::vector<uint32_t> a(N), b(N);
    for (int i = 0; i < N; ++i) {
        a[i] = i * 7 + 1;
        b[i] = i * 3 + 1;
    }
    volatile uint32_t sink = 0;
    const int REP = 50;

    // ===== A. 除法瓶颈 =====
    // 除数是常量 8(2 的幂):编译器应优化成位移,和手写位移一样快
    double t_div_pow2 = time_ns(
        [&] {
            for (int i = 0; i < N; ++i)
                sink += a[i] / 8;
        },
        REP);
    double t_shift = time_ns(
        [&] {
            for (int i = 0; i < N; ++i)
                sink += a[i] >> 3;
        },
        REP);
    // 除数是运行期值(编译器不能换成位移/乘法逆元)
    volatile uint32_t d = 7;
    double t_div_var = time_ns(
        [&] {
            for (int i = 0; i < N; ++i)
                sink += a[i] / d;
        },
        REP);
    double t_mul = time_ns(
        [&] {
            for (int i = 0; i < N; ++i)
                sink += a[i] * 3;
        },
        REP);
    do_not_optimize(sink);

    std::printf("===== A. 整数算术成本(%d 元素,每次平均 ns)=====\n", N);
    std::printf("  x/8   (除数=2 的幂,编译器换位移):%5.2f ns\n", t_div_pow2);
    std::printf("  x>>3  (手写位移)                 :%5.2f ns\n", t_shift);
    std::printf("  x/7   (除数=运行期变量)           :%5.2f ns  ← 除法瓶颈\n", t_div_var);
    std::printf("  x*3   (乘法)                      :%5.2f ns\n", t_mul);
    std::printf("  除法(变量)/乘法 = %.1fx\n", t_div_var / t_mul);

    // ===== B. switch vs if-else(跳转表)=====
    auto sw = [](int x) -> int {
        switch (x % 8) {
            case 0:
                return 100;
            case 1:
                return 101;
            case 2:
                return 102;
            case 3:
                return 103;
            case 4:
                return 104;
            case 5:
                return 105;
            case 6:
                return 106;
            default:
                return 107;
        }
    };
    auto ife = [](int x) -> int {
        int r = x % 8;
        if (r == 0)
            return 100;
        else if (r == 1)
            return 101;
        else if (r == 2)
            return 102;
        else if (r == 3)
            return 103;
        else if (r == 4)
            return 104;
        else if (r == 5)
            return 105;
        else if (r == 6)
            return 106;
        else
            return 107;
    };
    volatile int s2 = 0;
    double t_switch = time_ns(
        [&] {
            for (int i = 0; i < N; ++i)
                s2 += sw(i);
        },
        REP);
    double t_ifelse = time_ns(
        [&] {
            for (int i = 0; i < N; ++i)
                s2 += ife(i);
        },
        REP);
    do_not_optimize((uint64_t)s2);
    std::printf("\n===== B. switch vs if-else 链 =====\n");
    std::printf("  switch: %5.2f ns\n", t_switch);
    std::printf("  if-else: %5.2f ns\n", t_ifelse);
    std::printf("  if-else/switch = %.2fx\n", t_ifelse / t_switch);
    return 0;
}
