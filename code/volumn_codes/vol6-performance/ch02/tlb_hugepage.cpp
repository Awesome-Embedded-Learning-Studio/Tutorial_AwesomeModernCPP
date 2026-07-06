// tlb_hugepage.cpp — vol6 ch02-04 用
// 行为级隔离 TLB 成本:同工作集,4KB 页 vs 2MB 大页(THP),指针追逐比延迟。
// 大页让 TLB 项数减少 512 倍 → 若 TLB 是瓶颈,大页版本应该更快。
// 编译: g++ -O2 -std=c++17 tlb_hugepage.cpp -o tlb_hugepage
// 跑:   taskset -c 0 ./tlb_hugepage
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/mman.h>
#include <vector>

using clk = std::chrono::steady_clock;
inline void do_not_optimize(uint64_t v) {
    asm volatile("" : "+r"(v)::"memory");
}

// 在 [base, base+nbytes) 上构造置乱单环,指针追逐测延迟
double chase(const uint64_t* base, long nbytes) {
    long elems = nbytes / 8;
    std::vector<long> perm(elems), nxt(elems);
    for (long i = 0; i < elems; ++i)
        perm[i] = i;
    std::mt19937_64 rng(0xC0FFEEull);
    for (long i = elems - 1; i > 0; --i) {
        long j = (long)(rng() % (i + 1));
        std::swap(perm[i], perm[j]);
    }
    for (long i = 0; i < elems; ++i)
        nxt[perm[i]] = perm[(i + 1) % elems];

    const long ROUNDS = 4;
    long total = elems * ROUNDS;
    volatile uint64_t sink = 0;
    long idx = 0;
    // 写一遍 base 让页实际分配
    for (long i = 0; i < elems; ++i)
        ((uint64_t*)base)[i] = (uint64_t)i;
    auto t0 = clk::now();
    for (long s = 0; s < total; ++s) {
        idx = nxt[idx];
        sink = sink + base[idx];
    }
    auto t1 = clk::now();
    do_not_optimize(sink);
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return secs / total * 1e9; // ns/访问
}

int main() {
    const long SZ = 256L * 1024 * 1024; // 256 MB,远超 L3 与 L1 dTLB 覆盖范围

    // 版本 A:普通匿名 mmap(4KB 页)
    void* a4 = mmap(nullptr, SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // 版本 B:mmap 后 madvise(MADV_HUGEPAGE)请求透明大页
    void* a2 = mmap(nullptr, SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(a2, SZ, MADV_HUGEPAGE);

    if (a4 == MAP_FAILED || a2 == MAP_FAILED) {
        std::printf("mmap 失败\n");
        return 1;
    }

    // warmup
    chase((const uint64_t*)a4, SZ);
    chase((const uint64_t*)a2, SZ);

    double t_4k = chase((const uint64_t*)a4, SZ);
    double t_2m = chase((const uint64_t*)a2, SZ);

    std::printf("===== TLB 实验:256MB 工作集指针追逐(4KB 页 vs 2MB 大页)=====\n");
    std::printf("   4KB 页版本:%6.1f ns/访问\n", t_4k);
    std::printf("   2MB 大页版本:%6.1f ns/访问  (若 WSL2 实际给了大页,这里应明显更快)\n", t_2m);
    std::printf("   比值:%.2f\n", t_4k / t_2m);

    // 尝试探一下大页是否真的启用(看 /proc/self/smaps 的 AnonHugePmdMapped 或类似)
    // 简化:不解析 smaps,只报行为差。差值 ≈ 1 说明 WSL2 没给大页。
    return 0;
}
