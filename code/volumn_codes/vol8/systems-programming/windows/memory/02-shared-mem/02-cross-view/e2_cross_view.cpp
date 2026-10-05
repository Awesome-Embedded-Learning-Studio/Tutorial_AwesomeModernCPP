// e2_cross_view.cpp —— E2 跨进程视图:同名对象两进程各自映射,偏移才是通用语言
//
// 编译(WSL 里以相对路径调 MSYS2 UCRT64 g++,cwd 必须在 WSL 文件系统上):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2_cross_view.cpp -o
//   e2_cross_view.exe
// 运行(父进程自动用 CreateProcessW 拉起子进程,一条命令跑完整套):
//   chmod +x e2_cross_view.exe && ./e2_cross_view.exe
//
// 观察点:
//   (1) A 写 B 读:同名对象,父进程 Create 子进程 Open,各自 MapViewOfFile,
//       父在偏移 0x1000 写 0xC0FFEE,子在自己基址上读回同一值——共享的是"对象内容",
//       不是"地址"
//   (2) 指针不能跨进程:父把自己槽位的"指针值"(父基址+0x1000)当作数字写进共享内存,
//       子读出这个数字,与自己的(子基址+0x1000)并排打印——两串数字对不上;子进程
//       VirtualQuery 那个地址:在自己空间里 State=MEM_FREE;另派一个平级的探针进程真解引用一次,
//       拿退出码 0xC0000005 死给我们看(GCC 没有 __try/__except,隔进程撞最干净)
//   (3) 基址谁也不保证:子进程先映射一块无关的 16MiB 匿名区再开目标对象,两个进程的
//       视图基址就分道扬镳——分配历史不同,基址就不同;协议里只能有偏移
//   (4) MapViewOfFile 的 offset 必须 64KB(allocation granularity)对齐:offset=4096 → 失败,
//       错误码如实记;offset=0x10000+长度 4KB → 成功,长度不必对齐(对照 Linux mmap 的
//       offset 按页 4KB 对齐,那边宽 16 倍)
//   (5) 越界视图:请求长度 512KB 超过 256KB 对象 → 调用直接被拒(err 如实记,实测 5),
//       不会给你截断成 256KB 的短视图;另派平级的探针进程实撞验证
//   (6) 跨进程的 E1 复盘:父关光句柄,子还拿一个 → 名字还活着;子也关了(只剩视图) →
//       名字立刻没了,而子的视图照常读写——名字跟着"最后一个句柄"走,不分进程

#include "../common/shm_util.hpp"
#include <cstdlib>
#include <cstring>

// ---- VirtualQuery 探地址状态:不触碰字节本身,永远不会异常 ----
static const char* state_name(SIZE_T s) {
    switch (s) {
        case MEM_COMMIT:
            return "MEM_COMMIT";
        case MEM_RESERVE:
            return "MEM_RESERVE";
        case MEM_FREE:
            return "MEM_FREE";
        default:
            return "(other)";
    }
}

struct ShmLayout {                                    // 全部用"对象内偏移"当协议,没有指针字段
    static constexpr uint32_t OFF_VALUE = 0x1000;     // 父写 uint64 0xC0FFEE
    static constexpr uint32_t OFF_PTRVAL = 0x1008;    // 父写的"父侧指针值"(纯数字)
    static constexpr uint32_t OFF_CHILDBASE = 0x1010; // 子写的"子侧基址值"(纯数字)
    static constexpr uint32_t OFF_ECHO = 0x20000;     // 子回写的校验值
    static constexpr uint32_t OFF_SLICE_A = 0x10000;  // 64KB 对齐切片演示区
    static constexpr uint32_t SIZE = 0x40000;         // 对象总大小 256KB
};

