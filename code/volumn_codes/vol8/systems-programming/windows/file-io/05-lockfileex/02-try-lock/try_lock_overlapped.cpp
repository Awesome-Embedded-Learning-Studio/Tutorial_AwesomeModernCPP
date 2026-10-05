// try_lock_overlapped.cpp —— E2b(加餐):FILE_FLAG_OVERLAPPED 把「无限等」改造成「可限时等」
//
// 阻塞版 LockFileEx 在同步句柄上会卡到天荒地老;换 FILE_FLAG_OVERLAPPED 打开的
// 异步句柄,同一句调用立刻返回:拿得到 → TRUE;拿不到 → FALSE + ERROR_IO_PENDING(997),
// 批准与否落在 OVERLAPPED.hEvent 上。于是 WaitForSingleObject(事件, 超时) 天然就是
// try_lock_for,超时侧用 CancelIoEx 撤单 —— 不用轮询,分辨率就是内核的调度粒度。
//
// 角色:demo 主驱动兼 waiter;holder <file> <log> <epoch> <holdMs> 持锁者(另一进程)。
// 环境:Win11 26200 / NTFS / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
// 数据文件烧死为 C:/msys64/tmp/l05win/tryov.bin。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kDir = "C:/msys64/tmp/l05win/";
constexpr DWORD EX = LOCKFILE_EXCLUSIVE_LOCK;
constexpr unsigned long long kRange = 4096;

ULONGLONG g_epoch = 0;

const char* werr(DWORD e) {
    switch (e) {
        case ERROR_LOCK_VIOLATION:
            return "ERROR_LOCK_VIOLATION";
        case ERROR_IO_PENDING:
            return "ERROR_IO_PENDING";
        case ERROR_OPERATION_ABORTED:
            return "ERROR_OPERATION_ABORTED";
        default:
            return "(其他错误)";
    }
}

std::string fmt(BOOL ok, DWORD err) {
    char b[96];
    if (ok) {
        std::snprintf(b, sizeof b, "TRUE");
    } else {
        std::snprintf(b, sizeof b, "FALSE, GetLastError()=%lu(%s)", (unsigned long)err, werr(err));
    }
    return b;
}

HANDLE open_async(const char* path) {
    HANDLE h =
        CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "CreateFileA(%s) 失败:%lu\n", path, (unsigned long)GetLastError());
        std::exit(1);
    }
    return h;
}

HANDLE open_plain(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "CreateFileA(%s) 失败:%lu\n", path, (unsigned long)GetLastError());
        std::exit(1);
    }
    return h;
}

void flog(FILE* f, const char* who, const char* fmts, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmts);
    std::vsnprintf(b, sizeof b, fmts, ap);
    va_end(ap);
    std::fprintf(f, "  [%7llu ms] %s:%s\n", (unsigned long long)(GetTickCount64() - g_epoch), who,
                 b);
    std::fflush(f);
}

const char* exe_path_buf() {
    static char buf[MAX_PATH];
    if (!buf[0]) {
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
    }
    return buf;
}

