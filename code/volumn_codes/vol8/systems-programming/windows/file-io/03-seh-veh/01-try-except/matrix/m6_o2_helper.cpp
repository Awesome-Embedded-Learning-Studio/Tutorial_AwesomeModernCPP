// m6:__try1 放在普通函数(非 main)里,-O2 能不能过
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION m6_filter(void* ep, void*, PCONTEXT, void*) {
    PEXCEPTION_RECORD rec = (PEXCEPTION_RECORD)((void**)ep)[0];
    printf("  [filter] code=0x%08lX\n", (unsigned long)rec->ExceptionCode);
    return (EXCEPTION_DISPOSITION)1;
}

int guard_demo() {
    __try1(m6_filter) RaiseException(0xE0006606, 0, 0, NULL);
    __except1 printf("  [handler] landed\n");
    return 0;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    guard_demo();
    printf("done\n");
    return 0;
}
