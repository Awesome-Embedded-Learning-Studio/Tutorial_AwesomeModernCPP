// E1: CreateProcessW 全解剖
//  [0] STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_LIST:句柄白名单继承
//  [1] lpApplicationName vs lpCommandLine 的路径搜索规则矩阵
//  [2] 双句柄(hProcess/hThread)分工 + CREATE_SUSPENDED 两段式
//  [3] lpEnvironment:NULL 继承 vs 自建(整块替换,不是合并)
// 用法: 无参 = 驱动;子模式见各 argv 分支。
#include <cstdio>
#include <cstring>
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
static void ensure_dir(const std::wstring& d) {
    CreateDirectoryW(d.c_str(), nullptr);
}
static std::wstring temp_base() {
    wchar_t buf[MAX_PATH];
    GetTempPathW(MAX_PATH, buf);
    std::wstring base = std::wstring(buf) + L"vol8_e1";
    ensure_dir(base);
    return base;
}
static bool spawn(const wchar_t* app, std::wstring cmdline, DWORD flags, bool inherit,
                  STARTUPINFOEXW* siex, PROCESS_INFORMATION* pi) {
    STARTUPINFOEXW local{};
    if (!siex)
        siex = &local;
    siex->StartupInfo.cb = sizeof(*siex);
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    SetLastError(1234);
    BOOL ok = CreateProcessW(app, buf.data(), nullptr, nullptr, inherit, flags, nullptr, nullptr,
                             &siex->StartupInfo, pi);
    if (!ok)
        return false;
    return true;
}
static void wait_and_code(PROCESS_INFORMATION& pi, const char* tag) {
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    printf("    -> exit code=%lu (0x%08lX)\n", code, code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    (void)tag;
}
static void rm_file(const std::wstring& p) {
    DeleteFileW(p.c_str());
}

// ---------- 子模式 ----------
static int mode_attr(wchar_t** argv) {
    // argv[2]=ha argv[3]=hb:句柄值(十进制);白名单里的能写,没进白名单的 err=6
    HANDLE ha = (HANDLE)(uintptr_t)_wcstoui64(argv[2], nullptr, 10);
    HANDLE hb = (HANDLE)(uintptr_t)_wcstoui64(argv[3], nullptr, 10);
    const char* msg = "hello from child via inherited handle\n";
    DWORD n = 0;
    BOOL oa = WriteFile(ha, msg, (DWORD)lstrlenA(msg), &n, nullptr);
    printf("    [child] WriteFile(h_a=%llu) -> %d (err=%lu)\n", (unsigned long long)(uintptr_t)ha,
           oa, oa ? 0 : GetLastError());
    SetLastError(1234);
    BOOL ob = WriteFile(hb, msg, (DWORD)lstrlenA(msg), &n, nullptr);
    printf("    [child] WriteFile(h_b=%llu) -> %d (err=%lu)\n", (unsigned long long)(uintptr_t)hb,
           ob, ob ? 0 : GetLastError());
    return 9;
}
static int mode_echo(wchar_t** argv) {
    wchar_t m[MAX_PATH];
    GetModuleFileNameW(nullptr, m, MAX_PATH);
    printf("    [child echo] argv[0]    = %s\n", u8(argv[0]).c_str());
    printf("    [child echo] real module= %s\n", u8(m).c_str());
    printf("    [child echo] GetCommandLineW() = %s\n", u8(GetCommandLineW()).c_str());
    fflush(stdout);
    return 0;
}
static int mode_config(const wchar_t* token_file) {
    HANDLE f =
        CreateFileW(token_file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return 13;
    char buf[128]{};
    DWORD n = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &n, nullptr);
    CloseHandle(f);
    printf("    [child config] token read AFTER resume: %.*s\n", (int)n, buf);
    fflush(stdout);
    return (strstr(buf, "0xC0FFEE") != nullptr) ? 7 : 13;
}
static int mode_env() {
    const char* marker = getenv("VOL8_E1_MARKER");
    const char* path = getenv("PATH");
    PWSTR env = GetEnvironmentStringsW();
    int count = 0;
    for (PWSTR p = env; *p;) {
        while (*p)
            p++;
        p++;
        count++;
    }
    FreeEnvironmentStringsW(env);
    printf("    [child env] VOL8_E1_MARKER=%s\n", marker ? marker : "(NULL)");
    printf("    [child env] PATH=%s\n", path ? "(present)" : "(NULL - absent!)");
    printf("    [child env] environment entries=%d\n", count);
    fflush(stdout);
    return 0;
}

// ---------- 驱动 ----------
static void section0(const std::wstring& base) {
    printf("== [0] STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_LIST:句柄白名单继承 ==\n");
    std::wstring fa = base + L"\\log_a.txt";
    std::wstring fb = base + L"\\log_b.txt";
    rm_file(fa);
    rm_file(fb);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; // 两个都可继承
    HANDLE ha =
        CreateFileW(fa.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
    HANDLE hb =
        CreateFileW(fb.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
    printf("parent opened log_a/log_b both inheritable; whitelist contains ONLY log_a\n");

    SIZE_T need = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &need);
    auto list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, need);
    InitializeProcThreadAttributeList(list, 1, 0, &need);
    HANDLE ok_list[1] = {ha}; // 白名单:只有 log_a
    UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, ok_list, sizeof(ok_list),
                              nullptr, nullptr);

    STARTUPINFOEXW siex{};
    siex.lpAttributeList = list;
    siex.StartupInfo.dwFlags = STARTF_USESHOWWINDOW; // wShowWindow 一句话:GUI/新控制台窗口的显隐
    siex.StartupInfo.wShowWindow = SW_HIDE; // 本实验子进程共享父控制台,无视觉效果,仅示范字段
    PROCESS_INFORMATION pi{};
    std::wstring cmd = exe_path() + L" attr " + std::to_wstring((unsigned long long)(uintptr_t)ha) +
                       L" " + std::to_wstring((unsigned long long)(uintptr_t)hb);
    if (!spawn(nullptr, cmd, EXTENDED_STARTUPINFO_PRESENT, TRUE, &siex, &pi)) {
        printf("    CreateProcessW failed err=%lu\n", GetLastError());
    } else {
        wait_and_code(pi, "attr");
    }
    // 读回两个文件
    for (auto& f : {std::make_pair(std::string("log_a (whitelisted)"), fa),
                    std::make_pair(std::string("log_b (NOT whitelisted)"), fb)}) {
        HANDLE h = CreateFileW(f.second.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        char buf[128]{};
        DWORD n = 0;
        if (h != INVALID_HANDLE_VALUE) {
            ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr);
            CloseHandle(h);
        }
        printf("    %s content: \"%s\"\n", f.first.c_str(), buf);
    }
    DeleteProcThreadAttributeList(list);
    HeapFree(GetProcessHeap(), 0, list);
    CloseHandle(ha);
    CloseHandle(hb);
}

