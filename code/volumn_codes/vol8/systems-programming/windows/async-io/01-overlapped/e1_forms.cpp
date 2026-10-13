// e1_forms.cpp —— E1 三种打开形态下 ReadFile 的返回形态与位置来源
//
// 同一个数据文件,三种用法对齐着测:
//   [a] 同步句柄 + lpOverlapped=NULL      —— file-io/01 已讲过的正主,这里只回放三连读
//   [b] 同步句柄 + lpOverlapped(带 Offset) —— 位置改从 OVERLAPPED.Offset 取,但调用仍阻塞
//   [c] 异步句柄(FILE_FLAG_OVERLAPPED)     —— Offset 独立定位,返回形态二态(TRUE 内联 / 997 在途)
// 外加三组探针:EOF 三例(同步/异步各一遍)、异步句柄上裸调 ReadFile、异步句柄上 SetFilePointer。
//
// 数据文件 65536 字节,内容按 8 字节对齐编码:第 i 块的 8 字节存的就是它自己的偏移 i*8,
// 读回什么值,偏移就对得上号,位置来源一目了然。
// 环境:Win11 26200 / NTFS / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
// 数据文件烧死为 C:/msys64/tmp/wasync/e1data.bin。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

const char* kPath = "C:/msys64/tmp/wasync/e1data.bin";
constexpr DWORD kSize = 65536; // 8192 块 × 8 字节
constexpr DWORD kTail = 65532; // 跨过 EOF 的起点:只剩 4 字节可读

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

const char* gle_name(DWORD e) {
    switch (e) {
        case ERROR_IO_PENDING:
            return "ERROR_IO_PENDING";
        case ERROR_HANDLE_EOF:
            return "ERROR_HANDLE_EOF";
        case ERROR_INVALID_PARAMETER:
            return "ERROR_INVALID_PARAMETER";
        case ERROR_NEGATIVE_SEEK:
            return "ERROR_NEGATIVE_SEEK";
        default:
            return "(见 gle)";
    }
}

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

