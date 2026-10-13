// e1_iocp_basics.cpp —— IOCP E1 建端口、挂句柄、GetQueuedCompletionStatus 取完成
//
// 三件事:
//   [1] CreateIoCompletionPort(INVALID_HANDLE_VALUE,...) 造一枚裸端口(并发值给 0 = 处理器数)
//   [2] 把文件句柄挂上端口(带 key),投三发不同偏移的读,GQCS 循环收:每包带回
//       「字节数 / key / 当初投递的 OVERLAPPED 指针」三件套 —— 指针身份逐一核对
//   [3] 空队列上的 GQCS(带超时)收 258;第二把句柄挂另一个 key,完成包各认各的 key
// 环境:Win11 26200 / NTFS / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
// 数据文件烧死为 C:/msys64/tmp/wasync/e1data.bin(e1 造的偏移自编码文件,8 字节存自己的偏移)。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

const char* kPath = "C:/msys64/tmp/wasync/e1data.bin";
constexpr DWORD kSize = 65536;

long g_t0;
long ms_now() {
    return (long)(GetTickCount64() - (ULONGLONG)g_t0);
}
#define LOG(...)                             \
    do {                                     \
        std::printf("[%6ld ms] ", ms_now()); \
        std::printf(__VA_ARGS__);            \
        std::printf("\n");                   \
        std::fflush(stdout);                 \
    } while (0)

void make_data() {
    HANDLE h = CreateFileA(kPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::printf("make_data 打不开, gle=%lu\n", GetLastError());
        std::exit(1);
    }
    std::vector<std::uint64_t> blk(kSize / 8);
    for (DWORD i = 0; i < kSize / 8; ++i)
        blk[i] = (std::uint64_t)i * 8u;
    DWORD n = 0;
    WriteFile(h, blk.data(), kSize, &n, nullptr);
    CloseHandle(h);
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    make_data();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    LOG("[1] 建裸端口");
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    LOG("CreateIoCompletionPort(INVALID_HANDLE_VALUE,...) = %llu;并发值给 0 的含义=处理器数(本机 "
        "%lu)",
        (unsigned long long)(UINT_PTR)port, si.dwNumberOfProcessors);

    LOG("[2] 挂文件句柄(key=4242),投三发不同偏移的读");
    HANDLE f1 = CreateFileA(kPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    ULONG_PTR key1 = 4242;
    HANDLE ok_assoc = CreateIoCompletionPort(f1, port, key1, 0);
    LOG("挂接返回 %llu(就是那枚端口)", (unsigned long long)(UINT_PTR)ok_assoc);

    OVERLAPPED ov[3]{};
    std::uint64_t val[3]{};
    DWORD offs[3] = {3000, 1000, 2000}; // 投递偏移乱序
    for (int i = 0; i < 3; ++i) {
        ov[i].Offset = offs[i];
        DWORD got = 0;
        SetLastError(0);
        BOOL ok = ReadFile(f1, &val[i], 8, &got, &ov[i]);
        LOG("  投递#%d Offset=%lu: ret=%d gle=%lu(997 在途)", i + 1, offs[i], (int)ok,
            GetLastError());
    }
    for (int k = 0; k < 3; ++k) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 1000);
        int who = -1;
        for (int i = 0; i < 3; ++i)
            if (pov == &ov[i])
                who = i;
        LOG("  GQCS 第%d包: ret=%d bytes=%lu key=%lu OVERLAPPED* 与投递#%d 逐一相认(%s) 值=%llu",
            k + 1, (int)g, bytes, (unsigned long)key, who + 1, who >= 0 ? "指针同一" : "认不出",
            who >= 0 ? (unsigned long long)val[who] : 0ull);
    }

    LOG("[3] 空队列超时 + 第二把句柄另一个 key");
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED pov = nullptr;
    long tw = ms_now();
    BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 800);
    LOG("GQCS(空队列,800ms): ret=%d 历时 %ld ms gle=%lu(258=WAIT_TIMEOUT)", (int)g, ms_now() - tw,
        GetLastError());

    HANDLE f2 = CreateFileA(kPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    ULONG_PTR key2 = 777;
    CreateIoCompletionPort(f2, port, key2, 0);
    OVERLAPPED ov2{};
    std::uint64_t v2 = 0;
    ov2.Offset = 4000;
    DWORD got2 = 0;
    ReadFile(f2, &v2, 8, &got2, &ov2);
    g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 1000);
    LOG("第二把句柄(key=777)的一发: GQCS ret=%d key=%lu 值=%llu —— 完成包带着各自句柄的 key 回来",
        (int)g, (unsigned long)key, (unsigned long long)v2);

    CloseHandle(f1);
    CloseHandle(f2);
    CloseHandle(port);
    LOG("iocp-e1 完");
    return 0;
}
