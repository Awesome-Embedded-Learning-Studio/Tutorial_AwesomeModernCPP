// E3: 透明大页(THP)—— madvise 口径的实证(含 WSL2 的进程级开关坑)
// 诊断背景见 e3_thp_probe:本机 sysfs 是 [madvise],但 WSL2 的 init 链给整个进程树
// 设了 prctl 层的 MMF_DISABLE_THP,出厂状态下 MADV_HUGEPAGE 拿不到任何大页。
// 本实验分两幕:第一幕按出厂状态跑(THP 被关);第二幕 prctl 清掉开关后跑(THP 复活),
// 同一套 MADV_HUGEPAGE / MADV_NOHUGEPAGE 对照,计时分 A 首触(缺页主导)与 B 全量 memset(TLB 主导)。
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>

static constexpr size_t kGiB = 1ull << 30;

static std::string slurp(const char* path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

// smaps 的映射头是 "低地址-高地址 权限 偏移 设备 inode [名称]",设备字段含 ':',
// 所以识别头用 sscanf("%lx-%lx") 而不是"不含冒号"。
static bool parse_header(const std::string& line, unsigned long* lo, unsigned long* hi) {
    return std::sscanf(line.c_str(), "%lx-%lx", lo, hi) == 2 && line.find('-') < line.find(' ');
}

static void dump_smaps_for(void* addr) {
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(addr);
    std::ifstream f("/proc/self/smaps");
    std::string line, header;
    bool in_block = false;
    const char* keep[] = {"Size:", "Rss:", "AnonHugePages:", "THPeligible:", "VmFlags:"};
    while (std::getline(f, line)) {
        unsigned long lo = 0, hi = 0;
        if (parse_header(line, &lo, &hi)) {
            if (in_block)
                break;
            in_block = (a >= lo && a < hi);
            if (in_block)
                header = line;
        } else if (in_block) {
            for (const char* k : keep)
                if (line.rfind(k, 0) == 0) {
                    std::printf("    %s\n", line.c_str());
                }
        }
    }
    if (in_block)
        std::printf("    (smaps 片段,映射头: %s)\n", header.c_str());
    else
        std::printf("    (smaps 里没找到该地址?)\n");
}

static long anon_hugepages_kb(void* addr) {
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(addr);
    std::ifstream f("/proc/self/smaps");
    std::string line;
    bool mine = false;
    while (std::getline(f, line)) {
        unsigned long lo = 0, hi = 0;
        if (parse_header(line, &lo, &hi)) {
            mine = (a >= lo && a < hi);
            continue;
        }
        if (mine && line.rfind("AnonHugePages:", 0) == 0) {
            long kb = 0;
            std::sscanf(line.c_str(), "AnonHugePages: %ld", &kb);
            return kb;
        }
    }
    return -1;
}

// 一轮:mmap 1 GiB -> madvise -> A 首触(每页 1 字节) -> B 全量 memset -> smaps -> munmap
static void one_round(int advice, const char* tag, double* msA, double* msB, bool dump) {
    void* p = mmap(nullptr, kGiB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        std::perror("mmap");
        return;
    }
    if (madvise(p, kGiB, advice) != 0)
        std::perror("madvise");

    auto* bytes = static_cast<unsigned char*>(p);
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < kGiB; i += 4096)
        bytes[i] = static_cast<unsigned char>(i);
    auto t1 = std::chrono::steady_clock::now();
    *msA = std::chrono::duration<double, std::milli>(t1 - t0).count();
    long ah = anon_hugepages_kb(p);
    std::printf("%-11s A 首触写 1 GiB(每页 1 字节): %7.1f ms  AnonHugePages=%ld kB\n", tag, *msA,
                ah);

    auto t2 = std::chrono::steady_clock::now();
    std::memset(p, 0x5A, kGiB); // 页已全部驻留,这轮主要花在页表遍历/TLB
    auto t3 = std::chrono::steady_clock::now();
    *msB = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("%-11s B 全量 memset 1 GiB:            %7.1f ms  (%6.0f MiB/s)\n", tag, *msB,
                1024.0 / *msB * 1000.0);
    if (dump)
        dump_smaps_for(p);
    munmap(p, kGiB);
}

static double med3(double* a) {
    std::sort(a, a + 3);
    return a[1];
}

int main() {
    std::printf("THP enabled(全局) = %s\n",
                slurp("/sys/kernel/mm/transparent_hugepage/enabled").c_str());
    std::printf("THP defrag        = %s\n",
                slurp("/sys/kernel/mm/transparent_hugepage/defrag").c_str());
    std::printf("2048kB/enabled    = %s  (方括号=选中;sysfs 改动要 root,对照全走 madvise)\n",
                slurp("/sys/kernel/mm/transparent_hugepage/hugepages-2048kB/enabled").c_str());
    std::printf("prctl(PR_GET_THP_DISABLE) = %d\n\n", prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));

    std::printf("==== 第一幕:出厂状态(WSL2 init 给进程树设了 MMF_DISABLE_THP)====\n");
    {
        double a = 0, b = 0;
        one_round(MADV_HUGEPAGE, "HUGEPAGE", &a, &b, true);
        one_round(MADV_NOHUGEPAGE, "NOHUGEPAGE", &a, &b, false);
        std::printf(
            "  ==> 提示了 HUGEPAGE,大页却是 0:进程级开关压过了 sysfs(详见 e3_thp_probe)\n\n");
    }

    prctl(PR_SET_THP_DISABLE, 0, 0, 0, 0);
    std::printf("==== 第二幕:prctl(PR_SET_THP_DISABLE, 0) 清掉进程级开关(本内核允许)====\n");
    std::printf("prctl(PR_GET_THP_DISABLE) = %d\n\n", prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));

    double hpA[3], hpB[3], nhA[3], nhB[3];
    for (int r = 0; r < 3; ++r) {
        std::printf("---- 第 %d 轮 ----\n", r);
        one_round(MADV_HUGEPAGE, "HUGEPAGE", &hpA[r], &hpB[r], r == 0);
        one_round(MADV_NOHUGEPAGE, "NOHUGEPAGE", &nhA[r], &nhB[r], false);
    }
    std::printf("\n中位数(3 轮):\n");
    std::printf("  A 首触  HUGEPAGE=%.1f ms  NOHUGEPAGE=%.1f ms  倍差=%.2fx\n", med3(hpA),
                med3(nhA), med3(nhA) / med3(hpA));
    std::printf("  B memset HUGEPAGE=%.1f ms  NOHUGEPAGE=%.1f ms  倍差=%.2fx\n", med3(hpB),
                med3(nhB), med3(nhB) / med3(hpB));
    return 0;
}
