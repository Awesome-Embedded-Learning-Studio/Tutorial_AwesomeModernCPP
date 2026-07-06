// opt_levels_blockers.cpp — vol6 ch07-01 -O 级别与 optimization blockers
// A. -O0/-O1/-O2/-O3 对同一函数的速度差(展示各级别优化力度)
// B. 指针别名 blocker:编译器不知道 a/b 是否重叠,不敢激进优化;__restrict 解锁
// 编译:
//   g++ -O0 opt_levels_blockers.cpp -o o0  && ./o0
//   g++ -O2 opt_levels_blockers.cpp -o o2  && ./o2
//   g++ -O3 opt_levels_blockers.cpp -o o3  && ./o3
// 跑:   taskset -c 0 ./o0; taskset -c 0 ./o2; taskset -c 0 ./o3
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}
constexpr int N = 10'000'000;

// B. 别名 blocker:scale 一次,a/b 各 N 次。编译器若不信 a/b 别名,可把 scale 外提。
//    用 volatile scale 迫使每次 load,模拟「编译器不敢优化」;__restrict 版手动声明无别名。
void scale_add_alias(int* a, int* b, volatile int* scale, int n) {
    for (int i = 0; i < n; ++i)
        a[i] += b[i] * *scale; // 每次 load scale,且不敢假设 a≠b
}
void scale_add_restrict(int* __restrict a, int* __restrict b, int* __restrict scale, int n) {
    for (int i = 0; i < n; ++i)
        a[i] += b[i] * *scale; // __restrict:编译器信 a/b/scale 不别名
}

int main(int argc, char** argv) {
    std::vector<int> a(N, 1), b(N, 2);
    volatile int scale = 3;
    int which = (argc > 1) ? argv[1][0] : 'x'; // 'a'=alias 'r'=restrict

    auto t0 = clk::now();
    if (which == 'r')
        scale_add_restrict(a.data(), b.data(), (int*)&scale, N);
    else
        scale_add_alias(a.data(), b.data(), (int*)&scale, N);
    auto t1 = clk::now();
    do_not_optimize(a[0]);

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("%c: %6.1f ms  (编译参数决定优化力度;__restrict 版可向量化/外提 scale)\n", which,
                ms);
    return 0;
}
