// false_sharing.cpp — vol6 ch05-01 伪共享
// 两个线程各自频繁自增「相邻的计数器」。
//   A. 两计数器在同一 cacheline → 伪共享:每次写都让对方 cacheline 失效,实质串行
//   B. alignas(64) 让每个计数器独占 cacheline → 无伪共享,真并行
// 编译: g++ -O2 -std=c++17 -pthread false_sharing.cpp -o false_sharing
// 跑:   ./false_sharing   (多线程,不必 taskset 单核)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

constexpr long ITERS = 100'000'000;

// A. 伪共享:两个计数器紧挨着,同一条 64B cacheline
struct BadCounters {
    std::atomic<long> a{0};
    std::atomic<long> b{0};
};
// B. 无伪共享:每个 alignas(64) 独占 cacheline
struct alignas(64) PaddedCounter {
    std::atomic<long> v{0};
};
struct GoodCounters {
    PaddedCounter a;
    PaddedCounter b;
};

template <class C, class F> long run(C& c, F worker) {
    auto t0 = std::chrono::steady_clock::now();
    std::thread t1([&] { worker(c.a, 0); });
    std::thread t2([&] { worker(c.b, 1); });
    t1.join();
    t2.join();
    auto t1b = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1b - t0).count();
}

int main() {
    // warmup
    {
        BadCounters bc;
        auto f = [](auto& x, int) {
            for (long i = 0; i < 1000; ++i)
                x.store(x.load() + 1);
        };
        run(bc, f);
    }

    BadCounters bad;
    long t_bad = run(bad, [](std::atomic<long>& x, int) {
        for (long i = 0; i < ITERS; ++i)
            x.store(x.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    });
    GoodCounters good;
    long t_good = run(good, [](PaddedCounter& x, int) {
        for (long i = 0; i < ITERS; ++i)
            x.v.store(x.v.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    });

    std::printf("===== 伪共享(2 线程各自自增 %ld 亿次)=====\n", ITERS / 100000000);
    std::printf("  伪共享(同 cacheline):     %6.1f ms\n", (double)t_bad);
    std::printf("  alignas(64)(独占 cacheline):%6.1f ms\n", (double)t_good);
    std::printf("  伪共享/对齐 = %.1fx\n", (double)t_bad / t_good);
    std::printf("  sizeof(BadCounters)=%zu  sizeof(GoodCounters)=%zu\n", sizeof(BadCounters),
                sizeof(GoodCounters));
    return 0;
}
