// E2c dup2 重定向的完整工程版:fork → 子进程 dup2(pipefd[1], STDOUT_FILENO) → exec
// → 父进程从管道里拿到子进程的 stdout。与 E1 的裸 pipe 构成「机制 → 工程」两级。
// 子进程故意选 /bin/sh:stdout 行走管道,stderr 行落终端,显示 dup2 精确到「这一个 fd」。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e2_dup2_exec.cpp -o e2_dup2_exec
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    int pfd[2];
    sys_call("pipe", pipe, pfd);
    pid_t pid = sys_call("fork", fork);
    if (pid == 0) {
        // 子进程:标准三步——关不用的读端、把写端搬到 1 号位、关掉搬走后多余的原始 fd
        close(pfd[0]);
        sys_call("dup2", dup2, pfd[1], STDOUT_FILENO);
        if (pfd[1] != STDOUT_FILENO)
            close(pfd[1]);
        execl("/bin/sh", "sh", "-c",
              "echo stdout-line:我走的是被 dup2 过来的管道;"
              "echo stderr-line:我还是落终端 1>&2",
              (char*)nullptr);
        perror("execl");
        _exit(127);
    }

    // 父进程:占住读端,关掉写端(否则对方关了之后自己 read 不到 EOF),读干净
    close(pfd[1]);
    std::printf("父进程:等子进程(pid=%d)的 stdout……(stderr 那行你应该已经直接在下面看到了)\n", pid);
    char buf[512];
    std::string got;
    ssize_t r;
    while ((r = read(pfd[0], buf, sizeof buf)) > 0)
        got.append(buf, static_cast<size_t>(r));
    close(pfd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("父进程:从管道拿到 %zu 字节的子进程 stdout:\n%s", got.size(), got.c_str());
    std::printf("waitpid:exit=%d——这就是 popen 内部那套 fork+dup2+exec 的手工完整版\n",
                WEXITSTATUS(st));
    return 0;
}
