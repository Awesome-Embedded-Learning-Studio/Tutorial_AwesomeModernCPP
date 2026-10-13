// E2b: 伪共享对照 —— 两个线程各写各自的计数器
//   near 变体:两个 uint64_t 相邻躺在同一条 64 字节 cache line 里
//   far  变体:alignas(64) 隔开,各自独占一条 line
// 理论在 vol5 讲过,这里只出本机数字。计数器是 volatile,逼每次自增都真写内存。
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>

static constexpr std::uint64_t kIters = 400'000'000; // 每线程 4 亿次自增

// 相邻的两个全局变量:链接器把它们排在 BSS 里连续的 8 字节,同一条 line
static volatile std::uint64_t near_a = 0, near_b = 0;

// alignas(64) 的结构体:各自对齐到独立 line
struct alignas(64) PaddedCounter {
    volatile std::uint64_t v = 0;
};
static PaddedCounter far_a, far_b;

static void hammer(volatile std::uint64_t& c) {
    // C++20 起对 volatile 复合自增已弃用,拆成 load+store,每次迭代仍是真读写内存
    for (std::uint64_t i = 0; i < kIters; ++i)
        c = c + 1;
}

static double run_pair(volatile std::uint64_t& a, volatile std::uint64_t& b, const char* tag,
                       int round) {
    auto t0 = std::chrono::steady_clock::now();
    std::thread t1(hammer, std::ref(a)), t2(hammer, std::ref(b));
    t1.join();
    t2.join();
    auto t1e = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1e - t0).count();
    std::printf("%-5s r%d  %8.1f ms   (和=%llu)\n", tag, round, ms,
                static_cast<unsigned long long>(a + b));
    return ms;
}

static double med3(double* a) {
    std::sort(a, a + 3);
    return a[1];
}

int main() {
    std::printf("每次:2 线程 x %llu 次 volatile 自增,near/far 交错 3 轮\n\n",
                static_cast<unsigned long long>(kIters));
    std::printf("计数器地址:near_b=%p near_a=%p (相邻 %td 字节,同一条 64B line)\n",
                (const void*)&near_b, (const void*)&near_a,
                (const char*)&near_a - (const char*)&near_b);
    std::printf("计数器地址:far_b =%p far_a =%p (相隔 %td 字节;far_a%%64=%zu)\n\n",
                (const void*)&far_b, (const void*)&far_a, (const char*)&far_a - (const char*)&far_b,
                reinterpret_cast<std::uintptr_t>(&far_a) % 64);

    double near_ms[3], far_ms[3];
    for (int r = 0; r < 3; ++r) {
        near_a = 0;
        near_b = 0;
        far_a.v = 0;
        far_b.v = 0;
        near_ms[r] = run_pair(near_a, near_b, "near", r);
        far_ms[r] = run_pair(far_a.v, far_b.v, "far", r);
    }
    double mn = med3(near_ms), mf = med3(far_ms);
    std::printf("\n中位数:near(同 line)=%.1f ms   far(隔开)=%.1f ms   倍差=%.2fx\n", mn, mf,
                mn / mf);
    return 0;
}
