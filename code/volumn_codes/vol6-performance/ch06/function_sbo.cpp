// function_sbo.cpp — vol6 ch06-03 std::function 的 SBO
// std::function 类型擦除,调用是间接的,构造可能堆分配。
// 测:① 函数指针(无捕获,最便宜)② 小 lambda(捕获 ≤ SBO 阈值,存在对象内)③ 大 lambda(超 SBO,堆分配)
// 编译: g++ -O2 -std=c++17 function_sbo.cpp -o function_sbo
// 跑:   taskset -c 0 ./function_sbo
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

constexpr int N = 100'000'000;
constexpr int CONSTRUCT = 1'000'000;

int free_func(int x) {
    return x + 1;
}

int main() {
    volatile uint64_t sink = 0;

    // ===== 调用成本(构造一次,调用 N 次)=====
    {
        // ① 函数指针
        int (*fp)(int) = free_func;
        auto t0 = clk::now();
        for (int i = 0; i < N; ++i)
            sink += fp(i);
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
        std::printf("  调用 - 函数指针:        %5.2f ns\n", ns);
    }
    {
        // ② std::function 装小 lambda(捕获一个 int,SBO 命中)
        int cap = 42;
        std::function<int(int)> f = [cap](int x) { return x + cap; };
        auto t0 = clk::now();
        for (int i = 0; i < N; ++i)
            sink += f(i);
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
        std::printf("  调用 - function+小lambda(SBO): %5.2f ns\n", ns);
    }
    {
        // ③ 直接调用 lambda(对照:无类型擦除)
        int cap = 42;
        auto lam = [cap](int x) { return x + cap; };
        auto t0 = clk::now();
        for (int i = 0; i < N; ++i)
            sink += lam(i);
        auto t1 = clk::now();
        do_not_optimize(sink);
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
        std::printf("  调用 - 直接 lambda(对照):     %5.2f ns\n", ns);
    }

    // ===== 构造成本(构造 CONSTRUCT 次)=====
    std::printf("\n  构造 %d 次:\n", CONSTRUCT);
    {
        auto t0 = clk::now();
        for (int i = 0; i < CONSTRUCT; ++i) {
            std::function<int(int)> f = free_func;
            do_not_optimize((uint64_t)(uintptr_t)&f);
        }
        auto t1 = clk::now();
        std::printf("    function 装函数指针: %7.1f ns/次\n",
                    std::chrono::duration<double, std::nano>(t1 - t0).count() / CONSTRUCT);
    }
    {
        int cap = 42;
        auto t0 = clk::now();
        for (int i = 0; i < CONSTRUCT; ++i) {
            std::function<int(int)> f = [cap](int x) { return x + cap; };
            do_not_optimize((uint64_t)(uintptr_t)&f);
        }
        auto t1 = clk::now();
        std::printf("    function 装小lambda(SBO): %7.1f ns/次\n",
                    std::chrono::duration<double, std::nano>(t1 - t0).count() / CONSTRUCT);
    }
    {
        // 大捕获:double[64] = 512B,远超 SBO 阈值(libstdc++ 通常 16-24B)
        struct Big {
            double data[64];
        };
        Big big{};
        auto t0 = clk::now();
        for (int i = 0; i < CONSTRUCT; ++i) {
            std::function<int(int)> f = [big](int x) { return x + 1; };
            do_not_optimize((uint64_t)(uintptr_t)&f);
        }
        auto t1 = clk::now();
        std::printf("    function 装大lambda(堆分配): %7.1f ns/次 ← 堆分配开销\n",
                    std::chrono::duration<double, std::nano>(t1 - t0).count() / CONSTRUCT);
    }
    std::printf(
        "\n  sizeof(std::function<int(int)>)=%zu(libstdc++/libc++ 通常 32-48B,含 SBO 缓冲)\n",
        sizeof(std::function<int(int)>));
    return 0;
}