static void section1(const std::wstring& base) {
    printf("\n== [1] lpApplicationName vs lpCommandLine:路径搜索规则 ==\n");
    std::wstring plain = base + L"\\plain";       // 只进 PATH
    std::wstring spaced = base + L"\\with space"; // 只测引号规则
    std::wstring cwd = base + L"\\cwd";           // 空目录,当当前目录
    ensure_dir(plain);
    ensure_dir(spaced);
    ensure_dir(cwd);
    std::wstring self = exe_path();
    CopyFileW(self.c_str(), (plain + L"\\fakeecho.exe").c_str(), FALSE);
    CopyFileW(self.c_str(), (spaced + L"\\fakeecho.exe").c_str(), FALSE);
    // 前置:父进程所在目录与 CWD 都没有 fakeecho.exe(父进程自己叫 e1_anatomy.exe)
    SetCurrentDirectoryW(cwd.c_str());
    wchar_t old_path[32768];
    GetEnvironmentVariableW(L"PATH", old_path, 32768);
    std::wstring new_path = plain + L";" + old_path;
    SetEnvironmentVariableW(L"PATH", new_path.c_str());
    printf("staged: PATH gets 'plain' dir; CWD='cwd' (empty); parent dir has no fakeecho.exe\n");

    PROCESS_INFORMATION pi{};
    // (a) lpApplicationName=NULL:按 模块搜索顺序(应用目录->CWD->系统目录->Windows->PATH)找第一个
    // token
    printf("(a) app=NULL, cmdline='fakeecho.exe found_via_path'\n");
    if (spawn(nullptr, L"fakeecho.exe echo found_via_path", 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "a");
    else
        printf("    CreateProcessW failed err=%lu\n", GetLastError());

    // (b) lpApplicationName 给相对名:只相对当前目录解析,不搜索
    printf("(b) app=L'fakeecho.exe' (relative, CWD lacks it)\n");
    if (spawn(L"fakeecho.exe", L"fakeecho.exe via_appname", 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "b");
    else
        printf("    CreateProcessW failed err=%lu  <-- app name resolves against CWD ONLY, no PATH "
               "search\n",
               GetLastError());

    // (c) app=绝对路径 + cmdline 首 token 与它无关:argv[0] 与真实模块解耦
    std::wstring abs_spaced = spaced + L"\\fakeecho.exe";
    printf("(c) app=<abs 'with space' path>, cmdline='TOTALLY_FAKE_ARGV0.EXE arg1'\n");
    if (spawn(abs_spaced.c_str(), L"TOTALLY_FAKE_ARGV0.EXE echo arg1", 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "c");
    else
        printf("    CreateProcessW failed err=%lu\n", GetLastError());
    printf("    ^ argv[0] came from cmdline token, NOT from where the exe really loaded\n");

    // (d) app=NULL + 带空格路径不加引号,且目录里埋了截断名诱饵(with.exe):首 token + .exe 优先命中
    std::wstring decoy = base + L"\\with.exe"; // 首 token 'C:\...\vol8_e1\with' 补 .exe 就是它
    CopyFileW(self.c_str(), decoy.c_str(), FALSE);
    printf("(d) app=NULL, UNQUOTED '<abs with space> x', WITH decoy 'with.exe' planted (= "
           "Program.exe attack)\n");
    if (spawn(nullptr, abs_spaced + L" x", 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "d");
    else
        printf("    CreateProcessW failed err=%lu\n", GetLastError());
    rm_file(decoy);
    printf("(d2) decoy removed, SAME unquoted cmdline\n");
    if (spawn(nullptr, abs_spaced + L" x", 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "d2");
    else
        printf("    CreateProcessW failed err=%lu\n", GetLastError());

    // (e) 引号包住:成立
    printf("(e) app=NULL, cmdline=QUOTED \"<abs with space>\" x\n");
    std::wstring quoted = L"\"" + abs_spaced + L"\" echo x";
    if (spawn(nullptr, quoted, 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "e");
    else
        printf("    CreateProcessW failed err=%lu\n", GetLastError());

    SetEnvironmentVariableW(L"PATH", old_path);
}

static void section2(const std::wstring& base) {
    printf("\n== [2] 双句柄分工 + CREATE_SUSPENDED 两段式(先配置再跑) ==\n");
    std::wstring token = base + L"\\token.txt";
    rm_file(token);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = exe_path() + L" config " + token;
    printf("spawning suspended; token file does NOT exist yet\n");
    if (!spawn(nullptr, cmd, CREATE_SUSPENDED, FALSE, nullptr, &pi)) {
        printf("    CreateProcessW failed err=%lu\n", GetLastError());
        return;
    }
    // 两段式第 1 段:进程冻结着,父进程安心做先期配置(此刻子进程不可能读文件)
    HANDLE f = CreateFileW(token.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n = 0;
    WriteFile(f, "TOKEN=0xC0FFEE", 14, &n, nullptr);
    CloseHandle(f);
    printf("phase 1 done: token written while child is still frozen\n");
    // 第 2 段:ResumeThread 放行,返回的是"此前挂起计数"(应为 1)
    DWORD sc = ResumeThread(pi.hThread);
    printf("phase 2: ResumeThread -> prev suspend count=%lu\n", sc);
    CloseHandle(pi.hThread); // 线程句柄用完即关:进程不受影响
    printf("hThread closed; process keeps running (hProcess still waits)\n");
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    printf("exit code=%lu (7 = token verified, 13 = missing/bad token)\n", code);
    CloseHandle(pi.hProcess);
}

static void section3() {
    printf("\n== [3] lpEnvironment:NULL 继承 vs 自建(替换不合并) ==\n");
    PROCESS_INFORMATION pi{};
    SetEnvironmentVariableW(L"VOL8_E1_MARKER", L"inherited-from-parent");
    printf("(a) lpEnvironment=NULL -> child inherits parent block\n");
    std::wstring cmd = exe_path() + L" env";
    if (spawn(nullptr, cmd, 0, FALSE, nullptr, &pi))
        wait_and_code(pi, "env-a");
    else
        printf("    CreateProcessW failed err=%lu\n", GetLastError());

    printf(
        "(b1) minimal custom block (marker+SYSTEMROOT, NO PATH) -> loader cannot even start us\n");
    wchar_t root[512]{};
    GetEnvironmentVariableW(L"SYSTEMROOT", root, 512);
    // block 布局: "NAME=VAL\0NAME2=VAL2\0\0"(双 \0 结尾)
    std::wstring b1 = std::wstring(L"VOL8_E1_MARKER=custom-block-REPLACES") + L'\0' +
                      (L"SYSTEMROOT=" + std::wstring(root)) + L'\0' + L'\0';
    auto run_custom = [&](const std::wstring& block) {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION p{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        BOOL ok =
            CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                           (LPVOID)block.c_str(), nullptr, &si, &p);
        if (ok)
            wait_and_code(p, "env-custom");
        else
            printf("    CreateProcessW failed err=%lu\n", GetLastError());
    };
    run_custom(b1);
    printf("    ^ 0xC0000135 = STATUS_DLL_NOT_FOUND: dynamic exe needs PATH to find ucrt runtime "
           "DLLs\n");

    printf("(b2) custom block + PATH hand-carried -> starts, and parent's other vars are GONE\n");
    wchar_t pbuf[32768]{};
    GetEnvironmentVariableW(L"PATH", pbuf, 32768);
    std::wstring b2 = b1.substr(0, b1.size() - 1) + (L"PATH=" + std::wstring(pbuf)) + L'\0' + L'\0';
    run_custom(b2);
    printf(
        "    ^ entries count = exactly what we put in (no merge); parent marker value REPLACED\n");
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // 交叉进程转录,stdout 不缓冲
    if (argc >= 2) {
        if (wcscmp(argv[1], L"attr") == 0 && argc == 4)
            return mode_attr(argv);
        if (wcscmp(argv[1], L"echo") == 0)
            return mode_echo(argv);
        if (wcscmp(argv[1], L"config") == 0 && argc == 3)
            return mode_config(argv[2]);
        if (wcscmp(argv[1], L"env") == 0)
            return mode_env();
        printf("    [child dump] argc=%d argv[0]=%s argv[1]=%s\n", argc, u8(argv[0]).c_str(),
               argc > 1 ? u8(argv[1]).c_str() : "-");
        {
            wchar_t m[MAX_PATH];
            GetModuleFileNameW(nullptr, m, MAX_PATH);
            printf("    [child dump] real module= %s\n", u8(m).c_str());
            printf("    [child dump] GetCommandLineW() = %s\n", u8(GetCommandLineW()).c_str());
        }
        fflush(stdout);
        return 1;
    }
    std::wstring base = temp_base();
    printf("E1 CreateProcessW anatomy | parent pid=%lu\n", GetCurrentProcessId());
    section0(base);
    section1(base);
    section2(base);
    section3();
    printf("\nE1 done.\n");
    return 0;
}
