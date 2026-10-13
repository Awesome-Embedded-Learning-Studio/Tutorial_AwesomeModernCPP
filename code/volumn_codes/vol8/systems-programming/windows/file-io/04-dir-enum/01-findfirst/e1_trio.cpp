// e1_trio.cpp —— FindFirstFileW/FindNextFileW/FindClose 三件套:特殊项、过滤模式、字段解读、遍历序
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   cd 01-findfirst && /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common
//   e1_trio.cpp -o e1_trio.exe
// 运行:
//   chmod +x e1_trio.exe && ./e1_trio.exe
//
// 观察点:
//   (1) 测试树按"乱序"创建(z→a→m→中文→Beta→alpha),枚举出来是不是字母序——NTFS 的目录
//       B+ 树按 $UpCase 大写折叠排序,枚举序 = 大小写不敏感的字典序(与 ext4 的名字散列序对照)
//   (2) \\* 枚举带头两个特殊项 "." 与 ".."(attributes=DIRECTORY);根目录 C:\\ 没有这两项
//   (3) 过滤模式 *.txt 不含 "." ".."(点项只匹配 *);"*." 只匹配"无扩展名"的文件(DOS 通配
//       符的历史包袱);"*.htm" 会顺带匹配 longname.html——8.3 短名(FILE~1.HTM)在替它匹配,
//       cAlternateFileName 字段就是证据
//   (4) WIN32_FIND_DATA 字段:dwFileAttributes 位解码、三时间(Creation/LastAccess/
//       LastWrite)、大小 nFileSizeHigh<<32|nFileSizeLow(拿一个 4.5 GiB 的"账面"文件实证
//       高 32 位有货,SetEndOfFile 拉长不落盘,秒建)
//   (5) 三件套的失败值:INVALID_HANDLE_VALUE(与 CreateFileW 同款,不是 NULL)
//
// 测试树根目录是烧死的 C: 盘 %TEMP% 下,换机器改 kRoot。

#include "win_dir.hpp"

#include <cstdio>
#include <string>

// 契约工具来自 ../common/win_dir.hpp(unique_find 是本篇新增)

static const wchar_t* kRoot =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e1";

static void rm_tree(const std::wstring& dir); // 后向声明

static void make_file(const std::wstring& path, const char* data,
                      DWORD attr = FILE_ATTRIBUTE_NORMAL) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, attr, nullptr)};
    if (data) {
        DWORD written = 0;
        check_win32("WriteFile", WriteFile, f.get(), data, (DWORD)lstrlenA(data), &written,
                    nullptr);
    }
}

static void attr_bits(DWORD a, char* out) // dwFileAttributes 位解码
{
    struct {
        DWORD bit;
        const char* name;
    } tab[] = {
        {FILE_ATTRIBUTE_READONLY, "READONLY"},
        {FILE_ATTRIBUTE_HIDDEN, "HIDDEN"},
        {FILE_ATTRIBUTE_SYSTEM, "SYSTEM"},
        {FILE_ATTRIBUTE_DIRECTORY, "DIRECTORY"},
        {FILE_ATTRIBUTE_ARCHIVE, "ARCHIVE"},
        {FILE_ATTRIBUTE_DEVICE, "DEVICE"},
        {FILE_ATTRIBUTE_NORMAL, "NORMAL"},
        {FILE_ATTRIBUTE_TEMPORARY, "TEMPORARY"},
        {FILE_ATTRIBUTE_SPARSE_FILE, "SPARSE"},
        {FILE_ATTRIBUTE_REPARSE_POINT, "REPARSE"},
        {FILE_ATTRIBUTE_COMPRESSED, "COMPRESSED"},
        {FILE_ATTRIBUTE_OFFLINE, "OFFLINE"},
        {FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, "NOTIDX"},
        {FILE_ATTRIBUTE_ENCRYPTED, "ENCRYPTED"},
    };
    out[0] = '\0';
    for (auto& e : tab) {
        if (a & e.bit) {
            if (out[0]) {
                lstrcatA(out, "|");
            }
            lstrcatA(out, e.name);
        }
    }
    char hex[16];
    wsprintfA(hex, " (0x%lx)", (unsigned long)a);
    lstrcatA(out, hex);
}

