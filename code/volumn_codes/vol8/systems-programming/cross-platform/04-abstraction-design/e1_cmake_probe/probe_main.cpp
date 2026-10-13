// probe_main.cpp —— 把配置期检测结果带进程序,与宏/has_include 两路同台对拍
#include "config.h"
#include <cstdio>

int main() {
    std::printf("== route 3: CMake configure-time results (via config.h) ==\n");
    std::printf("  HAVE_SYS_EPOLL_H=%d HAVE_SYS_EVENT_H=%d HAVE_LIBURING_H=%d HAVE_WINDOWS_H=%d\n",
                HAVE_SYS_EPOLL_H, HAVE_SYS_EVENT_H, HAVE_LIBURING_H, HAVE_WINDOWS_H);
    return 0;
}
