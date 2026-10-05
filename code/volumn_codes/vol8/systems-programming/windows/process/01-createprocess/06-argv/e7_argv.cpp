// E7: 命令行与 argv —— main(argc,argv) 在 Windows 是 CRT 拿 GetCommandLineW 现拆的
//  [1] 花式引号传参:GetCommandLineW 原文 / CommandLineToArgvW 拆解 / main 的 argv 三方对账
//  [2] argv[0] 造假:lpApplicationName 指真身,lpCommandLine 首 token 随便写,两边 argv[0] 一起骗
// 用法: 无参=驱动;"argv"=子模式(打印三方对账)。链接需要 -lshell32。
#include <cstdio>
#include <cstring>
#include <shellapi.h>
#include <string>
#include <vector>
#include <windows.h>

static std::wstring exe_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}
static std::string u8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static int mode_argv(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("    [child] GetCommandLineW() = [%s]\n", u8(GetCommandLineW()).c_str());
    int n = 0;
    LPWSTR* parts = CommandLineToArgvW(GetCommandLineW(), &n);
    printf("    [child] CommandLineToArgvW -> argc=%d:\n", n);
    for (int i = 0; i < n; i++)
        printf("        [%d] \"%s\"\n", i, u8(parts[i]).c_str());
    const char* verdict = "YES, argument for argument";
    if (argc != n)
        verdict = "NO (count differs)";
    else
        for (int i = 0; i < argc; i++)
            if (wcscmp(argv[i], parts[i]) != 0) {
                verdict = "NO (content differs)";
                break;
            }
    LocalFree(parts); // 判定在释放前做完
    printf("    [child] main() got argc=%d:\n", argc);
    for (int i = 0; i < argc; i++)
        printf("        [%d] \"%s\"\n", i, u8(argv[i]).c_str());
    printf("    [child] CRT argv == CommandLineToArgvW? %s\n", verdict);
    return 0;
}

static void spawn_case(const wchar_t* app, const std::wstring& cmdline) {
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        printf("CreateProcessW failed err=%lu\n", GetLastError());
        return;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 2 && wcscmp(argv[1], L"argv") == 0)
        return mode_argv(argc, argv);

    printf("E7 GetCommandLineW + CommandLineToArgvW | driver pid=%lu\n", GetCurrentProcessId());
    printf("\n== [1] 花式引号:空格参数、内嵌引号、尾参数 ==\n");
    std::wstring quoted =
        L"\"" + exe_path() + L"\" argv plain \"two words\" \"quoted \"\"inner\"\" text\" tail";
    printf("parent sends cmdline:\n    %s\n", u8(quoted).c_str());
    spawn_case(nullptr, quoted);

    printf("\n== [2] argv[0] 造假:app=真身,cmdline 首 token=TOTALLY_NOT_ME.EXE ==\n");
    std::wstring spoof = L"TOTALLY_NOT_ME.EXE argv hello";
    printf("parent sends cmdline:\n    %s\n", u8(spoof).c_str());
    spawn_case(exe_path().c_str(), spoof);
    printf("\nE7 done.\n");
    return 0;
}
