// e3:PAGE_GUARD —— 一次性陷阱(W02 没讲,W03 的 VEH 工具在这里用上)
// 剧本:
//   初始   VirtualAlloc 直接给一页 PAGE_READWRITE|PAGE_GUARD(VQ 实读 Protect=0x104)
//   第1写  VEH 收到 0x80000001(STATUS_GUARD_PAGE_VIOLATION)→ CONTINUE_EXECUTION
//          → 原写指令重放成功;VQ 再读:Protect 只剩 0x04 —— 陷阱自己拆了
//   第2写  同页同地址:零异常(对照第 1 次)
//   再武装 VirtualProtect 塞回 GUARD 位 → 再写 → 再响一次(可重复埋雷)
// 对照:PAGE_NOACCESS 是持续保护(不改回就一直炸,W03 已测);GUARD 响一次就自灭
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static void* watch = nullptr;
static int hits = 0;

static LONG WINAPI veh(PEXCEPTION_POINTERS ep) {
    PEXCEPTION_RECORD r = ep->ExceptionRecord;
    ++hits;
    printf("  [VEH 命中 %d] code=0x%08lX info[0]=%llu(%s) info[1]=0x%llX 异常指令=%p\n", hits,
           (unsigned long)r->ExceptionCode, (unsigned long long)r->ExceptionInformation[0],
           r->ExceptionInformation[0] == 0   ? "读"
           : r->ExceptionInformation[0] == 1 ? "写"
                                             : "执行",
           (unsigned long long)r->ExceptionInformation[1], r->ExceptionAddress);
    if (r->ExceptionCode == 0x80000001 /* STATUS_GUARD_PAGE_VIOLATION */
        && (void*)r->ExceptionInformation[1] == watch) {
        printf("     是 guard 页的这一笔 → CONTINUE_EXECUTION,原指令重放\n");
        return EXCEPTION_CONTINUE_EXECUTION; // 重放时 GUARD 位已被内核清掉,写会成功
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
static const char* base_prot_name(DWORD p) { // 剥掉修饰位(0x100 GUARD/0x200 NOCACHE/0x400 WC)
    switch (p & 0xFF) {
        case PAGE_NOACCESS:
            return "NOACCESS";
        case PAGE_READONLY:
            return "READONLY";
        case PAGE_READWRITE:
            return "READWRITE";
        case PAGE_WRITECOPY:
            return "WRITECOPY";
        case PAGE_EXECUTE_READWRITE:
            return "EXEC_RW";
    }
    return "?";
}
static void protect_of(const char* tag, void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(p, &mbi, sizeof(mbi));
    printf("  [VQ %-8s] Protect=%#06lx(%s%s) State=%s\n", tag, mbi.Protect,
           base_prot_name(mbi.Protect), mbi.Protect & PAGE_GUARD ? "+GUARD" : "",
           state_str(mbi.State));
}

volatile char sink = 0;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, veh);
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    char* g = (char*)VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    if (!g) {
        printf("分配失败 err=%lu\n", GetLastError());
        return 1;
    }
    watch = g;
    printf("guard 页=%p(0x104 = PAGE_READWRITE|PAGE_GUARD)\n\n", g);

    printf("== 第 1 次写:应当响 ==\n");
    protect_of("写前", g);
    *g = 'A'; // → guard 异常 → VEH → 重放成功
    printf("  重放成功,*g='%c'\n", *g);
    protect_of("写后", g);
    printf("\n== 第 2 次写同页同地址:零异常(GUARD 已自灭)==\n");
    int hits_before = hits;
    *g = 'B';
    printf("  *g='%c',VEH 新增命中=%d(写前 %d → 写后 %d)\n", *g, hits - hits_before, hits_before,
           hits);
    protect_of("再写后", g);

    printf("\n== 再武装:VirtualProtect 塞回 GUARD 位 ==\n");
    DWORD old = 0;
    BOOL ok = VirtualProtect(g, si.dwPageSize, PAGE_READWRITE | PAGE_GUARD, &old);
    printf("  VirtualProtect(...RW|GUARD) -> %d(旧保护=%#lx)\n", ok, old);
    protect_of("再武装后", g);
    *g = 'C';
    printf("  *g='%c'(这一笔应当又响一次)\n", *g);
    protect_of("第三次写后", g);

    VirtualFree(g, 0, MEM_RELEASE);
    printf("\n结论:GUARD 语义 = 触碰即拆的一次性陷阱,要再响得再武装;"
           "NOACCESS 是持续封路(对照)。\n");
    return 0;
}
