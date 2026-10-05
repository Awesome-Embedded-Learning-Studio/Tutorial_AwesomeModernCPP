// e2c:截短的各条路在「文件被映射」时是不是都被拦(现代 API 一样吗)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static const DWORD PAGE_SZ = 4096;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    lstrcpyA(path + n, "veh_inpage_c.bin");
    HANDLE hw =
        CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING, NULL);
    static unsigned char buf[3 * PAGE_SZ] __attribute__((aligned(512)));
    DWORD w;
    WriteFile(hw, buf, sizeof(buf), &w, NULL);
    CloseHandle(hw);

    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE map = CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, 3 * PAGE_SZ, NULL);
    char* view = (char*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    printf("view=%p (3-page mapping active)\n", view);

    // 1) 经典 SetEndOfFile
    LARGE_INTEGER cut;
    cut.QuadPart = PAGE_SZ;
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    BOOL ok = SetEndOfFile(h);
    printf("SetEndToFile(4096)                 -> %d gle=%lu\n", ok, GetLastError());

    // 2) SetFileInformationByHandle FileEndOfFileInfo
    FILE_END_OF_FILE_INFO ei{4096};
    ok = SetFileInformationByHandle(h, FileEndOfFileInfo, &ei, sizeof(ei));
    printf("FileEndOfFileInfo(4096)             -> %d gle=%lu\n", ok, GetLastError());

    // 3) FileAllocationInfo(收分配)
    FILE_ALLOCATION_INFO ai{4096};
    ok = SetFileInformationByHandle(h, FileAllocationInfo, &ai, sizeof(ai));
    printf("FileAllocationInfo(4096)            -> %d gle=%lu\n", ok, GetLastError());

    // 4) 对照组:解除映射 + 关掉映射对象句柄再截(应该成功)
    UnmapViewOfFile(view);
    CloseHandle(map);
    SetFilePointerEx(h, cut, NULL, FILE_BEGIN);
    ok = SetEndOfFile(h);
    printf("unmap + SetEndOfFile(4096)          -> %d gle=%lu\n", ok, GetLastError());
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    printf("final size=%lld\n", sz.QuadPart);
    CloseHandle(h);
    DeleteFileA(path);
    printf("done\n");
    return 0;
}
