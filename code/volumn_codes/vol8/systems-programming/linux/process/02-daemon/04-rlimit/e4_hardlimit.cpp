// E4d rlim_cur/rlim_max 边界:普通用户(无 CAP_SYS_RESOURCE)升硬限 -> EPERM;
//   硬限降下去之后再想升回来 -> 还是 EPERM(硬限是单向阀门,只有 root 能升)
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e4_hardlimit e4_hardlimit.cpp
// 运行: ./e4_hardlimit
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>
#include <unistd.h>

int main() {
    printf("== E4d 软限/硬限边界(RLIMIT_NOFILE 作载体,uid=%d) ==\n", (int)getuid());
    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    unsigned long cur0 = rl.rlim_cur, max0 = rl.rlim_max;
    printf("初始: rlim_cur=%lu rlim_max=%lu\n\n", cur0, max0);

    // ① 软限升到当前硬限:允许(不需要任何特权)
    rlimit t1{};
    t1.rlim_cur = t1.rlim_max = max0;
    errno = 0;
    int r = setrlimit(RLIMIT_NOFILE, &t1);
    printf("① 软限升到硬限(%lu)        -> %s%s%s\n", max0, r == 0 ? "成功" : "失败 ",
           r == 0 ? "" : "errno=", r == 0 ? "" : strerror(errno));

    // ② 硬限升到 max+1:普通用户 -> EPERM
    rlimit t2{};
    t2.rlim_max = max0 + 1;
    t2.rlim_cur = t2.rlim_max; // cur 必须 <= max,否则先撞 EINVAL 就测不到 EPERM 了
    errno = 0;
    r = setrlimit(RLIMIT_NOFILE, &t2);
    printf("② 硬限升到 %lu(越权)      -> %s %s(errno=%d %s)\n", max0 + 1,
           r == 0 ? "成功(?)" : "失败", r == 0 ? "" : "EPERM 如约:", errno, strerror(errno));

    // ③ 硬限往下降:允许
    rlimit t3{};
    t3.rlim_cur = t3.rlim_max = 1024;
    errno = 0;
    r = setrlimit(RLIMIT_NOFILE, &t3);
    printf("③ 硬限降到 1024            -> %s%s%s\n", r == 0 ? "成功" : "失败 ",
           r == 0 ? "" : "errno=", r == 0 ? "" : strerror(errno));

    // ④ 想升回原值:EPERM——降下去就回不来了
    rlimit t4{};
    t4.rlim_cur = t4.rlim_max = max0;
    errno = 0;
    r = setrlimit(RLIMIT_NOFILE, &t4);
    printf("④ 再升回 %lu(降过之后)   -> %s %s(errno=%d %s)\n", max0, r == 0 ? "成功(?)" : "失败",
           r == 0 ? "" : "回不来了:", errno, strerror(errno));

    printf("\n结论: 软限在 [0,硬限] 内随便调;硬限只有 root(CAP_SYS_RESOURCE)能升,\n");
    printf("普通用户降了就锁死——所以守护进程常见写法是启动早期把软限直接拉到硬限\n");
    return 0;
}