// ---------------- 子进程:argv = e2_cross_view.exe child <父pid> ----------------
static int run_child(DWORD ppid) {
    const std::wstring name = local_name(L"E2", ppid);
    const std::wstring wready = L"Local\\SysProgShm_E2r_" + std::to_wstring(ppid);
    const std::wstring wgo = L"Local\\SysProgShm_E2g_" + std::to_wstring(ppid);
    const std::wstring wdone = L"Local\\SysProgShm_E2d_" + std::to_wstring(ppid);
    const std::wstring wstage = L"Local\\SysProgShm_E2stage_" + std::to_wstring(ppid);

    HANDLE hm = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!hm) {
        printf("[子] OpenFileMappingW 失败 err=%lu\n", GetLastError());
        return 1;
    }
    HANDLE hready = OpenEventW(EVENT_MODIFY_STATE, FALSE, wready.c_str());
    HANDLE hgo = OpenEventW(SYNCHRONIZE, FALSE, wgo.c_str());
    HANDLE hdone = OpenEventW(EVENT_MODIFY_STATE, FALSE, wdone.c_str());
    HANDLE hstage = OpenEventW(SYNCHRONIZE, FALSE, wstage.c_str());

    // (3) 先映一块无关的 16MiB 匿名区,让本进程的地址空间分配历史与父进程不同
    HANDLE dummy =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 16u << 20, nullptr);
    unsigned char* dv = (unsigned char*)MapViewOfFile(dummy, FILE_MAP_WRITE, 0, 0, 0);
    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    printf("[子] pid=%lu,先映一块无关 16MiB 区 base=%p,再映目标对象 base=%p\n",
           GetCurrentProcessId(), dv, base);
    *(uint64_t*)(base + ShmLayout::OFF_CHILDBASE) = (uint64_t)(uintptr_t)base;
    fflush(stdout);
    SetEvent(hready);
    WaitForSingleObject(hgo, INFINITE);

    // (1) A 写 B 读
    uint64_t v = *(uint64_t*)(base + ShmLayout::OFF_VALUE);
    // (2) 指针值对指针值
    uint64_t parent_ptr = *(uint64_t*)(base + ShmLayout::OFF_PTRVAL);
    uint64_t child_ptr = (uint64_t)(uintptr_t)(base + ShmLayout::OFF_VALUE);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery((void*)(uintptr_t)parent_ptr, &mbi, sizeof mbi);
    printf("[子] 读偏移 0x1000 的值 = 0x%llX(A 写 B 读成立)\n", (unsigned long long)v);
    printf("[子] 父指针值=0x%llX  我的同槽地址=0x%llX  相差 %+lli\n",
           (unsigned long long)parent_ptr, (unsigned long long)child_ptr,
           (long long)(child_ptr - parent_ptr));
    printf("[子] VirtualQuery(父指针值) 在我的空间里:State=%s —— 这地址在我这儿%s\n",
           state_name(mbi.State),
           mbi.State == MEM_FREE ? "根本没映射,解引用必 0xC0000005(父进程另派了探针进程实撞)"
                                 : "碰巧有别的东西,解引用更是读到无关字节");
    fflush(stdout);

    // (4) 子也做一次 64KB 对齐探针
    SetLastError(0);
    void* mis = MapViewOfFile(hm, FILE_MAP_READ, 0, 4096, 8192);
    printf("[子] MapViewOfFile(offset=4096) -> %p err=%lu(对齐规则对谁都是同一套)\n", mis,
           GetLastError());
    if (mis) {
        UnmapViewOfFile(mis);
    }

    // (4b) 读父写在切片 A 的内容(经全景视图)
    uint64_t slice = *(uint64_t*)(base + ShmLayout::OFF_SLICE_A);
    printf("[子] 经全景视图读偏移 0x10000:0x%llX\n", (unsigned long long)slice);
    *(uint64_t*)(base + ShmLayout::OFF_ECHO) = v ^ slice ^ 0xABCD;
    fflush(stdout);
    SetEvent(hdone);

    // (6) 阶段一:父要求子关句柄
    WaitForSingleObject(hstage, INFINITE);
    CloseHandle(hm);
    printf("[子] CloseHandle(映射句柄) —— 只剩视图,对象还有我一根引用\n");
    fflush(stdout);
    SetEvent(hdone);

    // (6) 阶段二:名字应该已经没了,这里证明对象还活着
    WaitForSingleObject(hstage, INFINITE);
    *(uint64_t*)(base + ShmLayout::OFF_ECHO) = 0x1234ABCD;
    printf("[子] 句柄已关,视图写偏移 0x20000 = 0x%llX 仍成功(对象匿名续命中)\n",
           (unsigned long long)*(uint64_t*)(base + ShmLayout::OFF_ECHO));
    fflush(stdout);
    SetEvent(hdone);

    UnmapViewOfFile(base);
    UnmapViewOfFile(dv);
    CloseHandle(dummy);
    CloseHandle(hready);
    CloseHandle(hgo);
    CloseHandle(hdone);
    CloseHandle(hstage);
    return 0;
}

