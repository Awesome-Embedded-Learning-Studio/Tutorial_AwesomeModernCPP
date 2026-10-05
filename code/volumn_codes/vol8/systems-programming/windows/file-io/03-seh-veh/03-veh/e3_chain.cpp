// e3:VEH(向量化异常处理)—— 链序 + 与 SEH/UEH 的完整时序
//
// 关键事实:
//   * AddVectoredExceptionHandler(1, h):插链头 —— 后注册的先跑
//   * AddVectoredExceptionHandler(0, h):插链尾 —— 在所有 First=1 的后面
//   * VEH 在【任何】SEH __except 之前、也在未处理过滤器(UEH)之前
// 本程序两个场景:
//   场景 A:三个 VEH + 一个 SEH __try1 作用域 → 期望 veh2→veh1→veh3→SEH filter→处理块
//   场景 B:三个 VEH + 无 SEH + SetUnhandledExceptionFilter → veh2→veh1→veh3→UEH→进程收场
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static int seq = 0;

static void tag(const char* who) {
    printf("  #%d %s\n", ++seq, who);
}

static LONG WINAPI veh1(PEXCEPTION_POINTERS ep) {
    tag("VEH-veh1(先注册,First=1)");
    printf("     veh1 看到 code=0x%08lX\n", (unsigned long)ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI veh2(PEXCEPTION_POINTERS ep) {
    (void)ep;
    tag("VEH-veh2(后注册,First=1,插到 veh1 前面)");
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI veh3(PEXCEPTION_POINTERS ep) {
    (void)ep;
    tag("VEH-veh3(First=0,排链尾)");
    return EXCEPTION_CONTINUE_SEARCH;
}

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION chain_filter(void* ep, void*, PCONTEXT,
                                                                    void*) {
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    printf("     SEH filter 看到 code=0x%08lX\n",
           (unsigned long)pointers->ExceptionRecord->ExceptionCode);
    tag("SEH __try1 的 filter");
    return (EXCEPTION_DISPOSITION)1;
}

static LONG WINAPI ueh(PEXCEPTION_POINTERS ep) {
    tag("UEH(SetUnhandledExceptionFilter)");
    printf("     UEH 看到 code=0x%08lX,返回 EXECUTE_HANDLER\n",
           (unsigned long)ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}

// 场景 A:受保护区里炸,SEH 接住
__attribute__((noinline)) void scene_a(volatile int* p, int* result) {
    int faulted = 1;
    __try1(chain_filter)* result = *p;
    faulted = 0;
    __except1 if (faulted)* result = -1;
}

// 场景 B:没有 SEH,一路裸奔到 UEH
__attribute__((noinline)) void scene_b(volatile int* p, int* result) {
    *result = *p; // 无人接住:VEH 们 → UEH
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, veh1);
    AddVectoredExceptionHandler(1, veh2); // 插到 veh1 前面
    AddVectoredExceptionHandler(0, veh3); // 排到链尾

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    LPVOID page = VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_NOACCESS);

    printf("== 场景 A:AV + SEH __try1 在场 ==\n");
    int result = 0;
    scene_a((volatile int*)page, &result);
    printf("  -> result=%d(SEH 处理块跑完)\n", result);

    printf("== 场景 B:AV + 无 SEH,只有 UEH ==\n");
    SetUnhandledExceptionFilter(ueh);
    scene_b((volatile int*)page, &result);
    printf("  (这行不会被执行:UEH EXECUTE_HANDLER 之后进程直接收场)\n");
    return 0;
}
