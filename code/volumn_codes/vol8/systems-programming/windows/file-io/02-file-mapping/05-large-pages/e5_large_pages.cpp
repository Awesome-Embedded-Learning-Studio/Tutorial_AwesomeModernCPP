// e5_large_pages.cpp —— 大页映射的特权门槛(SEC_LARGE_PAGES / MEM_LARGE_PAGES)
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e5_large_pages.cpp -o
//   e5_large_pages.exe
// 运行:
//   chmod +x e5_large_pages.exe && ./e5_large_pages.exe
//
// 观察点:
//   [1] GetLargePageMinimum() -> 大页粒度(本机预期 2MiB);
//   [2] SeLockMemoryPrivilege 特权探测:打开自己进程令牌 -> LookupPrivilegeValue ->
//       AdjustTokenPrivileges。经典陷阱:AdjustTokenPrivileges 返回 TRUE 不代表成功,
//       必须再查 GetLastError——本机实测 TRUE + 1300(ERROR_NOT_ALL_ASSIGNED,账户
//       根本没被授予该特权);随后的申请才是 1314(ERROR_PRIVILEGE_NOT_HELD);
//   [3] 若特权拿不到,VirtualAlloc(MEM_LARGE_PAGES) 与
//       CreateFileMappingW(SEC_LARGE_PAGES) 的失败就是"需要特权未测"的实证;
//   [4] 顺带记录:普通页 VirtualAlloc 对照成功,失败确实只与大页有关。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

int main() {
    printf("[1] 大页粒度\n");
    SIZE_T lpm = GetLargePageMinimum();
    printf("  GetLargePageMinimum() = %zu(%s,普通小页 %zu 字节,比值 %zu)\n", lpm,
           lpm == (2u << 20) ? "2MiB" : "?", (size_t)4096, lpm / 4096);

    printf("[2] SeLockMemoryPrivilege 特权探测\n");
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        printf("  OpenProcessToken 失败 err=%lu\n", GetLastError());
        return 1;
    }
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeLockMemoryPrivilege", &luid)) {
        printf("  LookupPrivilegeValueW(SE_LOCK_MEMORY_NAME) 失败 err=%lu\n", GetLastError());
        return 1;
    }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(0);
    BOOL adj = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof tp, nullptr, nullptr);
    DWORD adj_err = GetLastError(); // 经典陷阱:TRUE 也可能没拿到,看这里
    printf("  AdjustTokenPrivileges ret=%d  GetLastError=%lu%s%s\n", adj, adj_err,
           adj_err == 1300 ? "(1300=ERROR_NOT_ALL_ASSIGNED)" : "",
           adj_err == ERROR_PRIVILEGE_NOT_HELD ? "(1314=ERROR_PRIVILEGE_NOT_HELD)" : "");
    CloseHandle(tok);

    if (adj_err != ERROR_SUCCESS) {
        printf("  [判定] 本账户没有\"锁定内存页\"用户权限(组策略 secpol.msc "
               "可授予,需注销重登),大页路径按\"需要特权未测\"入档\n");
    }

    printf("[3] 大页申请实证\n");
    const size_t WANT = 2ull << 20; // 一个大页
    SetLastError(0);
    void* big =
        VirtualAlloc(nullptr, WANT, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    printf("  VirtualAlloc(MEM_LARGE_PAGES, 2MiB)      -> %p  err=%lu\n", big, GetLastError());

    SetLastError(0);
    HANDLE m =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                           PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES, 0, (DWORD)WANT, nullptr);
    printf("  CreateFileMappingW(SEC_LARGE_PAGES, 2MiB) -> %p  err=%lu\n", m, GetLastError());
    if (m)
        CloseHandle(m);

    printf("[4] 普通页对照\n");
    SetLastError(0);
    void* small = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    printf("  VirtualAlloc(普通页 4KiB)                -> %p  err=%lu\n", small, GetLastError());
    if (small)
        VirtualFree(small, 0, MEM_RELEASE);
    if (big)
        VirtualFree(big, 0, MEM_RELEASE);
    return 0;
}
