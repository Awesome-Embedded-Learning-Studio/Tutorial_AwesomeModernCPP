// e4a:不装任何 handler 的 0xC0000005 —— 进程退出码是什么
// (配套读法:cmd 里跑完 echo %ERRORLEVEL%;WSL 直跑的话 $? 是被截断的 8 位值)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("bare: about to write to NULL\n");
    *(volatile int*)nullptr = 0xC5;
    printf("unreachable\n");
    return 0;
}
