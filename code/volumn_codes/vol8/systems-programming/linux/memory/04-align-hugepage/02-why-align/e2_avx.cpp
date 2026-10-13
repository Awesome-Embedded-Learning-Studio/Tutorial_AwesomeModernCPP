// E2a: 对齐要求的第一性来源 —— vmovdqa(对齐版 AVX 加载)撞上未对齐地址 = SIGSEGV
// _mm256_load_si256 映射到 vmovdqa,硬件要求地址 32 字节对齐(不对齐直接 #GP);
// _mm256_loadu_si256 映射到 vmovdqu,无对齐要求,作为对照。
// 装载函数带 target("avx2") + noinline,保证编译器真发出 vmovdqa(见 e2_disasm.txt),
// SIGSEGV 用 sigaction 捕获打印后退出,让 .out 自含证据。
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <ucontext.h>

static volatile unsigned sink; // 防止加载被优化掉

// 两个函数都把 256 位全部消费掉(四路异约),防止编译器把加载缩窄成 128 位 XMM 版
// (XMM 版 vmovdqa 只要求 16 字节对齐,证据就不纯了)。
__attribute__((noinline, target("avx2"))) static void load_aligned(const __m256i* p) {
    __m256i v = _mm256_load_si256(p);
    unsigned long long h = 0;
    for (int i = 0; i < 4; ++i)
        h ^= static_cast<unsigned long long>(_mm256_extract_epi64(v, i));
    sink = static_cast<unsigned>(h);
}

__attribute__((noinline, target("avx2"))) static void load_unaligned(const void* p) {
    __m256i v = _mm256_loadu_si256(static_cast<const __m256i*>(p));
    unsigned long long h = 0;
    for (int i = 0; i < 4; ++i)
        h ^= static_cast<unsigned long long>(_mm256_extract_epi64(v, i));
    sink = static_cast<unsigned>(h);
}

static void on_segv(int /*sig*/, siginfo_t* info, void*) {
    std::printf("  ==> SIGSEGV 捕获! si_code=%d(%s) si_addr=%p\n", info->si_code,
                info->si_code == SI_KERNEL ? "SI_KERNEL" : "?", info->si_addr);
    std::printf("  ==> 这不是缺页,是未对齐 vmovdqa 引发的 #GP;"
                "内核把通用保护异常报成 si_code=SI_KERNEL(128)、si_addr 通常无意义\n");
    std::fflush(stdout); // _Exit 不冲缓冲区,必须手动 ff
    std::_Exit(139);     // 139 = 128+SIGSEGV,shell 口径
}

int main() {
    struct sigaction sa{};
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);

    // ① 对齐地址 + 对齐加载:应当成功
    alignas(32) static unsigned long long ok_buf[16]{};
    std::printf("[1] alignas(32) 静态数组   addr=%#zx  %%32=%zu\n",
                reinterpret_cast<std::uintptr_t>(ok_buf),
                reinterpret_cast<std::uintptr_t>(ok_buf) % 32);
    load_aligned(reinterpret_cast<const __m256i*>(ok_buf));
    std::printf("    _mm256_load_si256 成功(vmovdqa 吃下对齐地址)\n");

    // 造一个"铁定未对齐"的地址:先拿 32 对齐的块,再偏 8 字节 -> %32 == 8
    char* base = static_cast<char*>(aligned_alloc(32, 4096));
    char* mis = base + 8;
    std::printf("[2] 对齐块偏移 8 字节       addr=%#zx  %%32=%zu\n",
                reinterpret_cast<std::uintptr_t>(mis), reinterpret_cast<std::uintptr_t>(mis) % 32);

    // ② 同一个未对齐地址 + 无对齐要求加载:应当成功(对照组)
    load_unaligned(mis);
    std::printf("    _mm256_loadu_si256 成功(vmovdqu 对任何地址都行)\n");

    // ③ 未对齐地址 + 对齐加载:预期段错误
    std::printf("[3] 未对齐地址 + _mm256_load_si256(vmovdqa)—— 预期崩:\n");
    std::fflush(stdout);
    load_aligned(reinterpret_cast<const __m256i*>(mis));
    std::printf("    居然没崩?(说明编译器没发 vmovdqa,本 .out 作废)\n");
    return 0;
}
