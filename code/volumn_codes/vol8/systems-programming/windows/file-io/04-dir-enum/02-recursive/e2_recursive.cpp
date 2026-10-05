// e2_recursive.cpp —— 手搓递归枚举:5 层树的 DFS 序、junction 默认不下钻、跟随时的环
//
// 编译:
//   cd 02-recursive && /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common
//   e2_recursive.cpp -o e2_recursive.exe
// 运行:
//   chmod +x e2_recursive.exe
//   ./e2_recursive.exe setup    # 建树 + 两个 junction(外部目标 / 指回根的环)
//   ./e2_recursive.exe          # 默认:REPARSE_POINT 目录不下钻
//   ./e2_recursive.exe follow   # 跟随 junction 下钻(自备 40 层深度闸)
//   ./e2_recursive.exe clean
//
// 观察点:
//   (1) DFS 序:目录本身先报,枚举中撞到子目录立刻下钻(与 Linux 侧
//       recursive_directory_iterator 的 DFS 前序同构);NTFS 字母序让同层条目有序
//   (2) 默认对 REPARSE_POINT 目录:报一行(带 [junction] 标记)、计一个节点、不下钻
//   (3) follow 模式:junction 指回根自己的环,Windows 内核不拦(Linux 有 40 层
//       ELOOP 兜底)——手搓递归必须自己带深度闸,否则栈溢出;数一数根目录被转了几圈
//   (4) 节点统计:默认 vs follow 的 files/dirs/links 三列对照
//
// junction 造法:代码里没有官方 CreateJunction API,正经路子是 FSCTL_SET_REPARSE_POINT
// 自写 reparse 数据(e3 里有它的读侧),这里直接 cmd mklink /J,命令原样记进 .out。

#include "win_dir.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

static const wchar_t* kRoot =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e2tree";
static const wchar_t* kExt =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e2ext";

struct Stats {
    long files = 0, dirs = 0, links = 0, dots = 0; // links = 被当叶子处理的 reparse 目录
    long printed = 0;
};

static void make_file(const std::wstring& path, const char* data) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    DWORD w = 0;
    if (data) {
        check_win32("WriteFile", WriteFile, f.get(), data, (DWORD)lstrlenA(data), &w, nullptr);
    }
}

static void rm_tree(const std::wstring& dir) // 与 e1 同款:reparse 不下钻
{
    WIN32_FIND_DATAW fd{};
    unique_find h{FindFirstFileW((dir + L"\\*").c_str(), &fd)};
    if (!h) {
        return;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
            continue;
        }
        std::wstring p = dir + L"\\" + fd.cFileName;
        bool subdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (subdir && !reparse) {
            rm_tree(p);
            RemoveDirectoryW(p.c_str());
        } else if (subdir) {
            RemoveDirectoryW(p.c_str()); // 只删链接本身
        } else {
            DeleteFileW(p.c_str());
        }
    } while (FindNextFileW(h.get(), &fd));
    RemoveDirectoryW(dir.c_str());
}

