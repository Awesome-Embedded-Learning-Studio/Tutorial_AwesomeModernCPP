// e3_named_sync.cpp —— E3 跨进程同步三件套:命名互斥体 / 命名事件 / 命名信号量
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3_named_sync.cpp -o
//   e3_named_sync.exe
// 运行(四个子命令,各自一条命令跑完;父进程都用 CreateProcessW 拉子进程):
//   chmod +x e3_named_sync.exe
//   ./e3_named_sync.exe mutex          # 无锁 vs 命名互斥体:共享计数 20 万次自增
//   ./e3_named_sync.exe mutex abandoned # 持锁进程暴毙:等待方收到 WAIT_ABANDONED
//   ./e3_named_sync.exe event          # 手动复位(广播) vs 自动复位(单播)
//   ./e3_named_sync.exe semaphore      # 计数 2 的信号量管 3 个进程并发
//
// 观察点:
//   (mutex)      无锁:两个进程各 10 万次 counter++ 终值 < 200000(丢更新);
//                命名互斥体护住后恰好 200000;第二次 CreateMutexW 同名 err=183(与映射同一套合一语义)
//   (abandoned)  子进程抱着互斥体 ExitProcess:父 WaitForSingleObject 返回 WAIT_ABANDONED(0x80),
//                互斥体没坏,接着能用——Linux 要 PTHREAD_MUTEX_ROBUST 才换回 EOWNERDEAD,
//                Windows 命名互斥体天然带这层
//   (event)      手动复位:一次 SetEvent 两个等待进程全醒(广播);自动复位:一次 SetEvent
//                醒一个,再来一次才轮到第二个(单播)——同一个 API 就 bManualReset 一个参数之差
//   (semaphore)  初值 2:三个进程抢,两个进门第三个挂起;ReleaseSemaphore(1) 后第三个才过
//   共同点      :三个原语都是内核对象,Create*W 即"创建或打开",天然跨进程;
//                对照 Linux:pthread 互斥体默认进程内,要 PTHREAD_PROCESS_SHARED 放进共享内存
//                才跨进程——Windows 不用把锁放共享内存里,名字就是通道

#include "../common/shm_util.hpp"
#include <cstdlib>
#include <cstring>

static constexpr uint32_t SHM_SIZE = 0x10000; // 64KB:计数器 / 时间线槽位都放这里
static constexpr uint32_t OFF_COUNTER = 0x1000;
static constexpr uint32_t OFF_TIMELINE = 0x2000; // 每槽 32 字节:{序号, 时间戳, 备注}

static HANDLE open_or_die(const std::wstring& name) {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name.c_str());
    if (!h) {
        printf("[worker] OpenEventW(%ls) 失败 err=%lu\n", name.c_str(), GetLastError());
        ExitProcess(1);
    }
    return h;
}

// ---------------- mutex worker:argv = e3_named_sync.exe mutex-worker <父pid> <lock|nolock>
// ----------------
static int run_mutex_worker(DWORD ppid, bool lock) {
    const std::wstring mname = local_name(L"E3M", ppid);
    HANDLE hm = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, local_name(L"E3S", ppid).c_str());
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    uint64_t* counter = (uint64_t*)(base + OFF_COUNTER);
    HANDLE mtx = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, mname.c_str());
    HANDLE go = open_or_die(local_name(L"E3Ego", ppid));
    WaitForSingleObject(go, INFINITE); // 等父进程发令,保证两边的循环真正并行重叠
    for (int i = 0; i < 100000; ++i) {
        if (lock) {
            WaitForSingleObject(mtx, INFINITE);
        }
        *counter = *counter + 1; // 故意非原子:读-改-写三步,没锁就互相踩
        if (lock) {
            ReleaseMutex(mtx);
        }
    }
    printf("[worker pid=%lu] 完成(%s),此刻计数=%llu\n", GetCurrentProcessId(),
           lock ? "互斥体护驾" : "无锁裸奔", (unsigned long long)*counter);
    fflush(stdout);
    UnmapViewOfFile(base);
    CloseHandle(hm);
    CloseHandle(mtx);
    CloseHandle(go);
    return 0;
}

// ---------------- abandoner:抱着互斥体就死 argv = abandoner <父pid> ----------------
static int run_abandoner(DWORD ppid) {
    HANDLE mtx = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, local_name(L"E3M", ppid).c_str());
    DWORD w = WaitForSingleObject(mtx, INFINITE);
    printf("[abandoner pid=%lu] 拿到互斥体(等待结果 %lu),啥也不干直接 ExitProcess\n",
           GetCurrentProcessId(), w);
    fflush(stdout);
    ExitProcess(1); // 不 ReleaseMutex,带着锁暴毙
    return 0;
}

