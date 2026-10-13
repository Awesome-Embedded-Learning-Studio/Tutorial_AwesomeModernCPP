// E4: Job 对象
//  [1] JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE:最后一只 Job 句柄一关,全 Job 陪葬(显式 CloseHandle)
//  [2] 父进程退出 = 它手里的 Job 句柄自动关闭 -> 同样全体陪葬;对照:没这面旗,孩子变孤儿继续活
//  [3] 反例:孩子自己继承了 Job 句柄 -> 父退出杀不掉(经典的"陪葬失灵"坑)
//  [4] QueryInformationJobObject:基本计数(活跃数/总页错误/进程 id 清单)
//  [5] JOB_OBJECT_LIMIT_PROCESS_MEMORY:40MB 限额下分配必败(ERROR_COMMITMENT_LIMIT / bad_alloc)
// 用法: 无参=驱动;"loop <ms>"|"quick <ms> <code>"|"alloc <total_mb> <step_mb>"|"mid kill|nokill
// <pidfile> <hbfile>" 为子模式。
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <windows.h>

static std::wstring exe_path() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}
static std::wstring temp_file(const wchar_t* name) {
    wchar_t tb[MAX_PATH];
    GetTempPathW(MAX_PATH, tb);
    std::wstring base = std::wstring(tb) + L"vol8_e4";
    CreateDirectoryW(base.c_str(), nullptr);
    return base + L"\\" + name;
}
// 挂起出生 -> 进 Job -> 放行:经典三连,杜绝"孩子抢跑进不了 Job"的竞态
static bool spawn_into_job(HANDLE job, std::wstring cmdline, HANDLE child_stdout, bool inherit_job,
                           PROCESS_INFORMATION* pi, DWORD extra_flags = 0) {
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = child_stdout;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (out) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = out;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    } // out==NULL: 不设旗,孩子自动继承驱动现成的 std 句柄
    SECURITY_ATTRIBUTES job_sa{sizeof(job_sa), nullptr, inherit_job ? TRUE : FALSE};
    HANDLE my_job = job;
    if (inherit_job) {
        // 复制一份可继承的 Job 句柄专门给孩子继承
        if (!DuplicateHandle(GetCurrentProcess(), job, GetCurrentProcess(), &my_job, 0, TRUE,
                             DUPLICATE_SAME_ACCESS))
            return false;
    }
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | extra_flags,
                        nullptr, nullptr, &si, pi))
        return false;
    if (job && AssignProcessToJobObject(job, pi->hProcess)) {
        // ok
    } else if (job) {
        printf("    [spawn_into_job] AssignProcessToJobObject failed err=%lu\n", GetLastError());
    }
    ResumeThread(pi->hThread);
    if (inherit_job && my_job != job)
        CloseHandle(my_job); // 父进程这份副本立刻放手,孩子那份已到手
    return true;
}
static int count_lines(const std::wstring& file) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    char buf[8192];
    DWORD n = 0;
    int lines = 0;
    BOOL any = FALSE;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) {
        for (DWORD i = 0; i < n; i++) {
            if (buf[i] == '\n')
                lines++;
            any = TRUE;
        }
    }
    CloseHandle(h);
    return any ? lines : 0;
}
static HANDLE make_job(bool kill_on_close) {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
    if (kill_on_close)
        eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &eli, sizeof(eli));
    return job;
}

