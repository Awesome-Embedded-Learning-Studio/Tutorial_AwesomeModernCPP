// m12:longjmp 直接跳出受保护区 —— @except 型作用域没有 __finally,什么都不补跑
#define WIN32_LEAN_AND_MEAN
#include <csetjmp>
#include <cstdio>
#include <excpt.h>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION m12_filter(void* ep, void*, PCONTEXT,
                                                                  void*) {
    printf("  [filter] 不该跑到这(longjmp 不经过异常分发)\n");
    return (EXCEPTION_DISPOSITION)1;
}

static jmp_buf jb;

__attribute__((noinline)) void guarded(volatile int* p) {
    __try1(m12_filter) if (*p)
        longjmp(jb, 1); // 直接跳走,SEH 分发器全程无感(noreturn,后面留直落路径)
    printf("  no jump happened\n");
    __except1 printf("  handler 也不该跑\n");
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    volatile int fire = 1;
    if (setjmp(jb) == 0) {
        printf("first pass\n");
        guarded(&fire);
        printf("unreachable\n");
    } else {
        printf("landed back via longjmp —— 受保护区被跳过,无 filter 无 handler\n");
    }
    printf("done\n");
    return 0;
}
