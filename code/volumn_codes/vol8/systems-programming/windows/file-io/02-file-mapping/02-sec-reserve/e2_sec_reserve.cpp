// e2_sec_reserve.cpp —— SEC_RESERVE:页文件后备映射的"先保留后提交"两段式
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2_sec_reserve.cpp -o
//   e2_sec_reserve.exe
// 运行(逐阶段):
//   chmod +x e2_sec_reserve.exe
//   ./e2_sec_reserve.exe probe       # 只看 VirtualQuery 状态,不触雷
//   ./e2_sec_reserve.exe touch       # 访问未提交区 -> 0xC0000005
//   ./e2_sec_reserve.exe commit      # VirtualAlloc(MEM_COMMIT) 逐段提交后可写
//   ./e2_sec_reserve.exe file-backed # SEC_RESERVE + 真文件句柄 -> 期望被拒
//
// 观察点:
//   [probe]        64MiB SEC_RESERVE 保留区,VirtualQuery 应报 State=MEM_RESERVE /
//                  Protect=PAGE_NOACCESS;提交后变 State=MEM_COMMIT / PAGE_READWRITE。
//   [touch]        保留态未提交就访问 -> 0xC0000005(与 E1 的 PAGE_NOACCESS 同表现,
//                  保留区的 PTE 干脆不存在)。
//   [commit]       VirtualAlloc(基地址+偏移, MEM_COMMIT) 逐段提交:两段之间留空洞;
//                  已提交段读写/回读全对;对保留区重复 MEM_RESERVE -> ERROR_INVALID_ADDRESS。
//   [file-backed]  SEC_RESERVE 只对页文件后备(CreateFileMapping 的 hFile 传
//                  INVALID_HANDLE_VALUE)合法,配真文件句柄 -> CreateFileMapping 失败。
//   叙事对照:这是 Windows 版的懒分配——Linux 匿名 mmap 默认 overcommit、写时才给页,
//                  Windows 把"答应给你"和"真给你"拆成两步,中间这步就是 VirtualAlloc(MEM_COMMIT)。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

static const char* state_name(SIZE_T s) {
    switch (s) {
        case MEM_COMMIT:
            return "MEM_COMMIT(0x1000)";
        case MEM_RESERVE:
            return "MEM_RESERVE(0x2000)";
        case MEM_FREE:
            return "MEM_FREE(0x10000)";
        default:
            return "(other)";
    }
}

static std::string protect_name(DWORD p) {
    char buf[64];
    switch (p) {
        case PAGE_NOACCESS:
            return "PAGE_NOACCESS(0x01)";
        case PAGE_READONLY:
            return "PAGE_READONLY(0x02)";
        case PAGE_READWRITE:
            return "PAGE_READWRITE(0x04)";
        default:
            snprintf(buf, sizeof buf, "raw=0x%lx", (unsigned long)p);
            return buf;
    }
}

static const size_t kRegion = 64ull << 20; // 64 MiB

// 建 64MiB SEC_RESERVE 保留区(页文件后备)并映射
static char* setup() {
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE, 0,
                                  (DWORD)kRegion, nullptr);
    if (!m) {
        fprintf(stderr, "CreateFileMappingW(SEC_RESERVE) failed err=%lu\n", GetLastError());
        ExitProcess(1);
    }
    char* base = (char*)MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0);
    if (!base) {
        fprintf(stderr, "MapViewOfFile failed err=%lu\n", GetLastError());
        ExitProcess(1);
    }
    // 映射句柄故意不关:视图在,句柄泄漏到进程退出,实验程序不背 RAII 包袱
    printf("  SEC_RESERVE 64MiB 映射成功,视图=%p\n", base);
    return base;
}

