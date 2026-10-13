// m2:同函数里 __try1 宏 + 带析构的 C++ 对象(MSVC 的 C2712 在 MinGW 会不会有对应限制?)
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <excpt.h>
#include <string>
#include <windows.h>

extern "C" __attribute__((used)) EXCEPTION_DISPOSITION m2_filter(void* ep, void* frame,
                                                                 PCONTEXT ctx, void* disp) {
    PEXCEPTION_RECORD rec = (PEXCEPTION_RECORD)((void**)ep)[0];
    printf("  [filter] code=0x%08lX\n", (unsigned long)rec->ExceptionCode);
    return (EXCEPTION_DISPOSITION)1;
}

struct Loud {
    ~Loud() { printf("  [~Loud] dtor ran\n"); }
};

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    std::string s = "a string with dtor semantics"; // 带析构的局部对象
    Loud l;
    printf("s=%s\n", s.c_str());
    __try1(m2_filter) printf("  [body] raising\n");
    RaiseException(0xE0001111, 0, 0, NULL);
    printf("  [body] NOT REACHED\n");
    __except1 printf("  [handler] landed, s.size=%zu\n", s.size());
    printf("done\n");
    return 0;
}
