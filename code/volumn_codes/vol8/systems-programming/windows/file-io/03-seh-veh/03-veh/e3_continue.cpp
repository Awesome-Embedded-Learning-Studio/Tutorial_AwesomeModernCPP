// e3b:VEH 的 EXCEPTION_CONTINUE_EXECUTION —— 修好现场,原指令重放
// 剧本:一页先改回 PAGE_READONLY,向它写 → 0xC0000005(write);
//       VEH 里 VirtualProtect 改回可写 → 返回 CONTINUE_EXECUTION → 那条写指令重放成功
// 对照:不修现场硬返回 CONTINUE_EXECUTION → 原地打转(数到 3 次放弃,放行去崩)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static int hits = 0;
static void* expect_page = nullptr;
static int mode = 0; // 0=修现场 1=不修,硬continue

static LONG WINAPI repair_veh(PEXCEPTION_POINTERS ep) {
    PEXCEPTION_RECORD r = ep->ExceptionRecord;
    ++hits;
    printf("  [VEH hit %d] code=0x%08lX info[0]=%llu(%s) info[1]=0x%llX\n", hits,
           (unsigned long)r->ExceptionCode, (unsigned long long)r->ExceptionInformation[0],
           r->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
           (unsigned long long)r->ExceptionInformation[1]);
    if (r->ExceptionCode != 0xC0000005 || r->ExceptionInformation[0] != 1 ||
        (void*)r->ExceptionInformation[1] != expect_page) {
        printf("     不是等的那一笔,放行\n");
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (mode == 0) {
        DWORD old = 0;
        VirtualProtect(expect_page, 4096, PAGE_READWRITE, &old);
        printf("     修现场:VirtualProtect -> PAGE_READWRITE,返回 CONTINUE_EXECUTION\n");
        return EXCEPTION_CONTINUE_EXECUTION; // 原写指令重放
    }
    if (hits < 3) {
        printf("     不修现场,硬返回 CONTINUE_EXECUTION(同一个错会再来)\n");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    printf("     打转 3 次了,放行让它崩\n");
    return EXCEPTION_CONTINUE_SEARCH;
}

volatile int sink = 0;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, repair_veh);

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    char* page = (char*)VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
    DWORD old = 0;
    VirtualProtect(page, si.dwPageSize, PAGE_READONLY, &old);
    expect_page = page;
    printf("page=%p 当前 PAGE_READONLY\n", page);

    printf("== 模式 0:VEH 修好现场再 CONTINUE_EXECUTION ==\n");
    mode = 0;
    hits = 0;
    *(volatile int*)page = 0x1234; // 写只读页 → AV → VEH 修 → 重放成功
    printf("  写指令重放成功:*(int*)page = 0x%x\n", *(volatile int*)page);

    printf("== 模式 1:不修现场硬 CONTINUE_EXECUTION(原地打转,3 次后放行)==\n");
    mode = 1;
    hits = 0;
    VirtualProtect(page, si.dwPageSize, PAGE_READONLY, &old);
    printf("  (这行之后进程应当死掉)\n");
    *(volatile int*)page = 0x5678;
    printf("  不可达\n");
    return 0;
}