// ---------- 子模式 ----------
static int mode_loop(int max_ms) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    ULONGLONG t0 = GetTickCount64();
    for (int i = 1;; i++) {
        printf("hb %d %llu\n", i, (unsigned long long)(GetTickCount64() - t0));
        if (GetTickCount64() - t0 >= (ULONGLONG)max_ms)
            break;
        Sleep(100);
    }
    return 0;
}
static int mode_quick(int ms, int code) {
    Sleep(ms);
    return code;
}
static int mode_alloc(int total_mb, int step_mb) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[alloc child] pid=%lu target=%dMB step=%dMB\n", GetCurrentProcessId(), total_mb,
           step_mb);
    int committed = 0;
    for (int mb = step_mb; mb <= total_mb; mb += step_mb) {
        void* p =
            VirtualAlloc(nullptr, (SIZE_T)step_mb << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            printf("[alloc child] VirtualAlloc failed at total=%dMB err=%lu\n", committed,
                   GetLastError());
            break;
        }
        *(volatile int*)p = 1; // 真摸一页
        committed = mb;
    }
    printf("[alloc child] VirtualAlloc phase ended at %dMB\n", committed);
    try {
        auto* big = new unsigned char[64 << 20];
        *(volatile unsigned char*)big = 2;
        printf("[alloc child] new 64MB: got %p\n", (void*)big);
        delete[] big;
    } catch (const std::bad_alloc& e) {
        printf("[alloc child] new 64MB threw std::bad_alloc (%s)\n", e.what());
    }
    printf("[alloc child] exiting 42\n");
    return 42;
}
static SECURITY_ATTRIBUTES g_inherit_sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
static int mode_mid(bool kill, const wchar_t* pidfile, const wchar_t* hbfile) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    DeleteFileW(pidfile);
    HANDLE job = make_job(kill);
    HANDLE hb = CreateFileW(hbfile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &g_inherit_sa, CREATE_ALWAYS, 0, nullptr);
    PROCESS_INFORMATION pi{};
    if (!spawn_into_job(job, exe_path() + L" loop 15000", hb, false, &pi)) {
        printf("[mid] spawn failed err=%lu\n", GetLastError());
        return 1;
    }
    printf("[mid] child pid=%lu in job (kill_on_close=%d); mid holds on 400ms for handle pickup, "
           "then exits\n",
           pi.dwProcessId, kill ? 1 : 0);
    HANDLE pf = CreateFileW(pidfile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    char line[64];
    int n = sprintf_s(line, sizeof(line), "%lu", pi.dwProcessId);
    DWORD w = 0;
    WriteFile(pf, line, n, &w, nullptr);
    CloseHandle(pf);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hb);
    Sleep(400); // 给驱动留抓句柄的窗口(测量起点仍是 mid 真退出)
    return 0;   // mid 的 Job 句柄在此刻随进程关闭
}

// ---------- 驱动 ----------
static void section1() {
    printf("== [1] KILL_ON_JOB_CLOSE:显式关掉 Job 句柄 ==\n");
    std::wstring hbfile = temp_file(L"hb1.txt");
    DeleteFileW(hbfile.c_str());
    HANDLE job = make_job(true);
    HANDLE hb = CreateFileW(hbfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &g_inherit_sa, CREATE_ALWAYS, 0, nullptr);
    PROCESS_INFORMATION pi{};
    spawn_into_job(job, exe_path() + L" loop 15000", hb, false, &pi);
    Sleep(600); // 让孩子蹦几拍
    int before = count_lines(hbfile);
    printf("child alive, heartbeats=%d; closing the ONLY job handle now\n", before);
    ULONGLONG t0 = GetTickCount64();
    CloseHandle(job);
    DWORD w = WaitForSingleObject(pi.hProcess, 5000);
    ULONGLONG dt = GetTickCount64() - t0;
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    printf("job handle closed -> child wait=0x%lX after %llums, exit code=%lu (0x%08lX)\n",
           (unsigned long)w, (unsigned long long)dt, code, code);
    Sleep(200);
    printf("heartbeats frozen at %d (no more lines after death)\n", count_lines(hbfile));
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hb);
}