// ---------------- event waiter:argv = event-waiter <父pid> <idx> <manual|auto> ----------------
static int run_event_waiter(DWORD ppid, int idx, bool manual) {
    HANDLE hm = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, local_name(L"E3S", ppid).c_str());
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    const std::wstring ename = local_name(manual ? L"E3Em" : L"E3Ea", ppid);
    HANDLE ev = open_or_die(ename);
    HANDLE go = open_or_die(local_name(L"E3Ego", ppid));
    uint64_t* slot = (uint64_t*)(base + OFF_TIMELINE + 32 * idx);
    WaitForSingleObject(go, INFINITE); // 大家就位后统一起跑
    uint64_t t0 = qpc_ns();
    DWORD w = WaitForSingleObject(ev, INFINITE);
    uint64_t t1 = qpc_ns();
    slot[0] = idx;
    slot[1] = t1 - t0; // 从起跑到被唤醒的等待时长
    slot[2] = w;
    printf("[waiter %d pid=%lu] 醒了:等待结果=%lu,等了 %llu 微秒\n", idx, GetCurrentProcessId(), w,
           (unsigned long long)((t1 - t0) / 1000));
    fflush(stdout);
    UnmapViewOfFile(base);
    CloseHandle(hm);
    CloseHandle(ev);
    CloseHandle(go);
    return 0;
}

// ---------------- semaphore waiter:argv = sem-waiter <父pid> <idx> ----------------
static int run_sem_waiter(DWORD ppid, int idx) {
    HANDLE hm = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, local_name(L"E3S", ppid).c_str());
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    HANDLE sem = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, local_name(L"E3Sem", ppid).c_str());
    HANDLE go = open_or_die(local_name(L"E3Ego", ppid));
    HANDLE done = open_or_die(local_name(L"E3done", ppid));
    uint64_t* slot = (uint64_t*)(base + OFF_TIMELINE + 32 * idx);
    WaitForSingleObject(go, INFINITE);
    uint64_t t0 = qpc_ns();
    DWORD w = WaitForSingleObject(sem, INFINITE);
    uint64_t t1 = qpc_ns();
    slot[0] = idx;
    slot[1] = t1 - t0;
    slot[2] = w;
    printf("[sem-waiter %d pid=%lu] 过闸:等待结果=%lu,排队 %llu "
           "微秒(名额到手,攥着不放直到父进程放行)\n",
           idx, GetCurrentProcessId(), w, (unsigned long long)((t1 - t0) / 1000));
    fflush(stdout);
    WaitForSingleObject(done, INFINITE); // 攥着名额等收工令:别让"先到的快出快还"把堵的现状藏掉
    UnmapViewOfFile(base);
    CloseHandle(hm);
    CloseHandle(sem);
    CloseHandle(go);
    CloseHandle(done);
    return 0;
}

