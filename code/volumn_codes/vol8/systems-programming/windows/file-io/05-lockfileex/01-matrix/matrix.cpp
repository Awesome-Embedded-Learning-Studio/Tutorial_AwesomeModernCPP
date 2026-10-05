// matrix.cpp —— E1 LockFileEx 语义矩阵 + 句柄继承四组(文章《LockFileEx》E1/E5 素材)
//
// 一份二进制,第一个参数分派角色:
//   demo                                            主驱动:E1a..E1g + 继承 I1..I4
//   holdA    <file> <log> <epoch> <holdMs>          E1a 独占持锁者 A
//   waitB    <file> <log> <epoch>                   E1a 阻塞等待者 B
//   shareX   <file> <log> <epoch> <who> <holdMs>    E1b 共享持锁者
//   inherit  <file> <log> <epoch> <handleValue>     I1/I2:继承句柄三连测
//   stale    <file> <log> <epoch> <handleValue>     I3:bInheritHandles=FALSE 对照
//   proxy    <file> <log> <epoch>                   I4:上锁、spawn sleeper、立即退出
//   sleeper  <file> <log> <epoch> <handleValue>     I4:攥着继承句柄睡 1200 ms
//
// 环境口径:Windows 11 26200 / NTFS(C:) / MSYS2 UCRT64 g++ 16.1.0
//   编译:g++ -std=c++20 -Wall -Wextra matrix.cpp -o matrix.exe(零警告)
// 数据文件烧死为 C:/msys64/tmp/l05win/matrix.bin(demo 可用 argv[2] 覆盖)。
// 子进程不往 stdout 写,只写各自的日志文件,主驱动按时间戳归并打印 —— .out 才是确定的。
// 时间戳相对公共纪元(demo 启动时刻的 GetTickCount64,经 argv 传给各子进程)。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

const char* kDir = "C:/msys64/tmp/l05win/";
constexpr unsigned long long kBig = 1ull << 20; // E1a/E1b 用的 1 MiB 区间
constexpr DWORD EX = LOCKFILE_EXCLUSIVE_LOCK;
constexpr DWORD F = LOCKFILE_FAIL_IMMEDIATELY;

ULONGLONG g_epoch = 0;

