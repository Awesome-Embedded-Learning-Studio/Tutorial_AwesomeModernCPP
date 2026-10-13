// 走查补测:只解视图、映射对象句柄保留,SetEndOfFile 是否仍被拦(2026-10-02 复验通过)
// 结果: 1) mapping alive -> 0 gle=1224; 2) unmap+handle open -> 0 gle=1224; 3) unmap+close -> 1,
// final size=4096
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>
int main() {
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    lstrcpyA(path + n, "unmap_only_test.bin");
    HANDLE hw =
        CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    static unsigned char buf[3 * 4096];
    DWORD w;
    WriteFile(hw, buf, sizeof(buf), &w, NULL);
    CloseHandle(hw);
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE map = CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, 3 * 4096, NULL);
    char* view = (char*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    printf("view=%p\n", view);
    LARGE_INTEGER cut;
    cut.QuadPart = 4096;
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    SetLastError(0);
    BOOL ok = SetEndOfFile(h);
    printf("1) mapping alive:        SetEndOfFile -> %d gle=%lu\n", ok, GetLastError());
    UnmapViewOfFile(view);
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    SetLastError(0);
    ok = SetEndOfFile(h);
    printf("2) unmap, HANDLE OPEN:   SetEndOfFile -> %d gle=%lu\n", ok, GetLastError());
    CloseHandle(map);
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    SetLastError(0);
    ok = SetEndOfFile(h);
    printf("3) unmap + close handle: SetEndOfFile -> %d gle=%lu\n", ok, GetLastError());
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    printf("final size=%lld\n", (long long)sz.QuadPart);
    CloseHandle(h);
    DeleteFileA(path);
    return 0;
}