// ---------------- 父进程各场景 ----------------
static int mode_mutex(bool abandoned) {
    const DWORD pid = GetCurrentProcessId();
    const std::wstring shm_name = local_name(L"E3S", pid);
    const std::wstring mtx_name = local_name(L"E3M", pid);
    if (abandoned) {
        printf("[E3-abandoned] 持锁暴毙:子进程抱着命名互斥体 ExitProcess\n");
        HANDLE mtx = CreateMutexW(nullptr, FALSE, mtx_name.c_str());
        PROCESS_INFORMATION pi{};
        if (!spawn_self((L"abandoner " + std::to_wstring(pid)).c_str(), pi)) {
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD child_code = 0;
        GetExitCodeProcess(pi.hProcess, &child_code);
        printf("[父] 子进程退出码=%lu,现在去等那把没人放的互斥体...\n", child_code);
        fflush(stdout);
        DWORD w = WaitForSingleObject(mtx, INFINITE);
        printf("[父] WaitForSingleObject 返回 %lu(%s) —— 互斥体没坏,拿到手还能继续用\n", w,
               w == WAIT_ABANDONED ? "WAIT_ABANDONED=0x80" : "?!");
        ReleaseMutex(mtx);
        CloseHandle(mtx);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        fflush(stdout);
        return 0;
    }

    for (int variant = 0; variant < 2; ++variant) {
        const bool lock = variant == 1;
        printf("\n[E3-mutex-%s] 两进程各 10 万次共享计数自增\n", lock ? "锁" : "无锁");
        HANDLE hm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, SHM_SIZE,
                                       shm_name.c_str());
        unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        *(uint64_t*)(base + OFF_COUNTER) = 0;
        SetLastError(0);
        HANDLE mtx = CreateMutexW(nullptr, FALSE, mtx_name.c_str());
        DWORD e0 = GetLastError();
        SetLastError(0);
        HANDLE mtx2 = CreateMutexW(nullptr, FALSE, mtx_name.c_str());
        if (variant == 0) {
            printf(
                "[父] 首次 CreateMutexW err=%lu;同名再 Create -> err=%lu(183=ERROR_ALREADY_EXISTS,"
                "句柄照常有效——与命名映射同一套\"创建即打开\"语义)\n",
                e0, GetLastError());
        }
        CloseHandle(mtx2);
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, local_name(L"E3Ego", pid).c_str());

        // 双方各自钉一颗核,确保真并行(不然调度串行化会把丢更新藏起来)
        SetProcessAffinityMask(GetCurrentProcess(), 1 << 2);
        PROCESS_INFORMATION pi{};
        spawn_self(
            (L"mutex-worker " + std::to_wstring(pid) + (lock ? L" lock" : L" nolock")).c_str(), pi);
        SetProcessAffinityMask(pi.hProcess, 1 << 3);
        Sleep(150);   // 等 worker 就位挂上 go 事件
        SetEvent(go); // 发令:两边同时开跑
        uint64_t* counter = (uint64_t*)(base + OFF_COUNTER);
        for (int i = 0; i < 100000; ++i) {
            if (lock) {
                WaitForSingleObject(mtx, INFINITE);
            }
            *counter = *counter + 1;
            if (lock) {
                ReleaseMutex(mtx);
            }
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        printf("[父] 终值=%llu / 期望 200000 -> %s(差 %llu 次)\n", (unsigned long long)*counter,
               *counter == 200000 ? "一个不丢" : (*counter < 200000 ? "丢更新" : "?!"),
               (unsigned long long)(200000 - *counter));
        CloseHandle(mtx);
        CloseHandle(go);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        UnmapViewOfFile(base);
        CloseHandle(hm);
        fflush(stdout);
    }
    return 0;
}

static int mode_event() {
    const DWORD pid = GetCurrentProcessId();
    printf("[E3-event] 同一个 CreateEventW,bManualReset 一个参数定广播还是单播\n");
    for (int variant = 0; variant < 2; ++variant) {
        const bool manual = variant == 0;
        printf("\n[%s复位事件] 两个等待进程就位,父进程只 SetEvent 一次\n",
               manual ? "手动" : "自动");
        HANDLE hm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, SHM_SIZE,
                                       local_name(L"E3S", pid).c_str());
        unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        memset(base + OFF_TIMELINE, 0, 32 * 4);
        HANDLE ev = CreateEventW(nullptr, manual ? TRUE : FALSE, FALSE,
                                 local_name(manual ? L"E3Em" : L"E3Ea", pid).c_str());
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE,
                                 local_name(L"E3Ego", pid).c_str()); // 手动复位:一声令下全员起跑
        PROCESS_INFORMATION pi[2]{};
        for (int i = 0; i < 2; ++i) {
            std::wstring arg = L"event-waiter " + std::to_wstring(pid) + L" " + std::to_wstring(i) +
                               (manual ? L" manual" : L" auto");
            spawn_self(arg.c_str(), pi[i]);
        }
        Sleep(150);   // 等两个孩子都挂上 WaitForSingleObject
        SetEvent(go); // 起跑
        Sleep(50);    // 让它们先等上
        SetEvent(ev); // 全场只有这一次(自动复位场景)
        printf("[父] SetEvent 一次\n");
        HANDLE both[2] = {pi[0].hProcess, pi[1].hProcess};
        if (manual) {
            Sleep(80); // 留时间让两个都醒
            ResetEvent(ev);
            WaitForMultipleObjects(2, both, TRUE, INFINITE);
        } else {
            DWORD w = WaitForMultipleObjects(2, both, FALSE, INFINITE); // 只等第一个醒的
            int first = (int)(w - WAIT_OBJECT_0);
            printf("[父] 第一发只放行了 waiter %d;再 SetEvent 一发才轮到另一个\n", first);
            SetEvent(ev);
            WaitForMultipleObjects(2, both, TRUE, INFINITE);
        }
        for (int i = 0; i < 2; ++i) {
            uint64_t* slot = (uint64_t*)(base + OFF_TIMELINE + 32 * i);
            printf("[父] waiter%llu 等待时长 %llu 微秒(结果 %llu)\n", (unsigned long long)slot[0],
                   (unsigned long long)(slot[1] / 1000), (unsigned long long)slot[2]);
            CloseHandle(pi[i].hThread);
            CloseHandle(pi[i].hProcess);
        }
        if (manual) {
            printf("[父] 手动复位:一次 SetEvent,两个 waiter 都醒——广播\n");
        } else {
            printf("[父] 自动复位:一次 SetEvent 只放行一个;第二个得等下一次\n");
        }
        UnmapViewOfFile(base);
        CloseHandle(hm);
        CloseHandle(ev);
        CloseHandle(go);
        fflush(stdout);
    }
    return 0;
}

