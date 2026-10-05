// 探针:MinGW-w64 g++ 是否支持 MS 扩展 __try/__except
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

int main() {
    __try {
        RaiseException(0xE0001234, 0, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        printf("caught code=0x%08lX\n", (unsigned long)GetExceptionCode());
    }
    return 0;
}
