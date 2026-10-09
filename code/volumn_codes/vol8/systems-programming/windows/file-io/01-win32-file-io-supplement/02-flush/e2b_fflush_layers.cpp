// e2b_fflush_layers.cpp —— 与 C 运行时的分层:fflush 只到 OS,FlushFileBuffers 才到盘
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2b_fflush_layers.cpp -o
//   e2b_fflush_layers.exe
// 运行:
//   chmod +x e2b_fflush_layers.exe && ./e2b_fflush_layers.exe
//
// 观察点:
//   [1] CRT 层的持留:FILE* 写 100 字节(小于 CRT 内部缓冲),旁边的 Win32 读句柄
//       看到的尺寸是 0 —— 数据还在 CRT 缓冲里,WriteFile 都没发生;fflush 之后才到 100
//   [2] 三层计时(32MiB):
//         fwrite 循环  -> 数据进 OS(大块 fwrite 直通 WriteFile,不过 CRT 缓冲)
//         fflush       -> 只清 CRT 残余缓冲(快,纯用户态拷贝 + 一次小 WriteFile)
//         FlushFileBuffers -> 刷缓存管理器脏页 + 存储栈落盘(慢,数量级差)
//   [3] 每一步之后,旁路句柄(GetFileSizeEx)看到的尺寸变化:fflush 之前 fwrite 的
//       大块其实已经在 OS 里了 —— "fflush 之后别的进程能读到" 对大块不成立,
//       块进 OS 的时机是 WriteFile,不是 fflush
//
// 分层结论:
//   fwrite/fputc -> CRT 缓冲(用户态) -> WriteFile -> 系统缓存(内核) -> 盘
//   fflush 清第一层;FlushFileBuffers 清第二层。fclose 会顺手做 fflush,
//   但谁也不会替你做 FlushFileBuffers。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>

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

static std::wstring make_path(const wchar_t* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + name;
}

// 旁路观测:另开一个只读句柄看当前尺寸(FILE* 的句柄 share 给足了才能进)
static long long sibling_size(const std::wstring& path) {
    HANDLE r = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (r == INVALID_HANDLE_VALUE) {
        return -GetLastError();
    }
    LARGE_INTEGER sz;
    GetFileSizeEx(r, &sz);
    CloseHandle(r);
    return sz.QuadPart;
}

int main() {
    // ---------- [1] CRT 层持留:小写不 fflush 旁路看不见 ----------
    std::wstring p1 = make_path(L"supp_e2b_small.bin");
    DeleteFileW(p1.c_str());
    HANDLE h1 = CreateFileW(p1.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    int fd1 = _open_osfhandle((intptr_t)h1, 0);
    FILE* f1 = _wfdopen(fd1, L"wb");

    printf("[1] CRT 层持留:FILE* 逐字节写 100 个 's'\n");
    for (int i = 0; i < 100; i++) {
        fputc('s', f1);
    }
    printf("    fwrite 完成未 fflush,旁路句柄看到的尺寸 -> %lld  (-1xx 是打不开的错误码)\n",
           sibling_size(p1));
    double t = now_ms();
    fflush(f1);
    printf("    fflush 之后,旁路句柄看到的尺寸       -> %lld  (fflush 耗时 %.3f ms)\n",
           sibling_size(p1), now_ms() - t);
    fclose(f1);
    DeleteFileW(p1.c_str());

    // ---------- [2][3] 三层计时:32MiB ----------
    printf("\n[2] 三层计时:32MiB(1MiB 块 fwrite)\n");
    std::wstring p2 = make_path(L"supp_e2b_big.bin");
    DeleteFileW(p2.c_str());
    HANDLE h2 = CreateFileW(p2.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    int fd2 = _open_osfhandle((intptr_t)h2, 0);
    FILE* f2 = _wfdopen(fd2, L"wb");

    const size_t chunk = 1 << 20;
    const int chunks = 32;
    std::string blk(chunk, 'B');

    double t0 = now_ms();
    for (int i = 0; i < chunks; i++) {
        if (fwrite(blk.data(), 1, chunk, f2) != chunk) {
            printf("fwrite 失败\n");
            return 1;
        }
    }
    double t1 = now_ms();
    long long sz_after_write = sibling_size(p2);

    fflush(f2);
    double t2 = now_ms();
    long long sz_after_fflush = sibling_size(p2);

    double t2b0 = now_ms();
    fflush(f2); // 再来一次:流里已无残余,量的是 fflush 本身的固定开销
    double t2b = now_ms() - t2b0;

    FlushFileBuffers(h2);
    double t3 = now_ms();
    long long sz_after_ffb = sibling_size(p2);

    fclose(f2); // 只做残余 fflush,不再动 h2 的所有权(fd 已被 fclose 关掉)
    DeleteFileW(p2.c_str());

    printf("    fwrite  32MiB      -> %8.3f ms  (进 OS 缓存)\n", t1 - t0);
    printf("    fflush 第一次      -> %8.3f ms  (清 CRT 残余)\n", t2 - t1);
    printf("    fflush 第二次(空) -> %8.3f ms  (无残余时的固定开销,对照用)\n", t2b);
    printf("    FlushFileBuffers   -> %8.3f ms  (刷缓存管理器脏页 + 存储栈)\n", t3 - t2);
    printf("    三段累计           -> %8.3f ms\n", t3 - t0);
    printf("\n[3] 尺寸可见性:旁路句柄在每步之后看到的字节数\n");
    printf("    fwrite 完(未 fflush) -> %lld  <- 大块 fwrite 已直通 OS,不在 CRT 缓冲里\n",
           sz_after_write);
    printf("    fflush 之后           -> %lld\n", sz_after_fflush);
    printf("    FlushFileBuffers 之后 -> %lld(尺寸早就在了,这一步买的是持久性,不是可见性)\n",
           sz_after_ffb);

    printf("\n分层:fwrite/fputc -> CRT 缓冲 -> WriteFile -> 系统缓存 -> 盘\n");
    printf("     fflush 清 CRT 层;FlushFileBuffers 清系统缓存层;谁也不越层替别人干活\n");
    return 0;
}
