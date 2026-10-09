// m10:「析构对象 + __try1 混用」的工程解法 —— 受保护区拆进独立 noinline 函数
// (MSVC C2712 的官方解法同样成立;外层函数照常有析构、照常 throw)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <string>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION m10_filter(void* ep, void*, PCONTEXT,
                                                                  void*) {
    PEXCEPTION_RECORD rec = (PEXCEPTION_RECORD)((void**)ep)[0];
    printf("  [filter] code=0x%08lX\n", (unsigned long)rec->ExceptionCode);
    return (EXCEPTION_DISPOSITION)1;
}

__attribute__((noinline)) int guarded_probe(volatile int* p, int* faulted) {
    int r = 0;
    *faulted = 0;
    __try1(m10_filter) r = *p; // 故障点:读 NOACCESS 页;受保护体内不能 return(见 README)
    __except1 printf("  [handler] in-block\n");
    *faulted = 1;
    r = -1; // 捕获后走这里
    return r;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    LPVOID p = VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_NOACCESS);
    std::string s = "outer has dtors, fine"; // 外层带析构对象
    int faulted = 0;
    int r = guarded_probe((volatile int*)p, &faulted);
    printf("guarded_probe -> %d faulted=%d (expect -1/1)\n", r, faulted);
    printf("s still alive: %s\n", s.c_str());
    VirtualFree(p, 0, MEM_RELEASE);
    printf("done\n");
    return 0;
}