void spawn_holder(const char* file, const char* logp, int holdMs) {
    char cmd[400];
    std::snprintf(cmd, sizeof cmd, "\"%s\" holder %s %s %llu %d", exe_path_buf(), file, logp,
                  (unsigned long long)g_epoch, holdMs);
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::fprintf(stderr, "CreateProcessA 失败:%lu\n", (unsigned long)GetLastError());
        std::exit(1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

void print_merged(const char* a, const char* b) {
    struct Row {
        unsigned long long t;
        unsigned seq;
        std::string s;
    };
    std::vector<Row> rows;
    unsigned seq = 0;
    for (const char* p : {a, b}) {
        FILE* f = std::fopen(p, "r");
        if (!f) {
            continue;
        }
        char line[600];
        while (std::fgets(line, sizeof line, f)) {
            line[std::strcspn(line, "\r\n")] = '\0';
            rows.push_back({strtoull(line + 3, nullptr, 10), seq++, line});
        }
        std::fclose(f);
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& x, const Row& y) { return x.t < y.t; });
    for (const Row& r : rows) {
        std::printf("%s\n", r.s.c_str());
    }
}

int child_holder(const char* file, const char* logp, ULONGLONG epoch, int holdMs) {
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "holder(pid=%lu)", (unsigned long)GetCurrentProcessId());
    FILE* f = std::fopen(logp, "a");
    HANDLE h = open_plain(file);
    OVERLAPPED ov{};
    ov.Offset = 0;
    SetLastError(0);
    BOOL ok = LockFileEx(h, EX, 0, (DWORD)kRange, 0, &ov);
    DWORD err = ok ? 0 : GetLastError();
    flog(f, tag, "LockFileEx([0,%llu) EX) = %s,持锁 %d ms", kRange, fmt(ok, err).c_str(), holdMs);
    Sleep(holdMs);
    SetLastError(0);
    ok = UnlockFile(h, 0, 0, (DWORD)kRange, 0);
    err = ok ? 0 : GetLastError();
    flog(f, "holder", "UnlockFile = %s,离场", fmt(ok, err).c_str());
    std::fclose(f);
    CloseHandle(h);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 6 && std::strcmp(argv[1], "holder") == 0) {
        return child_holder(argv[2], argv[3], strtoull(argv[4], nullptr, 10), std::atoi(argv[5]));
    }
    if (argc < 2 || std::strcmp(argv[1], "demo") != 0) {
        std::printf("用法:try_lock_overlapped.exe demo\n");
        return 1;
    }
    const char* file = "C:/msys64/tmp/l05win/tryov.bin";
    g_epoch = GetTickCount64();
    {
        HANDLE h = CreateFileA(file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            std::printf("!! 建数据文件失败:%lu(先 mkdir C:/msys64/tmp/l05win)\n",
                        (unsigned long)GetLastError());
            return 1;
        }
        CloseHandle(h);
    }
    std::printf("pid=%lu(当前进程), 文件:%s(NTFS)\n", (unsigned long)GetCurrentProcessId(), file);
    std::printf("口径:waiter 用 FILE_FLAG_OVERLAPPED 句柄 + OVERLAPPED.hEvent(手动重置),不轮询\n");

    // ---- 场景 1:限期 3000 ms,holder 握 600 ms → 事件在 ~600 ms 置位 ----
    std::string lh = std::string(kDir) + "log_o1.txt";
    DeleteFileA(lh.c_str());
    std::printf("\n==== 场景 1:持锁者握 600 ms,waiter WaitForSingleObject(事件, 3000 ms) ====\n");
    spawn_holder(file, lh.c_str(), 600);
    Sleep(100); // 让 holder 先把锁拿到手
    HANDLE h = open_async(file);
    OVERLAPPED ov{};
    ov.Offset = 0;
    HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    ov.hEvent = ev;
    std::string lw = std::string(kDir) + "log_ow1.txt";
    DeleteFileA(lw.c_str());
    FILE* fw = std::fopen(lw.c_str(), "a");
    SetLastError(0);
    BOOL ok = LockFileEx(h, EX, 0, (DWORD)kRange, 0, &ov);
    DWORD err = ok ? 0 : GetLastError();
    flog(fw, "waiter", "LockFileEx(异步句柄,不带 FAIL_IMMEDIATELY)= %s —— 冲突不再阻塞,改挂账排队",
         fmt(ok, err).c_str());
    const ULONGLONG t0 = GetTickCount64();
    DWORD w = WaitForSingleObject(ev, 3000);
    const unsigned long long waited = GetTickCount64() - t0;
    DWORD nx = 0;
    BOOL g = FALSE;
    if (w == WAIT_OBJECT_0) {
        g = GetOverlappedResult(h, &ov, &nx, TRUE);
        flog(fw, "waiter", "事件等了 %llu ms 置位,GetOverlappedResult = %s —— 锁到手,一次也没轮询",
             waited, fmt(g, GetLastError()).c_str());
    } else {
        flog(fw, "waiter", "WaitForSingleObject 返回 0x%lx(等了 %llu ms)—— 意外", (unsigned long)w,
             waited);
    }
    { // 收尾:走 UnlockFileEx(异步句柄配 overlapped 版解锁)
        OVERLAPPED uo{};
        uo.Offset = 0;
        SetLastError(0);
        BOOL u = UnlockFileEx(h, 0, (DWORD)kRange, 0, &uo);
        flog(fw, "waiter", "UnlockFileEx = %s(显式放锁后关句柄)", fmt(u, GetLastError()).c_str());
    }
    std::fclose(fw);
    CloseHandle(ev);
    CloseHandle(h);
    Sleep(300);
    print_merged(lh.c_str(), lw.c_str());

    // ---- 场景 2:限期 200 ms,holder 握 1200 ms → 超时 + CancelIoEx 撤单 ----
    std::string lh2 = std::string(kDir) + "log_o2.txt";
    DeleteFileA(lh2.c_str());
    std::printf("\n==== 场景 2:持锁者握 1200 ms,waiter 限期 200 ms → 超时撤单 ====\n");
    spawn_holder(file, lh2.c_str(), 1200);
    Sleep(100);
    h = open_async(file);
    std::string lw2 = std::string(kDir) + "log_ow2.txt";
    DeleteFileA(lw2.c_str());
    FILE* fw2 = std::fopen(lw2.c_str(), "a");
    OVERLAPPED ov2{};
    ov2.Offset = 0;
    ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    ov2.hEvent = ev;
    SetLastError(0);
    ok = LockFileEx(h, EX, 0, (DWORD)kRange, 0, &ov2);
    err = ok ? 0 : GetLastError();
    flog(fw2, "waiter", "LockFileEx = %s,请求在队列里挂着", fmt(ok, err).c_str());
    const ULONGLONG t1 = GetTickCount64();
    w = WaitForSingleObject(ev, 200);
    const unsigned long long waited2 = GetTickCount64() - t1;
    if (w == WAIT_TIMEOUT) {
        flog(fw2, "waiter", "WaitForSingleObject = WAIT_TIMEOUT(0x102),等了 %llu ms,限期到",
             waited2);
        BOOL c = CancelIoEx(h, &ov2);
        flog(fw2, "waiter", "CancelIoEx(定向撤这一笔) = %s", fmt(c, GetLastError()).c_str());
        g = GetOverlappedResult(h, &ov2, &nx, TRUE); // 收尸
        const DWORD gerr = GetLastError();
        flog(fw2, "waiter", "收尾 GetOverlappedResult = %s —— 请求作废,%s", fmt(g, gerr).c_str(),
             g ? "撞上「取消与批准赛跑」的窗口:锁实际批下来了,下面补放" : "锁没到手");
        if (g) {
            OVERLAPPED uo{};
            uo.Offset = 0;
            UnlockFileEx(h, 0, (DWORD)kRange, 0, &uo);
        }
    } else {
        flog(fw2, "waiter", "竟在限期内置位(等了 %llu ms,holder 提前放了?)", waited2);
    }
    CloseHandle(ev);
    CloseHandle(h);
    Sleep(1300); // 等 holder 退场
    {            // 幽灵锁检查:被撤销的请求不许留下锁
        HANDLE p = open_plain(file);
        OVERLAPPED po{};
        po.Offset = 0;
        SetLastError(0);
        BOOL pok = LockFileEx(p, EX | LOCKFILE_FAIL_IMMEDIATELY, 0, (DWORD)kRange, 0, &po);
        flog(fw2, "探针", "holder 退场后,新句柄试同区间 = %s —— 被撤销的请求没有留下锁",
             fmt(pok, GetLastError()).c_str());
        if (pok) {
            UnlockFile(p, 0, 0, (DWORD)kRange, 0);
        }
        CloseHandle(p);
    }
    std::fclose(fw2);
    print_merged(lh2.c_str(), lw2.c_str());
    return 0;
}
