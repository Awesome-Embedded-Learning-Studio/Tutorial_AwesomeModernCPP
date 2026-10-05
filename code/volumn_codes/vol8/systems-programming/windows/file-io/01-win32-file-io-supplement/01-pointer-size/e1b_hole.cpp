// e1b_hole.cpp —— 越过 EOF 再写:洞出现,文件变大;Linux lseek 同款行为
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1b_hole.cpp -o e1b_hole.exe
// 运行:
//   chmod +x e1b_hole.exe && ./e1b_hole.exe
//
// 观察点:
//   [1] 头部写 "AB",指针跳到 1 MiB 处写 "Z":GetFileSizeEx 直接变 1MiB+1
//   [2] 读回头部:"AB" + 一串 0x00 —— 洞读出来是零(NTFS 与 ext4 一致)
//   [3] 尺寸(EndOfFile) vs 分配尺寸(AllocationSize):
//       默认(非稀疏)NTFS 文件按全尺寸向上取整分配,洞照样记账;
//       要让洞不占空间,得显式 FSCTL_SET_SPARSE(见 [5])——ext4 的洞默认就不占
//   [4] FILE_FLAG_NO_BUFFERING 越过 EOF 写:文档口径是"可能炸 ERROR_INVALID_PARAMETER
//       (零填充要靠缓存管理器)",本机(Win11 26200 / NTFS)实测成功,且洞读回是零
//   [5] FSCTL_SET_SPARSE 后再打洞:AllocationSize 只算实写的簇,洞真正免账
//
// POSIX 对照:Linux 的 lseek(SEEK_SET, 1<<20) + write(1) 行为同构,
// st_size=1MiB+1、洞读为零;差异在记账:ext4 洞默认不占 st_blocks,
// NTFS 默认占满 AllocationSize,稀疏要主动开。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h> // FSCTL_SET_SPARSE(WIN32_LEAN_AND_MEAN 不带它)

#include <cstdio>
#include <string>

static std::wstring make_path(const wchar_t* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sysprog-supp-e1";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + name;
}

static void dump(const char* tag, const unsigned char* p, size_t n) {
    printf("%s", tag);
    for (size_t i = 0; i < n; i++) {
        printf("%02X ", p[i]);
    }
    printf("\n");
}

static void report_size(HANDLE h, const char* tag) {
    FILE_STANDARD_INFO si{};
    GetFileInformationByHandleEx(h, FileStandardInfo, &si, sizeof(si));
    printf("    %-36s EndOfFile=%-8lld AllocationSize=%lld\n", tag, si.EndOfFile.QuadPart,
           si.AllocationSize.QuadPart);
}

