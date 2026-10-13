// e1_errors.cpp —— FindFirstFileW/FindNextFileW
// 的错误路径:「目录不存在」vs「目录空」vs「模式无匹配」
//
// 编译:
//   cd 01-findfirst && /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common
//   e1_errors.cpp -o e1_errors.exe
// 运行:
//   chmod +x e1_errors.exe && ./e1_errors.exe
//
// 观察点(错误码三兄弟:2 ERROR_FILE_NOT_FOUND / 3 ERROR_PATH_NOT_FOUND / 18 ERROR_NO_MORE_FILES):
//   (1) 不存在的目录 + \\*      -> FindFirstFileW 直接 INVALID_HANDLE_VALUE, err=3
//   (2) 不存在的目录 + \\*.txt   -> 同样 err=3(路径先判,模式还没轮上)
//   (3) 存在的目录 + 无匹配模式  -> err=2(FILE_NOT_FOUND——路径没问题,是"没找到文件")
//   (4) 空目录 + \\*             -> 成功!先吐 "." 再吐 "..",FindNextFileW 尽头 err=18
//       ——"目录空"在 \\* 枚举下永远不空,空判定要跳过两个点项再看
//   (5) 空目录 + \\*.txt         -> err=2(点项只匹配 *)
//   (6) 给文件路径挂 \\*          -> err=267 ERROR_DIRECTORY(目录名非法)
//   (7) 尾部反斜杠(无模式)      -> 看 Windows 认不认
//   (8) FindNextFileW 失败后不看 GetLastError 直接再枚举会怎样(句柄已尽,继续 FALSE)

#include "win_dir.hpp"

#include <cstdio>
#include <string>

static const wchar_t* kRoot =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e1e";

static void probe(const wchar_t* label, const wchar_t* pattern) {
    WIN32_FIND_DATAW fd{};
    SetLastError(1234); // 哨兵:证明后面的错误码是本次调用落下的
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  %-28s -> INVALID_HANDLE_VALUE, err=%lu\n", to_utf8(label).c_str(),
               GetLastError());
        return;
    }
    unique_find guard{h};
    int n = 0;
    do {
        ++n;
    } while (FindNextFileW(guard.get(), &fd));
    printf("  %-28s -> 成功,吐了 %d 项(最后一项之后 FindNextFileW err=%lu)\n",
           to_utf8(label).c_str(), n, GetLastError());
}

int main() {
    // 建树:一个有货的目录、一个空目录、一个孤零零的文件
    CreateDirectoryW(kRoot, nullptr);
    CreateDirectoryW((std::wstring(kRoot) + L"\\full").c_str(), nullptr);
    CreateDirectoryW((std::wstring(kRoot) + L"\\empty").c_str(), nullptr);
    unique_handle f{check_win32("CreateFileW", CreateFileW,
                                (std::wstring(kRoot) + L"\\plain.txt").c_str(), GENERIC_WRITE, 0,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};

    std::wstring root = kRoot;
    printf("== [1] 错误路径区分 ==\n");
    probe(L"不存在目录 \\*", (root + L"\\no_such\\*").c_str());
    probe(L"不存在目录 \\*.txt", (root + L"\\no_such\\*.txt").c_str());
    probe(L"存在目录,模式无匹配", (root + L"\\full\\*.nomatch").c_str());
    probe(L"空目录 \\*", (root + L"\\empty\\*").c_str());
    probe(L"空目录 \\*.txt", (root + L"\\empty\\*.txt").c_str());
    probe(L"文件路径挂 \\*", (root + L"\\plain.txt\\*").c_str());
    probe(L"尾部反斜杠(无模式)", (root + L"\\full\\").c_str());
    probe(L"尾部双反斜杠+*", (root + L"\\full\\\\*").c_str());

    printf("\n== [2] 空目录的完整枚举过程(三件套逐项) ==\n");
    {
        WIN32_FIND_DATAW fd{};
        unique_find h{
            check_win32("FindFirstFileW", FindFirstFileW, (root + L"\\empty\\*").c_str(), &fd)};
        printf("  FindFirstFileW 成功,第一项 = \"%s\"\n", to_utf8(fd.cFileName).c_str());
        while (FindNextFileW(h.get(), &fd)) {
            printf("  FindNextFileW  -> \"%s\"\n", to_utf8(fd.cFileName).c_str());
        }
        printf("  FindNextFileW 止步,err=%lu(18=ERROR_NO_MORE_FILES,正常收尾,不是错误)\n",
               GetLastError());
        BOOL again = FindNextFileW(h.get(), &fd);
        printf("  句柄用尽后再 FindNextFileW -> %d, err=%lu(还是 18,不会翻页也不会崩)\n", again,
               GetLastError());
    }

    printf("\n== [3] 对照:ERROR_NO_MORE_FILES 只属于 FindNextFileW ==\n");
    printf("  FindFirstFileW 的失败码是 2/3/267 这类\"真错误\";18 只会出现在收尾的\n");
    printf("  FindNextFileW 上——把它当异常抛就冤枉了,它是循环的退出条件。\n");

    // 清理
    DeleteFileW((root + L"\\plain.txt").c_str());
    RemoveDirectoryW((root + L"\\full").c_str());
    RemoveDirectoryW((root + L"\\empty").c_str());
    RemoveDirectoryW(root.c_str());
    printf("\n(测试树已清理)\n");
    return 0;
}