// 打开同步句柄(无 FILE_FLAG_OVERLAPPED)
HANDLE open_sync() {
    return CreateFileA(kPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}
// 打开异步句柄(FILE_FLAG_OVERLAPPED)
HANDLE open_async() {
    return CreateFileA(kPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
}

DWORD file_pointer(HANDLE h) {
    DWORD ptr = SetFilePointer(h, 0, nullptr, FILE_CURRENT);
    return ptr;
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    make_data();
    std::uint64_t val = 0;
    DWORD got = 0;
    BOOL ok = FALSE;

    LOG("== [a] 同步句柄 + lpOverlapped=NULL:file-io/01 的正主,回放三连读 ==");
    {
        HANDLE h = open_sync();
        for (int i = 0; i < 3; ++i) {
            ok = ReadFile(h, &val, 8, &got, nullptr);
            LOG("第%d次裸读: ret=%d got=%lu 值=%llu (期望 %d)", i + 1, (int)ok, got,
                (unsigned long long)val, i * 8);
        }
        // EOF 三例之(iii):正对 EOF 读 8 字节
        SetFilePointer(h, kSize, nullptr, FILE_BEGIN);
        ok = ReadFile(h, &val, 8, &got, nullptr);
        LOG("同步+EOF(0字节可读): ret=%d got=%lu (file-io/01 口径:TRUE 且 got=0)", (int)ok, got);
        // EOF 三例之(ii):骑在 EOF 上,只剩 4 字节
        SetFilePointer(h, kTail, nullptr, FILE_BEGIN);
        ok = ReadFile(h, &val, 8, &got, nullptr);
        LOG("同步+骑EOF(4字节可读): ret=%d got=%lu (口径:TRUE 且 got=剩余字节数)", (int)ok, got);
        CloseHandle(h);
    }

    LOG("== [b] 同步句柄 + lpOverlapped:位置搬进 Offset,调用还是阻塞的 ==");
    {
        HANDLE h = open_sync();
        // 先裸读两块,把文件指针推到 16
        ReadFile(h, &val, 8, &got, nullptr);
        ReadFile(h, &val, 8, &got, nullptr);
        LOG("预备:两次裸读后文件指针 = %lu", file_pointer(h));

        OVERLAPPED ov{};
        ov.Offset = 8000;
        val = 0;
        ok = ReadFile(h, &val, 8, &got, &ov);
        LOG("带 Offset=8000 读: ret=%d got=%lu 值=%llu gle=%lu(%s) —— 永远不回 997,同步完成",
            (int)ok, got, (unsigned long long)val, GetLastError(), gle_name(GetLastError()));
        LOG("读完文件指针 = %lu(不是 16 了:同步句柄上 OVERLAPPED 读会把指针跟到 Offset+字节数)",
            file_pointer(h));
        ReadFile(h, &val, 8, &got, nullptr);
        LOG("接着裸读: 值=%llu(顺着跟过来的新指针取)—— 同步句柄上两套定位其实共用一条指针",
            (unsigned long long)val);

        // 同步句柄 + OVERLAPPED 的 EOF 形态
        OVERLAPPED ov2{};
        ov2.Offset = kTail;
        ok = ReadFile(h, &val, 8, &got, &ov2);
        LOG("同步+OVERLAPPED 骑EOF: ret=%d got=%lu gle=%lu(%s)", (int)ok, got, GetLastError(),
            gle_name(GetLastError()));
        CloseHandle(h);
    }

    LOG("== [c] 异步句柄(FILE_FLAG_OVERLAPPED):Offset 独立定位,返回二态 ==");
    {
        HANDLE h = open_async();
        OVERLAPPED ovA{}, ovB{};
        ovA.Offset = 2000;
        ovB.Offset = 1000; // 故意后发先读(投递序与偏移序相反)
        val = 0;
        ok = ReadFile(h, &val, 8, &got, &ovA);
        LOG("读 Offset=2000: ret=%d gle=%lu(%s) —— 异步句柄上这轮直接回 997 在途(内核没当场做完)",
            (int)ok, GetLastError(), gle_name(GetLastError()));
        DWORD g2 = 0;
        std::uint64_t v2 = 0;
        ok = ReadFile(h, &v2, 8, &g2, &ovB);
        LOG("读 Offset=1000: ret=%d gle=%lu(%s)", (int)ok, GetLastError(),
            gle_name(GetLastError()));
        ok = GetOverlappedResult(h, &ovA, &got, TRUE);
        LOG("GOR(ovA): ret=%d got=%lu 值=%llu —— 一把句柄两处偏移,数据各归各", (int)ok, got,
            (unsigned long long)val);
        ok = GetOverlappedResult(h, &ovB, &g2, TRUE);
        LOG("GOR(ovB): ret=%d got=%lu 值=%llu —— 投递序 2000→1000,定位靠 Offset 不靠指针", (int)ok,
            g2, (unsigned long long)v2);

        // EOF 三例(异步句柄版)
        struct {
            DWORD off;
            const char* tag;
        } cases[] = {
            {kSize - 8, "满读(8字节可读)"},
            {kTail, "骑EOF(4字节可读)"},
            {kSize, "正对EOF(0字节可读)"},
        };
        for (auto& c : cases) {
            OVERLAPPED ov{};
            ov.Offset = c.off;
            val = 0;
            got = 0;
            ok = ReadFile(h, &val, 8, &got, &ov);
            DWORD e1 = GetLastError();
            BOOL g = GetOverlappedResult(h, &ov, &got, TRUE);
            DWORD e2 = GetLastError();
            LOG("异步+%s: ReadFile ret=%d gle=%lu(%s); GOR ret=%d got=%lu gle=%lu(%s)", c.tag,
                (int)ok, e1, gle_name(e1), (int)g, got, e2, gle_name(e2));
        }

        // 探针:异步句柄上裸调 ReadFile(不给 lpOverlapped)
        ok = ReadFile(h, &val, 8, &got, nullptr);
        LOG("异步句柄裸读(不给 OVERLAPPED): ret=%d gle=%lu(%s) —— 文档口径:必须给", (int)ok,
            GetLastError(), gle_name(GetLastError()));
        // 探针:异步句柄上指针被架空的直接证据 —— 把指针推到 8000,再从 Offset=0 读
        SetFilePointer(h, 8000, nullptr, FILE_BEGIN);
        OVERLAPPED ovP{};
        ovP.Offset = 0;
        DWORD gotP = 0;
        std::uint64_t vP = 0;
        ok = ReadFile(h, &vP, 8, &gotP, &ovP);
        GetOverlappedResult(h, &ovP, &gotP, TRUE);
        LOG("指针推到 8000 后从 Offset=0 读: 值=%llu(不是 8000)—— 异步句柄只认 "
            "OVERLAPPED.Offset,文件指针形同虚设",
            (unsigned long long)vP);
        CloseHandle(h);
    }
    LOG("e1 完");
    return 0;
}
