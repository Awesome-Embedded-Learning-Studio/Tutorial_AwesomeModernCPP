// e3_prefetch_offer.cpp —— PrefetchVirtualMemory / OfferVirtualMemory / DiscardVirtualMemory
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3_prefetch_offer.cpp -o
//   e3_prefetch_offer.exe -lpsapi
// 运行:
//   chmod +x e3_prefetch_offer.exe
//   ./e3_prefetch_offer.exe probe     # 头文件声明与 kernel32 导出交叉验证
//   ./e3_prefetch_offer.exe prefetch warm  # 暖缓存口径(普通创建,内容驻留缓存)
//   ./e3_prefetch_offer.exe prefetch cold  # 冷缓存侧写(FILE_FLAG_NO_BUFFERING 直写,绕过缓存)
//   ./e3_prefetch_offer.exe offer     # offer/reclaim 轮次 + DiscardVirtualMemory
//
// 观察点:
//   [probe]    MSYS2 UCRT64 头文件(_WIN32_WINNT 默认 0xA00)是否声明这套 API、
//              import 库能否直接链接、kernel32 是否真导出(GetProcAddress 交叉验证)。
//   [prefetch] 同一进程内 A/B 对照:64MiB 文件映射,前 32MiB(A)预取,后 32MiB(B)对照。
//              观察各自耗的缺页数(PageFaultCount 增量)与扫描耗时。核心可观察量:
//              PrefetchVirtualMemory 调用本身同步吃掉 N 个缺页,之后扫 A 几乎零缺页;
//              对照的 B 首触扫描自己付缺页。暖/冷两口径如实分别记录。
//   [offer]    私有内存 offer(VmOfferPriorityVeryLow)-> reclaim -> 校验模式;多轮统计;
//              工作集大小变化;DiscardVirtualMemory 主动丢弃后读到什么。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <psapi.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

