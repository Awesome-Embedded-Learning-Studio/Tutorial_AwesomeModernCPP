// e1_pointer_moves.cpp —— SetFilePointerEx 的三种基准 + 正负偏移 + 越过 EOF 不改尺寸
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_pointer_moves.cpp -o
//   e1_pointer_moves.exe
// 运行:
//   chmod +x e1_pointer_moves.exe && ./e1_pointer_moves.exe
//
// 观察点:
//   [1] FILE_BEGIN / FILE_CURRENT / FILE_END 三种基准,正偏移、负偏移各自落到哪
//   [2] 查询当前位置的惯用法:距离 0 + FILE_CURRENT,新位置从出参读
//   [3] 越过 EOF 移动指针:合法,GetFileSizeEx 不变——移动只动指针,不动文件
//   [4] 读越过 EOF 的位置:ReadFile 返回 TRUE + 0 字节(EOF 不是错误)
//   [5] FILE_BEGIN 配负偏移:SetFilePointerEx 失败,ERROR_NEGATIVE_SEEK(131)
//
// POSIX 对照:lseek 的 SEEK_SET/SEEK_CUR/SEEK_END 三基准一一对应;
// 负偏移越过头同样是 EINVAL。差异在:Linux 里 lseek 溢出 RLIMIT_FSIZE 之外
// 不常见报错路径,Windows 这边句柄层面的语义就这五个,全贴在下面。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>

static HANDLE g_file = INVALID_HANDLE_VALUE;

// 移动并回报落点;失败时打印错误码
static long long move(long long dist, DWORD method, const char* label) {
    LARGE_INTEGER d, got;
    d.QuadPart = dist;
    SetLastError(0);
    BOOL ok = SetFilePointerEx(g_file, d, &got, method);
    DWORD err = GetLastError();
    if (!ok) {
        printf("  %-34s -> 失败 ret=0 err=%lu%s\n", label, err,
               err == 131 ? "(ERROR_NEGATIVE_SEEK)" : "");
        return -1;
    }
    printf("  %-34s -> pos=%lld err=%lu\n", label, got.QuadPart, err);
    return got.QuadPart;
}

static long long cur_pos() {
    LARGE_INTEGER d, got;
    d.QuadPart = 0;
    if (!SetFilePointerEx(g_file, d, &got, FILE_CURRENT)) {
        return -1;
    }
    return got.QuadPart;
}

static long long file_size() {
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(g_file, &sz)) {
        return -1;
    }
    return sz.QuadPart;
}

int main() {
    // 工作文件放在 Windows 真实 NTFS 临时目录(避开 \wsl.localhost 的 9P 路径)
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sysprog-supp-e1";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\moves.bin";
    DeleteFileW(path.c_str());

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CreateFileW 失败 err=%lu\n", GetLastError());
        return 1;
    }
    g_file = h;

    // 10 字节的已知内容
    const char data[] = "ABCDEFGHIJ";
    DWORD written = 0;
    WriteFile(h, data, 10, &written, nullptr);
    printf("初始:写入 %lu 字节,文件大小=%lld,当前指针=%lld\n", written, file_size(), cur_pos());

    printf("\n[1] 三种基准 + 正负偏移(文件 10 字节,内容 ABCDEFGHIJ)\n");
    move(+3, FILE_BEGIN, "FILE_BEGIN  +3");
    move(+2, FILE_CURRENT, "FILE_CURRENT +2");
    move(-1, FILE_CURRENT, "FILE_CURRENT -1");
    move(-3, FILE_END, "FILE_END  -3");
    move(0, FILE_END, "FILE_END  0  (定位到 EOF)");

    printf("\n[2] 查询当前位置:距离 0 + FILE_CURRENT\n");
    move(0, FILE_CURRENT, "FILE_CURRENT 0  (只查不动)");
    printf("  cur_pos() 独立复核                  -> pos=%lld\n", cur_pos());

    printf("\n[3] 越过 EOF 移动指针:合法,尺寸纹丝不动\n");
    move(+103, FILE_BEGIN, "FILE_BEGIN +103 (越过 EOF)");
    printf("  移动后 GetFileSizeEx                  -> %lld  (仍是 10)\n", file_size());
    move(+1000000, FILE_END, "FILE_END  +1000000");
    printf("  移动后 GetFileSizeEx                  -> %lld  (仍是 10)\n", file_size());

    printf("\n[4] 在越过 EOF 的位置读:TRUE + 0 字节,不是错误\n");
    move(+103, FILE_BEGIN, "FILE_BEGIN +103 再来一次");
    char buf[16];
    DWORD got = (DWORD)-1;
    SetLastError(0);
    BOOL r = ReadFile(h, buf, sizeof(buf), &got, nullptr);
    printf("  ReadFile(16B)                        -> ret=%d 读到=%lu err=%lu  (EOF 是 0 "
           "字节,不是失败)\n",
           r, got, GetLastError());

    printf("\n[5] FILE_BEGIN 配负偏移:失败,ERROR_NEGATIVE_SEEK\n");
    move(-100, FILE_BEGIN, "FILE_BEGIN -100");
    move(-1, FILE_BEGIN, "FILE_BEGIN -1");
    printf("  此后指针停在原地                     -> pos=%lld\n", cur_pos());

    CloseHandle(h);
    DeleteFileW(path.c_str());
    RemoveDirectoryW(dir.c_str());
    printf("\n收尾:临时文件与目录已清理\n");
    return 0;
}
