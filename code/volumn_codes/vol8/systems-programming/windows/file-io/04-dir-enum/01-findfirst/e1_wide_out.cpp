// e1_wide_out.cpp —— 宽字符文件名输出的坑:wprintf vs printf × locale × 手动转 UTF-8
//
// 编译:
//   cd 01-findfirst && /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common
//   e1_wide_out.cpp -o e1_wide_out.exe
// 运行(每种输出路径一个 mode,分开跑,避免同一进程里互相污染 stdout 的字节流):
//   chmod +x e1_wide_out.exe
//   ./e1_wide_out.exe setup   # 建一个带中文名/emoji 名文件的目录
//   ./e1_wide_out.exe a|b|c|d|e|f|orient|orient2|base
//
// 实验口径:stdout 重定向到文件(管道/文件,不是控制台)——.out 里落的是什么字节,
// 就是 CRT 真正写出的字节。每个 mode 把 printf/wprintf 的返回值报给 stderr(成功写了
// 多少字符),stdout 保持纯字节,由外层脚本 od 成十六进制留证。
//
// mode 一览:
//   a      C locale(默认)下 printf("%ls", 宽名)
//   b      C locale 下 wprintf(L"%ls", 宽名)
//   c      setlocale(LC_ALL, "")(系统 ANSI 代码页,本机 GBK)后 printf("%ls")
//   d      setlocale(LC_ALL, ".UTF8")(UCRT 的 UTF-8 locale)后 printf("%ls")
//   e      setlocale(LC_ALL, ".UTF8") 后 wprintf(L"%ls")
//   f      手动 WideCharToMultiByte(CP_UTF8) 后 printf("%s")——系列契约工具的路线
//   orient 同一进程先 wprintf 再 printf(经典坑:第二个调用静默失败)
//   orient2 同一进程先 printf 再 wprintf
//   base    只报环境事实(代码页/是否 tty/locale 名),不碰中文

#include "win_dir.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <locale.h>

static const wchar_t* kDir =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e1w";

static const wchar_t* kNames[] = {L"中文文件.txt", L"emoji😀.txt", L"ascii.txt"};

static void note(const char* fmt, ...) // 报到 stderr,不污染 stdout 字节流
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

// 真实运行时数据:从目录里枚举出来的名字(专挑非 ASCII 的两个——NTFS 按字母序返回,
// 无脑取第一个会拿到 ascii.txt,那测不出任何东西)
static std::wstring pick_name(const wchar_t* want) {
    WIN32_FIND_DATAW fd{};
    unique_find f{
        check_win32("FindFirstFileW", FindFirstFileW, (std::wstring(kDir) + L"\\*").c_str(), &fd)};
    do {
        if (wcscmp(fd.cFileName, want) == 0) {
            return fd.cFileName;
        }
    } while (FindNextFileW(f.get(), &fd));
    return L"";
}

