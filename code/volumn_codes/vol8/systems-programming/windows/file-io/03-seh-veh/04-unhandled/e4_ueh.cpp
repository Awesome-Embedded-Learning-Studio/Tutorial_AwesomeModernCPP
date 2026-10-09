// e4b:SetUnhandledExceptionFilter 返回 EXCEPTION_EXECUTE_HANDLER 的下场
// UEH 处理器本身跑了,然后进程收场 —— 收场码是多少?由 cmd 的 %ERRORLEVEL% 读
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

static LONG WINAPI ueh(PEXCEPTION_POINTERS ep) {
    printf("  [UEH] code=0x%08lX at rip=%p, returning EXECUTE_HANDLER\n",
           (unsigned long)ep->ExceptionRecord->ExceptionCode,
           ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(ueh);
    printf("ueh: about to write to NULL\n");
    *(volatile int*)nullptr = 0xC5;
    printf("unreachable\n");
    return 0;
}
