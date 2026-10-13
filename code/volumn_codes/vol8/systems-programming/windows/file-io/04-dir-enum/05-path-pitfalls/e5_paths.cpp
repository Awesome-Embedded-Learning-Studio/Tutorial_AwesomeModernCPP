// e5_paths.cpp —— Windows 路径坑:\\?\ 前缀与 MAX_PATH / 正反斜杠混用 / 尾部反斜杠 / fs::path 对拍
//
// 编译:
//   cd 05-path-pitfalls && /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common
//   e5_paths.cpp -o e5_paths.exe
// 运行:
//   chmod +x e5_paths.exe && ./e5_paths.exe
//
// 观察点:
//   [1] 长路径:本机注册表 LongPathsEnabled=1,但那条政策只放行带 longPathAware
//       清单的应用——MSYS2 g++ 编出来的裸 exe 没有清单,260 的墙照样在。
//       层层下钻 8 层长名目录,裸 CreateDirectoryW 在哪层倒下(err=3);
//       \\?\C:\... 前缀一挂,同样的链建到底(设备路径不走 Win32 路径归一化,
//       上限 32767)。查询/枚举/删除同款对照。
//   [2] 正反斜杠:普通路径里 '/' 与 '\\' Win32 层等效(都收);但 \\?\ 设备路径
//       只认反斜杠——混一个正斜杠进去就是 err=123 ERROR_INVALID_NAME。
//   [3] 尾部反斜杠:目录名带尾杠,CreateDirectoryW/GetFileAttributesW 收;
//       CreateFileW 开目录收不收;FindFirstFileW 的模式串尾杠(无通配符)= err=2。
//   [4] std::filesystem 对拍(Linux 侧 E5 的镜像点):value_type 是 wchar_t;
//       右侧带根的名字顶掉左侧;尾斜杠吸收/双斜杠残留;"a/b/" 迭代出什么。

#include "win_dir.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static const wchar_t* kRoot =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e5";

static std::wstring seg(int i) // 每层 40 个字符,凑长度
{
    wchar_t s[48];
    swprintf(s, 48, L"lvl%d_%036d", i, i); // "lvl" + 序号 + 填充,共 40 字符
    return s;
}

static void probe_attr(const char* label, const std::wstring& p) {
    SetLastError(1234);
    DWORD a = GetFileAttributesW(p.c_str());
    printf("    %-34s -> attr=0x%lx err=%lu\n", label, a, GetLastError());
}

