// contention.cpp —— E4 锁争用计时(对照 Linux 侧 E6 的 8090.5 / 1009.9 ms)
//
// 口径:8 个工作进程(各自独立 CreateFileA),命名事件对齐起跑线:
//   locked:每轮 LockFileEx(EXCLUSIVE) → Sleep(10ms) → UnlockFile,100 轮
//   free  :同样 8×100×Sleep(10ms),只是不拿锁(对照:纯并行睡眠)
//   micro :8 进程 × 2000 轮「空临界区」lock/unlock,量纯锁传递开销
// 每种模式跑 3 轮取中位数。另附单句柄无争抢 1000000 对 lock/unlock。
// locked 的预期:临界区被串行化,总时长 ≈ 8 × 100 × Sleep(10) 的真实粒度。
//
// 角色:demo 主驱动;worker <file> <mode:locked|free|micro> <rounds> 工作进程。
// 环境:Win11 26200 / NTFS / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
// 数据文件烧死为 C:/msys64/tmp/l05win/contention.bin。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kFile = "C:/msys64/tmp/l05win/contention.bin";
const char* kGoEvent = "Local\\l05win_go";
constexpr int kWorkers = 8;
constexpr int kRounds = 100;
constexpr int kMicroRounds = 2000;
constexpr unsigned long long kRange = 8;

double now_ms() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
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

bool lockrange(HANDLE h) {
    OVERLAPPED ov{}; // lpOverlapped 必填:区间起点住在 Offset 里
    ov.Offset = 0;
    return LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, (DWORD)kRange, 0, &ov) != 0;
}

void unlockrange(HANDLE h) {
    UnlockFile(h, 0, 0, (DWORD)kRange, 0);
}

const char* exe_path_buf() {
    static char buf[MAX_PATH];
    if (!buf[0]) {
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
    }
    return buf;
}

int child_worker(const char* file, const char* mode, int rounds) {
    HANDLE ev = OpenEventA(SYNCHRONIZE, FALSE, kGoEvent);
    if (!ev) {
        return 2;
    }
    HANDLE h = INVALID_HANDLE_VALUE;
    if (std::strcmp(mode, "free") != 0) {
        h = openfile(file);
    }
    WaitForSingleObject(ev, INFINITE); // 起跑线
    for (int r = 0; r < rounds; ++r) {
        if (std::strcmp(mode, "locked") == 0) {
            lockrange(h);
            Sleep(10); // 临界区:睡 10 ms
            unlockrange(h);
        } else if (std::strcmp(mode, "free") == 0) {
            Sleep(10);
        } else { // micro:空临界区
            lockrange(h);
            unlockrange(h);
        }
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    CloseHandle(ev);
    return 0;
}

double measure(const char* mode, int rounds) {
    HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, kGoEvent);
    if (!ev) {
        std::fprintf(stderr, "CreateEventA 失败:%lu\n", (unsigned long)GetLastError());
        std::exit(1);
    }
    std::vector<HANDLE> kids;
    for (int i = 0; i < kWorkers; ++i) {
        char cmd[400];
        std::snprintf(cmd, sizeof cmd, "\"%s\" worker %s %s %d", exe_path_buf(), kFile, mode,
                      rounds);
        STARTUPINFOA si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            std::fprintf(stderr, "CreateProcessA 失败:%lu\n", (unsigned long)GetLastError());
            std::exit(1);
        }
        CloseHandle(pi.hThread);
        kids.push_back(pi.hProcess);
    }
    Sleep(150); // 等 8 个孩子都到位(事件未触发,全堵在起跑线)
    const double t1 = now_ms();
    SetEvent(ev);
    WaitForMultipleObjects((DWORD)kids.size(), kids.data(), TRUE, INFINITE);
    const double t2 = now_ms();
    for (HANDLE k : kids) {
        DWORD code = 0;
        GetExitCodeProcess(k, &code);
        if (code != 0) {
            std::printf("!! worker 异常退出:%lu\n", (unsigned long)code);
        }
        CloseHandle(k);
    }
    CloseHandle(ev);
    return t2 - t1;
}

void report(const char* tag, std::vector<double> v, int rounds, int workers) {
    std::sort(v.begin(), v.end());
    const double med = v[v.size() / 2];
    std::printf("%-8s", tag);
    for (double x : v) {
        std::printf("  %8.1f ms", x);
    }
    std::printf("  | 中位 %8.1f ms", med);
    if (rounds > 0) {
        const double one_cs = med / (workers * rounds); // 摊到每次临界区
        std::printf(",%.1f µs/人次", one_cs * 1000.0);
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 5 && std::strcmp(argv[1], "worker") == 0) {
        return child_worker(argv[2], argv[3], std::atoi(argv[4]));
    }
    if (argc < 2 || std::strcmp(argv[1], "demo") != 0) {
        std::printf("用法:contention.exe demo\n");
        return 1;
    }
    { // 清场 + 保证文件存在
        HANDLE h = CreateFileA(kFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            std::printf("!! 建数据文件失败:%lu(先 mkdir C:/msys64/tmp/l05win)\n",
                        (unsigned long)GetLastError());
            return 1;
        }
        CloseHandle(h);
    }
    std::printf(
        "pid=%lu(当前进程), 文件:%s(NTFS), %d 工作进程 × %d 轮,临界区 Sleep(10ms),3 轮取中位\n\n",
        (unsigned long)GetCurrentProcessId(), kFile, kWorkers, kRounds);

    std::printf("模式        r1            r2            r3\n");
    report("locked",
           {measure("locked", kRounds), measure("locked", kRounds), measure("locked", kRounds)},
           kRounds, kWorkers);
    const std::vector<double> freeV{measure("free", kRounds), measure("free", kRounds),
                                    measure("free", kRounds)};
    report("free", freeV, kRounds, kWorkers);
    {
        std::vector<double> fv = freeV;
        std::sort(fv.begin(), fv.end());
        std::printf("        (free 折算:每轮 Sleep(10) 实际 ≈ %.2f ms —— 并行睡眠的单份成本)\n",
                    fv[fv.size() / 2] / kRounds);
    }

    std::printf("\n空临界区(纯锁传递开销):%d 进程 × %d 轮 lock/unlock\n", kWorkers, kMicroRounds);
    report("micro",
           {measure("micro", kMicroRounds), measure("micro", kMicroRounds),
            measure("micro", kMicroRounds)},
           0, 0);

    { // 单句柄无争抢:1000000 对 lock/unlock
        HANDLE h = openfile(kFile);
        const double t1 = now_ms();
        constexpr int kPairs = 1000000;
        for (int i = 0; i < kPairs; ++i) {
            lockrange(h);
            unlockrange(h);
        }
        const double t2 = now_ms();
        std::printf("\n单句柄无争抢:%d 对 LockFileEx/UnlockFile → %.3f µs/一对\n", kPairs,
                    (t2 - t1) * 1000.0 / kPairs);
        CloseHandle(h);
    }
    { // Sleep(10) 的真实粒度(free 模式的口径注脚)
        const double t1 = now_ms();
        for (int i = 0; i < 100; ++i) {
            Sleep(10);
        }
        const double t2 = now_ms();
        std::printf("Sleep(10) 真实粒度:100 次 → %.2f ms/次(计时口径的注脚)\n", (t2 - t1) / 100.0);
    }
    return 0;
}
