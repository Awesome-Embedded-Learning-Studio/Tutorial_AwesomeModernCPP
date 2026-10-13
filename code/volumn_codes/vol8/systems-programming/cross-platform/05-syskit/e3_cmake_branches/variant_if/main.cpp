// variant_if 的探测目标:CMake 选中的分支往这里送宏,宏决定包含哪个平台的头。
// 裸 CC 覆盖把 CMAKE_SYSTEM_NAME 认成 Linux 时,POSIX 分支被选中,
// g++.exe 手里没有 <sys/epoll.h>(e1 实测 ABSENT),构建当场失败——
// "平台身份认错 -> 分支选错 -> 编译失败"的完整链条就在这里。
#include <cstdio>
#ifdef SYSKIT_PLATFORM_POSIX
#    include <sys/epoll.h>
#    define REPORT "posix branch selected (epoll.h included)"
#elif defined(SYSKIT_PLATFORM_WIN32)
#    include <windows.h>
#    define REPORT "win32 branch selected (windows.h included)"
#else
#    define REPORT "no branch selected"
#endif
int main() {
    std::puts(REPORT);
    return 0;
}
