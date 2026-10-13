// m7:同一个翻译单元里用两个 __try1(excpt.h 的标签 .l_startw/.l_endw 会不会撞)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION f1(void* ep, void*, PCONTEXT, void*) {
    printf("f1\n");
    return (EXCEPTION_DISPOSITION)1;
}
extern "C" __attribute__((used)) EXCEPTION_DISPOSITION f2(void* ep, void*, PCONTEXT, void*) {
    printf("f2\n");
    return (EXCEPTION_DISPOSITION)1;
}

void a() {
    __try1(f1) RaiseException(0xE000A001, 0, 0, NULL);
    __except1 printf("a handled\n");
}
void b() {
    __try1(f2) RaiseException(0xE000B002, 0, 0, NULL);
    __except1 printf("b handled\n");
}
int main() {
    a();
    b();
    printf("done\n");
    return 0;
}
