// e4_readfile_vs_map.cpp —— ReadFile vs MapViewOfFile 选型实测(256MiB 同一文件)
//
// 编译(基准程序,开 O2;WSL 里相对路径调 MSYS2 UCRT64 g++):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra e4_readfile_vs_map.cpp -o
//   e4_readfile_vs_map.exe
// 运行(顺序:create 先建文件并暖缓存 -> 各场景独立进程跑,同一文件):
//   chmod +x e4_readfile_vs_map.exe
//   ./e4_readfile_vs_map.exe envinfo
//   ./e4_readfile_vs_map.exe create
//   ./e4_readfile_vs_map.exe readfile-seq
//   ./e4_readfile_vs_map.exe map-seq
//   ./e4_readfile_vs_map.exe readfile-rand
//   ./e4_readfile_vs_map.exe map-rand
//   ./e4_readfile_vs_map.exe readfile-nobuf
//
// 口径:
//   - 文件 256MiB,内容确定性生成(每 4KiB 块按块号填充),两法校验和必须一致;
//   - 顺序读:ReadFile 1MiB 块 vs 映射视图逐字节扫,各 3 轮取中位;
//   - 随机读:固定种子预生成 10000 个 4KiB 页偏移,两法访问完全相同的页序列;
//   - 冷读侧写:FILE_FLAG_NO_BUFFERING 整读(绕过缓存,逼近冷),3 轮中位;
//   - 计时 QueryPerformanceCounter(单调),只包读+校验循环,不含建文件/映射;
//   - 除 readfile-nobuf 外都是暖缓存口径(文件刚建完驻留缓存),README 如实交代。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static const size_t kFileSize = 256ull << 20; // 256 MiB
static const size_t kPage = 4096;
static const size_t kPages = kFileSize / kPage; // 65536
static const int kRounds = 3;

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

static std::wstring temp_path(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}
static const wchar_t* kFile = L"sysprog-e4-256m.bin";

// 内容确定性:块 i 的填充字节 = (i*31+7)&0xff —— 校验和可跨方法比对
static unsigned fill_byte(size_t page) {
    return (unsigned)(page * 31 + 7) & 0xff;
}

static unsigned long long sum_buf(const unsigned char* p, size_t n) {
    unsigned long long s = 0;
    for (size_t i = 0; i < n; ++i)
        s += p[i];
    return s;
}

static unsigned long long content_sum() {
    // 独立算一遍理论校验和:每页 4096 个相同字节
    unsigned long long s = 0;
    for (size_t pg = 0; pg < kPages; ++pg)
        s += (unsigned long long)fill_byte(pg) * kPage;
    return s;
}

static std::vector<size_t> random_pages() {
    std::mt19937_64 rng(42); // 固定种子:两法访问同一序列
    std::vector<size_t> v;
    v.reserve(10000);
    for (int i = 0; i < 10000; ++i)
        v.push_back(rng() % kPages);
    return v;
}

static void report(const char* tag, double secs[kRounds], unsigned long long sum_expect,
                   unsigned long long sum_got, size_t bytes) {
    double s[kRounds] = {secs[0], secs[1], secs[2]};
    for (int i = 0; i < kRounds; ++i)
        printf("  %s 轮%d %8.2f ms  %7.0f MiB/s\n", tag, i + 1, s[i] * 1e3,
               bytes / (s[i] * 1024.0 * 1024.0));
    // 中位(3 个数取中间)
    if (s[0] > s[1])
        std::swap(s[0], s[1]);
    if (s[1] > s[2])
        std::swap(s[1], s[2]);
    if (s[0] > s[1])
        std::swap(s[0], s[1]);
    printf("  %s 中位 %8.2f ms  %7.0f MiB/s  校验和=%llu(%s)\n", tag, s[1] * 1e3,
           bytes / (s[1] * 1024.0 * 1024.0), sum_got,
           sum_got == sum_expect ? "与理论一致" : "不一致!");
}