const char* werr(DWORD e) {
    switch (e) {
        case ERROR_LOCK_VIOLATION:
            return "ERROR_LOCK_VIOLATION";
        case ERROR_NOT_LOCKED:
            return "ERROR_NOT_LOCKED";
        case ERROR_INVALID_HANDLE:
            return "ERROR_INVALID_HANDLE";
        case ERROR_IO_PENDING:
            return "ERROR_IO_PENDING";
        case ERROR_OPERATION_ABORTED:
            return "ERROR_OPERATION_ABORTED";
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

std::string rng(unsigned long long off, unsigned long long len) {
    char b[64];
    if (len == 0) {
        std::snprintf(b, sizeof b, "[%llu,EOF)", off);
    } else {
        std::snprintf(b, sizeof b, "[%llu,%llu)", off, off + len);
    }
    return b;
}

LockR lockx(HANDLE h, unsigned long long off, unsigned long long len, DWORD flags) {
    OVERLAPPED ov{}; // lpOverlapped 是必填项:区间起点住在 Offset/OffsetHigh
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

HANDLE openfile(const char* path, SECURITY_ATTRIBUTES* sa = nullptr) {
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "CreateFileA(%s) 失败:%lu\n", path, (unsigned long)GetLastError());
        std::exit(1);
    }
    return h;
}

void tlog(const char* who, const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    std::printf("  [%7llu ms] %s:%s\n", (unsigned long long)(GetTickCount64() - g_epoch), who, b);
}

void flog(FILE* f, const char* who, const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    std::fprintf(f, "  [%7llu ms] %s:%s\n", (unsigned long long)(GetTickCount64() - g_epoch), who,
                 b);
    std::fflush(f);
}

// ---- 子进程设施 ---------------------------------------------------------

struct Kid {
    PROCESS_INFORMATION pi{};
    void done() // 等退场并收句柄
    {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    void dismiss() // 不等,只把两份内核句柄还回去
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
};

std::string exe_path() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;
}

Kid spawn(const std::string& args, BOOL inherit) {
    std::string cmd = "\"" + exe_path() + "\" " + args;
    std::vector<char> m(cmd.begin(), cmd.end());
    m.push_back('\0');
    STARTUPINFOA si{};
    si.cb = sizeof si;
    Kid k;
    if (!CreateProcessA(nullptr, m.data(), nullptr, nullptr, inherit, 0, nullptr, nullptr, &si,
                        &k.pi)) {
        std::fprintf(stderr, "CreateProcessA 失败:%lu(cmd=%s)\n", (unsigned long)GetLastError(),
                     cmd.c_str());
        std::exit(1);
    }
    return k;
}

void print_merged(std::initializer_list<const char*> logs) {
    struct Row {
        unsigned long long t;
        unsigned seq;
        std::string s;
    };
    std::vector<Row> rows;
    unsigned seq = 0;
    for (const char* p : logs) {
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
}

// 新开一个句柄做非阻塞试探,拿得到就顺手放掉再关:只回答「这段此刻有没有人占」
LockR probe(const char* file, unsigned long long off, unsigned long long len,
            bool exclusive = true) {
    HANDLE h = openfile(file);
    LockR r = lockx(h, off, len, (exclusive ? EX : 0u) | F);
    if (r.ok) {
        unlockx(h, off, len);
    }
    CloseHandle(h);
    return r;
}

// ---- E1f 的阻塞版取证线程 ----------------------------------------------

volatile LONG g_woken = 0;

struct BlockArg {
    HANDLE h;
};

DWORD WINAPI blocked_locker(LPVOID p) {
    BlockArg* a = (BlockArg*)p;
    LockR r = lockx(a->h, 0, 100, EX); // 阻塞版:拿不到就不回来
    if (r.ok) {
        unlockx(a->h, 0, 100);
        InterlockedIncrement(&g_woken);
    }
    return 0;
}

// ---- 各节 ---------------------------------------------------------------

void e1a(const char* file) {
    std::printf("\n==== E1a  LOCKFILE_EXCLUSIVE_LOCK 互斥:A 拿 → B 阻塞 → A 放 → B 接棒 ====\n");
    std::string la = std::string(kDir) + "log_a.txt";
    std::string lb = std::string(kDir) + "log_b.txt";
    DeleteFileA(la.c_str());
    DeleteFileA(lb.c_str());
    char argsA[300], argsB[300];
    std::snprintf(argsA, sizeof argsA, "holdA %s %s %llu 800", file, la.c_str(),
                  (unsigned long long)g_epoch);
    std::snprintf(argsB, sizeof argsB, "waitB %s %s %llu", file, lb.c_str(),
                  (unsigned long long)g_epoch);
    Kid a = spawn(argsA, FALSE);
    Sleep(250);
    Kid b = spawn(argsB, FALSE);
    a.done();
    b.done();
    print_merged({la.c_str(), lb.c_str()});
}

void e1b(const char* file) {
    std::printf("\n==== E1b  共享锁(不带 EXCLUSIVE):两边同时持有 ====\n");
    std::string la = std::string(kDir) + "log_sa.txt";
    std::string lb = std::string(kDir) + "log_sb.txt";
    std::string lc = std::string(kDir) + "log_sc.txt";
    DeleteFileA(la.c_str());
    DeleteFileA(lb.c_str());
    DeleteFileA(lc.c_str());
    char argsA[300], argsB[300];
    std::snprintf(argsA, sizeof argsA, "shareX %s %s %llu A 600", file, la.c_str(),
                  (unsigned long long)g_epoch);
    std::snprintf(argsB, sizeof argsB, "shareX %s %s %llu B 600", file, lb.c_str(),
                  (unsigned long long)g_epoch);
    Kid a = spawn(argsA, FALSE);
    Sleep(250);
    Kid b = spawn(argsB, FALSE);
    Sleep(150);
    { // 探针也走日志文件,时间线才与两个共享者按 t 归并
        FILE* fc = std::fopen(lc.c_str(), "a");
        flog(fc, "探针C",
             "两个共享锁都在场时,EXCLUSIVE|FAIL_IMMEDIATELY 试同区间 = %s(共享之上独占进不来)",
             fmt(probe(file, 0, kBig)).c_str());
        std::fclose(fc);
    }
    a.done();
    b.done();
    print_merged({la.c_str(), lb.c_str(), lc.c_str()});
}

void e1c(const char* file) {
    std::printf("\n==== E1c  LOCKFILE_FAIL_IMMEDIATELY:冲突 = FALSE + GetLastError()=33 ====\n");
    HANDLE a = openfile(file);
    HANDLE b = openfile(file);
    tlog("A", "hA: LockFileEx %s EX = %s", rng(0, 100).c_str(), fmt(lockx(a, 0, 100, EX)).c_str());
    tlog("B", "hB: LockFileEx %s EX|FAIL_IMMEDIATELY = %s(同区间被占,立刻回来)",
         rng(0, 100).c_str(), fmt(lockx(b, 0, 100, EX | F)).c_str());
    LockR r = lockx(b, 100, 100, EX | F);
    tlog("B", "hB: LockFileEx %s EX|FAIL_IMMEDIATELY = %s(不相交就放行)", rng(100, 100).c_str(),
         fmt(r).c_str());
    unlockx(b, 100, 100);
    CloseHandle(a);
    CloseHandle(b);
}

void e1w(const char* file) {
    std::printf("\n==== E1w  锁与读写:别的句柄 read/write 真会被挡吗(咨询锁吗) ====\n");
    HANDLE a = openfile(file);
    HANDLE b = openfile(file);
    char buf[16];
    DWORD io = 0;
    LARGE_INTEGER at{};
    tlog("A", "hA: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
    at.QuadPart = 50; // 锁内读
    SetFilePointerEx(b, at, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r1 = ReadFile(b, buf, 10, &io, nullptr);
    tlog("B", "hB: ReadFile@[50,10) = %s, GetLastError()=%lu(%s) —— 锁内,被挡",
         r1 ? "TRUE" : "FALSE", (unsigned long)GetLastError(), werr(GetLastError()));
    at.QuadPart = 250; // 锁外读
    SetFilePointerEx(b, at, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r2 = ReadFile(b, buf, 10, &io, nullptr);
    tlog("B", "hB: ReadFile@[250,10) = %s(锁外,畅通)", r2 ? "TRUE" : "FALSE");
    at.QuadPart = 50; // 持锁者自己读
    SetFilePointerEx(a, at, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r3 = ReadFile(a, buf, 10, &io, nullptr);
    tlog("A", "hA(持锁者): ReadFile@[50,10) = %s(自己不受影响)", r3 ? "TRUE" : "FALSE");
    unlockx(a, 0, 100);
    at.QuadPart = 50;
    SetFilePointerEx(b, at, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r4 = ReadFile(b, buf, 10, &io, nullptr);
    tlog("B", "hB: 放锁后再读 @[50,10) = %s(恢复)", r4 ? "TRUE" : "FALSE");
    // 共享锁:读放行、写挡
    tlog("A", "hA: LockFileEx [0,100) 共享 = %s", fmt(lockx(a, 0, 100, 0)).c_str());
    at.QuadPart = 50;
    SetFilePointerEx(b, at, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r5 = ReadFile(b, buf, 10, &io, nullptr);
    tlog("B", "hB: 共享锁下 ReadFile@[50,10) = %s(读放行)", r5 ? "TRUE" : "FALSE");
    SetFilePointerEx(b, at, nullptr, FILE_BEGIN);
    SetLastError(0);
    BOOL r6 = WriteFile(b, buf, 10, &io, nullptr);
    tlog("B", "hB: 共享锁下 WriteFile@[50,10) = %s, GetLastError()=%lu(%s) —— 写被挡",
         r6 ? "TRUE" : "FALSE", (unsigned long)GetLastError(), werr(GetLastError()));
    unlockx(a, 0, 100);
    CloseHandle(a);
    CloseHandle(b);
    tlog("本进程", "   → 对照:POSIX flock/fcntl 是咨询锁,read/write 永远不看锁;Windows "
                   "的区间锁会真挡别的句柄的 I/O(文档另注明映射视图不受限,未测)");
}

void e1d(const char* file) {
    std::printf("\n==== E1d  字节区间:A 锁 [0,100),B 试 [50,150) 冲突、[100,200) 成功 ====\n");
    HANDLE a = openfile(file);
    HANDLE b = openfile(file);
    tlog("A", "hA: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
    tlog("B", "hB: LockFileEx [50,150) EX|FAIL = %s(与 [0,100) 交叠 50 字节)",
         fmt(lockx(b, 50, 100, EX | F)).c_str());
    LockR r = lockx(b, 100, 100, EX | F);
    tlog("B", "hB: LockFileEx [100,200) EX|FAIL = %s(相邻不相交 → 拿到)", fmt(r).c_str());
    unlockx(b, 100, 100);
    tlog("B", "hB: LockFileEx [99,101) EX|FAIL = %s(单字节交叠也算冲突)",
         fmt(lockx(b, 99, 2, EX | F)).c_str());
    CloseHandle(b);
    tlog("A", "hA: LockFileEx [200,长度0) EX = %s(零长度是「空区间」还是「到 EOF」?往下看)",
         fmt(lockx(a, 200, 0, EX)).c_str());
    tlog("B", "探针试 [900,50)(文件 1000 字节,在 200 之后)= %s", fmt(probe(file, 900, 50)).c_str());
    tlog("B", "探针试 [250,10)(紧挨 200)= %s", fmt(probe(file, 250, 10)).c_str());
    tlog("本进程",
         "   → 长度 0 = 空区间,什么都没锁(对照:fcntl 的 l_len=0 是锁到 EOF,Windows 这里不是)");
    tlog("A", "hA: LockFileEx [200,%llu) EX = %s(把长度给足,锁过 EOF 也没问题)", kBig,
         fmt(lockx(a, 200, kBig, EX)).c_str());
    tlog("B", "探针再试 [900,50) = %s(这回真挡了)", fmt(probe(file, 900, 50)).c_str());
    unlockx(a, 200, kBig);
    CloseHandle(a);
    tlog("B", "hA CloseHandle 清场后再试 [0,100) = %s", fmt(probe(file, 0, 100)).c_str());
}

void e1e(const char* file) {
    std::printf("\n==== E1e  同句柄区间语义:重复加锁、重叠、部分解锁 ====\n");
    { // e-1 同句柄重叠加锁:自冲突吗
        HANDLE a = openfile(file);
        tlog("本进程", "h1: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
        LockR r2 = lockx(a, 50, 100, EX | F);
        tlog("本进程", "h1: 同句柄再锁 [50,150) EX(带 FAIL_IMMEDIATELY 防挂死)= %s",
             fmt(r2).c_str());
        tlog("本进程", "   → 对照:fcntl 同进程后锁直接改写重叠段、flock 同 fd 是转换,LockFileEx "
                       "连自己名下都挡");
        LockR u = unlockx(a, 0, 100);
        tlog("本进程", "h1: UnlockFile [0,100) = %s(唯一的锁就是它,整段放干净)", fmt(u).c_str());
        tlog("探针", "h2 试 [0,50) = %s;[50,150) = %s(确认场上已无锁)",
             fmt(probe(file, 0, 50)).c_str(), fmt(probe(file, 50, 100)).c_str());
        CloseHandle(a);
    }
    { // e-2 同句柄原地重复锁同一区间
        HANDLE a = openfile(file);
        tlog("本进程", "h1: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
        tlog("本进程", "h1: 原地再锁 [0,100) EX|FAIL = %s(第二把没立起来)",
             fmt(lockx(a, 0, 100, EX | F)).c_str());
        tlog("本进程", "h1: UnlockFile [0,100) 第一次 = %s", fmt(unlockx(a, 0, 100)).c_str());
        tlog("探针", "h2 试 [0,100) = %s(放一次就干净 —— 没有计数、没有第二把)",
             fmt(probe(file, 0, 100)).c_str());
        tlog("本进程", "h1: UnlockFile [0,100) 第二次 = %s", fmt(unlockx(a, 0, 100)).c_str());
        CloseHandle(a);
    }
    { // e-3 部分解锁必须整段
        HANDLE a = openfile(file);
        tlog("本进程", "h1: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
        tlog("本进程", "h1: UnlockFile [10,20)(部分解锁)= %s", fmt(unlockx(a, 10, 10)).c_str());
        tlog("本进程", "h1: UnlockFile [0,100)(整段)= %s", fmt(unlockx(a, 0, 100)).c_str());
        CloseHandle(a);
    }
    { // e-4 文档特例:同句柄 EX 之上叠 SH 是允许的,解锁要两次
        HANDLE a = openfile(file);
        tlog("本进程", "h1: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
        tlog("本进程",
             "h1: 同句柄再锁 [0,100) 共享(不带 EXCLUSIVE,带 FAIL)= %s(文档特例:同句柄可叠)",
             fmt(lockx(a, 0, 100, F)).c_str());
        tlog("本进程", "h1: UnlockFile [0,100) 第一次 = %s", fmt(unlockx(a, 0, 100)).c_str());
        tlog("探针", "EX 试 [0,100) = %s;共享试 [0,100) = %s(看剩下的是哪一把)",
             fmt(probe(file, 0, 100, true)).c_str(), fmt(probe(file, 0, 100, false)).c_str());
        tlog("本进程", "h1: UnlockFile [0,100) 第二次 = %s", fmt(unlockx(a, 0, 100)).c_str());
        tlog("探针", "EX 试 [0,100) = %s(两把都该放干净了,EX 探针才作数)",
             fmt(probe(file, 0, 100, true)).c_str());
        CloseHandle(a);
    }
}

void e1f(const char* file) {
    std::printf("\n==== E1f  两个句柄:同进程 CreateFileA 两次,互相冲突吗 ====\n");
    HANDLE a = openfile(file);
    tlog("本进程", "h1: LockFileEx [0,100) EX = %s", fmt(lockx(a, 0, 100, EX)).c_str());
    HANDLE b = openfile(file);
    tlog("本进程", "h2(同进程第二次 open):LockFileEx [0,100) EX|FAIL = %s",
         fmt(lockx(b, 0, 100, EX | F)).c_str());
    tlog("本进程", "   → 同进程两个句柄互相挡:冲突判定没有属主豁免,进程身份救不了");
    BlockArg ba{b};
    HANDLE th = CreateThread(nullptr, 0, blocked_locker, &ba, 0, nullptr);
    Sleep(1500);
    tlog("本进程", "1.5 s 过去,对 h2 的阻塞版 LockFileEx 仍未返回(线程 g_woken=%d)", (int)g_woken);
    tlog("本进程", "   → 放锁的人永远不来:同进程两句柄,自己挡死了自己");
    CloseHandle(a);
    Sleep(300);
    tlog("本进程", "CloseHandle(h1) 之后:g_woken=%d —— 阻塞线程%s", (int)g_woken,
         g_woken ? "应声拿到并已放掉:它等的正是「另一个句柄」" : "仍未返回(异常)");
    WaitForSingleObject(th, 2000);
    CloseHandle(th);
    CloseHandle(b);
}

void e1g(const char* file) {
    std::printf("\n==== E1g  CloseHandle:该句柄名下所有区间一起放 ====\n");
    HANDLE a = openfile(file);
    tlog("A", "hA: [0,100) = %s,[200,300) = %s,两把都上身", fmt(lockx(a, 0, 100, EX)).c_str(),
         fmt(lockx(a, 200, 100, EX)).c_str());
    tlog("B", "hB 试 [0,100) = %s;[200,300) = %s", fmt(probe(file, 0, 100)).c_str(),
         fmt(probe(file, 200, 100)).c_str());
    CloseHandle(a);
    tlog("B", "hA CloseHandle(一次 UnlockFile 都没调)后:试 [0,100) = %s;[200,300) = %s",
         fmt(probe(file, 0, 100)).c_str(), fmt(probe(file, 200, 100)).c_str());
}

void e1h(const char* file) {
    std::printf("\n==== E1h  句柄继承(bInheritHandles):继承来的句柄能干什么 ====\n");
    // I1/I2:可继承句柄,子进程三连测
    std::string li = std::string(kDir) + "log_i.txt";
    DeleteFileA(li.c_str());
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE hl = openfile(file, &sa);
    tlog("父", "可继承句柄 hL(SA.bInheritHandle=TRUE):LockFileEx [0,100) EX = %s",
         fmt(lockx(hl, 0, 100, EX)).c_str());
    char args[300];
    std::snprintf(args, sizeof args, "inherit %s %s %llu %llu", file, li.c_str(),
                  (unsigned long long)g_epoch, (unsigned long long)(uintptr_t)hl);
    Kid k = spawn(args, TRUE); // bInheritHandles=TRUE:hL 的值在子进程里有效
    k.done();
    print_merged({li.c_str()});
    LockR pr = probe(file, 0, 100);
    tlog("父", "子进程退场后,父探针试 [0,100) = %s", fmt(pr).c_str());
    tlog("父",
         "   → ②③ 连起来读:继承句柄既加不进去(冲突判定不豁免任何人)、也放不掉父的锁(解锁认进程)");
    tlog("父",
         "   → 对照:flock 的子进程能经继承 fd 替父放锁(同一描述共享同一把锁),Windows 这条路走不通");
    CloseHandle(hl);

    // I3:对照,bInheritHandles=FALSE,句柄值照样传
    std::printf("\n---- I3 对照:bInheritHandles=FALSE,句柄值照样传过去 ----\n");
    std::string ls = std::string(kDir) + "log_s.txt";
    DeleteFileA(ls.c_str());
    HANDLE hx = openfile(file);
    tlog("父", "普通句柄(不可继承):LockFileEx [0,100) EX = %s", fmt(lockx(hx, 0, 100, EX)).c_str());
    std::snprintf(args, sizeof args, "stale %s %s %llu %llu", file, ls.c_str(),
                  (unsigned long long)g_epoch, (unsigned long long)(uintptr_t)hx);
    Kid s = spawn(args, FALSE); // bInheritHandles=FALSE:那个值在子进程里是空头支票
    s.done();
    print_merged({ls.c_str()});
    tlog("父", "对照组的锁毫发无损:父探针 [0,100) = %s(应仍被挡)",
         fmt(probe(file, 0, 100)).c_str());
    CloseHandle(hx);

    // I4:锁的寿命 —— 持锁进程退场,继承句柄还攥在别人手里,锁还在吗
    std::printf("\n---- I4 锁的寿命:持锁进程退出,继承句柄还在别人手里 ----\n");
    std::string lp = std::string(kDir) + "log_p.txt";
    std::string lz = std::string(kDir) + "log_z.txt";
    std::string ld = std::string(kDir) + "log_d.txt";
    DeleteFileA(lp.c_str());
    DeleteFileA(lz.c_str());
    DeleteFileA(ld.c_str());
    std::snprintf(args, sizeof args, "proxy %s %s %llu", file, lp.c_str(),
                  (unsigned long long)g_epoch);
    Kid p = spawn(args, TRUE); // proxy 内部再 spawn sleeper 继承句柄
    p.done();
    {
        FILE* fd_ = std::fopen(ld.c_str(), "a");
        flog(fd_, "探针",
             "proxy 进程已退出(全程没 UnlockFile,但 sleeper 攥着同一文件对象的句柄):试 [1000,200) "
             "= %s",
             fmt(probe(file, 1000, 200)).c_str());
        std::fclose(fd_);
    }
    Sleep(1700); // sleeper 睡 1200 ms 后退场,继承句柄随之关闭
    {
        FILE* fd_ = std::fopen(ld.c_str(), "a");
        flog(fd_, "探针", "sleeper 也退场后,再试 [1000,200) = %s",
             fmt(probe(file, 1000, 200)).c_str());
        std::fclose(fd_);
    }
    print_merged({lp.c_str(), lz.c_str(), ld.c_str()});

    // I5:归属单位判别六连测 —— DuplicateHandle(同一文件对象)/ 新 open(新文件对象)/ 关句柄次序
    std::printf("\n---- I5 归属单位判别:DuplicateHandle、新 open、关句柄次序 ----\n");
    {
        HANDLE h1 = openfile(file);
        tlog("本进程", "h1(新 open): LockFileEx [0,100) EX = %s",
             fmt(lockx(h1, 0, 100, EX)).c_str());
        HANDLE h2 = nullptr;
        DuplicateHandle(GetCurrentProcess(), h1, GetCurrentProcess(), &h2, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        tlog("本进程", "DuplicateHandle 复制出 h2(与 h1 指向同一文件对象)");
        tlog("本进程", "h2: LockFileEx [0,100) EX|FAIL = %s(同进程同对象,照样加不进)",
             fmt(lockx(h2, 0, 100, EX | F)).c_str());
        LockR u2 = unlockx(h2, 0, 100);
        tlog("本进程", "h2: UnlockFile [0,100)(放 h1 立的那把)= %s —— 同进程的复制品放得掉",
             fmt(u2).c_str());
        tlog("本进程", "h1: 重新 LockFileEx [0,100) EX = %s", fmt(lockx(h1, 0, 100, EX)).c_str());
        HANDLE h3 = openfile(file);
        LockR u3 = unlockx(h3, 0, 100);
        tlog("本进程", "h3(全新 open,同进程): UnlockFile [0,100) = %s", fmt(u3).c_str());
        if (u3.ok) {
            tlog("本进程", "   → 解锁只认进程,不认文件对象");
            tlog("本进程", "h1: 再重新 LockFileEx [0,100) EX = %s",
                 fmt(lockx(h1, 0, 100, EX)).c_str());
        } else {
            tlog("本进程", "   → 解锁认进程+文件对象,新 open 的句柄放不掉");
        }
        CloseHandle(h1); // 原主关门,复制品 h2 还攥着同一文件对象
        tlog("本进程", "CloseHandle(h1) 后(h2 还开着同一对象),新句柄试 [0,100) = %s",
             fmt(probe(file, 0, 100)).c_str());
        CloseHandle(h2);
        CloseHandle(h3);
        tlog("本进程", "h2/h3 也全关后,再试 = %s(清场确认)", fmt(probe(file, 0, 100)).c_str());
    }
}

// ---- 子进程角色 ----------------------------------------------------------

int child_holdA(const char* file, const char* logp, ULONGLONG epoch, int holdMs) {
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "A(pid=%lu)", (unsigned long)GetCurrentProcessId());
    FILE* f = std::fopen(logp, "a");
    HANDLE h = openfile(file);
    LockR r = lockx(h, 0, kBig, EX);
    flog(f, tag, "LockFileEx([0,%llu) EX,阻塞版) = %s,持锁", kBig, fmt(r).c_str());
    Sleep(holdMs);
    flog(f, "A", "UnlockFile [0,%llu) = %s → B 的拿锁时刻应紧贴这一行", kBig,
         fmt(unlockx(h, 0, kBig)).c_str());
    std::fclose(f);
    CloseHandle(h);
    return 0;
}

int child_waitB(const char* file, const char* logp, ULONGLONG epoch) {
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "B(pid=%lu)", (unsigned long)GetCurrentProcessId());
    FILE* f = std::fopen(logp, "a");
    flog(f, "B", "LockFileEx(同区间,阻塞版)调用中 …");
    HANDLE h = openfile(file);
    LockR r = lockx(h, 0, kBig, EX);
    flog(f, tag, "阻塞版返回 = %s,拿到锁(A 一放就接棒)", fmt(r).c_str());
    unlockx(h, 0, kBig);
    std::fclose(f);
    CloseHandle(h);
    return 0;
}

int child_shareX(const char* file, const char* logp, ULONGLONG epoch, const char* who, int holdMs) {
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "共享%s(pid=%lu)", who, (unsigned long)GetCurrentProcessId());
    FILE* f = std::fopen(logp, "a");
    HANDLE h = openfile(file);
    LockR r = lockx(h, 0, kBig, 0); // 不带 EXCLUSIVE:共享锁
    flog(f, tag, "LockFileEx([0,%llu) 不带 EXCLUSIVE) = %s,共享锁到手(与另一边同时持有)", kBig,
         fmt(r).c_str());
    Sleep(holdMs);
    flog(f, tag, "UnlockFile = %s", fmt(unlockx(h, 0, kBig)).c_str());
    std::fclose(f);
    CloseHandle(h);
    return 0;
}

int child_inherit(const char* file, const char* logp, ULONGLONG epoch, const char* hvs) {
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "子(pid=%lu)", (unsigned long)GetCurrentProcessId());
    HANDLE hv = (HANDLE)(uintptr_t)strtoull(hvs, nullptr, 10);
    // ① 先测自己 open 的新句柄:子进程身份没有豁免
    HANDLE own = openfile(file);
    LockR r1 = lockx(own, 0, 100, EX | F);
    CloseHandle(own);
    // ② 再测继承来的句柄值(与父指向同一文件对象)
    LockR r2 = lockx(hv, 0, 100, EX | F);
    // ③ 试着经继承句柄放掉这段
    LockR r3 = unlockx(hv, 0, 100);
    FILE* f = std::fopen(logp, "a");
    flog(f, tag,
         "① 自己 CreateFileA 的新句柄:LockFileEx [0,100) EX|FAIL = %s —— 「我是它孩子」不算数",
         fmt(r1).c_str());
    flog(f, "子", "② 继承来的句柄值(与父指向同一文件对象):LockFileEx [0,100) EX|FAIL = %s",
         fmt(r2).c_str());
    flog(f, "子", "③ UnlockFile(继承句柄,[0,100)) = %s", fmt(r3).c_str());
    std::fclose(f);
    return 0;
}

int child_stale(const char* file, const char* logp, ULONGLONG epoch, const char* hvs) {
    (void)file;
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "子(pid=%lu)", (unsigned long)GetCurrentProcessId());
    HANDLE hv = (HANDLE)(uintptr_t)strtoull(hvs, nullptr, 10);
    // 先探再开日志文件:让子进程句柄表尽量空,减少句柄值撞车的可能
    SetLastError(0);
    DWORD ft = GetFileType(hv);
    DWORD ftErr = GetLastError();
    LockR r = lockx(hv, 0, 100, EX | F);
    char msg[320];
    std::snprintf(msg, sizeof msg, "LockFileEx(父传来的句柄值) = %s;GetFileType=%lu(错误 %lu)",
                  fmt(r).c_str(), (unsigned long)ft, (unsigned long)ftErr);
    FILE* f = std::fopen(logp, "a");
    flog(f, tag, "%s —— bInheritHandles=FALSE 时,句柄值没有跨进程的意义", msg);
    std::fclose(f);
    return 0;
}

int child_proxy(const char* file, const char* logp, ULONGLONG epoch) {
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "proxy(pid=%lu)", (unsigned long)GetCurrentProcessId());
    FILE* f = std::fopen(logp, "a");
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE h = openfile(file, &sa);
    LockR r = lockx(h, 1000, 200, EX);
    flog(f, tag,
         "可继承句柄上锁 [1000,200) EX = %s,接着 spawn sleeper、自己立即退出(全程不 UnlockFile)",
         fmt(r).c_str());
    char args[300];
    std::string lz = std::string(kDir) + "log_z.txt";
    std::snprintf(args, sizeof args, "sleeper %s %s %llu %llu", file, lz.c_str(),
                  (unsigned long long)g_epoch, (unsigned long long)(uintptr_t)h);
    Kid z = spawn(args, TRUE);
    z.dismiss();
    std::fclose(f);
    return 0; // 退出即关自己的 h;文件对象与锁由 sleeper 的继承句柄续命
}

int child_sleeper(const char* file, const char* logp, ULONGLONG epoch, const char* hvs) {
    (void)file;
    (void)hvs;
    g_epoch = epoch;
    char tag[32];
    std::snprintf(tag, sizeof tag, "sleeper(pid=%lu)", (unsigned long)GetCurrentProcessId());
    FILE* f = std::fopen(logp, "a");
    flog(f, tag, "攥着继承句柄睡 1200 ms,什么都不做");
    Sleep(1200);
    std::fclose(f);
    return 0; // 退出即关句柄 → 若锁的寿命跟句柄走,这一刻才是释放点
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::printf(
            "用法:matrix.exe demo [数据文件]\n"
            "     matrix.exe <holdA|waitB|shareX|inherit|stale|proxy|sleeper> …(见源码头)\n");
        return 1;
    }
    const std::string role = argv[1];
    if (role == "holdA" && argc >= 6) {
        return child_holdA(argv[2], argv[3], strtoull(argv[4], nullptr, 10), std::atoi(argv[5]));
    }
    if (role == "waitB" && argc >= 5) {
        return child_waitB(argv[2], argv[3], strtoull(argv[4], nullptr, 10));
    }
    if (role == "shareX" && argc >= 7) {
        return child_shareX(argv[2], argv[3], strtoull(argv[4], nullptr, 10), argv[5],
                            std::atoi(argv[6]));
    }
    if (role == "inherit" && argc >= 6) {
        return child_inherit(argv[2], argv[3], strtoull(argv[4], nullptr, 10), argv[5]);
    }
    if (role == "stale" && argc >= 6) {
        return child_stale(argv[2], argv[3], strtoull(argv[4], nullptr, 10), argv[5]);
    }
    if (role == "proxy" && argc >= 5) {
        return child_proxy(argv[2], argv[3], strtoull(argv[4], nullptr, 10));
    }
    if (role == "sleeper" && argc >= 6) {
        return child_sleeper(argv[2], argv[3], strtoull(argv[4], nullptr, 10), argv[5]);
    }
    if (role == "demo") {
        const char* file = argc >= 3 ? argv[2] : "C:/msys64/tmp/l05win/matrix.bin";
        g_epoch = GetTickCount64();
        { // 重建数据文件,写满 1000 字节(E1d 的 [200,EOF) 要用 EOF 位置)
            HANDLE h = CreateFileA(file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                std::printf("!! 建数据文件失败:%lu(先 mkdir C:/msys64/tmp/l05win)\n",
                            (unsigned long)GetLastError());
                return 1;
            }
            char z[1000]{};
            DWORD w = 0;
            WriteFile(h, z, sizeof z, &w, nullptr);
            CloseHandle(h);
        }
        std::printf("pid=%lu(当前进程), 数据文件:%s(NTFS)\n", (unsigned long)GetCurrentProcessId(),
                    file);
        e1a(file);
        e1b(file);
        e1c(file);
        e1w(file);
        e1d(file);
        e1e(file);
        e1f(file);
        e1g(file);
        e1h(file);
        return 0;
    }
    std::printf("角色/参数不对,argc=%d\n", argc);
    return 1;
}
