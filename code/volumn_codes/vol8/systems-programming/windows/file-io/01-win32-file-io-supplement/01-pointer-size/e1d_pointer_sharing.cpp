// e1d_pointer_sharing.cpp —— 文件指针归谁:两次 CreateFileW 各自独立,DuplicateHandle 共享
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1d_pointer_sharing.cpp -o
//   e1d_pointer_sharing.exe
// 运行:
//   chmod +x e1d_pointer_sharing.exe && ./e1d_pointer_sharing.exe
//
// 观察点:
//   [1] h1 读 4 字节后 h1 指针=4,h2 指针仍是 0,h2 再读拿到的是同样的前 4 字节
//       ——每次 CreateFileW 造一个新的"文件对象",CurrentByteOffset 各一份
//   [2] h1 负向回退只影响 h1,h2 的指针不动
//   [3] DuplicateHandle 复制的是"句柄值 -> 同一个文件对象"的映射:
//       dup 继承 h1 此刻的指针,之后两边读写互相推进 —— 共享
//   [4] 结论矩阵(与 POSIX 对齐,见 e1e Linux 侧实测):
//         Windows: CreateFileW x2   -> 独立指针   ==  POSIX: open() x2   -> 独立偏移
//                  DuplicateHandle  -> 共享指针   ==  POSIX: dup()/dup2() -> 共享偏移
//       (POSIX 的 fork() 子进程继承 fd,偏移也共享 —— 对应 Windows 句柄可继承 +
//        bInheritHandles,本实验不展开)
//
// 注意:同文件双开必须给 FILE_SHARE_READ|FILE_SHARE_WRITE,否则第二个句柄直接撞
//       ERROR_SHARING_VIOLATION(32) —— 03-sharemode 的主场,这里只是路过。

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

static long long pos(HANDLE h) {
    LARGE_INTEGER d, got;
    d.QuadPart = 0;
    if (!SetFilePointerEx(h, d, &got, FILE_CURRENT)) {
        return -1;
    }
    return got.QuadPart;
}

static std::string read_at_cur(HANDLE h, DWORD n) {
    std::string out(n, '\0');
    DWORD got = 0;
    ReadFile(h, out.data(), n, &got, nullptr);
    out.resize(got);
    return out;
}

int main() {
    std::wstring path = make_path(L"share_ptr.bin");
    DeleteFileW(path.c_str());
    HANDLE seed = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(seed, "0123456789", 10, &w, nullptr);
    CloseHandle(seed);

    HANDLE h1 = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE h2 = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    printf("准备:文件 \"0123456789\",h1=%p h2=%p(两次 CreateFileW)\n", h1, h2);

    printf("\n[1] h1 读 4 字节,h2 的指针不动\n");
    printf("    读前  pos(h1)=%lld  pos(h2)=%lld\n", pos(h1), pos(h2));
    printf("    h1 读到 \"%s\"\n", read_at_cur(h1, 4).c_str());
    printf("    读后  pos(h1)=%lld  pos(h2)=%lld   <- 只有 h1 前进\n", pos(h1), pos(h2));
    printf("    h2 读到 \"%s\"  <- h2 从 0 读起,拿到同样 4 字节\n", read_at_cur(h2, 4).c_str());
    printf("    读后  pos(h1)=%lld  pos(h2)=%lld\n", pos(h1), pos(h2));

    printf("\n[2] h1 负向回退 -2,再读;h2 置若罔闻\n");
    LARGE_INTEGER d;
    d.QuadPart = -2;
    SetFilePointerEx(h1, d, nullptr, FILE_CURRENT);
    printf("    回退后 pos(h1)=%lld  pos(h2)=%lld\n", pos(h1), pos(h2));
    printf("    h1 读到 \"%s\"  (从下标 2 起)\n", read_at_cur(h1, 4).c_str());
    printf("    此刻  pos(h1)=%lld  pos(h2)=%lld\n", pos(h1), pos(h2));

    printf("\n[3] DuplicateHandle(h1 -> h3):同一个文件对象,指针共享\n");
    HANDLE h3 = INVALID_HANDLE_VALUE;
    DuplicateHandle(GetCurrentProcess(), h1, GetCurrentProcess(), &h3, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    printf("    h3=%p 诞生即继承 pos(h3)=%lld  (h1 刚读完,停在这)\n", h3, pos(h3));
    printf("    h3 读到 \"%s\"\n", read_at_cur(h3, 4).c_str());
    printf("    读后  pos(h3)=%lld  pos(h1)=%lld   <- h1 被 h3 的读推着走\n", pos(h3), pos(h1));
    printf("          pos(h2)=%lld                <- h2 纹丝不动\n", pos(h2));
    d.QuadPart = -4;
    SetFilePointerEx(h3, d, nullptr, FILE_CURRENT);
    printf("    h3 回退 -4 后 pos(h1)=%lld  pos(h3)=%lld  <- h1 跟着回退,共享实锤\n", pos(h1),
           pos(h3));

    printf("\n[4] 收束成表\n");
    printf("    CreateFileW x2  -> 指针独立(== POSIX open x2 独立偏移)\n");
    printf("    DuplicateHandle -> 指针共享(== POSIX dup/dup2 共享偏移)\n");
    printf("    Windows 没有\"每次 CreateFileW 共享指针\"一说;共享只来自复制/继承句柄\n");

    CloseHandle(h1);
    CloseHandle(h2);
    CloseHandle(h3);
    DeleteFileW(path.c_str());
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    RemoveDirectoryW((std::wstring(tmp) + L"sysprog-supp-e1").c_str());
    printf("\n收尾:临时文件与目录已清理\n");
    return 0;
}
