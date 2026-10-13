// m11:filter 返回 0 = EXCEPTION_CONTINUE_SEARCH:这一帧不接,继续往上找
// (没人接就裸崩 —— 对照 m9 的 1 和 -1)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION m11_filter(void* ep, void*, PCONTEXT,
                                                                  void*) {
    PEXCEPTION_POINTERS p = (PEXCEPTION_POINTERS)ep;
    printf("  [filter] code=0x%08lX, return 0 (CONTINUE_SEARCH)\n",
           (unsigned long)p->ExceptionRecord->ExceptionCode);
    return (EXCEPTION_DISPOSITION)0;
}

__attribute__((noinline)) void guarded(int* r) {
    int faulted = 1;
    __try1(m11_filter)* r = *(volatile int*)0;
    faulted = 0;
    __except1 if (faulted)* r = -1;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int r = 0;
    printf("before\n");
    guarded(&r);
    printf("unreachable\n");
    return 0;
}
