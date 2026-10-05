// 探针 7:filter 的返回地址落在哪个模块 -> 谁在调 filter
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

static void modreport(const char* tag, void* p) {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)p, &m);
    wchar_t name[MAX_PATH] = L"?";
    if (m)
        GetModuleFileNameW(m, name, MAX_PATH);
    printf("  %s %p -> module %p (%ls)\n", tag, p, (void*)m, name);
}

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION dbg_filter(PEXCEPTION_RECORD rec,
                                                                  void* frame, PCONTEXT ctx,
                                                                  void* disp) {
    void* ret = __builtin_return_address(0);
    printf("  [filter] rec=%p frame=%p ctx=%p disp=%p ret=%p\n", (void*)rec, frame, (void*)ctx,
           disp, ret);
    modreport("ret", ret);
    modreport("ntdll!__C_specific_handler",
              (void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "__C_specific_handler"));
    modreport("exe!thunk", (void*)0x1400082b0 + (long long)((char*)frame - (char*)frame)); // 占位
    HMODULE exe = GetModuleHandleW(nullptr);
    printf("  exe base=%p  ntdll __C_sh=%p\n", (void*)exe,
           (void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "__C_specific_handler"));
    return (EXCEPTION_DISPOSITION)1;
}

int main() {
    __try1(dbg_filter) RaiseException(0xE0001234, 0, 0, NULL);
    __except1 printf("  [handler] landed\n");
    return 0;
}
