// e1c:异常码从哪来 —— 硬件异常(int3/除零)与软件自定码(RaiseException)
// GetExceptionCode() 的等价物:filter 里从 ExceptionRecord->ExceptionCode 直接读
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static const char* code_name(DWORD c) {
    switch (c) {
        case 0x80000003:
            return "STATUS_BREAKPOINT (int3)";
        case 0xC0000094:
            return "STATUS_INTEGER_DIVIDE_BY_ZERO";
        case 0xC0000095:
            return "STATUS_INTEGER_OVERFLOW";
        case 0xE06D7363:
            return "MSC EH (C++ throw)";
        default:
            return "";
    }
}

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION code_filter(void* ep, void* frame,
                                                                   PCONTEXT ctx, void* disp) {
    (void)frame;
    (void)disp;
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    PEXCEPTION_RECORD r = pointers->ExceptionRecord;
    printf("  [filter] code=0x%08lX flags=0x%08lX %s  fault/raise-at=%p nparams=%lu\n",
           (unsigned long)r->ExceptionCode, (unsigned long)r->ExceptionFlags,
           code_name(r->ExceptionCode), r->ExceptionAddress, (unsigned long)r->NumberParameters);
    for (unsigned i = 0; i < r->NumberParameters && i < 4; ++i)
        printf("    info[%u]=0x%016llX\n", i, (unsigned long long)r->ExceptionInformation[i]);
    return (EXCEPTION_DISPOSITION)1;
}

volatile int zero = 0;
volatile int one = 1;

__attribute__((noinline)) void probe(int round, int* result) {
    *result = 0;
    __try1(code_filter) if (round == 0) {
        __asm__ volatile("int3"); // 硬件:陷阱指令
    }
    else if (round == 1) {
        ULONG_PTR args[4] = {0xAAAA1111, 0xAAAA2222, 0xAAAA3333, 0xAAAA4444};
        RaiseException(0xE0001234, 0, 4, args); // 软件:自定码 + 4 个附加参数
    }
    else if (round == 2) {
        *result = one / zero; // 硬件:#DE 除零
    }
    else {
        *result = (int)(0x7FFFFFFF / (one - 1)); // 仍会走到这:int3 后除外
    }
    __except1* result = -1;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int result = 0;
    printf("== int3\n");
    probe(0, &result);
    printf("-> %d\n", result);
    printf("== RaiseException\n");
    probe(1, &result);
    printf("-> %d\n", result);
    printf("== divide by zero\n");
    probe(2, &result);
    printf("-> %d\n", result);
    printf("done\n");
    return 0;
}
