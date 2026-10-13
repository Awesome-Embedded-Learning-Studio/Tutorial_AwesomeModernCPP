// E2d strace 证据:五个包装变体在系统调用层全部落到 execve;
//     fork 落到 clone3,posix_spawn 落到 clone3(CLONE_VM|CLONE_VFORK) 再 execve
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e2d_strace_target e2d_strace_target.cpp
// 采集:strace -f -e trace=execve,execveat,clone3 ./e2d_strace_target > e2d.out 2>&1
//      (-f 跟进子进程;strace 输出走 stderr,与程序 stdout 合并进 e2d.out)
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

static void run_fork_wrapper(const char* which, const char* msg) {
    std::printf("[e2d] 代码里调用的包装:%s\n", which);
    std::fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        if (std::strcmp(which, "execl") == 0)
            execl("/bin/echo", "echo", msg, (char*)nullptr);
        else if (std::strcmp(which, "execlp") == 0)
            execlp("echo", "echo", msg, (char*)nullptr);
        else // execvp
            execvp("echo", (char* const[]){(char*)"echo", (char*)msg, nullptr});
        _exit(126); // exec 失败才会到这
    }
    int st = 0;
    waitpid(p, &st, 0);
}

static void run_posix_spawn(const char* msg) {
    std::printf("[e2d] 再看 posix_spawn\n");
    std::fflush(stdout);
    pid_t pid = -1;
    char* const av[] = {(char*)"echo", (char*)msg, nullptr};
    int rc = posix_spawn(&pid, "/bin/echo", nullptr, nullptr, av, environ);
    if (rc != 0) {
        std::printf("spawn 失败 rc=%d\n", rc);
        return;
    }
    int st = 0;
    waitpid(pid, &st, 0);
}

int main() {
    run_fork_wrapper("execl", "[e2d] 经 execl 到达 echo");
    run_fork_wrapper("execlp", "[e2d] 经 execlp(PATH 搜到 echo)到达");
    run_fork_wrapper("execvp", "[e2d] 经 execvp(PATH 搜到 echo)到达");
    run_posix_spawn("[e2d] 经 posix_spawn 到达");
    std::printf("[e2d] 全部完成:对照 .out 里 strace 记下的系统调用行\n");
    return 0;
}
