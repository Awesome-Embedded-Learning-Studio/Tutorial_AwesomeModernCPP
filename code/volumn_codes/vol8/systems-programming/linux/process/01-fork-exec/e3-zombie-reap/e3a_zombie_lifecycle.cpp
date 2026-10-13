// E3a 双阶段退出与僵尸实证:exit 只是第一阶段,wait 才让进程真正消失
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -o e3a_zombie_lifecycle e3a_zombie_lifecycle.cpp
// 时序:t1 子进程活着(State: S)→ t2 子进程 _exit 后、父进程未 wait(State: Z,ps 里 <defunct>)
//      → t3 父进程 waitpid 收尸 → /proc/<pid> 整个消失(ENOENT)
// 内核视角:exit_group() 释放内存、关 fd,只留下一个约 几百字节 的 task_struct
//         存退出码,等父进程来取——这个残骸就是僵尸。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

// 读 /proc/<pid>/status 的 State 行,成功则把整行拷进 out(带换行)
static bool read_state_line(pid_t pid, char* out, size_t cap) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/status", pid);
    FILE* f = std::fopen(path, "r");
    if (!f)
        return false;
    bool found = false;
    while (std::fgets(out, (int)cap, f))
        if (std::strncmp(out, "State:", 6) == 0) {
            found = true;
            break;
        }
    std::fclose(f);
    return found;
}

int main() {
    pid_t child = fork();
    if (child == 0) {
        std::printf("[子] pid=%d,先睡 300ms,给父进程留时间读我的 State\n", getpid());
        std::fflush(stdout);
        usleep(300000);
        std::printf("[子] 现在调 _exit(42):释放内存、关 fd,但 task_struct 和退出码留下\n");
        std::fflush(stdout);
        _exit(42);
    }

    char line[128];
    usleep(50000); // 确保子进程已进入睡眠态
    if (read_state_line(child, line, sizeof line))
        std::printf("[父][t1 子还活着] %s", line); // 期望 State: S (sleeping)

    // 等子退出并变成僵尸:轮询直到 State 出现 Z
    bool saw_zombie = false;
    for (int i = 0; i < 2000 && !saw_zombie; ++i) {
        if (read_state_line(child, line, sizeof line) && std::strstr(line, "Z") != nullptr) {
            std::printf("[父][t2 子已退、未收尸] %s", line); // State: Z (zombie)
            saw_zombie = true;
            break;
        }
        usleep(1000);
    }
    if (!saw_zombie) {
        std::printf("[父] 没等到僵尸状态(异常)\n");
        return 1;
    }

    // ps 旁证:STAT 列 Z,COMMAND 列 <defunct>
    char cmd[128];
    std::snprintf(cmd, sizeof cmd, "ps -o pid,ppid,stat,comm -p %d", child);
    std::printf("[父] 同一时刻 ps 眼里:\n");
    std::fflush(stdout);
    FILE* p = popen(cmd, "r");
    if (p) {
        char l[256];
        while (std::fgets(l, sizeof l, p))
            std::printf("    %s", l);
        pclose(p);
    }
    std::fflush(stdout);

    std::printf("[父] 现在才 waitpid 收尸(第二阶段)……\n");
    std::fflush(stdout);
    int st = 0;
    if (waitpid(child, &st, 0) != child) {
        std::perror("waitpid");
        return 1;
    }
    std::printf("[父][t3 收尸完成] WIFEXITED=%d WEXITSTATUS=%d(退出码从僵尸残骸里取出)\n",
                WIFEXITED(st), WEXITSTATUS(st));

    char path[64];
    std::snprintf(path, sizeof path, "/proc/%d/status", child);
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("[父] 再读 %s:fopen 失败(%s)——进程彻底消失,双阶段结束\n", path,
                    std::strerror(errno));
    } else {
        std::printf("[父] %s 居然还在(不该发生)\n", path);
        std::fclose(f);
    }
    return 0;
}