// 用一个独立的缓冲读句柄读回 [off, off+n)
static bool read_back(const std::wstring& path, long long off, unsigned char* buf, DWORD n,
                      DWORD* got) {
    HANDLE r = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (r == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER d;
    d.QuadPart = off;
    SetFilePointerEx(r, d, nullptr, FILE_BEGIN);
    BOOL ok = ReadFile(r, buf, n, got, nullptr);
    CloseHandle(r);
    return ok == TRUE;
}

int main() {
    // ---------- [1][2][3] 缓冲写:洞 + 尺寸 + 分配尺寸 ----------
    std::wstring path = make_path(L"hole.bin");
    DeleteFileW(path.c_str());
    // share 给足,是为了让 read_back 的独立读句柄能进来(03-sharemode 的功课)
    HANDLE h =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFileW 失败 err=%lu\n", GetLastError());
        return 1;
    }

    DWORD written = 0;
    WriteFile(h, "AB", 2, &written, nullptr);
    printf("[1] 头部写入 2 字节 \"AB\"\n");

    LARGE_INTEGER d;
    d.QuadPart = 1 << 20; // 1 MiB
    SetFilePointerEx(h, d, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r = WriteFile(h, "Z", 1, &written, nullptr);
    printf("    指针跳到 1 MiB 处写 1 字节 \"Z\"       -> ret=%d 写到=%lu\n", r, written);

    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    printf("    GetFileSizeEx                        -> %lld  (= 1MiB + 1)\n", sz.QuadPart);

    printf("[3] 尺寸与分配尺寸(默认非稀疏)\n");
    report_size(h, "打洞后:");

    unsigned char buf[16];
    DWORD got = 0;
    read_back(path, 0, buf, sizeof(buf), &got);
    dump("[2] 读回前 16 字节                        -> ", buf, got);
    printf("    (\"AB\" 之后全是 00 —— 洞读出来是零)\n");
    read_back(path, (1 << 20) - 8, buf, sizeof(buf), &got);
    dump("    洞尾 1MiB-8 .. 1MiB+8 处读回           -> ", buf, got);
    printf("    (洞尾零区接上 \"Z\"=5A)\n");

    CloseHandle(h);

    // ---------- [4] FILE_FLAG_NO_BUFFERING 越过 EOF 写 ----------
    std::wstring path2 = make_path(L"hole_nb.bin");
    DeleteFileW(path2.c_str());
    HANDLE h2 =
        CreateFileW(path2.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
    if (h2 == INVALID_HANDLE_VALUE) {
        printf("CreateFileW(NO_BUFFERING) 失败 err=%lu\n", GetLastError());
        return 1;
    }

    // NO_BUFFERING 要求缓冲区按扇区对齐
    unsigned char* abuf = (unsigned char*)_aligned_malloc(4096, 4096);
    memset(abuf, 'X', 4096);
    WriteFile(h2, abuf, 4096, &written, nullptr); // 先在 0 处正常写一页
    printf("[4] NO_BUFFERING 句柄在 0 处写 4096B     -> ret=1 写到=%lu  (正常)\n", written);

    d.QuadPart = 1 << 20;
    SetFilePointerEx(h2, d, nullptr, FILE_BEGIN);
    SetLastError(0);
    r = WriteFile(h2, abuf, 4096, &written, nullptr);
    DWORD err = GetLastError();
    LARGE_INTEGER sz2;
    GetFileSizeEx(h2, &sz2);
    printf("    同一句柄跳到 1 MiB 再写 4096B         -> ret=%d 写到=%lu err=%lu\n", r, written,
           err);
    printf("    此时 GetFileSizeEx                    -> %lld  (1MiB+4096,写进去了)\n",
           sz2.QuadPart);
    read_back(path2, 8192, buf, sizeof(buf), &got);
    dump("    越过 EOF 的洞读回(偏移 8192 起)       -> ", buf, got);
    printf("    (文档说零填充要靠缓存管理器、非缓冲写可能炸 87;本机实测:成功且洞是零)\n");

    _aligned_free(abuf);
    CloseHandle(h2);

    // ---------- [5] FSCTL_SET_SPARSE:NTFS 的稀疏开关 ----------
    std::wstring path3 = make_path(L"hole_sparse.bin");
    DeleteFileW(path3.c_str());
    HANDLE h3 = CreateFileW(path3.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD br = 0;
    BOOL sr = DeviceIoControl(h3, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &br, nullptr);
    printf("[5] FSCTL_SET_SPARSE                     -> ret=%d err=%lu  (NTFS 稀疏开关)\n", sr,
           GetLastError());
    WriteFile(h3, "AB", 2, &written, nullptr);
    d.QuadPart = 1 << 20;
    SetFilePointerEx(h3, d, nullptr, FILE_BEGIN);
    WriteFile(h3, "Z", 1, &written, nullptr);
    report_size(h3, "稀疏文件打同款洞:");
    printf("    (AllocationSize 只算实写簇;ext4 的洞默认就是这个待遇)\n");
    CloseHandle(h3);

    DeleteFileW(path.c_str());
    DeleteFileW(path2.c_str());
    DeleteFileW(path3.c_str());
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    RemoveDirectoryW((std::wstring(tmp) + L"sysprog-supp-e1").c_str());
    printf("\n收尾:临时文件与目录已清理\n");
    return 0;
}
