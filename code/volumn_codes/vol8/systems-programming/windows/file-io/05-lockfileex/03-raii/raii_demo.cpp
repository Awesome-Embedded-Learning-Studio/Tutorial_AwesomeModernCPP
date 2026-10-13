// raii_demo.cpp —— E3:unique_file_lock 双进程时序 + move 语义(对照 Linux 侧 E5 的 ticket=42)
//
// 角色:demo 主驱动兼 B;writerA <file> <log> <epoch> 持锁写数据的 A(另一进程)。
// A 用命名事件 Local\\l05win_ticket 通知「锁已到手、数据已写」,B 才开始三连试
// (Windows 起一个新进程比 fork 贵一个量级,不设起跑线的话 B 的第一次 try_lock
// 会抢在 A 拿锁之前,B 的日志就不可信了 —— 这本身就是两平台的差异之一)。
// 环境:Win11 26200 / NTFS / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
// 数据文件烧死为 C:/msys64/tmp/l05win/raii.bin。
#include "unique_file_lock.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kDir = "C:/msys64/tmp/l05win/";
const char* kGoEvent = "Local\\l05win_ticket";
constexpr unsigned long long kRange = 4096;

ULONGLONG g_epoch = 0;

void tlog(const char* who, const char* fmts, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmts);
    std::vsnprintf(b, sizeof b, fmts, ap);
    va_end(ap);
    std::printf("  [%7llu ms] %s:%s\n", (unsigned long long)(GetTickCount64() - g_epoch), who, b);
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

BOOL probe(const char* file) {
    HANDLE h = CreateFileA(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    OVERLAPPED ov{};
    ov.Offset = 0;
    SetLastError(0);
    BOOL ok = LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, (DWORD)kRange,
                         0, &ov);
    if (ok) {
        UnlockFile(h, 0, 0, (DWORD)kRange, 0);
    }
    CloseHandle(h);
    return ok;
}

