// variant_genex 的探测目标:generator expression 送进来的宏。
#include <cstdio>
#ifdef GENEX_SAYS_WINDOWS
#    include <windows.h>
#    define REPORT "genex says: Windows"
#endif
#ifdef GENEX_SAYS_LINUX
#    include <sys/epoll.h>
#    define REPORT "genex says: Linux"
#endif
#ifndef REPORT
#    define REPORT "genex matched neither"
#endif
int main() {
    std::puts(REPORT);
    return 0;
}
