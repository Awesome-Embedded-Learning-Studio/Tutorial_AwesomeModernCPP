// E5a environ 全局变量 vs getenv/setenv/putenv/unsetenv 家族 + 环境块在栈上的位置
//   - environ 遍历 = 原始视角;getenv/setenv/... = 库包装的舒适视角
//   - putenv 的所有权坑(man 3 putenv):glibc 不拷贝字符串,直接把指针塞进环境
//   - 地址判定:environ 数组与环境字符串都住在 [stack] 映射区(栈顶附近)
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e5_environ e5_environ.cpp
// 运行: ./e5_environ
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern char** environ;

static bool in_range(unsigned long lo, unsigned long hi, const void* p) {
    auto a = (unsigned long)p;
    return lo != 0 && a >= lo && a < hi;
}

int main(int argc, char** argv) {
    (void)argc;
    printf("== E5a 环境变量家族 ==\n");

    // ---- 环境块的位置:先看,后面 setenv 会把它搬家 ----
    int n = 0;
    while (environ[n])
        ++n;
    printf("environ 指针数组条目数 = %d\n", n);

    unsigned long slo = 0, shi = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (strstr(line, "[stack]")) {
                sscanf(line, "%lx-%lx", &slo, &shi);
                printf("maps [stack]: %s", line);
                break;
            }
        }
        fclose(f);
    }
    printf("environ 数组本身在     %p\n", (void*)environ);
    printf("environ[0] 字符串在    %p\n", (void*)environ[0]);
    printf("argv[0] 字符串在       %p\n", (void*)argv[0]);
    int on_stack = 0;
    printf("main 的局部变量在      %p\n", (void*)&on_stack);
    void* heap = malloc(16);
    printf("malloc 的堆块在        %p\n", heap);
    printf("判定: environ 数组在 [stack] 内? %s ; environ[0] 字符串在 [stack] 内? %s\n",
           in_range(slo, shi, (void*)environ) ? "是" : "否",
           in_range(slo, shi, (void*)environ[0]) ? "是" : "否");
    printf("(exec 时内核把 argv/envp 数组和字符串铺在栈顶,libc 的 environ 就指过去)\n\n");

    // ---- 遍历 vs getenv ----
    printf("遍历 environ 找 PATH(手工 strncmp):");
    for (int i = 0; i < n; ++i)
        if (strncmp(environ[i], "PATH=", 5) == 0) {
            printf(" environ[%d]=\"%.48s...\"\n", i, environ[i]);
            break;
        }
    printf("getenv(\"PATH\")            = \"%.48s...\"\n", getenv("PATH"));

    // ---- setenv 的 overwrite 语义 ----
    printf("\nsetenv(E5_VAR, \"v1\", 0)  返回 %d(不存在则新建)\n", setenv("E5_VAR", "v1", 0));
    setenv("E5_VAR", "v2", 0);
    printf("setenv(E5_VAR, \"v2\", 0) 后 getenv = %s(overwrite=0: 已存在则不动)\n",
           getenv("E5_VAR"));
    setenv("E5_VAR", "v3", 1);
    printf("setenv(E5_VAR, \"v3\", 1) 后 getenv = %s(overwrite=1: 覆盖)\n", getenv("E5_VAR"));
    unsetenv("E5_VAR");
    printf("unsetenv(\"E5_VAR\")   后 getenv = %s(不存在时返回 NULL)\n",
           getenv("E5_VAR") ? getenv("E5_VAR") : "(nil)");

    // ---- putenv 所有权陷阱 ----
    printf("\nputenv 陷阱(man 3 putenv: 字符串归环境所有,glibc 不做拷贝):\n");
    static char buf[64];
    strcpy(buf, "E5_PUT=aaa");
    putenv(buf);
    printf("putenv(buf 内容 \"E5_PUT=aaa\") 后 getenv = %s\n", getenv("E5_PUT"));
    strcpy(buf, "E5_PUT=zzz"); // 注意:没有再调用 putenv!
    printf("只把 buf 改成 \"E5_PUT=zzz\"     后 getenv = %s  <-- 环境直接引用了 buf\n",
           getenv("E5_PUT"));
    printf("推论: 传栈上/临时的缓冲区给 putenv,函数返回后 environ 就悬垂了(man 的原话:\n");
    printf("       \"the string becomes part of the environment\")\n\n");

    // ---- setenv 之后 environ 数组搬家 ----
    n = 0;
    while (environ[n])
        ++n;
    printf("setenv/putenv 折腾完: 条目数=%d, environ 数组现在在 %p\n", n, (void*)environ);
    printf("判定: 还在 [stack] 内? %s(第一次扩容时 libc 在堆上另建了数组)\n",
           in_range(slo, shi, (void*)environ) ? "是" : "否,已搬到堆上");
    return 0;
}
