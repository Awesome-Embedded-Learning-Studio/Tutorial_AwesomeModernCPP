// e2_last_error.cpp —— GetLastError 的时机陷阱 + 错误描述从哪来(FormatMessageW vs
// std::system_category().message vs std::generic_category().message)
//
// 编译:
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2_last_error.cpp -o
//   e2_last_error.exe
// 运行:
//   chmod +x e2_last_error.exe && ./e2_last_error.exe
//
// 观察点:
//   [1] 失败与 GetLastError 之间插入什么,会把错误码冲掉/改写:
//       立刻取 / 插一个成功的 Win32 调用 / 插 fprintf(stderr) / 插 printf / 插 new
//   [2] FormatMessageW + CP_UTF8:错误描述的正路(输出可读中文)
//   [3] MinGW libstdc++ 的 system_category().message():有 Win32 文本,但字节是
//       ANSI 代码页(本机 GBK)——打印原始字节、十六进制、转码后三份
//   [4] 同一个数字挂 generic_category 是另一套宇宙(errno)
//   [5] system_category().default_error_condition() 在 MinGW 上映射到哪

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>
#include <system_error>

static HANDLE open_missing() {
    return CreateFileW(L"no_such_file.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

// FormatMessageW 拿系统文本 → 转 UTF-8(文章 01 的 win32_text 同款)
static std::string win32_text(DWORD e) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring ws = buf ? buf : L"(no message)";
    LocalFree(buf);
    while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n')) {
        ws.pop_back();
    }
    int n =
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// ANSI 代码页(本机 GBK)字节 → UTF-8,用于把 system_category 的 message 转可读
static std::string acp_to_utf8(const std::string& a) {
    int w = MultiByteToWideChar(CP_ACP, 0, a.c_str(), (int)a.size(), nullptr, 0);
    std::wstring ws(size_t(w), L'\0');
    MultiByteToWideChar(CP_ACP, 0, a.c_str(), (int)a.size(), ws.data(), w);
    int n =
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

int main() {
    printf("== [1] GetLastError 的时机:失败和取码之间插了什么? ==\n");

    // (a) 正确姿势:失败后立刻取
    open_missing();
    DWORD a = GetLastError();
    printf("(a) 失败后立刻取                 -> err=%lu  (2=ERROR_FILE_NOT_FOUND)\n", a);

    // (b) 中间插一个会成功的 Win32 调用
    open_missing();
    DWORD pid = GetProcessId(GetCurrentProcess());
    DWORD b = GetLastError();
    printf("(b) 插入 GetProcessId(成功,%lu) -> err=%lu%s\n", pid, b,
           b == 2 ? "" : "  <-- 错误码被冲掉了!");

    // (c) 中间插 fprintf(stderr):UCRT 的 stderr 不缓冲,落地要过系统调用
    open_missing();
    fprintf(stderr, "    (c) 这行 stderr 本身就是实验的一部分\n");
    DWORD c = GetLastError();
    printf("(c) 插入 fprintf(stderr)         -> err=%lu%s\n", c, c == 2 ? "" : "  <-- 冲掉了");

    // (d) 中间插 printf:stdout 在管道下是全缓冲,这行大概率不落地
    open_missing();
    printf("    (d) 这行 printf 在缓冲区里躺着,还没过系统调用\n");
    DWORD d = GetLastError();
    printf("(d) 插入 printf(管道全缓冲)    -> err=%lu%s\n", d, d == 2 ? "" : "  <-- 冲掉了");

    // (e) 中间插一次堆分配(C++ 层面看起来"什么都没干")
    open_missing();
    char* p = new char[1000];
    p[0] = 'x';
    delete[] p;
    DWORD e = GetLastError();
    printf("(e) 插入 new/delete 1000 字节    -> err=%lu%s\n", e, e == 2 ? "" : "  <-- 冲掉了");

    printf("\n== [2] FormatMessageW + CP_UTF8:错误描述的正路 ==\n");
    for (DWORD code : {2ul, 6ul, 13ul, 32ul, 87ul}) {
        printf("  err=%-3lu -> %s\n", code, win32_text(code).c_str());
    }

    printf("\n== [3] MinGW libstdc++ system_category().message():有文本,但是 ANSI 代码页字节 ==\n");
    for (int code : {2, 6, 13, 32, 87}) {
        std::string m = std::system_category().message(code);
        printf("  system(%d) 原始字节 -> \"%s\"   hex:", code, m.c_str());
        for (unsigned char ch : m)
            printf(" %02x", ch);
        printf("\n");
        printf("  system(%d) GBK转UTF8 -> \"%s\"\n", code, acp_to_utf8(m).c_str());
    }

    printf("\n== [4] 同一个数字,挂 generic_category 是另一套宇宙(errno) ==\n");
    for (int code : {2, 13, 32}) {
        printf("  值 %-3d | system 转码: %-28s | generic: %s\n", code,
               acp_to_utf8(std::system_category().message(code)).c_str(),
               std::generic_category().message(code).c_str());
    }

    printf("\n== [5] system_category().default_error_condition 在 MinGW 上映射到哪 ==\n");
    for (int code : {2, 13, 32, 87}) {
        auto cond = std::system_category().default_error_condition(code);
        printf("  system(%d) -> condition{value=%d, category=%s, message=\"%s\"}\n", code,
               cond.value(), cond.category().name(), acp_to_utf8(cond.message()).c_str());
    }
    return 0;
}