static void run_mid_case(bool kill) {
    std::wstring pidfile = temp_file(kill ? L"pid_k.txt" : L"pid_n.txt");
    std::wstring hbfile = temp_file(kill ? L"hb_k.txt" : L"hb_n.txt");
    DeleteFileW(pidfile.c_str());
    DeleteFileW(hbfile.c_str());
    std::wstring cmd = L"\"" + exe_path() + L"\" mid " + (kill ? L"kill" : L"nokill") + L" \"" +
                       pidfile + L"\" \"" + hbfile + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                        &pi)) {
        printf("spawn mid failed err=%lu\n", GetLastError());
        return;
    }
    // 等 mid 写出孙 pid(轮询 pidfile),趁 mid 还活着先把孙句柄攥在手里
    DWORD grandpid = 0;
    HANDLE gc = nullptr;
    for (int i = 0; i < 300 && !gc; i++) {
        HANDLE h = CreateFileW(pidfile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char b[32]{};
            DWORD n = 0;
            if (ReadFile(h, b, sizeof(b) - 1, &n, nullptr) && n)
                grandpid = (DWORD)atoll(b);
            CloseHandle(h);
        }
        if (grandpid) {
            gc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                             FALSE, grandpid);
            if (!gc)
                grandpid = 0;
        }
        if (!gc)
            Sleep(20);
    }
    if (!gc) {
        printf("failed to grab grandchild handle (err=%lu)\n", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return;
    }
    printf("mid pid=%lu grandchild pid=%lu (handle secured while mid alive)\n", pi.dwProcessId,
           grandpid);
    WaitForSingleObject(pi.hProcess, 10000); // mid 退出 = 它的 Job 句柄自动关
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ULONGLONG t0 = GetTickCount64();
    DWORD w = WaitForSingleObject(gc, kill ? 3000 : 1500);
    ULONGLONG dt = GetTickCount64() - t0;
    int hb = count_lines(hbfile);
    if (kill)
        printf("after mid exit: grandchild wait=0x%lX in %llums  -> KILLED WITH ITS PARENT, hb=%d "
               "lines\n",
               (unsigned long)w, (unsigned long long)dt, hb);
    else if (w == WAIT_TIMEOUT)
        printf("after mid exit: grandchild STILL ALIVE after 1.5s (wait TIMEOUT), hb=%d lines -> "
               "orphan lives on\n",
               hb);
    TerminateProcess(gc, 99); // 收尾别留孤儿
    WaitForSingleObject(gc, 3000);
    CloseHandle(gc);
}

static void section2() {
    printf("\n== [2] 父进程退出时,Job 句柄自动关闭 -> 孩子陪葬? ==\n");
    printf("(a) mid 用了 KILL_ON_JOB_CLOSE:\n");
    run_mid_case(true);
    printf("(b) mid 没用这面旗(对照):\n");
    run_mid_case(false);
}

static void section3() {
    printf("\n== [3] 反例:孩子自己继承了 Job 句柄 -> 陪葬失灵 ==\n");
    std::wstring hbfile = temp_file(L"hb3.txt");
    DeleteFileW(hbfile.c_str());
    HANDLE job = make_job(true); // 旗子还挂着
    HANDLE hb = CreateFileW(hbfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &g_inherit_sa, CREATE_ALWAYS, 0, nullptr);
    PROCESS_INFORMATION pi{};
    spawn_into_job(job, exe_path() + L" loop 15000", hb, true, &pi); // 这次连 Job 句柄一起继承
    Sleep(500);
    printf("child pid=%lu inherited ITS OWN copy of the job handle; driver closes job handle\n",
           pi.dwProcessId);
    CloseHandle(job); // 驱动这份关了,孩子手里还有一份 -> 旗子不触发
    DWORD w = WaitForSingleObject(pi.hProcess, 1200);
    if (w == WAIT_TIMEOUT)
        printf("child STILL ALIVE 1.2s after our close (its own handle keeps the job open), hb=%d "
               "lines\n",
               count_lines(hbfile));
    else
        printf("child died?! unexpected (wait=0x%lX)\n", (unsigned long)w);
    TerminateProcess(pi.hProcess, 98);
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hb);
    printf("=> 教训:开 KILL_ON_JOB_CLOSE 时,千万别让 Job 句柄被孩子继承\n");
}

