// e5b:反向实验 —— SEH 的 __except 把 C++ 异常「吞」了会发生什么
// throw 在 __try1 受保护区里,filter 返回 1(执行处理块):
// C++ 的 catch 永远等不到这一票。进程还能不能活?
// (MSVC 文档明说这是未定义;实测 MinGW/UCRT 看现场)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <stdexcept>
#include <windows.h>

struct Loud {
    ~Loud() { printf("  [~Loud] 析构跑了\n"); }
};

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION swallow_filter(void* ep, void*, PCONTEXT,
                                                                      void*) {
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    PEXCEPTION_RECORD r = pointers->ExceptionRecord;
    printf("  [SEH filter] code=0x%08lX flags=0x%08lX nparams=%lu\n",
           (unsigned long)r->ExceptionCode, (unsigned long)r->ExceptionFlags,
           (unsigned long)r->NumberParameters);
    if (r->ExceptionCode == 0xE06D7363 && r->NumberParameters >= 3)
        printf("    info[0..2]=0x%llX 0x%llX 0x%llX(MSVC throw 三件套:构造副本/析构/类名)\n",
               (unsigned long long)r->ExceptionInformation[0],
               (unsigned long long)r->ExceptionInformation[1],
               (unsigned long long)r->ExceptionInformation[2]);
    if (r->ExceptionCode == 0x20474343)
        printf("    info[0]=0x%llX(GCC 变体只带 1 个参数)\n",
               (unsigned long long)r->ExceptionInformation[0]);
    return (EXCEPTION_DISPOSITION)1; // 吞掉
}

// 注意:m2 的教训(受保护区函数不能带需要展开的对象);另有一条实测新教训:
// throw 直接写在 __try1 同函数里,进程直接 0xC0000409 收场,filter 都进不去 ——
// 所以 throw 放进被调函数,让 C++ 异常【穿越】SEH 帧被截住
__attribute__((noinline)) void inner_throw(volatile int* cond) {
    if (*cond)
        throw std::runtime_error("crossing the SEH frame");
}

__attribute__((noinline)) void guarded_call(volatile int* cond, int* result) {
    __try1(swallow_filter) inner_throw(cond);
    *result = 0;
    __except1* result = -1; // 吞掉后从这继续
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== SEH 吞 C++ throw ==\n");
    int result = 0;
    volatile int fire = 1;
    try {
        Loud l; // 外层对象,析构应当有机会跑
        guarded_call(&fire, &result);
        printf("  guarded_call 正常返回(result=%d)——注意:catch 没接到这一票\n", result);
    } catch (const std::exception& e) {
        printf("  catch 跑到了:%s(没被吞?)\n", e.what());
    }
    printf("  try/catch 之后还活着,继续跑\n");
    printf("done\n");
    return 0;
}
