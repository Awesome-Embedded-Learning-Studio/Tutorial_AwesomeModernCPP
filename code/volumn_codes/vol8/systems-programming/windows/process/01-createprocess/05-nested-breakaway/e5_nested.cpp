// E5: Job 的嵌套与出走
//  [1] 世袭:进了 Job 的进程,它生的孩子自动进同一个 Job(Chrome 当年的困局)
//  [2] breakaway:Job 允许(JOB_OBJECT_LIMIT_BREAKAWAY_OK)+ 创建方带 CREATE_BREAKAWAY_FROM_JOB ->
//  出走成功 [3] 强闯:Job 不允许,创建方硬带旗 -> CreateProcessW 直接失败(错误码实测) [4]
//  静默出走:SILENT_BREAKAWAY_OK,不带旗也能走 [5] 嵌套(Win8+):子 Job 塞进父 Job;子 Job 限额比父宽 ->
//  无效,父的帽子扣到底
// 用法: 无参=驱动;"breakout <jobname>"|"trybreak <jobname>"|"alloc <mb> <step>"|"quick <ms> <code>"
// 为子模式。
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
static HANDLE make_job(DWORD limit_flags, SIZE_T process_mem_limit, const wchar_t* name) {
    HANDLE job = CreateJobObjectW(nullptr, name);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
    eli.BasicLimitInformation.LimitFlags = limit_flags;
    if (process_mem_limit) {
        eli.ProcessMemoryLimit = process_mem_limit;
        eli.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    }
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &eli, sizeof(eli));
    return job;
}
// 挂起出生 -> 进 Job -> 放行

// ---------- 子模式 ----------
static int mode_breakout(const wchar_t* jobname) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    HANDLE job = OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, jobname);
    if (!job) {
        printf("    [breakout] OpenJobObjectW failed err=%lu\n", GetLastError());
        return 1;
    }
    BOOL self_in = FALSE;
    IsProcessInJob(GetCurrentProcess(), job, &self_in);
    printf("    [breakout] self in job? %d\n", self_in);
    std::wstring cmd = L"\"" + exe_path() + L"\" quick 300 9";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION g{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                        &g)) {
        printf("    [breakout] grandchild spawn failed err=%lu\n", GetLastError());
        CloseHandle(job);
        return 1;
    }
    BOOL grand_in = FALSE;
    if (g.hProcess) {
        IsProcessInJob(g.hProcess, job, &grand_in);
        printf("    [breakout] grandchild spawned WITHOUT any flag -> in job? %d\n", grand_in);
        WaitForSingleObject(g.hProcess, 5000);
        CloseHandle(g.hProcess);
        CloseHandle(g.hThread);
    }
    CloseHandle(job);
    return grand_in ? 10 : 11; // 10=世袭进 Job,11=逃出
}
static int mode_trybreak(const wchar_t* jobname) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    HANDLE job = OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, jobname);
    if (!job) {
        printf("    [trybreak] OpenJobObjectW failed err=%lu\n", GetLastError());
        return 1;
    }
    std::wstring cmd = L"\"" + exe_path() + L"\" quick 300 9";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION g{};
    SetLastError(1234);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                             CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &g);
    if (!ok) {
        printf("    [trybreak] CreateProcessW(CREATE_BREAKAWAY_FROM_JOB) FAILED err=%lu\n",
               GetLastError());
        CloseHandle(job);
        return 20;
    }
    BOOL grand_in = FALSE;
    IsProcessInJob(g.hProcess, job, &grand_in);
    printf("    [trybreak] breakaway spawn OK -> grandchild in job? %d (0 = walked out)\n",
           grand_in);
    WaitForSingleObject(g.hProcess, 5000);
    CloseHandle(g.hProcess);
    CloseHandle(g.hThread);
    CloseHandle(job);
    return grand_in ? 21 : 22;
}
static int mode_alloc(int total_mb, int step_mb) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("    [alloc child] pid=%lu target=%dMB step=%dMB\n", GetCurrentProcessId(), total_mb,
           step_mb);
    int committed = 0;
    for (int mb = step_mb; mb <= total_mb; mb += step_mb) {
        void* p =
            VirtualAlloc(nullptr, (SIZE_T)step_mb << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            printf("    [alloc child] VirtualAlloc FAILED at total=%dMB err=%lu\n", committed,
                   GetLastError());
            break;
        }
        *(volatile int*)p = 1;
        committed = mb;
    }
    printf("    [alloc child] ended at %dMB\n", committed);
    return 42;
}
static int mode_quick(int ms, int code) {
    Sleep(ms);
    return code;
}