static LONG crash_log(EXCEPTION_POINTERS* ep) {
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    fprintf(stderr, "[unhandled-exception] code=0x%08lX\n", (unsigned long)r->ExceptionCode);
    fprintf(stderr, "  ExceptionInformation[0]   = %llu  (%s)\n",
            (unsigned long long)r->ExceptionInformation[0],
            r->ExceptionInformation[0] == 0 ? "读访问冲突" : "写访问冲突");
    fprintf(stderr, "  ExceptionInformation[1]   = %p  (目标数据地址)\n",
            (void*)r->ExceptionInformation[1]);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void query_at(const char* base, size_t off, const char* tag) {
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(base + off, &mbi, sizeof mbi);
    printf("  VirtualQuery(%-14s +0x%06zx): State=%-19s Protect=%-18s RegionSize=0x%zx\n", tag, off,
           state_name(mbi.State), protect_name(mbi.Protect).c_str(), mbi.RegionSize);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s probe|touch|commit|file-backed\n", argv[0]);
        return 2;
    }
    SetUnhandledExceptionFilter(crash_log);
    const std::string phase = argv[1];

    if (phase == "file-backed") {
        // 文件 16KiB,映射对象申请 1MiB SEC_RESERVE:若"保留"被文件后备兑现,
        // 映射时文件会被零扩展到映射对象大小(文档行为),文件长度就是试金石
        printf("[file-backed] SEC_RESERVE 配真文件句柄(文件 16KiB,映射对象 1MiB)\n");
        wchar_t dir[MAX_PATH];
        GetTempPathW(MAX_PATH, dir);
        std::wstring p = std::wstring(dir) + L"sysprog-e2.bin";
        HANDLE f = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        char junk[16384];
        memset(junk, 'E', sizeof junk);
        DWORD w = 0;
        WriteFile(f, junk, sizeof junk, &w, nullptr); // 先放 16KiB 真内容
        DWORD fsz0 = GetFileSize(f, nullptr);
        SetLastError(0);
        HANDLE m =
            CreateFileMappingW(f, nullptr, PAGE_READWRITE | SEC_RESERVE, 0, 1 << 20, nullptr);
        printf("  CreateFileMappingW(真文件 + SEC_RESERVE) -> %s  err=%lu\n", m ? "成功" : "NULL",
               GetLastError());
        if (m) {
            char* v = (char*)MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 0);
            printf("  MapViewOfFile                            -> %p\n", v);
            if (v) {
                MEMORY_BASIC_INFORMATION mbi;
                VirtualQuery(v, &mbi, sizeof mbi);
                printf("  VirtualQuery: State=%s Protect=%s\n", state_name(mbi.State),
                       protect_name(mbi.Protect).c_str());
                printf("  写 v[0]='F' ...");
                fflush(stdout);
                v[0] = 'F';
                printf(" 写入后 v[0]=%c <- 文件后备把 SEC_RESERVE 兑现成了已提交\n", v[0]);
                FlushViewOfFile(v, 4096);
                UnmapViewOfFile(v);
            }
        }
        DWORD fsz1 = GetFileSize(f, nullptr);
        printf("  文件长度:映射前 %lu -> 映射后 %lu(被零扩展到 1MiB = 文件后备兑现)\n", fsz0, fsz1);
        if (m)
            CloseHandle(m);
        CloseHandle(f);
        DeleteFileW(p.c_str());
        return 0;
    }

    if (phase == "probe") {
        printf("[probe] 保留区状态体检(不访问任何字节)\n");
        char* base = setup();
        query_at(base, 0, "区首");
        query_at(base, 4 << 20, "中部");
        query_at(base, kRegion - 4096, "区尾");
        return 0;
    }

    if (phase == "touch") {
        char* base = setup();
        query_at(base, 0, "写入前");
        printf("  写 base[0]='Z' ...");
        fflush(stdout);
        base[0] = 'Z'; // 保留态,PTE 都没有
        printf("  base[0]=%c(不应到达)\n", base[0]);
        return 0;
    }

    if (phase == "commit") {
        char* base = setup();
        const size_t seg = 1ull << 20; // 每段 1MiB
        printf("[commit] 两段式:先保留整块,再逐段 VirtualAlloc(MEM_COMMIT)\n");
        printf("  段A=[0,1MiB) 段B=[32MiB,33MiB),两段之间 31MiB 保持保留态\n");

        void* a = VirtualAlloc(base, seg, MEM_COMMIT, PAGE_READWRITE);
        void* b = VirtualAlloc(base + (32ull << 20), seg, MEM_COMMIT, PAGE_READWRITE);
        printf("  VirtualAlloc(段A, MEM_COMMIT)              -> %p(%s)\n", a, a ? "ok" : "fail");
        printf("  VirtualAlloc(段B, MEM_COMMIT)              -> %p(%s)\n", b, b ? "ok" : "fail");

        query_at(base, 0, "段A");
        query_at(base, 16 << 20, "空洞");
        query_at(base, 32 << 20, "段B");

        // 已提交段读写
        memset(base, 0x5A, seg);
        memset(base + (32ull << 20), 0xA5, seg);
        unsigned sum = 0;
        for (size_t i = 0; i < seg; ++i)
            sum += (unsigned char)base[i];
        for (size_t i = 0; i < seg; ++i)
            sum += (unsigned char)base[(32ull << 20) + i];
        printf("  段A全0x5A/段B全0xA5 写入+回读校验和 = %u(期望 %u)\n", sum,
               (unsigned)(0x5Au * seg + 0xA5u * seg));

        // 保留区上重复 MEM_RESERVE(带提交)应被拒
        SetLastError(0);
        void* bad =
            VirtualAlloc(base + (16ull << 20), seg, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        printf("  VirtualAlloc(空洞, RESERVE|COMMIT)          -> %p "
               "err=%lu(487=ERROR_INVALID_ADDRESS)\n",
               bad, GetLastError());

        // 部分提交时的整体尺寸不变:RegionSize 仍按保留边界
        query_at(base, 8 << 20, "空洞2");
        return 0;
    }

    fprintf(stderr, "未知阶段 %s\n", phase.c_str());
    return 2;
}