static int mode_semaphore() {
    const DWORD pid = GetCurrentProcessId();
    printf("[E3-semaphore] 初值 2 的命名信号量,3 个进程排队\n");
    HANDLE hm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, SHM_SIZE,
                                   local_name(L"E3S", pid).c_str());
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    memset(base + OFF_TIMELINE, 0, 32 * 4);
    SetLastError(0);
    HANDLE sem = CreateSemaphoreW(nullptr, 2, 2, local_name(L"E3Sem", pid).c_str());
    printf("[父] CreateSemaphoreW(初值2,上限2) -> %p err=%lu\n", sem, GetLastError());
    HANDLE go = CreateEventW(nullptr, TRUE, FALSE,
                             local_name(L"E3Ego", pid).c_str()); // 手动复位:三 waiter 同步起跑
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, local_name(L"E3done", pid).c_str());
    PROCESS_INFORMATION pi[3]{};
    for (int i = 0; i < 3; ++i) {
        std::wstring arg = L"sem-waiter " + std::to_wstring(pid) + L" " + std::to_wstring(i);
        spawn_self(arg.c_str(), pi[i]);
    }
    Sleep(400); // UNC 路径拉进程不快,等三个都就位挂上 go
    SetEvent(go);
    Sleep(300); // 留足时间让第三个 waiter 堵在 WaitForSingleObject 上
    SetLastError(0);
    LONG prev = -1;
    BOOL rel = ReleaseSemaphore(sem, 1, &prev);
    printf("[父] 300ms 后 ReleaseSemaphore(1) -> ret=%d prev=%ld err=%lu(0=释放前名额已被两个 "
           "waiter 占光)\n",
           rel, prev, GetLastError());
    Sleep(100);
    SetEvent(done); // 收工令:三个都放行退出
    HANDLE all[3] = {pi[0].hProcess, pi[1].hProcess, pi[2].hProcess};
    WaitForMultipleObjects(3, all, TRUE, INFINITE);
    for (int i = 0; i < 3; ++i) {
        uint64_t* slot = (uint64_t*)(base + OFF_TIMELINE + 32 * i);
        printf("[父] waiter%llu 排队 %llu 微秒后过闸\n", (unsigned long long)slot[0],
               (unsigned long long)(slot[1] / 1000));
        CloseHandle(pi[i].hThread);
        CloseHandle(pi[i].hProcess);
    }
    printf("[父] 前两个瞬时过闸、第三个干等 300ms+——计数信号量跨进程管并发名额\n");
    UnmapViewOfFile(base);
    CloseHandle(hm);
    CloseHandle(sem);
    CloseHandle(go);
    CloseHandle(done);
    fflush(stdout);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3) {
        DWORD ppid = (DWORD)strtoul(argv[2], nullptr, 10);
        if (strcmp(argv[1], "mutex-worker") == 0) {
            return run_mutex_worker(ppid, strcmp(argv[3], "lock") == 0);
        }
        if (strcmp(argv[1], "abandoner") == 0) {
            return run_abandoner(ppid);
        }
        if (strcmp(argv[1], "event-waiter") == 0) {
            return run_event_waiter(ppid, atoi(argv[3]), strcmp(argv[4], "manual") == 0);
        }
        if (strcmp(argv[1], "sem-waiter") == 0) {
            return run_sem_waiter(ppid, atoi(argv[3]));
        }
    }
    if (argc >= 2 && strcmp(argv[1], "mutex") == 0) {
        return mode_mutex(argc >= 3 && strcmp(argv[2], "abandoned") == 0);
    }
    if (argc >= 2 && strcmp(argv[1], "event") == 0) {
        return mode_event();
    }
    if (argc >= 2 && strcmp(argv[1], "semaphore") == 0) {
        return mode_semaphore();
    }
    printf("用法:e3_named_sync.exe mutex|mutex abandoned|event|semaphore\n");
    return 1;
}
