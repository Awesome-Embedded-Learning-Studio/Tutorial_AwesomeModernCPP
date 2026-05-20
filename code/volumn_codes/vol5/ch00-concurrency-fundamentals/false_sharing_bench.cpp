/*
 * 验证：false sharing 与 alignas(64) 对齐的性能差异
 *
 * 背景：文章 ch00-03 "CPU cache 与 OS 线程" 声称——
 *       两个线程各自写同一个结构体中不同的 int 成员时，
 *       由于两个 int 落在同一条 64 字节缓存行上，
 *       MESI 协议会反复触发 RFO 使缓存行在核心间乒乓，
 *       导致性能甚至比单线程更差。
 *       使用 alignas(64) 让两个变量各占一条缓存行可消除此问题。
 *
 * 预期结果：
 *   False sharing 版本 >> 单线程 >> Aligned 版本（最快）
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/false_sharing_bench
 *
 * 编译器：GCC 16.1.1 | Clang 20+
 * 平台：x86-64 Linux
 */

#include <chrono>
#include <iostream>
#include <thread>

// --- Version 1: false sharing（两个 int 在同一条缓存行） ---
struct Counters {
    volatile int a;
    volatile int b;
};

// --- Version 2: alignas(64)（每个变量独占一条缓存行） ---
constexpr std::size_t kCacheLineSize = 64;

struct alignas(kCacheLineSize) AlignedCounter {
    volatile int value;
};

int main() {
    constexpr int kIterations = 100'000'000;

    // --- Benchmark 1: false sharing ---
    {
        Counters counters{0, 0};

        auto start = std::chrono::high_resolution_clock::now();

        std::thread t1([&]() {
            for (int i = 0; i < kIterations; ++i) {
                counters.a++;
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < kIterations; ++i) {
                counters.b++;
            }
        });

        t1.join();
        t2.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "False sharing:   " << ms.count() << " ms"
                  << " (a=" << counters.a << ", b=" << counters.b << ")\n";
    }

    // --- Benchmark 2: aligned (no false sharing) ---
    {
        AlignedCounter counter_a{0};
        AlignedCounter counter_b{0};

        auto start = std::chrono::high_resolution_clock::now();

        std::thread t1([&]() {
            for (int i = 0; i < kIterations; ++i) {
                counter_a.value++;
            }
        });
        std::thread t2([&]() {
            for (int i = 0; i < kIterations; ++i) {
                counter_b.value++;
            }
        });

        t1.join();
        t2.join();

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Aligned:         " << ms.count() << " ms"
                  << " (a=" << counter_a.value << ", b=" << counter_b.value << ")\n";
    }

    // --- Benchmark 3: single-threaded baseline ---
    {
        volatile int a = 0;
        volatile int b = 0;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < kIterations; ++i) {
            a++;
        }
        for (int i = 0; i < kIterations; ++i) {
            b++;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Single-threaded: " << ms.count() << " ms"
                  << " (a=" << a << ", b=" << b << ")\n";
    }

    return 0;
}