int main() {
    // 干净地基
    {
        WIN32_FIND_DATAW fd{};
        if (unique_find h{FindFirstFileW((std::wstring(kRoot) + L"\\*").c_str(), &fd)}) {
            do {
                if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
                    std::wstring p = std::wstring(kRoot) + L"\\" + fd.cFileName;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        RemoveDirectoryW(p.c_str());
                    } else {
                        DeleteFileW(p.c_str());
                    }
                }
            } while (FindNextFileW(h.get(), &fd));
        } else {
            CreateDirectoryW(kRoot, nullptr);
        }
    }
    std::wstring root = kRoot;

    printf("== [1] 长路径:260 的墙与 \\\\?\\ 的梯子 ==\n");
    printf("  本机注册表 HKLM\\...\\FileSystem\\LongPathsEnabled=1,但那是给带清单的应用的;\n");
    printf("  本 exe 无 longPathAware 清单,裸路径仍按 MAX_PATH=260 裁。8 层 × 40 字符下钻:\n");
    std::wstring bare = root;
    std::wstring deep;
    int failAt = -1;
    for (int i = 0; i < 8; ++i) {
        bare += L"\\" + seg(i);
        SetLastError(1234);
        BOOL ok = CreateDirectoryW(bare.c_str(), nullptr);
        if (!ok) {
            failAt = i;
            printf("    第 %d 层倒下:路径 %zu 字符,err=%lu(3=ERROR_PATH_NOT_FOUND)\n", i + 1,
                   bare.size(), GetLastError());
            deep = root;
            for (int j = 0; j < i; ++j) {
                deep += L"\\" + seg(j);
            } // 裸路径最后成功的链
            break;
        }
    }
    if (failAt < 0) {
        printf("    裸路径 8 层全过(超出预期,记录之)\n");
        deep = bare;
    }

    // 裸路径已经把前几层建出来了,前缀链撞 ERROR_ALREADY_EXISTS(183)不算失败,接着钻
    std::wstring pre = L"\\\\?\\" + root;
    for (int i = 0; i < 8; ++i) {
        pre += L"\\" + seg(i);
        SetLastError(1234);
        BOOL ok = CreateDirectoryW(pre.c_str(), nullptr);
        if (!ok && GetLastError() != ERROR_ALREADY_EXISTS) {
            printf("    \\\\?\\ 第 %d 层倒下 err=%lu\n", i + 1, GetLastError());
            return 1;
        }
    }
    printf("    \\\\?\\ 前缀把 8 层建完:总长 %zu 字符(含前缀 4 字符,去掉前缀 %zu 字符)\n",
           pre.size(), pre.size() - 4);

    // 同一个最深层,两种写法的查询/枚举对照
    printf("    最深层的第 8 层目录(%zu 字符):\n", pre.size());
    probe_attr("裸路径 GetFileAttributesW", pre.substr(4));
    probe_attr("\\\\?\\ 前缀 GetFileAttributesW", pre);
    {
        WIN32_FIND_DATAW fd{};
        SetLastError(1234);
        HANDLE h = FindFirstFileW((pre.substr(4) + L"\\*").c_str(), &fd);
        printf("    %-34s -> %s err=%lu\n", "裸路径 FindFirstFileW \\*",
               h == INVALID_HANDLE_VALUE ? "失败" : "成功", GetLastError());
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
        }
        SetLastError(1234);
        unique_find hp{FindFirstFileW((pre + L"\\*").c_str(), &fd)};
        printf("    %-34s -> %s(枚举到 \".\" 与 \"..\",证明通配符路径也能带前缀)\n",
               "\\\\?\\ 前缀 FindFirstFileW \\*", hp ? "成功" : "失败");
    }

    printf("\n== [2] 正反斜杠 ==\n");
    {
        std::wstring file = root + L"\\slash.txt";
        unique_handle f{check_win32("CreateFileW(正斜杠)", CreateFileW,
                                    (root + L"/slash.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        printf("    CreateFileW 全正斜杠 C:/.../slash.txt:成功(Win32 层 '/' 与 '\\\\' 等效)\n");
        WIN32_FIND_DATAW fd{};
        SetLastError(1234);
        HANDLE h = FindFirstFileW((root + L"/*").c_str(), &fd);
        printf("    FindFirstFileW 混用 \"C:\\...\\\\e5/*\":%s err=%lu\n",
               h == INVALID_HANDLE_VALUE ? "失败" : "成功", GetLastError());
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
        }
        SetLastError(1234);
        HANDLE bad = CreateFileW((L"\\\\?\\" + root + L"/slash.txt").c_str(), GENERIC_WRITE, 0,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        printf("    \\\\?\\ 设备路径里混正斜杠 -> %s "
               "err=%lu(123=ERROR_INVALID_NAME,设备路径不归一化)\n",
               bad == INVALID_HANDLE_VALUE ? "失败" : "意外成功", GetLastError());
        if (bad != INVALID_HANDLE_VALUE) {
            CloseHandle(bad);
        }
    }

    printf("\n== [3] 尾部反斜杠 ==\n");
    {
        std::wstring d = root + L"\\taildir";
        CreateDirectoryW(d.c_str(), nullptr);
        SetLastError(1234);
        BOOL ok1 = CreateDirectoryW((root + L"\\taildir2\\").c_str(), nullptr);
        printf("    CreateDirectoryW \"newdir\\\\\"          -> %s err=%lu(尾杠收下)\n",
               ok1 ? "成功" : "失败", GetLastError());
        SetLastError(1234);
        DWORD a = GetFileAttributesW((d + L"\\").c_str());
        printf("    GetFileAttributesW \"dir\\\\\"            -> attr=0x%lx err=%lu\n", a,
               GetLastError());
        SetLastError(1234);
        HANDLE h = CreateFileW((d + L"\\").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        printf("    CreateFileW(开目录) \"dir\\\\\"          -> %s err=%lu\n",
               h == INVALID_HANDLE_VALUE ? "失败" : "成功",
               h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
        WIN32_FIND_DATAW fd{};
        SetLastError(1234);
        HANDLE q = FindFirstFileW((d + L"\\").c_str(), &fd);
        printf("    FindFirstFileW \"dir\\\\\"(无通配符)     -> %s "
               "err=%lu(2=当模式匹配,啥也匹配不上)\n",
               q == INVALID_HANDLE_VALUE ? "失败" : "成功", GetLastError());
        if (q != INVALID_HANDLE_VALUE) {
            FindClose(q);
        }
        SetLastError(1234);
        HANDLE q2 = FindFirstFileW((d + L"\\\\*").c_str(), &fd);
        printf("    FindFirstFileW \"dir\\\\\\\\*\"(双杠+通配)   -> %s err=%lu(冗余分隔符被容忍)\n",
               q2 == INVALID_HANDLE_VALUE ? "失败" : "成功", GetLastError());
        if (q2 != INVALID_HANDLE_VALUE) {
            FindClose(q2);
        }
    }

    printf("\n== [4] std::filesystem 对拍(Linux E5 的镜像) ==\n");
    {
        printf(
            "    value_type 宽度 %zu 字节(Linux 侧是 1 字节 char),native()/c_str() 是 wchar_t 系\n",
            sizeof(fs::path::value_type));
        auto q = [](const wchar_t* a, const wchar_t* b) {
            fs::path r = fs::path(a) / b;
            printf("    path(\"%ls\") / \"%ls\" = \"%ls\"\n", a, b, r.c_str());
        };
        q(L"base", L"leaf");
        q(L"base", L"C:/abs");   // 右侧带根名:顶掉左侧
        q(L"base\\", L"leaf");   // 尾杠吸收
        q(L"base\\\\", L"leaf"); // 双杠残留
        q(L"base", L"");         // 拼空串
        printf("    lexically_normal(\"./a/../b\") = \"%ls\"\n",
               fs::path(L"./a/../b").lexically_normal().c_str());
        printf("    path(\"C:/x\") == path(\"C:\\\\x\") ? %s(比较前先归一化正斜杠)\n",
               fs::path(L"C:/x") == fs::path(L"C:\\x") ? "是" : "否");
        fs::path p = L"a/b/";
        printf("    迭代 path(\"a/b/\") 产:");
        for (auto& e : p) {
            printf(" [%ls]", e.c_str());
        }
        printf("(Windows 侧迭代器同样带出尾部空元素吗,看上面)\n");
    }

    // 清理:长链必须带前缀从深层往外拆
    for (int i = 7; i >= 0; --i) {
        std::wstring p = L"\\\\?\\" + root;
        for (int j = 0; j <= i; ++j) {
            p += L"\\" + seg(j);
        }
        RemoveDirectoryW(p.c_str());
    }
    for (const wchar_t* k : {L"taildir", L"taildir2"}) {
        RemoveDirectoryW((root + L"\\" + k).c_str());
    }
    DeleteFileW((root + L"\\slash.txt").c_str());
    RemoveDirectoryW(root.c_str());
    printf("\n(测试目录已清理;注意:深层目录的删除/创建全程靠 \\\\?\\ 前缀才够得着)\n");
    return 0;
}
