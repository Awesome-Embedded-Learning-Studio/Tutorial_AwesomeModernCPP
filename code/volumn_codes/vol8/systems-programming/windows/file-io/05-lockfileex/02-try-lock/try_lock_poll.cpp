// try_lock_poll.cpp —— E2a 有限等待(轮询版):LockFileEx 没有超时参数
//
// 阻塞版会一等到底,FAIL_IMMEDIATELY 只会立刻回绝,两头都没有「等多久」的参数
// —— 这一点与 Linux 侧 flock/F_SETLKW 完全同形,出路人人都一样:
// FAIL_IMMEDIATELY 小步轮询,等待分辨率 = 轮询间隔(本实验取 10 ms)。
//
// 角色:demo 主驱动兼 waiter;holder <file> <log> <epoch> <holdMs> 持锁者(另一进程)。
// 环境:Win11 26200 / NTFS / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
// 数据文件烧死为 C:/msys64/tmp/l05win/trylock.bin。
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
constexpr DWORD F = LOCKFILE_FAIL_IMMEDIATELY;
constexpr unsigned long long kRange = 4096;

ULONGLONG g_epoch = 0;

const char* werr(DWORD e) {
    switch (e) {
        case ERROR_LOCK_VIOLATION:
            return "ERROR_LOCK_VIOLATION";
        default:
            return "(其他错误)";
    }
}

struct LockR {
    BOOL ok;
    DWORD err;
};

std::string fmt(const LockR& r) {
    char b[96];
    if (r.ok) {
        std::snprintf(b, sizeof b, "TRUE");
    } else {
        std::snprintf(b, sizeof b, "FALSE, GetLastError()=%lu(%s)", (unsigned long)r.err,
                      werr(r.err));
    }
    return b;
}

LockR lockx(HANDLE h, unsigned long long off, unsigned long long len, DWORD flags) {
    OVERLAPPED ov{};
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    SetLastError(0);
    BOOL ok = LockFileEx(h, flags, 0, (DWORD)len, (DWORD)(len >> 32), &ov);
    return {ok, ok ? 0 : GetLastError()};
}

LockR unlockx(HANDLE h, unsigned long long off, unsigned long long len) {
    SetLastError(0);
    BOOL ok = UnlockFile(h, (DWORD)off, (DWORD)(off >> 32), (DWORD)len, (DWORD)(len >> 32));
    return {ok, ok ? 0 : GetLastError()};
}

HANDLE openfile(const char* path) {
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

std::string exe_path() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;
}

void spawn_holder(const char* file, const char* logp, int holdMs) {
    std::string cmd = "\"" + exe_path() + "\" holder " + file + " " + logp + " " +
                      std::to_string(g_epoch) + " " + std::to_string(holdMs);
    char m[400];
    std::snprintf(m, sizeof m, "%s", cmd.c_str());
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, m, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::fprintf(stderr, "CreateProcessA 失败:%lu\n", (unsigned long)GetLastError());
        std::exit(1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess); // 不等它:holder 自己会退场
}

// 轮询版 try_lock_for:到点返回 false,拿到返回 true;顺便报尝试次数与实际等待
bool poll_try_lock_for(HANDLE h, unsigned long long off, unsigned long long len, DWORD timeoutMs,
                       DWORD pollMs, int* attempts, unsigned long long* waitedMs) {
    const ULONGLONG t0 = GetTickCount64();
    const ULONGLONG deadline = t0 + timeoutMs;
    int n = 0;
    for (;;) {
        ++n;
        if (lockx(h, off, len, EX | F).ok) {
            *attempts = n;
            *waitedMs = GetTickCount64() - t0;
            return true;
        }
        if (GetTickCount64() >= deadline) {
            *attempts = n;
            *waitedMs = GetTickCount64() - t0;
            return false;
        }
        Sleep(pollMs);
    }
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
    HANDLE h = openfile(file);
    LockR r = lockx(h, 0, kRange, EX);
    flog(f, tag, "LockFileEx([0,%llu) EX) = %s,持锁 %d ms", kRange, fmt(r).c_str(), holdMs);
    Sleep(holdMs);
    flog(f, "holder", "UnlockFile = %s,离场", fmt(unlockx(h, 0, kRange)).c_str());
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
        std::printf("用法:try_lock_poll.exe demo\n");
        return 1;
    }
    const char* file = "C:/msys64/tmp/l05win/trylock.bin";
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
    constexpr DWORD kPoll = 10;
    std::printf("pid=%lu(当前进程), 文件:%s(NTFS)\n", (unsigned long)GetCurrentProcessId(), file);
    std::printf("口径:LockFileEx 无超时参数 → EXCLUSIVE|FAIL_IMMEDIATELY + Sleep(%u ms) 轮询\n",
                (unsigned)kPoll);

    std::string lh = std::string(kDir) + "log_h1.txt";
    std::string lw = std::string(kDir) + "log_w1.txt";
    DeleteFileA(lh.c_str());
    DeleteFileA(lw.c_str());
    std::printf("\n==== 场景 1:持锁者握 600 ms,waiter 限期 3000 ms ====\n");
    spawn_holder(file, lh.c_str(), 600);
    Sleep(100); // 让 holder 先把锁拿到手
    HANDLE h = openfile(file);
    int n = 0;
    unsigned long long w = 0;
    const bool ok = poll_try_lock_for(h, 0, kRange, 3000, kPoll, &n, &w);
    {
        FILE* fw = std::fopen(lw.c_str(), "a");
        flog(fw, "waiter",
             "try_lock_for(3000ms) = %s:第 %d 次尝试%s(实际等了 %llu ms,其余 %d 次都是 FALSE+33)",
             ok ? "true" : "false", n, ok ? "拿到" : "未拿到", w, n - 1);
        std::fclose(fw);
    }
    unlockx(h, 0, kRange);
    CloseHandle(h);
    Sleep(400); // 等 holder 退场,日志落齐
    print_merged(lh.c_str(), lw.c_str());

    std::string lh2 = std::string(kDir) + "log_h2.txt";
    std::string lw2 = std::string(kDir) + "log_w2.txt";
    DeleteFileA(lh2.c_str());
    DeleteFileA(lw2.c_str());
    std::printf("\n==== 场景 2:持锁者握 1200 ms,waiter 限期 200 ms → 如期超时 ====\n");
    spawn_holder(file, lh2.c_str(), 1200);
    Sleep(100);
    h = openfile(file);
    n = 0;
    w = 0;
    const bool ok2 = poll_try_lock_for(h, 0, kRange, 200, kPoll, &n, &w);
    {
        FILE* fw = std::fopen(lw2.c_str(), "a");
        flog(fw, "waiter",
             "try_lock_for(200ms) = %s:%d 次尝试全部 "
             "ERROR_LOCK_VIOLATION(%lu),限期一到就返回,没有死等",
             ok2 ? "true" : "false", n, (unsigned long)ERROR_LOCK_VIOLATION);
        std::fclose(fw);
    }
    CloseHandle(h);
    Sleep(1300); // 等 holder 退场,时间线落齐
    print_merged(lh2.c_str(), lw2.c_str());
    return 0;
}
