// E4a RLIMIT_NOFILE:把软限压到 4,再 open -> EMFILE(file table overflow)
//   L01 用 ulimit -n 看过数值,这里是程序内 getrlimit/setrlimit 的调节机制
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e4_nofile e4_nofile.cpp
// 运行: ./e4_nofile
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

static void show(const char* when) {
    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    printf("%-16s rlim_cur=%-10lu rlim_max=%lu\n", when, (unsigned long)rl.rlim_cur,
           (unsigned long)rl.rlim_max);
}

int main() {
    printf("== E4a RLIMIT_NOFILE: 压低软限 -> open 报 EMFILE ==\n");
    show("初始");

    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    rlimit want{};
    want.rlim_cur = 4;           // 软限压到 4(fd 0/1/2 已占 3 个)
    want.rlim_max = rl.rlim_max; // 硬限不动
    if (setrlimit(RLIMIT_NOFILE, &want) != 0) {
        perror("setrlimit");
        return 1;
    }
    show("setrlimit 后");

    printf("软限=4 且 fd0/1/2 已占 3 个名额 -> 还能开 1 个(fd=3),之后:\n");
    for (int i = 1; i <= 3; ++i) {
        errno = 0;
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0)
            printf("  open #%d -> -1, errno=%d = %s\n", i, errno, strerror(errno));
        else
            printf("  open #%d -> fd=%d(成功)\n", i, fd);
    }

    // 降软限随时可逆:软限可以升回硬限以内的任意值
    rlimit back{};
    getrlimit(RLIMIT_NOFILE, &back);
    back.rlim_cur = back.rlim_max;
    errno = 0;
    int r = setrlimit(RLIMIT_NOFILE, &back);
    printf("再把软限升回硬限(%lu) -> %s\n", (unsigned long)back.rlim_cur,
           r == 0 ? "成功(软限双向可调)" : strerror(errno));
    show("恢复后");
    return 0;
}