static void walk(const std::wstring& dir, int depth, bool follow, Stats& st, long& laps) {
    // 环圈数口径:每次下钻进一个叫 jn_cycle 的目录就是又转了一圈(根的内容被整套重访)

    WIN32_FIND_DATAW fd{};
    // 不走 check_win32:环跟得深了路径会超 MAX_PATH,打不开是"这个分支到头了"的
    // 正常下场,记下来比抛异常有用
    unique_find h{FindFirstFileW((dir + L"\\*").c_str(), &fd)};
    if (!h) {
        printf("%*s[打不开:路径 %zu 字符 err=%lu]\n", depth * 2, "", dir.size(), GetLastError());
        return;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
            ++st.dots;
            continue;
        }
        std::wstring p = dir + L"\\" + fd.cFileName;
        bool subdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        const char* tag = reparse ? (subdir ? " [junction]" : " [symlink]") : (subdir ? "/" : "");
        if (st.printed < 64) {
            printf("%*s%s%s\n", depth * 2, "", to_utf8(fd.cFileName).c_str(), tag);
            ++st.printed;
        } else if (st.printed == 64) {
            printf("%*s……(打印截断,计数继续)\n", depth * 2, "");
            ++st.printed;
        }
        if (subdir && !reparse) {
            ++st.dirs;
            if (!follow || depth < 40) {
                walk(p, depth + 1, follow, st, laps);
            }
        } else if (subdir && follow && depth < 40) {
            ++st.links;
            if (!wcscmp(fd.cFileName, L"jn_cycle")) {
                ++laps;
            }
            walk(p, depth + 1, follow, st, laps); // 跟随:下钻
        } else {
            if (subdir) {
                ++st.links;
            } else {
                ++st.files;
            }
        }
    } while (FindNextFileW(h.get(), &fd));
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    std::wstring root = kRoot;

    if (mode == "setup") {
        // 子 cmd 的 cwd 若还是 WSL 的 UNC 路径,cmd 启动横幅会以 GBK 抱怨"UNC 不支持"
        // (重定向拦不住横幅)。把本进程 cwd 挪到 C:\ ,横幅从根上消失;后续路径全是绝对路径。
        SetCurrentDirectoryW(L"C:\\");
        rm_tree(kRoot);
        rm_tree(kExt);
        CreateDirectoryW(kRoot, nullptr);
        CreateDirectoryW(kExt, nullptr);
        // 5 层:a/ab/abc/abcd/abcde,每层一个文件
        std::wstring d = root;
        for (const wchar_t* seg : {L"a", L"ab", L"abc", L"abcd", L"abcde"}) {
            d += L"\\";
            d += seg;
            CreateDirectoryW(d.c_str(), nullptr);
            make_file(d + L"\\f_" + seg + L".txt", "x");
        }
        make_file(root + L"\\f_root.txt", "r");
        CreateDirectoryW((root + L"\\b").c_str(), nullptr);
        make_file(root + L"\\b\\f_b.txt", "b");
        // 外部目标(树外,junction 指过去)
        make_file(std::wstring(kExt) + L"\\outside1.txt", "o1");
        make_file(std::wstring(kExt) + L"\\outside2.txt", "o2");
        // 两个 junction:一个出树,一个指回根(环)
        // (子 cmd 整体静默——UNC cwd 的抱怨与 mklink 回显的代码页都不受控,结果由下面的
        //  FindFirstFileW 属性验证后用 UTF-8 自己报)
        int rc1 = _wsystem(
            (L"cmd /c mklink /J \"" + root + L"\\jn_ext\" \"" + kExt + L"\" >nul 2>&1").c_str());
        int rc2 = _wsystem(
            (L"cmd /c mklink /J \"" + root + L"\\jn_cycle\" \"" + root + L"\" >nul 2>&1").c_str());
        printf("mklink /J jn_ext -> rc=%d, mklink /J jn_cycle -> rc=%d\n", rc1, rc2);
        for (const wchar_t* jn : {L"jn_ext", L"jn_cycle"}) {
            WIN32_FIND_DATAW fd{};
            unique_find q{check_win32("FindFirstFileW", FindFirstFileW, (root + L"\\" + jn).c_str(),
                                      &fd)}; // 单名查询不跟随
            printf("  验证 %s:attr=0x%lx(DIRECTORY=0x10 REPARSE=0x400 %s)\n", to_utf8(jn).c_str(),
                   (unsigned long)fd.dwFileAttributes,
                   (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? "命中" : "没有!");
        }
        printf("树形(5 层,每层 1 文件;另有 b/ 与 2 个 junction)\n");
        return 0;
    }
    if (mode == "clean") {
        rm_tree(kRoot);
        rm_tree(kExt);
        printf("已清理\n");
        return 0;
    }

    bool follow = (mode == "follow");
    printf("== 递归枚举(%s)DFS 前序:撞到子目录立刻下钻 ==\n",
           follow ? "follow:跟随 junction,自备 40 层深度闸" : "默认:REPARSE_POINT 目录不下钻");
    Stats st;
    long laps = 0;
    walk(root, 0, follow, st, laps);
    printf("\n统计:文件 %ld,真目录 %ld,链接叶子 %ld,点项 %ld\n", st.files, st.dirs, st.links,
           st.dots);
    if (follow) {
        printf("junction 环共转了 %ld 圈。结束机制看 .out 尾部:是路径长度撞上 MAX_PATH 的墙\n"
               "(259/262/264 字符那批 err=3)先拦住的,40 层深度闸这次没轮上;Linux 侧同样的环\n"
               "是内核 40 层 ELOOP 自动刹车——Windows 没有内核兜底,跟随链接的递归只能自备闸\n",
               laps);
    } else {
        printf("junction 环 0 圈(不下钻,无环)\n");
    }
    return 0;
}