static HANDLE open_read(bool no_buffering) {
    HANDLE f =
        CreateFileW(temp_path(kFile).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | (no_buffering ? FILE_FLAG_NO_BUFFERING : 0), nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "open failed err=%lu(先跑 create)\n", GetLastError());
        ExitProcess(1);
    }
    return f;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(
            stderr,
            "用法: %s envinfo|create|readfile-seq|map-seq|readfile-rand|map-rand|readfile-nobuf\n",
            argv[0]);
        return 2;
    }
    const std::string mode = argv[1]; // std::string 方便 printf 与判等
    const unsigned long long expect = content_sum();

    if (mode == "envinfo") {
        printf("[envinfo] 本机环境自报\n");
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof ms;
        GlobalMemoryStatusEx(&ms);
        printf("  物理内存 %.1f GiB,可用 %.1f GiB\n", ms.ullTotalPhys / (1024.0 * 1048576.0),
               ms.ullAvailPhys / (1024.0 * 1048576.0));
        wchar_t dir[MAX_PATH];
        GetTempPathW(MAX_PATH, dir);
        wchar_t root[4] = {dir[0], L':', L'\\', 0};
        wchar_t fs[64]{};
        DWORD vsn = 0;
        GetVolumeInformationW(root, nullptr, 0, &vsn, nullptr, nullptr, fs, 64);
        printf("  测试文件所在卷 %ls 文件系统=%ls(后备盘总线类型见 README,PowerShell 口径)\n", root,
               fs);
        printf("  注:盘型探测在程序内两条路都走不通——IOCTL_STORAGE_QUERY_PROPERTY 对文件句柄\n"
               "      err=87/1,GetFileInformationByHandleEx(FileStorageInfo) err=50,且 MinGW 头的\n"
               "      FILE_STORAGE_INFO 是截断版(缺 BusType/FileSystemType/MediaType 尾部字段)\n");
        return 0;
    }

    if (mode == "create") {
        printf("[create] 建 256MiB 测试文件(确定性内容)+ FlushFileBuffers\n");
        HANDLE f = CreateFileW(temp_path(kFile).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "create failed err=%lu\n", GetLastError());
            return 1;
        }
        unsigned char* blk = (unsigned char*)_aligned_malloc(kPage, 4096);
        double t0 = now_s();
        for (size_t pg = 0; pg < kPages; ++pg) {
            memset(blk, fill_byte(pg), kPage);
            DWORD w = 0;
            if (!WriteFile(f, blk, kPage, &w, nullptr) || w != kPage) {
                fprintf(stderr, "write err\n");
                return 1;
            }
        }
        FlushFileBuffers(f);
        printf("  写入+落盘耗时 %.2f s(%s)\n", now_s() - t0,
               expect == content_sum() ? "理论校验和自检通过" : "?");
        _aligned_free(blk);
        CloseHandle(f);
        return 0;
    }

    if (mode == "readfile-seq" || mode == "readfile-nobuf") {
        const bool nb = (mode == "readfile-nobuf");
        printf("[%s] ReadFile 顺序整读,块 1MiB,3 轮%s\n", mode.c_str(),
               nb ? "(FILE_FLAG_NO_BUFFERING,绕过缓存,冷读侧写)" : "(暖缓存)");
        HANDLE f = open_read(nb);
        unsigned char* blk = (unsigned char*)_aligned_malloc(1 << 20, 4096);
        double secs[kRounds];
        unsigned long long got = 0;
        for (int r = 0; r < kRounds; ++r) {
            LARGE_INTEGER z{};
            SetFilePointerEx(f, z, nullptr, FILE_BEGIN);
            got = 0;
            double t0 = now_s();
            for (size_t off = 0; off < kFileSize; off += (1 << 20)) {
                DWORD rd_ = 0;
                if (!ReadFile(f, blk, 1 << 20, &rd_, nullptr) || rd_ != (1 << 20)) {
                    fprintf(stderr, "read err %lu\n", GetLastError());
                    return 1;
                }
                got += sum_buf(blk, 1 << 20);
            }
            secs[r] = now_s() - t0;
        }
        report(mode.c_str(), secs, expect, got, kFileSize);
        _aligned_free(blk);
        CloseHandle(f);
        return 0;
    }

    if (mode == "map-seq") {
        printf("[map-seq] MapViewOfFile 顺序扫全文件,3 轮(轮1含首触缺页,轮2/3暖)\n");
        HANDLE f = open_read(false);
        HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        unsigned char* view = (unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        double secs[kRounds];
        unsigned long long got = 0;
        for (int r = 0; r < kRounds; ++r) {
            got = 0;
            double t0 = now_s();
            got = sum_buf(view, kFileSize);
            secs[r] = now_s() - t0;
        }
        report("map-seq", secs, expect, got, kFileSize);
        UnmapViewOfFile(view);
        CloseHandle(m);
        CloseHandle(f);
        return 0;
    }

    if (mode == "readfile-rand") {
        std::vector<size_t> pages = random_pages();
        unsigned long long expect_r = 0;
        for (size_t pg : pages)
            expect_r += (unsigned long long)fill_byte(pg) * kPage;
        printf("[readfile-rand] 随机 4KiB x 10000:SetFilePointerEx + ReadFile,3 轮(暖缓存)\n");
        HANDLE f = open_read(false);
        unsigned char* blk = (unsigned char*)_aligned_malloc(kPage, 4096);
        double secs[kRounds];
        unsigned long long got = 0;
        for (int r = 0; r < kRounds; ++r) {
            got = 0;
            double t0 = now_s();
            for (size_t pg : pages) {
                LARGE_INTEGER off;
                off.QuadPart = (LONGLONG)(pg * kPage);
                SetFilePointerEx(f, off, nullptr, FILE_BEGIN);
                DWORD rd_ = 0;
                if (!ReadFile(f, blk, kPage, &rd_, nullptr) || rd_ != kPage) {
                    fprintf(stderr, "read err\n");
                    return 1;
                }
                got += sum_buf(blk, kPage);
            }
            secs[r] = now_s() - t0;
        }
        double s0 = secs[0], s1 = secs[1], s2 = secs[2];
        (void)s0;
        (void)s1;
        (void)s2;
        // 中位单算:随机场景报每 op 均摊
        double srt[kRounds] = {secs[0], secs[1], secs[2]};
        if (srt[0] > srt[1])
            std::swap(srt[0], srt[1]);
        if (srt[1] > srt[2])
            std::swap(srt[1], srt[2]);
        if (srt[0] > srt[1])
            std::swap(srt[0], srt[1]);
        for (int i = 0; i < kRounds; ++i)
            printf("  readfile-rand 轮%d %8.2f ms  %6.2f us/op\n", i + 1, secs[i] * 1e3,
                   secs[i] * 1e6 / pages.size());
        printf("  readfile-rand 中位 %8.2f ms  %6.2f us/op  校验和=%llu(%s)\n", srt[1] * 1e3,
               srt[1] * 1e6 / pages.size(), got,
               got == expect_r ? "与同一偏移序列理论值一致" : "不一致!");
        _aligned_free(blk);
        CloseHandle(f);
        return 0;
    }

    if (mode == "map-rand") {
        std::vector<size_t> pages = random_pages();
        unsigned long long expect_r = 0;
        for (size_t pg : pages)
            expect_r += (unsigned long long)fill_byte(pg) * kPage;
        printf("[map-rand] 随机 4KiB x 10000:视图直接寻址,3 轮(轮1含首触缺页)\n");
        HANDLE f = open_read(false);
        HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        unsigned char* view = (unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        double secs[kRounds];
        unsigned long long got = 0;
        for (int r = 0; r < kRounds; ++r) {
            got = 0;
            double t0 = now_s();
            for (size_t pg : pages)
                got += sum_buf(view + pg * kPage, kPage);
            secs[r] = now_s() - t0;
        }
        double srt[kRounds] = {secs[0], secs[1], secs[2]};
        if (srt[0] > srt[1])
            std::swap(srt[0], srt[1]);
        if (srt[1] > srt[2])
            std::swap(srt[1], srt[2]);
        if (srt[0] > srt[1])
            std::swap(srt[0], srt[1]);
        for (int i = 0; i < kRounds; ++i)
            printf("  map-rand 轮%d %8.2f ms  %6.2f us/op\n", i + 1, secs[i] * 1e3,
                   secs[i] * 1e6 / pages.size());
        printf("  map-rand 中位 %8.2f ms  %6.2f us/op  校验和=%llu(%s)\n", srt[1] * 1e3,
               srt[1] * 1e6 / pages.size(), got,
               got == expect_r ? "与同一偏移序列理论值一致" : "不一致!");
        UnmapViewOfFile(view);
        CloseHandle(m);
        CloseHandle(f);
        return 0;
    }

    fprintf(stderr, "未知模式 %s\n", mode.c_str());
    return 2;
}
