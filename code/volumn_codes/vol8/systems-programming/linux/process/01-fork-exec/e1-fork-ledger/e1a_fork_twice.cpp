// E1a fork 的语义账本:一次调用,两次返回
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e1a_fork_twice e1a_fork_twice.cpp
// 要点:fork() 在父进程里返回子进程 pid,在子进程里返回 0;
//      调用前 fflush(stdout) 是关键——子进程继承 stdio 缓冲区的副本,
//      不冲刷的话缓冲区里的内容会被两边各打一次。
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    std::printf("[调用前   ] pid=%d ppid=%d,准备调用 fork()\n", getpid(), getppid());
    std::fflush(stdout); // 不冲刷,子进程会带着缓冲副本把上面那行再打一遍

    pid_t r = fork();

    if (r == 0) {
        // 子进程侧:从这里开始跑,前面的代码没有执行过
        std::printf("[子进程侧] fork() 返回 %d —— 返回 0 说明我是新进程: pid=%d ppid=%d\n", r,
                    getpid(), getppid());
        std::fflush(stdout);
        _exit(0);
    }

    // 父进程侧:同一个 r,值是子进程的 pid
    std::printf("[父进程侧] fork() 返回 %d —— 这是子进程的 pid: pid=%d ppid=%d\n", r, getpid(),
                getppid());
    int st = 0;
    waitpid(r, &st, 0);
    std::printf("[父进程侧] 子进程 %d 已收尸: WIFEXITED=%d WEXITSTATUS=%d\n", r, WIFEXITED(st),
                WEXITSTATUS(st));
    std::printf("账本小结:fork 之前只有父进程一份执行流;fork 之后同一个调用点有两份返回值,\n");
    std::printf("          子进程的整个世界从 fork 返回那一刻开始,pid/ppid 视角各是各的。\n");
    return 0;
}