// ---------------- 探针进程(父进程 spawn_self 直接拉起,与 child 平级):argv = e2_cross_view.exe
// probe <父pid> ptr|pastend ---------------- 它的使命就是撞一次 0xC0000005 死给我们看(GCC 没有
// __try/__except,隔进程撞最干净)
static int run_probe(DWORD ppid, const char* mode) {
    const std::wstring name = local_name(L"E2", ppid);
    HANDLE hm = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!hm) {
        printf("[探针] 打开对象失败 err=%lu\n", GetLastError());
        return 1;
    }
    if (strcmp(mode, "ptr") == 0) {
        unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        uint64_t parent_ptr = *(uint64_t*)(base + ShmLayout::OFF_PTRVAL);
        printf("[探针-ptr] 读到父指针值 0x%llX,现在真的去解引用它...\n",
               (unsigned long long)parent_ptr);
        fflush(stdout);
        volatile unsigned char sink = *(volatile unsigned char*)(uintptr_t)parent_ptr;
        printf("[探针-ptr] 意外没崩,读到 0x%02X(这说明该地址在本进程碰巧有映射)\n", sink);
        fflush(stdout);
        return 0;
    }
    // pastend:映射超长视图,摸对象末尾后一字节
    unsigned char* big = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0x80000);
    printf("[探针-pastend] 超长视图 base=%p,摸偏移 0x40000(对象只有 0x40000)...\n", big);
    fflush(stdout);
    if (!big) {
        printf("[探针-pastend] 视图都没给 err=%lu\n", GetLastError());
        return 0;
    }
    volatile unsigned char sink = big[ShmLayout::SIZE];
    printf("[探针-pastend] 意外没崩,读到 0x%02X\n", sink);
    fflush(stdout);
    return 0;
}