static void section4() {
    printf("\n== [4] QueryInformationJobObject:Job 的账本 ==\n");
    HANDLE job = make_job(false);
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acc{};
    QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acc, sizeof(acc), nullptr);
    printf("empty job: ActiveProcesses=%lu TotalProcesses=%lu TotalPageFaults=%llu "
           "TotalTerminated=%lu\n",
           acc.ActiveProcesses, acc.TotalProcesses, (unsigned long long)acc.TotalPageFaultCount,
           acc.TotalTerminatedProcesses);

    PROCESS_INFORMATION p1{}, p2{};
    HANDLE trash =
        CreateFileW(temp_file(L"trash4.txt").c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &g_inherit_sa, CREATE_ALWAYS, 0, nullptr);
    spawn_into_job(job, exe_path() + L" loop 2500", trash, false, &p1);
    spawn_into_job(job, exe_path() + L" quick 1200 3", trash, false, &p2);
    Sleep(250);
    // 进程 id 清单(变长结构,先给足缓冲)
    unsigned char buf[1024];
    auto* list = (JOBOBJECT_BASIC_PROCESS_ID_LIST*)buf;
    if (QueryInformationJobObject(job, JobObjectBasicProcessIdList, buf, sizeof(buf), nullptr)) {
        printf("both alive: NumberOfProcesses=%lu ids=[", list->NumberOfProcessIdsInList);
        for (DWORD i = 0; i < list->NumberOfProcessIdsInList; i++)
            printf("%lu%s", (DWORD)list->ProcessIdList[i],
                   i + 1 < list->NumberOfProcessIdsInList ? ", " : "");
        printf("]  (driver pid=%lu is NOT in the list)\n", GetCurrentProcessId());
    }
    WaitForSingleObject(p2.hProcess, 5000);
    Sleep(2600); // 等 loop 那个自己也到站
    WaitForSingleObject(p1.hProcess, 5000);
    QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acc, sizeof(acc), nullptr);
    printf("after both exited: ActiveProcesses=%lu TotalProcesses=%lu TotalPageFaults=%llu "
           "TotalTerminated=%lu\n",
           acc.ActiveProcesses, acc.TotalProcesses, (unsigned long long)acc.TotalPageFaultCount,
           acc.TotalTerminatedProcesses);
    QueryInformationJobObject(job, JobObjectBasicProcessIdList, buf, sizeof(buf), nullptr);
    printf("id list now: NumberOfProcesses=%lu\n", list->NumberOfProcessIdsInList);
    CloseHandle(p1.hProcess);
    CloseHandle(p1.hThread);
    CloseHandle(p2.hProcess);
    CloseHandle(p2.hThread);
    CloseHandle(trash);
    CloseHandle(job);
}

static void section5() {
    printf("\n== [5] JOB_OBJECT_LIMIT_PROCESS_MEMORY:40MB 限额 ==\n");
    HANDLE job = make_job(false);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
    eli.ProcessMemoryLimit = 40ULL << 20;
    eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &eli, sizeof(eli));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION back{};
    QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &back, sizeof(back), nullptr);
    printf("limit set & read back: ProcessMemoryLimit=%lluMB\n",
           (unsigned long long)(back.ProcessMemoryLimit >> 20));

    printf("(a) capped run:\n");
    PROCESS_INFORMATION pi{};
    spawn_into_job(job, exe_path() + L" alloc 256 4", nullptr, false,
                   &pi); // NULL = 继承驱动 std,输出直通 .out
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    printf("(a) exit code=%lu (expect 42)\n", code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    printf("(b) control run, no job:\n");
    std::wstring cmd2 = exe_path() + L" alloc 256 4";
    std::vector<wchar_t> buf(cmd2.begin(), cmd2.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi2{};
    CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi2);
    WaitForSingleObject(pi2.hProcess, 30000);
    GetExitCodeProcess(pi2.hProcess, &code);
    printf("(b) exit code=%lu\n", code);
    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);
    CloseHandle(job);
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2) {
        if (wcscmp(argv[1], L"loop") == 0 && argc == 3)
            return mode_loop(_wtoi(argv[2]));
        if (wcscmp(argv[1], L"quick") == 0 && argc == 4)
            return mode_quick(_wtoi(argv[2]), _wtoi(argv[3]));
        if (wcscmp(argv[1], L"alloc") == 0 && argc == 4)
            return mode_alloc(_wtoi(argv[2]), _wtoi(argv[3]));
        if (wcscmp(argv[1], L"mid") == 0 && argc == 5)
            return mode_mid(wcscmp(argv[2], L"kill") == 0, argv[3], argv[4]);
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("E4 job objects | driver pid=%lu\n", GetCurrentProcessId());
    section1();
    section2();
    section3();
    section4();
    section5();
    printf("\nE4 done.\n");
    return 0;
}