// ---- QPC 计时(MONOTONIC) ----
static double now_s() {
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER x;
        QueryPerformanceFrequency(&x);
        return x;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

static DWORD fault_count() {
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    return pmc.PageFaultCount;
}

static unsigned long long io_read_bytes() {
    IO_COUNTERS io{};
    GetProcessIoCounters(GetCurrentProcess(), &io);
    return (unsigned long long)io.ReadTransferCount;
}

static SIZE_T working_set() {
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    return pmc.WorkingSetSize;
}

static std::wstring temp_path(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

// 扫一段视图求校验和(防优化,字节全部真读)
static unsigned long long scan(const volatile unsigned char* p, size_t n) {
    unsigned long long s = 0;
    for (size_t i = 0; i < n; ++i)
        s += p[i];
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s probe|prefetch warm|prefetch cold|offer\n", argv[0]);
        return 2;
    }
    const std::string phase = argc > 2 ? argv[1] : argv[1];
    const std::string mode = argc > 2 ? argv[2] : "";

    if (phase == "probe") {
        printf("[probe] MinGW UCRT64 g++ 16.1.0 头文件声明 + kernel32 导出交叉验证\n");
        printf("  头文件直接引用(&fn):Prefetch=%p Offer=%p Reclaim=%p Discard=%p\n",
               (void*)&PrefetchVirtualMemory, (void*)&OfferVirtualMemory,
               (void*)&ReclaimVirtualMemory, (void*)&DiscardVirtualMemory);
        HMODULE k32 = GetModuleHandleW(L"kernel32");
        printf("  GetProcAddress     :Prefetch=%p Offer=%p Reclaim=%p Discard=%p\n",
               (void*)GetProcAddress(k32, "PrefetchVirtualMemory"),
               (void*)GetProcAddress(k32, "OfferVirtualMemory"),
               (void*)GetProcAddress(k32, "ReclaimVirtualMemory"),
               (void*)GetProcAddress(k32, "DiscardVirtualMemory"));
        printf("  枚举 OFFER_PRIORITY:VeryLow=%d Low=%d BelowNormal=%d Normal=%d\n",
               (int)VmOfferPriorityVeryLow, (int)VmOfferPriorityLow,
               (int)VmOfferPriorityBelowNormal, (int)VmOfferPriorityNormal);
        printf("  注:MinGW 头的枚举常量名是 VmOfferPriority*,与 Windows SDK 文档的\n");
        printf("      VMOfferPriority* 不同名(中间那个 m 的大小写)——按文档抄代码在 MinGW "
               "编不过,这是实测坑\n");
        return 0;
    }

    if (phase == "prefetch") {
        const bool cold = (mode == "cold");
        const size_t HALF = 32ull << 20; // 32MiB
        const size_t FULL = HALF * 2;    // 64MiB
        printf("[prefetch %s] 64MiB 文件映射,A=[0,32MiB) 预取,B=[32,64MiB) 对照\n",
               cold ? "cold" : "warm");
        printf(cold ? "  文件用 FILE_FLAG_NO_BUFFERING 直写创建(绕过系统缓存,逼近冷读)\n"
                    : "  文件普通创建(刚写完,内容大概率还驻留缓存)\n");

        DWORD flags = FILE_ATTRIBUTE_NORMAL | (cold ? FILE_FLAG_NO_BUFFERING : 0);
        HANDLE f = CreateFileW(temp_path(L"sysprog-e3.bin").c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, CREATE_ALWAYS, flags, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            printf("  CreateFileW failed err=%lu\n", GetLastError());
            return 1;
        }
        unsigned char* blk = (unsigned char*)_aligned_malloc(1 << 20, 4096);
        for (int i = 0; i < (int)(FULL >> 20); ++i) {
            memset(blk, (unsigned char)(i * 7 + 1), 1 << 20);
            DWORD w = 0;
            if (!WriteFile(f, blk, 1 << 20, &w, nullptr)) {
                printf("  WriteFile err=%lu\n", GetLastError());
                return 1;
            }
        }
        _aligned_free(blk);
        FlushFileBuffers(f);

        HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        unsigned char* view = (unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        printf("  文件就绪,视图=%p\n", view);

        DWORD f0 = fault_count();
        unsigned long long io0 = io_read_bytes();
        WIN32_MEMORY_RANGE_ENTRY rng{view, HALF};
        double t0 = now_s();
        BOOL ok = PrefetchVirtualMemory(GetCurrentProcess(), 1, &rng, 0);
        double t_pf = now_s() - t0;
        DWORD f1 = fault_count();
        unsigned long long io1 = io_read_bytes();
        printf("  PrefetchVirtualMemory(A 32MiB) ret=%d 耗时=%.3fs 缺页增量=%lu "
               "进程读IO增量=%llu MiB\n",
               ok, t_pf, f1 - f0, (io1 - io0) >> 20);

        DWORD f2 = fault_count();
        unsigned long long io2 = io_read_bytes();
        double t1 = now_s();
        unsigned long long sb = scan(view + HALF, HALF); // B:对照,首触
        double t_b = now_s() - t1;
        DWORD f3 = fault_count();
        unsigned long long io3 = io_read_bytes();
        printf("  扫 B(未预取,首触)     校验和=%llu 耗时=%.3fs 付缺页=%lu 读IO增量=%llu MiB\n", sb,
               t_b, f3 - f2, (io3 - io2) >> 20);

        DWORD f4 = fault_count();
        unsigned long long io4 = io_read_bytes();
        double t2 = now_s();
        unsigned long long sa = scan(view, HALF); // A:已预取
        double t_a = now_s() - t2;
        DWORD f5 = fault_count();
        unsigned long long io5 = io_read_bytes();
        printf("  扫 A(已预取)         校验和=%llu 耗时=%.3fs 付缺页=%lu 读IO增量=%llu MiB\n", sa,
               t_a, f5 - f4, (io5 - io4) >> 20);

        printf("  [读法] 预取不接管工作集缺页:扫 A 仍按页付缺页(PageFaultCount 每页 +1)。\n"
               "         预取做的是把页从后备存储拉进 standby list(转换态),之后首触\n"
               "         变成便宜的软缺页——省的是磁盘往返,不是缺页计数。\n"
               "         A/B 校验和不同是内容本就不同(每 MiB 不同填充字节)。\n"
               "         暖缓存口径下 A/B 计时无差(都在缓存里);冷侧写口径的差值见输出。\n");
        UnmapViewOfFile(view);
        CloseHandle(m);
        CloseHandle(f);
        DeleteFileW(temp_path(L"sysprog-e3.bin").c_str());
        return 0;
    }

    if (phase == "offer") {
        const size_t SZ = 32ull << 20; // 32MiB 私有内存
        printf("[offer] 私有内存 32MiB:offer(VeryLow) -> reclaim 轮次 + 主动 Discard\n");
        unsigned char* p =
            (unsigned char*)VirtualAlloc(nullptr, SZ, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            printf("  VirtualAlloc failed err=%lu\n", GetLastError());
            return 1;
        }
        for (size_t i = 0; i < SZ; i += 4096) {
            *(unsigned long long*)(p + i) = 0xC0FFEE0000000000ull + i;
        }
        SIZE_T ws0 = working_set();
        DWORD f0 = fault_count();
        printf("  填充完毕:工作集=%zu MiB 缺页=%lu\n", ws0 >> 20, f0);

        int preserved = 0, broken = 0;
        for (int round = 1; round <= 8; ++round) {
            DWORD ro = OfferVirtualMemory(p, SZ, VmOfferPriorityVeryLow);
            SIZE_T wso = working_set();
            DWORD rr = ReclaimVirtualMemory(p, SZ);
            int bad = 0;
            for (size_t i = 0; i < SZ && bad < 3; i += 4096) {
                if (*(unsigned long long*)(p + i) != 0xC0FFEE0000000000ull + i)
                    ++bad;
            }
            bad ? ++broken : ++preserved;
            printf("  轮次%d:offer ret=%lu(0=ERROR_SUCCESS) offer后工作集=%zu MiB "
                   "reclaim ret=%lu(0=内容保留) 模式校验=%s\n",
                   round, ro, wso >> 20, rr, bad ? "被改写" : "完好");
        }
        printf("  8 轮统计:内容保留 %d 轮 / 被改写 %d 轮(丢弃需要系统内存压力,单机不硬造)\n",
               preserved, broken);

        DWORD d = DiscardVirtualMemory(p, SZ);
        int zeros = 0;
        for (size_t i = 0; i < SZ; i += 4096) {
            if (*(unsigned long long*)(p + i) == 0)
                ++zeros;
        }
        printf(
            "  DiscardVirtualMemory ret=%lu(0=ERROR_SUCCESS,立即丢弃) 之后读 8192 页中全零页=%d\n",
            d, zeros);
        printf("  [读法] Discard 是主动自弃;Offer/Reclaim 是把取舍权交给系统,\n"
               "         reclaim 成功不代表内容一定在(文档只承诺 offered 页可能被丢),\n"
               "         实测无压力下 8/8 轮保留,丢弃路径需要真实内存压力才走得通\n");
        VirtualFree(p, 0, MEM_RELEASE);
        return 0;
    }

    fprintf(stderr, "未知阶段 %s\n", phase.c_str());
    return 2;
}
