// m9b:filter 返回 -1 时到底发生什么 —— 打印 RIP/ExceptionAddress/flags
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static int hits = 0;

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION m9b_filter(void* ep, void*, PCONTEXT ctx,
                                                                  void*) {
    void** s = (void**)ep;
    PEXCEPTION_RECORD rec = (PEXCEPTION_RECORD)s[0];
    PCONTEXT realctx = (PCONTEXT)s[1];
    ++hits;
    printf("  [hit %d] code=0x%08lX flags=0x%08lX excaddr=%p ctx=%p ctx.Rip=%p real.Rip=%p\n", hits,
           (unsigned long)rec->ExceptionCode, (unsigned long)rec->ExceptionFlags,
           rec->ExceptionAddress, (void*)ctx, (void*)ctx->Rip, (void*)realctx->Rip);
    if (hits >= 3)
        return (EXCEPTION_DISPOSITION)1;
    return (EXCEPTION_DISPOSITION)-1;
}

int guard_demo() {
    __try1(m9b_filter) printf("  [body] raising\n");
    RaiseException(0xE0000999, 0, 0, NULL);
    printf("  [body] AFTER RaiseException returned normally\n");
    __except1 printf("  [handler] landed, hits=%d\n", hits);
    printf("  [guard_demo] after guarded region\n");
    return 0;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    guard_demo();
    printf("done\n");
    return 0;
}
