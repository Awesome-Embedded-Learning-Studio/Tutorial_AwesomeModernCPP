// e1c_size_sources.cpp —— 尺寸从哪来:GetFileSizeEx vs GetFileInformationByHandle vs
// GetFileInformationByHandleEx(FileStandardInfo);顺带 nFileIndex 的"同一文件"证明
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1c_size_sources.cpp -o
//   e1c_size_sources.exe
// 运行:
//   chmod +x e1c_size_sources.exe && ./e1c_size_sources.exe
//
// 观察点:
//   [1] 三个来源读同一个数:尺寸挂在文件(流)上,不挂在句柄上
//   [2] 句柄 A 开着不动,句柄 B 把文件写大,A 的三路读数全部跟着变
//       ——对照 Linux:两个 open() 的 fd 各自 lseek(0, SEEK_END) 也都看到新 st_size
//   [3] BY_HANDLE_FILE_INFORMATION 额外给的东西:卷序列号 + nFileIndex(文件身份)
//       两次 CreateFileW 的 nFileIndex 相同 —— 两个句柄,同一文件
//   [4] GetFileSizeEx 的老前辈 GetFileSize(32 位拼装)与 64 位版对账
//
// 注:为了让"同一文件开两个句柄"成立,B 的打开必须带 FILE_SHARE_READ|FILE_SHARE_WRITE
//     ——这正是 03-sharemode 矩阵的功课,这里先拿来用。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>

static std::wstring make_path(const wchar_t* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sysprog-supp-e1";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + name;
}

static HANDLE open_rw(const std::wstring& p) {
    return CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

int main() {
    std::wstring path = make_path(L"size.bin");
    DeleteFileW(path.c_str());

    HANDLE seed = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(seed, "100 bytes of something...............", 100, &w, nullptr);
    CloseHandle(seed);
    printf("准备:文件 %llu 字节(内容无意义,尺寸是 100)\n", (unsigned long long)100);

    // ---------- [1] 三路读数 ----------
    HANDLE a = open_rw(path);
    HANDLE b = open_rw(path);
    if (a == INVALID_HANDLE_VALUE || b == INVALID_HANDLE_VALUE) {
        printf("双开失败 err=%lu\n", GetLastError());
        return 1;
    }

    LARGE_INTEGER sz{};
    GetFileSizeEx(a, &sz);

    BY_HANDLE_FILE_INFORMATION bhfi{};
    GetFileInformationByHandle(a, &bhfi);
    unsigned long long size_bhfi =
        ((unsigned long long)bhfi.nFileSizeHigh << 32) | bhfi.nFileSizeLow;

    FILE_STANDARD_INFO si{};
    GetFileInformationByHandleEx(a, FileStandardInfo, &si, sizeof(si));

    DWORD low = GetFileSize(a, nullptr); // 老版(32 位);只对小于 4GiB 的文件可靠
    printf("\n[1] 句柄 A 的三路尺寸读数\n");
    printf("    GetFileSizeEx                       -> %lld\n", sz.QuadPart);
    printf("    GetFileInformationByHandle 64 位拼装 -> %llu\n", size_bhfi);
    printf("    FileStandardInfo.EndOfFile          -> %lld\n", si.EndOfFile.QuadPart);
    printf("    GetFileSize(老 32 位版)             -> %lu   (与上面一致)\n", low);

    // ---------- [2] B 把文件写大,A 的读数跟着变 ----------
    LARGE_INTEGER d;
    d.QuadPart = 1 << 20;
    SetFilePointerEx(b, d, nullptr, FILE_BEGIN);
    DWORD w2 = 0;
    WriteFile(b, "X", 1, &w2, nullptr);
    printf("\n[2] 句柄 B 在 1 MiB 处写 1 字节后,句柄 A 重新读\n");
    GetFileSizeEx(a, &sz);
    GetFileInformationByHandle(a, &bhfi);
    GetFileInformationByHandleEx(a, FileStandardInfo, &si, sizeof(si));
    printf("    GetFileSizeEx                       -> %lld\n", sz.QuadPart);
    printf("    BY_HANDLE_FILE_INFORMATION 64 位拼装 -> %llu\n",
           ((unsigned long long)bhfi.nFileSizeHigh << 32) | bhfi.nFileSizeLow);
    printf("    FileStandardInfo.EndOfFile          -> %lld\n", si.EndOfFile.QuadPart);
    printf("    FileStandardInfo.AllocationSize     -> %lld  (分配尺寸,含洞策略)\n",
           si.AllocationSize.QuadPart);
    printf("    —— 尺寸挂在文件上,A 不动也看得见\n");

    // ---------- [3] 文件身份:nFileIndex ----------
    BY_HANDLE_FILE_INFORMATION bi{};
    GetFileInformationByHandle(b, &bi);
    printf("\n[3] 两次 CreateFileW 的身份字段(句柄 A vs 句柄 B)\n");
    printf("    卷序列号      A=%08lX  B=%08lX  (同盘)\n", bhfi.dwVolumeSerialNumber,
           bi.dwVolumeSerialNumber);
    printf("    nFileIndex    A=%08lX%08lX\n", bhfi.nFileIndexHigh, bhfi.nFileIndexLow);
    printf("                  B=%08lX%08lX  (相同 -> 同一文件;指针独立是另一回事,见 e1d)\n",
           bi.nFileIndexHigh, bi.nFileIndexLow);
    printf("    链接数        A=%lu  B=%lu  (NTFS 硬链接计数,FAT 上不可靠)\n", bhfi.nNumberOfLinks,
           bi.nNumberOfLinks);

    CloseHandle(a);
    CloseHandle(b);
    DeleteFileW(path.c_str());
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    RemoveDirectoryW((std::wstring(tmp) + L"sysprog-supp-e1").c_str());
    printf("\n收尾:临时文件与目录已清理\n");
    return 0;
}
