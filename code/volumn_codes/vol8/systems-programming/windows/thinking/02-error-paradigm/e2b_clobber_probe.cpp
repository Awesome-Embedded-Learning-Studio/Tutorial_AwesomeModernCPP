// e2b_clobber_probe.cpp —— 哪些"成功"的调用会冲掉 last-error?逐个 API 实测
//
// 方法:先 SetLastError(2) 放哨兵,只调一个目标 API,再读 GetLastError:
//   报 2   -> 没动槽位(成功调用不保证清除,也不保证保留,逐 API 而定)
//   报 0   -> 成功路径把槽位清了:这就是"插一个成功调用就丢错误"的实锤
//   报其他 -> 成功路径内部留下的杂音(内部吞掉的失败探测等)
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2b_clobber_probe.cpp -o
//   e2b_clobber_probe.exe
// 运行:
//   chmod +x e2b_clobber_probe.exe && ./e2b_clobber_probe.exe

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

// 每个 case:置哨兵 2 -> 调 API -> 读槽位
static DWORD probe(const char* name, void (*fn)()) {
    SetLastError(2); // 假装刚才 CreateFileW 失败留下的 ERROR_FILE_NOT_FOUND
    fn();
    DWORD e = GetLastError();
    const char* verdict = (e == 2) ? "保留" : (e == 0 ? "清零!" : "改成杂音!");
    printf("  %-34s -> err=%-4lu %s\n", name, e, verdict);
    return e;
}

static HANDLE g_file = INVALID_HANDLE_VALUE;
static HANDLE g_evt = NULL;
static HMODULE g_mod = NULL;
static char g_buf[512];
static wchar_t g_wbuf[512];
static SYSTEM_INFO g_si;
static LARGE_INTEGER g_qpc;
static DWORD g_n = 0;

int main() {
    // 先备好真资源
    g_file = CreateFileW(L"e2b_probe.bin", GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    g_evt = CreateEventW(nullptr, FALSE, TRUE, nullptr); // 已授信的手动事件
    g_mod = LoadLibraryW(L"kernel32.dll");

    printf("== 成功的调用对 last-error 槽位的影响(哨兵=2)==\n");

    probe("GetProcessId(GetCurrentProcess)", [] { g_n = GetProcessId(GetCurrentProcess()); });
    probe("GetCurrentProcessId()", [] { g_n = GetCurrentProcessId(); });
    probe("CreateFileW(真成功,开空闲文件)", [] {
        HANDLE other = CreateFileW(L"e2b_free.bin", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        // 断言真的成功了:开的是无争议的新文件
        if (other == INVALID_HANDLE_VALUE) {
            printf("  (这行不该出现)\n");
        }
        CloseHandle(other);
    });
    probe("CreateFileW(撞独占锁,失败32)", [] {
        // g_file 以 dwShareMode=0 挂着 e2b_probe.bin,再开必失败
        HANDLE h = CreateFileW(L"e2b_probe.bin", GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    });
    probe("ReadFile(成功)", [] {
        DWORD r = 0;
        if (!ReadFile(g_file, g_buf, 0, &r, nullptr)) {
            printf("  (ReadFile 返回 FALSE,这行不算)\n");
        }
    });
    probe("WriteFile(成功)", [] {
        DWORD w = 0;
        WriteFile(g_file, "x", 1, &w, nullptr);
    });
    probe("GetFullPathNameW(成功)", [] { g_n = GetFullPathNameW(L"x", 511, g_wbuf, nullptr); });
    probe("GetFileAttributesExW(成功)", [] {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        GetFileAttributesExW(L"e2b_probe.bin", GetFileExInfoStandard, &fa);
    });
    probe("FindFirstFileW+FindClose(成功)", [] {
        WIN32_FIND_DATAW fd{};
        HANDLE ff = FindFirstFileW(L"e2b_probe.bin", &fd);
        if (ff != INVALID_HANDLE_VALUE) {
            FindClose(ff);
        }
    });
    probe("RegOpenKeyExW+RegCloseKey(成功)", [] {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            RegCloseKey(k);
        }
    });
    probe("GetComputerNameW(成功)", [] {
        DWORD n = 511;
        GetComputerNameW(g_wbuf, &n);
    });
    probe("GetFileSizeEx(成功)", [] {
        LARGE_INTEGER sz{};
        GetFileSizeEx(g_file, &sz);
    });
    probe("GetTempPathW(成功)", [] { g_n = GetTempPathW(511, g_wbuf); });
    probe("WideCharToMultiByte(成功)", [] {
        g_n = WideCharToMultiByte(CP_UTF8, 0, L"ok", -1, g_buf, sizeof g_buf, nullptr, nullptr);
    });
    probe("GetModuleHandleW(成功)", [] { (void)GetModuleHandleW(L"kernel32.dll"); });
    probe("LoadLibraryW(成功)", [] { FreeLibrary(LoadLibraryW(L"kernel32.dll")); });
    probe("Sleep(0)", [] { Sleep(0); });
    probe("GetSystemInfo(成功)", [] { GetSystemInfo(&g_si); });
    probe("QueryPerformanceCounter(成功)", [] { QueryPerformanceCounter(&g_qpc); });
    probe("WaitForSingleObject(已授信事件)", [] { WaitForSingleObject(g_evt, 0); });
    probe("SetEvent(成功)", [] { SetEvent(g_evt); });
    probe("ResetEvent(成功)", [] { ResetEvent(g_evt); });
    probe("GetStdHandle(成功)", [] { (void)GetStdHandle(STD_OUTPUT_HANDLE); });
    probe("VirtualAlloc+VirtualFree(成功)", [] {
        void* p = VirtualAlloc(nullptr, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        VirtualFree(p, 0, MEM_RELEASE);
    });
    probe("GetFileAttributesW(存在的文件)", [] { g_n = GetFileAttributesW(L"e2b_probe.bin"); });
    probe("FormatMessageW(成功)", [] {
        wchar_t* buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, 2, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        LocalFree(buf);
    });
    probe("OutputDebugStringW(空串)", [] { OutputDebugStringW(L""); });
    probe("new char[1000] + delete[]", [] {
        char* p = new char[1000];
        p[0] = 1;
        delete[] p;
    });
    probe("fprintf(stderr)(不缓冲)", [] { fprintf(stderr, " "); });
    probe("fflush(stdout)", [] { fflush(stdout); });

    printf("\n== 失败的调用一定覆盖槽位(对照组)==\n");
    SetLastError(2);
    (void)GetProcessId((HANDLE)0xDEADBEEF); // 失败:句柄无效
    printf("  GetProcessId(野句柄,失败)      -> err=%lu\n", GetLastError());
    SetLastError(2);
    (void)CreateFileW(L"另一个不存在的文件.bin", GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    printf("  CreateFileW(又一个不存在,失败)  -> err=%lu\n", GetLastError());

    CloseHandle(g_file);
    CloseHandle(g_evt);
    (void)g_mod;
    DeleteFileW(L"e2b_probe.bin");
    DeleteFileW(L"e2b_free.bin");
    return 0;
}
