// Standard: C++23
// std::print vs cout（sync 开/关） vs printf 的格式化性能对比
// 200 万条短行写到 /dev/null（排除终端 I/O 噪声，只测格式化与缓冲），
// 耗时打到 stderr——print 绕开 cout 的虚调用 + sync 开销，通常最快。
#include <chrono>
#include <cstdio>
#include <iostream>
#include <print>

constexpr int kIterations = 2'000'000;

static void report(const char* name, std::chrono::steady_clock::time_point t0,
                   std::chrono::steady_clock::time_point t1) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::fprintf(stderr, "%-22s %lld us\n", name, (long long)us);
}

void bench_cout_sync() {
    std::ios::sync_with_stdio(true); // 默认
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::cout << "i=" << i << " sq=" << i * 2 << '\n';
    }
    report("cout (sync=true)", t0, std::chrono::steady_clock::now());
}

void bench_cout_nosync() {
    std::ios::sync_with_stdio(false); // 常见的"加速 cout"写法
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::cout << "i=" << i << " sq=" << i * 2 << '\n';
    }
    report("cout (sync=false)", t0, std::chrono::steady_clock::now());
}

void bench_printf() {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::printf("i=%d sq=%d\n", i, i * 2);
    }
    report("printf", t0, std::chrono::steady_clock::now());
}

void bench_print() {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::print("i={} sq={}\n", i, i * 2);
    }
    report("print", t0, std::chrono::steady_clock::now());
}

int main() {
    std::freopen("/dev/null", "w", stdout); // 排除终端 I/O 噪声
    bench_cout_sync();
    bench_printf();
    bench_print();
    bench_cout_nosync();
    return 0;
}