static std::wstring cn_name() {
    return pick_name(kNames[0]);
} // 中文文件.txt(BMP 内汉字)
static std::wstring emoji_name() {
    return pick_name(kNames[1]);
} // emoji😀.txt(代理对,U+1F600)

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "base";

    if (!strcmp(mode, "setup")) {
        CreateDirectoryW(kDir, nullptr);
        for (const wchar_t* n : kNames) {
            unique_handle h{check_win32("CreateFileW", CreateFileW,
                                        (std::wstring(kDir) + L"\\" + n).c_str(), GENERIC_WRITE, 0,
                                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        }
        // 源码里的宽字面量与真实文件名对不对得上(编译器输入字符集的健全性检查)
        std::wstring cn = cn_name();
        std::wstring emoji = emoji_name();
        note("setup: 建了 %zu 个文件;枚举回来对上号:中文=%s emoji=%s",
             sizeof(kNames) / sizeof(kNames[0]), cn.empty() ? "没找到!" : "对上",
             emoji.empty() ? "没找到!" : "对上");
        return 0;
    }

    if (!strcmp(mode, "base")) {
        printf("stdout isatty=%d stderr isatty=%d\n", _isatty(_fileno(stdout)),
               _isatty(_fileno(stderr)));
        printf("GetACP=%u (ANSI,本机 936=GBK)\n", GetACP());
        printf("GetOEMCP=%u\n", GetOEMCP());
        printf("GetConsoleOutputCP=%u (重定向下进程仍能读到控制台代码页设置)\n",
               GetConsoleOutputCP());
        printf("setlocale(LC_ALL,nullptr)=\"%s\"\n", setlocale(LC_ALL, nullptr));
        printf("MB_CUR_MAX=%zu\n", (size_t)MB_CUR_MAX);
        return 0;
    }

    std::wstring cn = cn_name();
    std::wstring emoji = emoji_name();
    note("mode=%s 运行时拿到:cn(经 CP_UTF8 转写)=%s", mode, to_utf8(cn.c_str()).c_str());

    if (!strcmp(mode, "a")) { // C locale + printf %ls
        int r1 = printf("[a] cn=[%ls]\n", cn.c_str());
        int r2 = printf("[a] emoji=[%ls]\n", emoji.c_str());
        note("printf(%%ls) 中文行返回 %d,emoji 行返回 %d(负数=失败;写到哪断了看 od)", r1, r2);
        return 0;
    }
    if (!strcmp(mode, "b")) { // C locale + wprintf
        int r1 = wprintf(L"[b] cn=[%ls]\n", cn.c_str());
        int r2 = wprintf(L"[b] emoji=[%ls]\n", emoji.c_str());
        note("wprintf(%%ls) 中文行返回 %d,emoji 行返回 %d", r1, r2);
        return 0;
    }
    if (!strcmp(mode, "c")) { // 系统 ANSI locale + printf
        const char* loc = setlocale(LC_ALL, "");
        note("setlocale(LC_ALL,\"\") -> \"%s\"", loc ? loc : "(null)");
        int r1 = printf("[c] cn=[%ls]\n", cn.c_str());
        int r2 = printf("[c] emoji=[%ls]\n", emoji.c_str());
        note("printf(%%ls) 中文行返回 %d,emoji 行返回 %d", r1, r2);
        return 0;
    }
    if (!strcmp(mode, "d")) { // UTF-8 locale + printf
        const char* loc = setlocale(LC_ALL, ".UTF8");
        note("setlocale(LC_ALL,\".UTF8\") -> \"%s\"", loc ? loc : "(null)");
        int r1 = printf("[d] cn=[%ls]\n", cn.c_str());
        int r2 = printf("[d] emoji=[%ls]\n", emoji.c_str());
        note("printf(%%ls) 中文行返回 %d,emoji 行返回 %d", r1, r2);
        return 0;
    }
    if (!strcmp(mode, "e")) { // UTF-8 locale + wprintf
        const char* loc = setlocale(LC_ALL, ".UTF8");
        note("setlocale(LC_ALL,\".UTF8\") -> \"%s\"", loc ? loc : "(null)");
        int r1 = wprintf(L"[e] cn=[%ls]\n", cn.c_str());
        int r2 = wprintf(L"[e] emoji=[%ls]\n", emoji.c_str());
        note("wprintf(%%ls) 中文行返回 %d,emoji 行返回 %d", r1, r2);
        return 0;
    }
    if (!strcmp(mode, "f")) { // 手动转 UTF-8(契约工具的路线)
        std::string u8a = to_utf8(cn.c_str());
        std::string u8b = to_utf8(emoji.c_str());
        int r1 = printf("[f] cn=[%s]\n", u8a.c_str());
        int r2 = printf("[f] emoji=[%s]\n", u8b.c_str());
        note("WideCharToMultiByte(CP_UTF8) 后 printf(%%s) 中文行返回 %d,emoji 行返回 %d", r1, r2);
        return 0;
    }
    if (!strcmp(mode, "orient")) { // 先宽后窄:第二个调用会怎样?
        int r1 = wprintf(L"[orient] wprintf 先走:cn=[%ls]\n", cn.c_str());
        note("第一步 wprintf 返回 %d", r1);
        int r2 = printf("[orient] 后到的 printf(%s):还想再补一行 ASCII\n", "narrow");
        note("第二步 printf 返回 %d(0/负数=这行根本没出去)", r2);
        return 0;
    }
    if (!strcmp(mode, "orient2")) { // 先窄后宽
        int r1 = printf("[orient2] printf 先走(%s):ASCII 一行\n", "narrow");
        note("第一步 printf 返回 %d", r1);
        int r2 = wprintf(L"[orient2] 后到的 wprintf:cn=[%ls]\n", cn.c_str());
        note("第二步 wprintf 返回 %d", r2);
        return 0;
    }
    fprintf(stderr, "unknown mode: %s\n", mode);
    return 2;
}