// ---------- 驱动 ----------
static std::wstring job_name(const wchar_t* tag) {
    return std::wstring(L"Local\\vol8e5-") + tag + L"-" + std::to_wstring(GetCurrentProcessId());
}
static DWORD run_child(const wchar_t* mode, const std::wstring& name, HANDLE job) {
    std::wstring cmd = L"\"" + exe_path() + L"\" " + mode + L" " + name;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        nullptr, &si, &pi))
        return (DWORD)-1;
    if (job)
        AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2) {
        if (wcscmp(argv[1], L"breakout") == 0 && argc == 3)
            return mode_breakout(argv[2]);
        if (wcscmp(argv[1], L"trybreak") == 0 && argc == 3)
            return mode_trybreak(argv[2]);
        if (wcscmp(argv[1], L"alloc") == 0 && argc == 4)
            return mode_alloc(_wtoi(argv[2]), _wtoi(argv[3]));
        if (wcscmp(argv[1], L"quick") == 0 && argc == 4)
            return mode_quick(_wtoi(argv[2]), _wtoi(argv[3]));
        return 1;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    BOOL self_in_any = FALSE;
    IsProcessInJob(GetCurrentProcess(), nullptr, &self_in_any);
    printf("E5 nested jobs & breakaway | driver pid=%lu | driver itself inside some job? %d\n",
           GetCurrentProcessId(), self_in_any);

    printf("\n== [1] 世袭:Job 里的进程生的孩子,自动进同一个 Job ==\n");
    std::wstring n1 = job_name(L"plain");
    HANDLE j1 = make_job(0, 0, n1.c_str());
    printf("child exit code=%lu (10=grandchild IN job, 11=escaped)\n",
           run_child(L"breakout", n1, j1));
    CloseHandle(j1);

    printf("\n== [2] breakaway 允许位:job 带 BREAKAWAY_OK,创建带 CREATE_BREAKAWAY_FROM_JOB ==\n");
    std::wstring n2 = job_name(L"allow");
    HANDLE j2 = make_job(JOB_OBJECT_LIMIT_BREAKAWAY_OK, 0, n2.c_str());
    printf("child exit code=%lu (21=failed escape, 22=WALKED OUT)\n",
           run_child(L"trybreak", n2, j2));
    CloseHandle(j2);

    printf("\n== [3] 强闯:job 没开允许位,创建硬带 CREATE_BREAKAWAY_FROM_JOB ==\n");
    std::wstring n3 = job_name(L"deny");
    HANDLE j3 = make_job(0, 0, n3.c_str());
    printf("child exit code=%lu (20=CreateProcessW REFUSED)\n", run_child(L"trybreak", n3, j3));
    CloseHandle(j3);

    printf("\n== [4] 静默出走:SILENT_BREAKAWAY_OK,不带旗也走 ==\n");
    std::wstring n4 = job_name(L"silent");
    HANDLE j4 = make_job(JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK, 0, n4.c_str());
    printf("child exit code=%lu (10=stayed, 11=SILENTLY escaped)\n",
           run_child(L"breakout", n4, j4));
    CloseHandle(j4);

    printf("\n== [5] 嵌套:子 Job 限额 256MB,父 Job 48MB,谁说了算? ==\n");
    HANDLE parent_job = make_job(0, 48ULL << 20, nullptr);
    HANDLE child_job = make_job(0, 256ULL << 20, nullptr);
    // 先试一个想当然的姿势:把子 Job 句柄当 hProcess 传 -> 实测被拒
    SetLastError(1234);
    BOOL nested = AssignProcessToJobObject(parent_job, child_job);
    printf("(a) naive: AssignProcessToJobObject(parent, child_job_handle) -> %d (err=%lu)\n",
           nested, GetLastError());
    printf("    ^ hProcess 只认进程句柄;嵌套不是这么建的\n");
    // 正路:同一个进程,先 Assign 根 Job,再 Assign 子 Job(顺序即层级,MSDN nested-jobs 页)
    std::wstring cmd = L"\"" + exe_path() + L"\" alloc 384 8";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                   &si, &pi);
    BOOL a1 = AssignProcessToJobObject(parent_job, pi.hProcess); // 1) 根
    BOOL a2 = AssignProcessToJobObject(child_job, pi.hProcess);  // 2) 子(子集入子 Job)
    printf(
        "(b) assign process to parent first (ret=%d), then to child (ret=%d) -> hierarchy formed\n",
        a1, a2);
    ResumeThread(pi.hThread);
    BOOL in_parent = FALSE, in_child = FALSE;
    IsProcessInJob(pi.hProcess, parent_job, &in_parent);
    IsProcessInJob(pi.hProcess, child_job, &in_child);
    printf("process is member of BOTH jobs? parent=%d child=%d\n", in_parent, in_child);
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    printf("alloc child exit=%lu (42); verdict: fails ~40MB -> PARENT cap binds; fails ~248MB -> "
           "only child cap\n",
           code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(child_job);
    CloseHandle(parent_job);
    printf("\nE5 done.\n");
    return 0;
}