// ---------------- 父进程:不带参数直接跑 ----------------
int main(int argc, char** argv) {
    if (argc == 4 && strcmp(argv[1], "probe") == 0) {
        return run_probe((DWORD)strtoul(argv[2], nullptr, 10), argv[3]);
    }
    if (argc == 3 && strcmp(argv[1], "child") == 0) {
        return run_child((DWORD)strtoul(argv[2], nullptr, 10));
    }

    const DWORD pid = GetCurrentProcessId();
    const std::wstring name = local_name(L"E2", pid);
    const std::wstring wready = L"Local\\SysProgShm_E2r_" + std::to_wstring(pid);
    const std::wstring wgo = L"Local\\SysProgShm_E2g_" + std::to_wstring(pid);
    const std::wstring wdone = L"Local\\SysProgShm_E2d_" + std::to_wstring(pid);
    const std::wstring wstage = L"Local\\SysProgShm_E2stage_" + std::to_wstring(pid);

    printf("[E2] 跨进程视图,父 pid=%lu,对象=Local\\SysProgShm_E2_%lu(256KB)\n", pid, pid);
    HANDLE hm = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   ShmLayout::SIZE, name.c_str());
    if (!hm) {
        printf("[父] CreateFileMappingW 失败 err=%lu\n", GetLastError());
        return 1;
    }
    HANDLE hready = CreateEventW(nullptr, FALSE, FALSE, wready.c_str());
    HANDLE hgo = CreateEventW(nullptr, FALSE, FALSE, wgo.c_str());
    HANDLE hdone = CreateEventW(nullptr, FALSE, FALSE, wdone.c_str());
    HANDLE hstage = CreateEventW(nullptr, FALSE, FALSE, wstage.c_str());

    unsigned char* base = (unsigned char*)MapViewOfFile(hm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    printf("[父] 全景视图 base=%p  dwAllocationGranularity=%lu(0x%lX)\n", base,
           si.dwAllocationGranularity, si.dwAllocationGranularity);
    fflush(stdout);

    PROCESS_INFORMATION pi{};
    if (!spawn_self((L"child " + std::to_wstring(pid)).c_str(), pi)) {
        printf("[父] 拉子进程失败 err=%lu\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(hready, INFINITE);

    uint64_t child_base = *(uint64_t*)(base + ShmLayout::OFF_CHILDBASE);
    printf("[父] 子进程基址 = 0x%llX,我的基址 = 0x%llX,%s\n", (unsigned long long)child_base,
           (unsigned long long)(uintptr_t)base,
           child_base == (uint64_t)(uintptr_t)base ? "本轮碰巧相同——碰巧相同也不能写进协议"
                                                   : "两边不同——这就是只能传偏移的原因");

    // (1) 写值;(2) 写"父侧指针值"
    *(uint64_t*)(base + ShmLayout::OFF_VALUE) = 0xC0FFEE;
    *(uint64_t*)(base + ShmLayout::OFF_PTRVAL) = (uint64_t)(uintptr_t)(base + ShmLayout::OFF_VALUE);
    printf("[父] 偏移 0x1000 写 0xC0FFEE;偏移 0x1008 写父侧指针值 0x%llX\n",
           (unsigned long long)(uintptr_t)(base + ShmLayout::OFF_VALUE));
    fflush(stdout);

    // (4) offset 对齐矩阵
    SetLastError(0);
    void* mis = MapViewOfFile(hm, FILE_MAP_READ, 0, 4096, 8192);
    printf("[父] MapViewOfFile(offset=4096, len=8192) -> %p err=%lu(offset 不按 64KB 对齐的下场)\n",
           mis, GetLastError());
    if (mis) {
        UnmapViewOfFile(mis);
    }
    SetLastError(0);
    unsigned char* slice =
        (unsigned char*)MapViewOfFile(hm, FILE_MAP_WRITE, 0, ShmLayout::OFF_SLICE_A, 4096);
    printf("[父] MapViewOfFile(offset=0x10000, len=4096) -> %p err=%lu(64KB 对齐;长度不必对齐)\n",
           slice, GetLastError());
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(slice, &mbi, sizeof mbi);
    printf("[父] 4KB 切片视图 VirtualQuery:State=%s Protect=0x%lX RegionSize=0x%zX"
           "(长度按页取整就是 4KB;基址 %p 仍落在 64KB 边界——粒度管 offset 与基址,不管长度)\n",
           state_name(mbi.State), (unsigned long)mbi.Protect, mbi.RegionSize, (void*)slice);
    *(uint64_t*)slice = 0xFEEDFACE;

    // (5) 越界视图:请求 512KB,对象只有 256KB
    SetLastError(0);
    unsigned char* big = (unsigned char*)MapViewOfFile(hm, FILE_MAP_WRITE, 0, 0, 0x80000);
    printf("[父] MapViewOfFile(offset=0, len=512KB 超对象) -> %p err=%lu"
           "(不给截断的短视图,整个请求直接拒绝)\n",
           big, GetLastError());
    if (big) {
        VirtualQuery(big, &mbi, sizeof mbi);
        printf("[父] 越界视图 VirtualQuery:RegionSize=0x%zX\n", mbi.RegionSize);
        VirtualQuery(big + ShmLayout::SIZE - 1, &mbi, sizeof mbi);
        printf("[父] 探对象末字节(0x3FFFF):State=%s Protect=0x%lX\n", state_name(mbi.State),
               (unsigned long)mbi.Protect);
        VirtualQuery(big + ShmLayout::SIZE, &mbi, sizeof mbi);
        printf("[父] 探对象外一字节(0x40000):State=%s %s\n", state_name(mbi.State),
               mbi.State == MEM_FREE ? "—— 内核没给多的,视图到对象边界为止" : "");
        UnmapViewOfFile(big);
    }
    fflush(stdout);

    // (2b)+(5b) 两个真撞探针:派平级的探针进程去解引用父指针 / 摸对象外一字节
    static const wchar_t* const probe_modes[2] = {L"ptr", L"pastend"};
    for (int i = 0; i < 2; ++i) {
        PROCESS_INFORMATION p2{};
        if (!spawn_self((L"probe " + std::to_wstring(pid) + L" " + probe_modes[i]).c_str(), p2)) {
            printf("[父] 探针 %ls 拉起失败 err=%lu\n", probe_modes[i], GetLastError());
            continue;
        }
        WaitForSingleObject(p2.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(p2.hProcess, &code);
        printf("[父] 探针(%ls)退出码=%lu(0x%08lX)%s\n", probe_modes[i], code, code,
               code == 3221225477 ? " —— STATUS_ACCESS_VIOLATION,和预告的一样" : "");
        CloseHandle(p2.hThread);
        CloseHandle(p2.hProcess);
        fflush(stdout);
    }

    SetEvent(hgo); // 子进程开读
    WaitForSingleObject(hdone, INFINITE);
    uint64_t expect = 0xC0FFEEull ^ 0xFEEDFACEull ^ 0xABCDull;
    uint64_t echo = *(uint64_t*)(base + ShmLayout::OFF_ECHO);
    printf("[父] 子回写校验 echo=0x%llX(期望 0x%llX)%s\n", (unsigned long long)echo,
           (unsigned long long)expect, echo == expect ? " —— 偏移协议全程自洽" : "?!");

    // (6) 跨进程生命周期复盘
    printf("\n[父] 跨进程生命周期复盘(对照 E1 两段论)\n");
    CloseHandle(hm);
    printf("[父] 我关掉自己的映射句柄(子进程还拿着一个)→ 按名打开: ");
    SetLastError(0);
    HANDLE hreopen = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    printf("%p err=%lu —— 别的进程的句柄也吊着名字\n", hreopen, GetLastError());
    if (hreopen) {
        CloseHandle(hreopen);
    }
    printf("[父] 通知子进程关句柄...\n");
    fflush(stdout);
    SetEvent(hstage);
    WaitForSingleObject(hdone, INFINITE);
    SetLastError(0);
    hreopen = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    printf("[父] 子也关光句柄后按名打开:%p err=%lu —— 名字没了(子进程手里只剩视图)\n", hreopen,
           GetLastError());
    fflush(stdout);
    SetEvent(hstage);
    WaitForSingleObject(hdone, INFINITE);
    printf("[父] 子进程视图写回的偏移 0x20000 = 0x%llX —— 名字亡了,对象还活着\n",
           (unsigned long long)*(uint64_t*)(base + ShmLayout::OFF_ECHO));
    fflush(stdout);

    UnmapViewOfFile(slice);
    WaitForSingleObject(pi.hProcess, INFINITE);
    UnmapViewOfFile(base); // hm 在 (6) 已关,这里只撒视图这根引用
    CloseHandle(hready);
    CloseHandle(hgo);
    CloseHandle(hdone);
    CloseHandle(hstage);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("[E2] 完\n");
    fflush(stdout);
    return 0;
}