int child_writerA(const char* file, const char* logp, ULONGLONG epoch) {
    g_epoch = epoch;
    FILE* f = std::fopen(logp, "a");
    HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, kGoEvent);
    {
        unique_file_lock lk(file, true, 0, kRange);
        const char ticket[] = "ticket=42\n";
        DWORD w = 0;
        SetFilePointer(lk.native_handle(), 0, nullptr, FILE_BEGIN);
        WriteFile(lk.native_handle(), ticket, (DWORD)sizeof ticket - 1, &w, nullptr);
        char tag[32];
        std::snprintf(tag, sizeof tag, "A(pid=%lu)", (unsigned long)GetCurrentProcessId());
        flog(f, tag, "构造 unique_file_lock,已写 \"ticket=42\",持锁 700 ms");
        if (ev) {
            SetEvent(ev);
        }
        Sleep(700);
        flog(f, "A", "作用域将尽,unique_file_lock 析构在即");
    }
    flog(f, "A", "已放锁(显式 UnlockFile)+ CloseHandle,退场");
    std::fclose(f);
    if (ev) {
        CloseHandle(ev);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 5 && std::strcmp(argv[1], "writerA") == 0) {
        return child_writerA(argv[2], argv[3], strtoull(argv[4], nullptr, 10));
    }
    if (argc < 2 || std::strcmp(argv[1], "demo") != 0) {
        std::printf("用法:raii_demo.exe demo\n");
        return 1;
    }
    const char* file = "C:/msys64/tmp/l05win/raii.bin";
    const char* file2 = "C:/msys64/tmp/l05win/raii2.bin";
    g_epoch = GetTickCount64();
    for (const char* p : {file, file2}) {
        HANDLE h = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            std::printf("!! 建数据文件失败:%lu(先 mkdir C:/msys64/tmp/l05win)\n",
                        (unsigned long)GetLastError());
            return 1;
        }
        CloseHandle(h);
    }
    std::printf("pid=%lu(当前进程), 文件:%s(NTFS)\n", (unsigned long)GetCurrentProcessId(), file);

    // ---- 双进程时序 ----
    std::string la = std::string(kDir) + "log_raii_a.txt";
    std::string lb = std::string(kDir) + "log_raii_b.txt";
    DeleteFileA(la.c_str());
    DeleteFileA(lb.c_str());
    HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, kGoEvent);
    // 经 WSL interop 启动时 argv[0] 可能带 \\wsl 前缀,统一用模块真身路径
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    char cmd[400];
    std::snprintf(cmd, sizeof cmd, "\"%s\" writerA %s %s %llu", self, file, la.c_str(),
                  (unsigned long long)g_epoch);
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("!! CreateProcessA 失败:%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(ev, 5000); // A 拿到锁、写完数据才放行
    {
        FILE* fb = std::fopen(lb.c_str(), "a");
        unique_file_lock b(file, defer_lock, 0, kRange);
        char tag[32];
        std::snprintf(tag, sizeof tag, "B(pid=%lu)", (unsigned long)GetCurrentProcessId());
        flog(fb, tag, "try_lock() = false(锁在 A 手里)");
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok2 = b.try_lock_for(std::chrono::milliseconds(200));
        const auto d2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        flog(fb, "B", "try_lock_for(200ms) = %s(实际等了 %lld ms,超时)", ok2 ? "true" : "false",
             (long long)d2);
        const auto t1 = std::chrono::steady_clock::now();
        const bool ok3 = b.try_lock_for(std::chrono::seconds(3));
        const auto d3 = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t1)
                            .count();
        flog(fb, "B", "try_lock_for(3s) = %s(等了 %lld ms —— A 一放锁,下一轮询就拿到)",
             ok3 ? "true" : "false", (long long)d3);
        char buf[64]{}; // 临界区数据完好?
        DWORD rd = 0;
        SetFilePointer(b.native_handle(), 0, nullptr, FILE_BEGIN);
        ReadFile(b.native_handle(), buf, (DWORD)sizeof buf - 1, &rd, nullptr);
        buf[std::strcspn(buf, "\r\n")] = '\0';
        flog(fb, "B", "读到 \"%s\"(临界区数据完好)", buf);
        std::fclose(fb);
    }
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(ev);

    std::printf("\n==== 双进程时序:A 拿独占锁写数据,B 有限等待接棒 ====\n");
    struct Row {
        unsigned long long t;
        unsigned seq;
        std::string s;
    };
    std::vector<Row> rows;
    unsigned seq = 0;
    for (const char* p : {la.c_str(), lb.c_str()}) {
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
                     [](const Row& a, const Row& b) { return a.t < b.t; });
    for (const Row& r : rows) {
        std::printf("%s\n", r.s.c_str());
    }

    // ---- move 语义 ----
    std::printf("\n==== move 语义:锁跟着新主人走,moved-from 析构不放锁 ====\n");
    {
        unique_file_lock lk2(file2, defer_lock, 0, kRange); // 外层:只接手,不上锁
        {
            unique_file_lock lk1(file2, true, 0, kRange); // 内层:构造即锁上
            tlog("本进程", "lk1 构造(默认独占 [0,%llu)):owns_lock()=%d", kRange,
                 (int)lk1.owns_lock());
            tlog("探针", "lk1 持锁中:新开句柄试同区间 = %s",
                 probe(file2) ? "TRUE(?!)" : "FALSE(锁被占)");
            lk2 = std::move(lk1); // move 赋值:句柄与锁一起过户
            tlog("本进程", "move 后:lk1.alive()=%d(moved-from 空壳);lk2.owns_lock()=%d",
                 (int)lk1.alive(), (int)lk2.owns_lock());
        }
        tlog("探针", "lk1 已析构(moved-from,交出了句柄,析构碰不到锁):仍 = %s",
             probe(file2) ? "TRUE(?!)" : "FALSE(锁被占)");
        lk2.reset(); // 显式放锁 + 关句柄
        tlog("探针", "lk2 reset 后:= %s", probe(file2) ? "TRUE(拿到)" : "FALSE(锁被占)");
    }
    return 0;
}
