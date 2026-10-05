// 探针 8:ucrtbase __C_specific_handler 的 filter 第 1 参是不是 EXCEPTION_POINTERS 形状
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static LONG WINAPI veh(PEXCEPTION_POINTERS ep) {
    printf("  [veh] record=%p ctx=%p code=0x%08lX\n", (void*)ep->ExceptionRecord,
           (void*)ep->ContextRecord, (unsigned long)ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION dbg_filter(void* mystery, void* frame,
                                                                  PCONTEXT ctx, void* disp) {
    void** s = (void**)mystery;
    printf("  [filter] mystery=%p s[0]=%p s[1]=%p (ctx=%p frame=%p disp=%p)\n", mystery, s[0], s[1],
           (void*)ctx, frame, disp);
    PEXCEPTION_RECORD rec = (PEXCEPTION_RECORD)s[0];
    printf("  s[0]->code=0x%08lX flags=0x%08lX addr=%p nparams=%lu\n",
           (unsigned long)rec->ExceptionCode, (unsigned long)rec->ExceptionFlags,
           rec->ExceptionAddress, (unsigned long)rec->NumberParameters);
    for (unsigned i = 0; i < rec->NumberParameters && i < 4; ++i)
        printf("    info[%u]=0x%016llX\n", i, (unsigned long long)rec->ExceptionInformation[i]);
    return (EXCEPTION_DISPOSITION)1;
}

int main() {
    AddVectoredExceptionHandler(1, veh);
    ULONG_PTR args[2] = {0xDEAD0001, 0xDEAD0002};
    __try1(dbg_filter) RaiseException(0xE0005678, 0, 2, args);
    __except1 printf("  [handler] landed\n");
    return 0;
}
