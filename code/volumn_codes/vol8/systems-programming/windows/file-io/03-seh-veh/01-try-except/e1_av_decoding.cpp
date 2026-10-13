// e1:__try/__except 的基本盘 —— EXCEPTION_ACCESS_VIOLATION 的 ExceptionInformation 解读
// Windows 版的「SIGSEGV + siginfo」:info[0] 是读/写(0/8),info[1] 是出错地址
//
// 环境事实:MSYS2 UCRT64 g++ 没有 __try/__except 关键字(GCC 至今未实现 MS 扩展语法),
// 这里用的是 mingw-w64 <excpt.h> 自带的内部宏 __try1/__except1 —— 用内联汇编在函数里
// 拼出 SEH 作用域表(.seh_handler __C_specific_handler)。这是本仓库实验 01 系列的口径。
//
// 两个实测出来的用法约束(详见 README):
//   1. 一个翻译单元只能用一次 __try1(标签 .l_startw/.l_endw 是写死的)
//   2. 受保护体内不能写 return(会让后面的 __except1 变成不可达代码被 GCC 删掉)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

// UCRT64 实测:filter 由 ucrtbase.dll 的 __C_specific_handler 调用,
// 第 1 参不是裸的 PEXCEPTION_RECORD,而是 EXCEPTION_POINTERS 形状 {record, context}
extern "C" __attribute__((used)) EXCEPTION_DISPOSITION av_filter(void* ep, void* frame,
                                                                 PCONTEXT ctx, void* disp) {
    (void)frame;
    (void)ctx;
    (void)disp;
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    PEXCEPTION_RECORD r = pointers->ExceptionRecord;
    printf("  [SEH filter] code=0x%08lX flags=0x%08lX fault-at=%p\n",
           (unsigned long)r->ExceptionCode, (unsigned long)r->ExceptionFlags, r->ExceptionAddress);
    printf("    info[0]=%llu (%s)  info[1]=0x%llX\n",
           (unsigned long long)r->ExceptionInformation[0],
           r->ExceptionInformation[0] == 0   ? "READ"
           : r->ExceptionInformation[0] == 1 ? "WRITE"
           : r->ExceptionInformation[0] == 8 ? "EXEC/DEP"
                                             : "?",
           (unsigned long long)r->ExceptionInformation[1]);
    return (EXCEPTION_DISPOSITION)1; // 1 = EXCEPTION_EXECUTE_HANDLER:执行处理块
}

// 受保护区拆进独立函数:外层函数可以照常有析构对象(对应 MSVC C2712 的解法)
__attribute__((noinline)) void probe(int round, volatile int* target, int* result) {
    *result = 0;
    __try1(av_filter) if (round == 0) {
        *result = *target;
    } // 读 NOACCESS 页
    else if (round == 1) {
        *target = round;
    } // 写 NOACCESS 页
    else {
        using F = void (*)();
        ((F)target)();
    } // 跳去执行数据页 → DEP 违例
    __except1* result = -1; // 异常被接住后从标签处继续
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    LPVOID page = VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_NOACCESS);
    printf("page=%p (PAGE_NOACCESS, size=0x%lx)\n", page, si.dwPageSize);

    int result = 0;
    printf("== round 0: read page\n");
    probe(0, (volatile int*)page, &result);
    printf("-> result=%d\n", result);

    printf("== round 1: write page\n");
    probe(1, (volatile int*)((char*)page + 0x1234), &result);
    printf("-> result=%d\n", result);

    printf("== round 2: read NULL\n");
    probe(0, (volatile int*)nullptr, &result);
    printf("-> result=%d\n", result);

    LPVOID rx = VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE); // 不可执行
    printf("== round 3: call into PAGE_READWRITE page (DEP)\n");
    probe(2, (volatile int*)rx, &result);
    printf("-> result=%d\n", result);
    VirtualFree(rx, 0, MEM_RELEASE);

    VirtualFree(page, 0, MEM_RELEASE);
    printf("done\n");
    return 0;
}
