// e4_wfmo_limit.cpp —— E4 WaitForMultipleObjects 的上限:64 是道墙,还是 63?
//
// process/01 在等一群孩子时点过 MAXIMUM_WAIT_OBJECTS,说「再多就得开线程、或者换 IOCP」,
// 本实验把那道墙实测出来:一口气开 130 枚事件,nCount 从 63 一路加到 130,看返回值和 gle。
// 顺带验一桩流传很广的说法:「一次最多等 64-1=63 个」——它的出处是 MsgWait 族
// (QS_ALLINPUT 的消息队列自己占一个名额),这里一并测:MsgWaitForMultipleObjectsEx
// 在 63 与 64 上的表现。哪个过哪个不过,数字说话。
// 环境:Win11 26200 / MSYS2 UCRT64 g++ 16.1.0,-std=c++20 -Wall -Wextra
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <vector>

namespace {

long g_t0;
long ms_now() {
    return (long)(GetTickCount64() - (ULONGLONG)g_t0);
}
#define LOG(...)                             \
    do {                                     \
        std::printf("[%6ld ms] ", ms_now()); \
        std::printf(__VA_ARGS__);            \
        std::printf("\n");                   \
        std::fflush(stdout);                 \
    } while (0)

const char* wret_name(DWORD r) {
    static char b[64];
    if (r == WAIT_FAILED) {
        std::snprintf(b, sizeof b, "WAIT_FAILED");
        return b;
    }
    if (r == WAIT_TIMEOUT) {
        std::snprintf(b, sizeof b, "WAIT_TIMEOUT");
        return b;
    }
    if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + 130) {
        std::snprintf(b, sizeof b, "WAIT_OBJECT_0+%lu (点名索引 %lu)", r - WAIT_OBJECT_0,
                      r - WAIT_OBJECT_0);
        return b;
    }
    std::snprintf(b, sizeof b, "%lu", r);
    return b;
}

} // namespace

int main() {
    g_t0 = (long)GetTickCount64();
    LOG("MAXIMUM_WAIT_OBJECTS 编译期常量 = %d", MAXIMUM_WAIT_OBJECTS);

    constexpr int kTotal = 130;
    std::vector<HANDLE> evs;
    evs.reserve(kTotal);
    for (int i = 0; i < kTotal; ++i)
        evs.push_back(CreateEventW(nullptr, TRUE, FALSE, nullptr)); // 手动重置,全部无信号
    // 在索引 5 和 70 各点一枚信号,好让「等到了」的返回值有名字
    SetEvent(evs[5]);
    SetEvent(evs[70]);

    LOG("== WaitForMultipleObjects:nCount 从 63 加到 130 ==");
    for (DWORD n : {(DWORD)1, (DWORD)63, (DWORD)64, (DWORD)65, (DWORD)66, (DWORD)100, (DWORD)130}) {
        SetLastError(0);
        DWORD r = WaitForMultipleObjects(n, evs.data(), FALSE, 200);
        DWORD e = GetLastError();
        LOG("nCount=%3lu → 返回 %s, gle=%lu%s", (unsigned long)n, wret_name(r), e,
            (r == WAIT_FAILED && e == ERROR_INVALID_PARAMETER) ? "(87=ERROR_INVALID_PARAMETER)"
                                                               : "");
    }

    LOG("== 边界复核:nCount=64、把信号放在最末一枚(索引 63) ==");
    ResetEvent(evs[5]);
    SetEvent(evs[63]);
    DWORD r = WaitForMultipleObjects(64, evs.data(), FALSE, 200);
    LOG("nCount=64、信号在索引 63 → 返回 %s", wret_name(r));
    ResetEvent(evs[63]);

    LOG("== MsgWaitForMultipleObjectsEx(消息队列占位的那一族):63 与 64 ==");
    SetEvent(evs[5]);
    for (DWORD n : {(DWORD)1, (DWORD)63, (DWORD)64}) {
        SetLastError(0);
        DWORD rm = MsgWaitForMultipleObjectsEx(n, evs.data(), 200, QS_ALLINPUT, 0);
        DWORD e = GetLastError();
        LOG("nCount=%3lu → 返回 %s, gle=%lu%s", (unsigned long)n, wret_name(rm), e,
            (rm == WAIT_FAILED && e == ERROR_INVALID_PARAMETER) ? "(87=ERROR_INVALID_PARAMETER)"
                                                                : "");
    }

    LOG("结论注记:WFMO 墙立在 64/65 之间还是 63/64 之间,以上两段各判各的;MsgWait "
        "族的消息队列名额是否坐实,看它俩的差别");
    for (auto h : evs)
        CloseHandle(h);
    LOG("e4 完");
    return 0;
}
