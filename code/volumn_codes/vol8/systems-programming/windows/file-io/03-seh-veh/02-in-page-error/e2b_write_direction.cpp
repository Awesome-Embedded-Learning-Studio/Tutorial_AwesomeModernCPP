// e2b:E2 的写方向补充 —— 「文件里没有的页」上写一笔会怎样
// 预期(posix 直觉):SIGBUS 的孪生场景。实测:写进零填充页,强制回写时文件自己长回来
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION wfilter(void* ep, void*, PCONTEXT, void*) {
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    PEXCEPTION_RECORD r = pointers->ExceptionRecord;
    printf(
        "  [filter 触发] code=0x%08lX flags=0x%08lX info[0]=%llu info[1]=0x%llX info[2]=0x%08lX\n",
        (unsigned long)r->ExceptionCode, (unsigned long)r->ExceptionFlags,
        (unsigned long long)r->ExceptionInformation[0],
        (unsigned long long)r->ExceptionInformation[1], (unsigned long)r->ExceptionInformation[2]);
    return (EXCEPTION_DISPOSITION)1;
}

__attribute__((noinline)) void probe_write(volatile char* p, char v, int* result) {
    int faulted = 1;
    __try1(wfilter)* p = v;
    faulted = 0;
    __except1 if (faulted)* result = -1;
}

static const DWORD PAGE_SZ = 4096;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    lstrcpyA(path + n, "seh_veh_inpage_w.bin");

    // 1 页文件(无缓冲写)
    HANDLE hw = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    static unsigned char buf[PAGE_SZ] __attribute__((aligned(512)));
    memset(buf, 0x5A, sizeof(buf));
    DWORD w = 0;
    WriteFile(hw, buf, sizeof(buf), &w, NULL);
    CloseHandle(hw);

    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    printf("file size = %lld(1 页,内容 0x5A)\n", sz.QuadPart);

    HANDLE map = CreateFileMappingA(h, NULL, PAGE_READWRITE, 0, 3 * PAGE_SZ, NULL);
    char* view = (char*)MapViewOfFile(map, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
    printf("view=%p(3 页映射盖在 1 页文件上)\n", view);

    int result = 0;
    printf("== 写 view[2*4096] = 0x77(第 2 页,文件里没有)\n");
    probe_write(view + 2 * PAGE_SZ, 0x77, &result);
    printf("-> %d(0=写成功无异常)\n", result);

    printf("== 读回 view[2*4096]\n");
    printf("-> 0x%02x\n", (unsigned char)view[2 * PAGE_SZ]);

    printf("== FlushViewOfFile(强制回写第 2 页)\n");
    BOOL ok = FlushViewOfFile(view + 2 * PAGE_SZ, PAGE_SZ);
    printf("FlushViewOfFile -> %d\n", ok);
    GetFileSizeEx(h, &sz);
    printf("file size after flush = %lld(文件被写胖了)\n", sz.QuadPart);

    UnmapViewOfFile(view);
    CloseHandle(map);
    CloseHandle(h);
    DeleteFileA(path);
    printf("done\n");
    return 0;
}
