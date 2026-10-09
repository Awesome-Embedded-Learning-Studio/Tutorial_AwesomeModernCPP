// e3b:系统在用 guard page 干正事 —— 线程栈的自动扩展与溢出探测
// 用法:./e3b_stack_guard.exe layout   —— 拍主线程栈的区段布局(已提交/guard 页/保留),
//      递归压栈 48 层 x 16 KiB 再拍一张:guard 页地址随之下移(栈长大 = 提交推进 + guard 重埋)
//      ./e3b_stack_guard.exe overflow —— 开一个 256KiB 栈的工作线程往深里递归,
//      VEH 应收到 0xC00000FD(STATUS_STACK_OVERFLOW):预约耗尽后 guard 陷阱变成硬溢出
// 关键观察:递归扩展期间用户态 VEH 一次都不响 —— 栈扩展是内核在 guard 异常上静默完成的
// 注意:栈向低地址生长,guard 页在"已提交区"的下方,遍历方向是往下(lo - 4 探下一区段)
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstdio>
#include <windows.h>

static int veh_hits = 0;
static volatile long depth_now = 0;

static LONG WINAPI veh(PEXCEPTION_POINTERS ep) {
    PEXCEPTION_RECORD r = ep->ExceptionRecord;
    ++veh_hits;
    printf("  [VEH] code=0x%08lX info[1]=0x%llX depth=%ld\n", (unsigned long)r->ExceptionCode,
           (unsigned long long)r->ExceptionInformation[1], depth_now);
    if (r->ExceptionCode == 0x80000001)
        return EXCEPTION_CONTINUE_EXECUTION; // 防御:guard 一律放行重放
    if (r->ExceptionCode == 0xC00000FD) {
        printf("  [VEH] STATUS_STACK_OVERFLOW —— 栈预约耗尽,放行让它收场(进程将崩)\n");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static const char* state_str(DWORD s) {
    switch (s) {
        case MEM_COMMIT:
            return "COMMIT";
        case MEM_RESERVE:
            return "RESERVE";
        case MEM_FREE:
            return "FREE";
    }
    return "?";
}

// 从栈上取样点出发向下(低地址)逐页采样再分组:COMMIT 段 → guard 页 → RESERVE 段 → FREE。
// 为什么逐页:VirtualQuery 的 BaseAddress 是"探测页下取整"、RegionSize 是"该页到区段尾的
// 剩余量"(探针不在区段头时不是整段大小,README 的坑),逐页采样最稳
static void dump_stack_layout(const char* tag, void* probe_addr, void** guard_out,
                              unsigned long long* total_out) {
    printf("  -- %s --\n", tag);
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(probe_addr, &mbi, sizeof(mbi)))
        return;
    unsigned long long top =
        ((unsigned long long)(uintptr_t)probe_addr & ~0xFFFULL) + mbi.RegionSize; // 栈方向高沿
    if (total_out && mbi.AllocationBase)
        *total_out = top - (unsigned long long)(uintptr_t)mbi.AllocationBase; // 本栈预约总量
    void* own_ab = mbi.AllocationBase; // 只看本栈:AllocBase 一变就到邻栈了
    struct Pg {
        DWORD st, pr;
        void* ab;
    } pages[1024];
    int np = 0;
    unsigned long long q = top;
    for (int i = 0; i < 1024; i++) {
        q -= 0x1000;
        if (!VirtualQuery((void*)(uintptr_t)q, &mbi, sizeof(mbi)))
            break;
        if (mbi.State != MEM_FREE && mbi.AllocationBase != own_ab)
            break;
        pages[np++] = {mbi.State, mbi.Protect, mbi.AllocationBase};
        if (mbi.State == MEM_FREE)
            break;
    }
    if (guard_out)
        *guard_out = nullptr;
    unsigned long long run_hi = top; // 当前分组的高沿
    int printed = 0;
    for (int i = 0; i < np && printed < 6; i++) {
        bool last = (i == np - 1) || pages[i + 1].st != pages[i].st ||
                    pages[i + 1].pr != pages[i].pr || pages[i + 1].ab != pages[i].ab;
        if (!last)
            continue;
        unsigned long long lo = top - (unsigned long long)(i + 1) * 0x1000;
        char guard[32];
        snprintf(guard, sizeof(guard), "%s", pages[i].pr & PAGE_GUARD ? "+GUARD" : "");
        printf("  %012llx..%012llx %-7s %#06lx%s(%d页) AllocBase=%p%s\n", lo, run_hi,
               state_str(pages[i].st), pages[i].pr, guard, i + 1 - (int)((top - run_hi) / 0x1000),
               pages[i].ab, pages[i].pr & PAGE_GUARD ? "  <-- 栈的 guard 带" : "");
        if ((pages[i].pr & PAGE_GUARD) && guard_out)
            *guard_out = (void*)(uintptr_t)lo;
        run_hi = lo;
        printed++;
    }
}

__attribute__((noinline)) static int burn(int depth) {
    volatile char pad[16384]; // 每层吃 16 KiB 栈
    pad[0] = (char)depth;
    pad[16383] = (char)depth;
    depth_now = depth;
    if (depth <= 0)
        return pad[0];
    return burn(depth - 1) + pad[16383];
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion" // 剧本就是要递归到栈爆
__attribute__((noinline)) static int dive_forever(int depth) {
    volatile char pad[16384];
    pad[0] = (char)depth;
    pad[16383] = (char)depth;
    depth_now = depth;
    return dive_forever(depth + 1) + pad[0];
}
#pragma GCC diagnostic pop

static DWORD WINAPI worker(void*) {
    ULONG keep = 64 * 1024; // 给异常处理留保底栈,VEH 里才能 printf
    SetThreadStackGuarantee(&keep);
    int local = 0;
    unsigned long long total = 0;
    printf("  工作线程开跑,先拍自己的栈布局:\n");
    dump_stack_layout("工作线程栈(请求预约 256 KiB)", &local, nullptr, &total);
    printf("  预约合计=%llu KiB —— 实际以 VQ 读数为准(CreateThread 的取整口径见 README)\n",
           total >> 10);
    printf("  开始无限递归...\n");
    dive_forever(0);
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* mode = argc > 1 ? argv[1] : "layout";
    AddVectoredExceptionHandler(1, veh);

    if (lstrcmpiA(mode, "layout") == 0) {
        int dummy = 0;
        void* here = &dummy;
        void* g0 = nullptr;
        unsigned long long total0 = 0;
        printf("mode=layout  栈上取样点=%p(主线程,预约总量以 VQ 读数为准)\n", here);
        dump_stack_layout("压栈前", here, &g0, &total0);
        printf("  guard 带=%p  本栈预约合计=%llu KiB\n\n", g0, total0 >> 10);
        int hits0 = veh_hits;
        burn(48); // 48 层 x 16 KiB = 768 KiB
        printf("递归 48 层(每层 16 KiB,共 768 KiB)平安回来,期间 VEH 命中=%d\n\n", veh_hits - hits0);
        void* g1 = nullptr;
        unsigned long long total1 = 0;
        dump_stack_layout("压栈后", here, &g1, &total1);
        long long moved = (long long)((char*)g0 - (char*)g1);
        printf("  guard 带=%p,较压栈前 %s %lld KiB —— 提交推进到哪,guard 重埋到哪\n", g1,
               moved > 0   ? "下移"
               : moved < 0 ? "上移"
                           : "未动",
               (moved < 0 ? -moved : moved) >> 10);
    } else if (lstrcmpiA(mode, "overflow") == 0) {
        printf("mode=overflow\n");
        HANDLE h = CreateThread(NULL, 256 * 1024, worker, NULL, 0, NULL);
        WaitForSingleObject(h, INFINITE);
        printf("  不可达\n");
    }
    return 0;
}
