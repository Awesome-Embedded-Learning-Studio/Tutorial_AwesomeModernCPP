// E5a 同一任务两种写法:fork+execve vs posix_spawn
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e5a_spawn_compare e5a_spawn_compare.cpp
// 任务一:跑 /usr/bin/printenv E5A_MARK(带自定义环境),验证环境传得过去;
// 任务二:跑 /bin/sh -c "exit 7",验证退出码拿得到。
// 两种写法行为完全一致;差别在手感:fork+execve 是两个原语自己组合(错误处理两段),
// posix_spawn 一次调用打包(创建+exec 一体,还能带 file_actions,见 e5b)。
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// —— 写法一:fork + execve,经典两段式 ——
static void run_fork_exec(const char* path, char* const av[], char* const ev[]) {
    std::fflush(stdout); // 提前冲父进程缓冲:子进程直写 fd 1,不冲会反超这些标签行
    pid_t p = fork();
    if (p < 0) {
        std::perror("fork");
        return;
    }
    if (p == 0) { // 子进程:换映像,失败只能 _exit
        execve(path, av, ev);
        std::perror("  execve");
        _exit(127); // 习惯法:127=命令没找到,126=找到了但不能执行
    }
    int raw = 0;
    waitpid(p, &raw, 0); // 父进程:负责收尸
    std::printf("  [fork+exec] 退出状态:%s=%d\n", WIFEXITED(raw) ? "exit" : "signal",
                WIFEXITED(raw) ? WEXITSTATUS(raw) : WTERMSIG(raw));
}

// —— 写法二:posix_spawn,一次调用 ——
static void run_posix_spawn(const char* path, char* const av[], char* const ev[]) {
    std::fflush(stdout);
    pid_t pid = -1;
    int rc = posix_spawn(&pid, path, nullptr, nullptr, av, ev);
    if (rc != 0) { // 注意:错误是返回值,不是 errno 约定
        std::printf("  posix_spawn 失败:%s\n", std::strerror(rc));
        return;
    }
    int raw = 0;
    waitpid(pid, &raw, 0);
    std::printf("  [posix_spawn] 退出状态:%s=%d\n", WIFEXITED(raw) ? "exit" : "signal",
                WIFEXITED(raw) ? WEXITSTATUS(raw) : WTERMSIG(raw));
}

int main() {
    std::printf("任务一:跑 printenv E5A_MARK,环境里塞 E5A_MARK=hello-spawn-env\n");
    char* const av1[] = {(char*)"printenv", (char*)"E5A_MARK", nullptr};
    char* const ev1[] = {(char*)"E5A_MARK=hello-spawn-env", nullptr};
    std::printf("  [fork+exec] 子进程输出:\n");
    run_fork_exec("/usr/bin/printenv", av1, ev1);
    std::printf("  [posix_spawn] 子进程输出:\n");
    run_posix_spawn("/usr/bin/printenv", av1, ev1);

    std::printf("任务二:跑 sh -c exit 7,看退出码能不能拿回来\n");
    char* const av2[] = {(char*)"sh", (char*)"-c", (char*)"exit 7", nullptr};
    run_fork_exec("/bin/sh", av2, environ);
    run_posix_spawn("/bin/sh", av2, environ);

    std::printf(
        "结论:行为一致。差别:fork+execve 需要 fork 分叉点+execve 失败路径+waitpid 三件套;\n");
    std::printf("      posix_spawn 一发完成,vfork 级的效率由实现负责(glibc 走 "
                "clone(CLONE_VM|CLONE_VFORK),\n");
    std::printf("      见 e2d 的 strace 证据),还免掉了 vfork 自己动手的危险。\n");
    return 0;
}
