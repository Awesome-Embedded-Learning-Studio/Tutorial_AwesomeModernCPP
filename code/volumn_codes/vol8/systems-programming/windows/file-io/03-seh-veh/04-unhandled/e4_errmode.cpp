// e4c:SEM_NOGPFAULTERRORBOX —— 压掉错误框之后的裸崩
// (SetErrorMode 是每进程开关,不动系统全局策略)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    printf("errmode: about to write to NULL (no GPF box)\n");
    *(volatile int*)nullptr = 0xC5;
    printf("unreachable\n");
    return 0;
}