static int dump_dir(const wchar_t* pattern, bool detail) // 返回枚举到的条目数
{
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("    FindFirstFileW(\"%s\") -> 失败 err=%lu\n", to_utf8(pattern).c_str(),
               GetLastError());
        return -1;
    }
    unique_find guard{h};
    int n = 0;
    do {
        ++n;
        if (!detail) {
            printf("    [%d] %-24s", n, to_utf8(fd.cFileName).c_str());
            if (fd.cAlternateFileName[0]) {
                printf("  短名=%s", to_utf8(fd.cAlternateFileName).c_str());
            }
            printf("\n");
            continue;
        }
        char bits[256];
        attr_bits(fd.dwFileAttributes, bits);
        unsigned long long size =
            ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;
        printf("    [%d] %-22s attr=%s\n", n, to_utf8(fd.cFileName).c_str(), bits);
        printf("        大小 %llu(0x%llx)= High 0x%lx << 32 | Low 0x%lx\n", size, size,
               (unsigned long)fd.nFileSizeHigh, (unsigned long)fd.nFileSizeLow);
        printf("        创建 %s / 访问 %s / 修改 %s\n", ft_str(fd.ftCreationTime).c_str(),
               ft_str(fd.ftLastAccessTime).c_str(), ft_str(fd.ftLastWriteTime).c_str());
    } while (FindNextFileW(guard.get(), &fd));
    DWORD err = GetLastError();
    if (err != ERROR_NO_MORE_FILES) {
        printf("    FindNextFileW 中途失败 err=%lu(不是 18,枚举被打断)\n", err);
    }
    return n;
}

