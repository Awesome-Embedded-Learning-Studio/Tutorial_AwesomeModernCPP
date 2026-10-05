// e2_flush_bench.cpp —— 32MiB 走四条持久化路径,写与刷盘分开计时
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2_flush_bench.cpp -o
//   e2_flush_bench.exe
// 运行(由 e2_run.sh 驱动多轮取中位数):
//   chmod +x e2_flush_bench.exe && ./e2_flush_bench.exe <plain|flush|wt|wt_flush> [totalMiB]
//   [chunkKiB]
//
// 四条路径(对齐 Linux 侧 03-page-cache/03-durability-bench 的方法论):
//   plain    : WriteFile 循环后不管 —— 只进系统缓存(CRT 不掺和,直接裸 Win32)
//   flush    : WriteFile 循环 + 结尾一次 FlushFileBuffers(计时单列)
//   wt       : FILE_FLAG_WRITE_THROUGH 打开,每次写穿缓存直去盘(含设备写缓存的透写)
//   wt_flush : wt 路径结尾再补一次 FlushFileBuffers(预期很便宜:没什么可刷的)
// 计时用 QueryPerformanceCounter(单调)。
//
// 与 Linux E3 的口径差异(诚实声明):
//   - Linux 侧靠 wait_clean(/proc/meminfo Dirty 压回)隔离轮次;Windows 没有等价的
//     干净观测窗,只能靠"每轮删文件 + 上一轮自然刷盘"接近,轮间干扰无法完全排除
//   - WriteFile 是同步调用,但"返回"只代表进了缓存管理器,不代表落盘 —— 这正是
//     plain 与 flush 两列计时差要量化的东西

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static double now_ms() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) * 1000.0 / double(freq.QuadPart);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <plain|flush|wt|wt_flush> [totalMiB=32] [chunkKiB=1024]\n",
                argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    const long long total = (argc > 2 ? atoll(argv[2]) : 32) << 20;      // MiB -> B
    const DWORD chunk = DWORD((argc > 3 ? atoll(argv[3]) : 1024) << 10); // KiB -> B

    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    bool do_flush = false;
    if (mode == "plain") {
    } else if (mode == "flush") {
        do_flush = true;
    } else if (mode == "wt") {
        flags |= FILE_FLAG_WRITE_THROUGH;
    } else if (mode == "wt_flush") {
        flags |= FILE_FLAG_WRITE_THROUGH;
        do_flush = true;
    } else {
        fprintf(stderr, "unknown mode %s\n", mode.c_str());
        return 2;
    }

    // 目标文件放在 Windows 真实 NTFS 临时目录(避开 9P)
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"supp_e2_bench.bin";
    DeleteFileW(path.c_str());

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        printf("RESULT mode=%s CreateFileW 失败 err=%lu\n", mode.c_str(), GetLastError());
        return 1;
    }

    std::vector<char> buf(chunk);
    memset(buf.data(), 'W', buf.size());

    double t0 = now_ms();
    long long off = 0;
    while (off < total) {
        DWORD put = 0;
        if (!WriteFile(h, buf.data(), chunk, &put, nullptr) || put != chunk) {
            printf("RESULT mode=%s WriteFile 失败 err=%lu\n", mode.c_str(), GetLastError());
            return 1;
        }
        off += put;
    }
    double t1 = now_ms();

    double flush_ms = 0.0;
    if (do_flush) {
        if (!FlushFileBuffers(h)) {
            printf("RESULT mode=%s FlushFileBuffers 失败 err=%lu\n", mode.c_str(), GetLastError());
            return 1;
        }
    }
    double t2 = now_ms();
    flush_ms = t2 - t1;

    CloseHandle(h);
    DeleteFileW(path.c_str());

    double total_ms = t2 - t0;
    printf("RESULT mode=%-8s totalMiB=%lld chunkKiB=%lu write_ms=%.3f flush_ms=%.3f "
           "total_ms=%.3f write_MiB_s=%.0f total_MiB_s=%.0f\n",
           mode.c_str(), total >> 20, chunk >> 10, t1 - t0, flush_ms, total_ms,
           double(total >> 20) / ((t1 - t0) / 1000.0), double(total >> 20) / (total_ms / 1000.0));
    return 0;
}
