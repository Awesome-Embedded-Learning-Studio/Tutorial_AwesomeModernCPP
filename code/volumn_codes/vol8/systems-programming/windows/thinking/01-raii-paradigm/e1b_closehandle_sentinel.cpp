// e1b_closehandle_sentinel.cpp —— 复核:CloseHandle 对 NULL / -1 / -2 / 野值各返回什么
//
// 背景:e1 的 [2] 节意外测到 CloseHandle(INVALID_HANDLE_VALUE) 返回 TRUE,单独隔离复核。
// -1 是 GetCurrentProcess() 的伪句柄,-2 是 GetCurrentThread() 的伪句柄;
// 结论(实测 Win11 26200):两个伪句柄都"关"成功且不动 last-error,
// NULL 与野值才失败(err=6,ERROR_INVALID_HANDLE)。
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1b_closehandle_sentinel.cpp -o
//   e1b_closehandle_sentinel.exe
// 运行:
//   chmod +x e1b_closehandle_sentinel.exe && ./e1b_closehandle_sentinel.exe

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdio>
#include <windows.h>
int main() {
    struct {
        const char* name;
        HANDLE h;
    } cases[] = {
        {"NULL", nullptr},
        {"INVALID_HANDLE_VALUE", INVALID_HANDLE_VALUE},
        {"GetCurrentProcess()", GetCurrentProcess()},
        {"(HANDLE)-2", (HANDLE)-2},
        {"(HANDLE)0x1234", (HANDLE)0x1234},
    };
    for (auto& c : cases) {
        SetLastError(1234); // 放个哨兵值,看清调用到底动没动 last-error
        BOOL r = CloseHandle(c.h);
        DWORD e = GetLastError();
        printf("CloseHandle(%-22s = %16p) -> ret=%d err=%lu\n", c.name, c.h, r, e);
    }
    return 0;
}