int main() {
    // ---------- 建树:名字按乱序给,看枚举序 ----------
    rm_tree(kRoot);
    CreateDirectoryW(kRoot, nullptr);
    make_file(std::wstring(kRoot) + L"\\zeta.txt", "zzz");
    make_file(std::wstring(kRoot) + L"\\alpha.txt", "a");
    make_file(std::wstring(kRoot) + L"\\Mike.TXT", "m");
    make_file(std::wstring(kRoot) + L"\\中文文件.txt", "chinese");
    make_file(std::wstring(kRoot) + L"\\Beta.dat", "b");
    make_file(std::wstring(kRoot) + L"\\noext", "n");           // 无扩展名:给 "*." 用
    make_file(std::wstring(kRoot) + L"\\longname.html", "htm"); // 给 8.3 短名匹配用
    CreateDirectoryW((std::wstring(kRoot) + L"\\子目录").c_str(), nullptr);

    printf("== [1] 测试树按乱序创建,FindFirstFileW 枚举序 ==\n");
    printf("  创建顺序:zeta.txt, alpha.txt, Mike.TXT, 中文文件.txt, Beta.dat, noext, "
           "longname.html, 子目录/\n");
    printf("  枚举结果(pattern=\\*,只报名字与短名):\n");
    int n = dump_dir((std::wstring(kRoot) + L"\\*").c_str(), false);
    printf("  共 %d 项(含 . 与 ..)——NTFS 按大小写折叠后的字典序返回,与创建顺序无关\n", n);

    printf("\n== [2] 特殊项 . 与 .. 的字段解读 ==\n");
    dump_dir((std::wstring(kRoot) + L"\\*").c_str(), true);

    printf("\n== [3] 过滤模式 ==\n");
    printf("  [3a] *.txt(点项不该出现):\n");
    dump_dir((std::wstring(kRoot) + L"\\*.txt").c_str(), false);
    printf("  [3b] *.(DOS 尾点模式,看看能匹配到谁):\n");
    dump_dir((std::wstring(kRoot) + L"\\*.").c_str(), false);
    printf("  [3c] *.htm(短名匹配:longname.html 会不会被捎上?):\n");
    dump_dir((std::wstring(kRoot) + L"\\*.htm").c_str(), false);
    printf("  [3d] 单个具体名字(等价于查一个文件的属性,不用通配符):\n");
    dump_dir((std::wstring(kRoot) + L"\\alpha.txt").c_str(), true);

    printf("\n== [4] 高低 32 位拼 64 位大小:SetEndOfFile 拉一个 4.5 GiB 账面文件 ==\n");
    {
        std::wstring big = std::wstring(kRoot) + L"\\big.bin";
        unique_handle f{check_win32("CreateFileW", CreateFileW, big.c_str(), GENERIC_WRITE, 0,
                                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        LARGE_INTEGER pos{.QuadPart = 0x1'2000'0000LL}; // 4.5 GiB = 0x1_2000_0000
        LARGE_INTEGER got{};
        check_win32("SetFilePointerEx", SetFilePointerEx, f.get(), pos, &got, FILE_BEGIN);
        check_win32("SetEndOfFile", SetEndOfFile, f.get());
        printf("  SetFilePointerEx 到 0x%llx 后 SetEndOfFile——没写一个字节,账面就这么大\n",
               (unsigned long long)got.QuadPart);
    }
    {
        // 用 FindFirstFileW 单名查询回看(顺带证明:单名查询走的是同一套三件套入口)
        WIN32_FIND_DATAW fd{};
        unique_find f{check_win32("FindFirstFileW", FindFirstFileW,
                                  (std::wstring(kRoot) + L"\\big.bin").c_str(), &fd)};
        unsigned long long size =
            ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;
        printf("  FindFirstFileW 回看:High=0x%lx Low=0x%lx -> 拼出 %llu 字节\n",
               (unsigned long)fd.nFileSizeHigh, (unsigned long)fd.nFileSizeLow, size);
    }

    printf("\n== [5] 根目录 C:\\ 的头几项(根目录没有 . 与 ..) ==\n");
    {
        WIN32_FIND_DATAW fd{};
        unique_find f{check_win32("FindFirstFileW", FindFirstFileW, L"C:\\*", &fd)};
        int k = 0;
        do {
            ++k;
            printf("    [%d] %s\n", k, to_utf8(fd.cFileName).c_str());
        } while (k < 8 && FindNextFileW(f.get(), &fd));
        printf("    (截断前 8 项;注意:没有 . 和 ..)\n");
    }

    printf("\n== [6] 收尾:查找句柄与文件句柄的失败值宇宙对照 ==\n");
    WIN32_FIND_DATAW fd{};
    HANDLE bad = FindFirstFileW(L"C:\\definitely_no_such_dir_xyz\\*", &fd);
    printf(
        "  失败返回 FindFirstFileW = %p,INVALID_HANDLE_VALUE? %s,err=%lu(3=ERROR_PATH_NOT_FOUND)\n",
        bad, bad == INVALID_HANDLE_VALUE ? "是" : "否", GetLastError());
    printf("  FindClose 对 INVALID_HANDLE_VALUE:%s(err=%lu,对照 01 篇 CloseHandle 对 -1 是静默 "
           "TRUE)\n",
           FindClose(bad) ? "TRUE" : "FALSE", GetLastError());

    rm_tree(kRoot);
    printf("\n(测试树已清理)\n");
    return 0;
}

// ---- 简易递归删除(实验自用;REPARSE_POINT 不下钻,防止把 junction 目标一起删了) ----
static void rm_tree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    unique_find guard{h};
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        std::wstring p = dir + L"\\" + fd.cFileName;
        bool subdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (subdir && !reparse) {
            rm_tree(p);
            RemoveDirectoryW(p.c_str());
        } else if (subdir) { // junction/目录符号链接:只删链接本身,不下钻
            RemoveDirectoryW(p.c_str());
        } else {
            DeleteFileW(p.c_str());
        }
    } while (FindNextFileW(guard.get(), &fd));
    RemoveDirectoryW(dir.c_str());
}
