// E2b pipe2(O_CLOEXEC):与 L01 的 CLOEXEC 结论接上——exec 换程序那一刻,
// 带 CLOEXEC 的 fd 自动关闭。三种姿势对照:裸 pipe / pipe2(O_CLOEXEC) / 裸 pipe+fcntl 补
// FD_CLOEXEC。 子进程 exec `ls -l /proc/self/fd`,fd 表自己作证。 编译:g++ -std=c++20 -Wall -Wextra
// -O2 -I ../common e2_cloexec.cpp -o e2_cloexec
#include "ipc_util.hpp"

#include <cstdarg>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void show_header(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
    std::fflush(stdout);
}

// fork 出的子进程 exec ls,把自己的 fd 表打印出来(stdout 还是终端)
void list_fds() {
    execl("/bin/sh", "sh", "-c", "ls -l /proc/self/fd", (char*)nullptr);
    perror("execl");
    _exit(127);
}

} // namespace

int main() {
    // ---- 姿势一:裸 pipe,什么都不设 ----
    {
        int pfd[2];
        sys_call("pipe", pipe, pfd);
        show_header("== 姿势一:裸 pipe(fd=%d 读、%d 写) ==  子进程 exec 后的 fd 表:", pfd[0],
                    pfd[1]);
        pid_t pid = sys_call("fork", fork);
        if (pid == 0)
            list_fds(); // 两个管道 fd 都活着穿过了 exec
        int st = 0;
        waitpid(pid, &st, 0);
        close(pfd[0]);
        close(pfd[1]);
    }

    // ---- 姿势二:pipe2(O_CLOEXEC),创建时就带 ----
    {
        int pfd[2];
        sys_call("pipe2", pipe2, pfd, O_CLOEXEC);
        show_header("== 姿势二:pipe2(O_CLOEXEC) ==  子进程 exec 后的 fd 表:");
        pid_t pid = sys_call("fork", fork);
        if (pid == 0)
            list_fds(); // exec 一到,两个管道 fd 已被内核自动 close
        int st = 0;
        waitpid(pid, &st, 0);
        close(pfd[0]);
        close(pfd[1]);
    }

    // ---- 姿势三:裸 pipe,事后 fcntl 只给写端补 FD_CLOEXEC ----
    {
        int pfd[2];
        sys_call("pipe", pipe, pfd);
        sys_call("F_SETFD", [](int fd) { return fcntl(fd, F_SETFD, FD_CLOEXEC); }, pfd[1]);
        show_header(
            "== 姿势三:裸 pipe + fcntl 只给写端(fd=%d)补 FD_CLOEXEC ==  子进程 exec 后的 fd 表:",
            pfd[1]);
        pid_t pid = sys_call("fork", fork);
        if (pid == 0)
            list_fds(); // 读端还活着,写端没了——标志位是逐 fd 的
        int st = 0;
        waitpid(pid, &st, 0);
        close(pfd[0]);
        close(pfd[1]);
    }
    std::printf("\n结论:FD_CLOEXEC 是 fd 级标志,exec 换程序时内核替你 close;pipe2(O_CLOEXEC) "
                "创建时一把带上,或事后 fcntl 逐个补——与 L01 文件篇的 CLOEXEC 结论同一条\n");
    return 0;
}
