// E5b exec 之后环境的两条路:
//   路 1: execle("/usr/bin/env", ..., envp 自备数组)  -> 子进程环境 = envp 白名单,全量换血
//   路 2: execve("/usr/bin/env", argv, environ)       -> 子进程环境 = 原样继承(含刚 setenv 的)
//   (execl/execlp 那一支不带 envp 参数,隐式用当前 environ,与路 2 同效)
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o e5_exec e5_exec.cpp
// 运行: ./e5_exec
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

int main() {
    printf("== E5b exec 的两条环境路 ==\n");
    setenv("E5_EXTRA", "inherited-via-environ", 1);
    int n = 0;
    while (environ[n])
        ++n;
    printf("父进程先 setenv(E5_EXTRA),当前 environ 条目数=%d,PATH 前 48 字符:\n", n);
    printf("  \"%.48s...\"\n\n", getenv("PATH"));

    printf("--- 路 1: execle + envp 白名单数组(子进程环境被整个替换) ---\n");
    fflush(stdout);
    pid_t c1 = fork();
    if (c1 == 0) {
        char* const envp[] = {(char*)"E5_FROM=execle", (char*)"PATH=/usr/bin:/bin", nullptr};
        execle("/usr/bin/env", "env", (char*)nullptr, envp);
        perror("execle");
        _exit(127);
    }
    waitpid(c1, nullptr, 0);

    printf("\n--- 路 2: execve + environ(子进程原样继承,能看到 E5_EXTRA) ---\n");
    fflush(stdout);
    pid_t c2 = fork();
    if (c2 == 0) {
        char* const av[] = {(char*)"env", nullptr};
        execve("/usr/bin/env", av, environ);
        perror("execve");
        _exit(127);
    }
    waitpid(c2, nullptr, 0);

    printf("\n结论: /usr/bin/env 只是把收到的环境原样打印——\n");
    printf("  路 1 只剩白名单两条(连父进程的 HOME/TERM 都没了);\n");
    printf("  路 2 全量在场且带着 E5_EXTRA。构造沙箱/清洗环境时走路 1,普通传递走路 2\n");
    return 0;
}
