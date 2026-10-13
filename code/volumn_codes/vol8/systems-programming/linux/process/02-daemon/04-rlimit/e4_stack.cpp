// E4c RLIMIT_STACK 读取:与内存篇的栈观察呼应——同一个栈,ulimit 视角 vs maps 视角
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e4_stack e4_stack.cpp
// 运行: ./e4_stack
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstring>
#include <sys/resource.h>

int main() {
    printf("== E4c RLIMIT_STACK 读取 ==\n");
    rlimit rl{};
    getrlimit(RLIMIT_STACK, &rl);
    printf("getrlimit: rlim_cur=%lu 字节 (%lu KiB = %.1f MiB), rlim_max=%s\n",
           (unsigned long)rl.rlim_cur, (unsigned long)(rl.rlim_cur / 1024), rl.rlim_cur / 1048576.0,
           rl.rlim_max == RLIM_INFINITY ? "unlimited(RLIM_INFINITY)" : "有限值");

    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) {
        perror("fopen maps");
        return 1;
    }
    char line[512];
    unsigned long lo = 0, hi = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "[stack]")) {
            sscanf(line, "%lx-%lx", &lo, &hi);
            printf("maps [stack]: %s", line);
            printf("当前已映射大小 = %lu KiB(远小于软限,软限是\"允许长到的上限\")\n",
                   (hi - lo) / 1024);
            break;
        }
    }
    fclose(f);
    printf("呼应内存篇: 栈越界不是立刻 SIGSEGV,而是先按需扩栈,碰到 rlim_cur 才拒扩展;\n");
    printf("拒扩展的那次访问才变成 SIGSEGV。多线程程序里这条 rlimit 只管主线程栈,\n");
    printf("其他线程栈走 pthread_attr 的默认大小(通常也是 8 MiB,由 glibc 缓存/分配)\n");
    return 0;
}
