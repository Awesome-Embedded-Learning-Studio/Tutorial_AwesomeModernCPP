// E1c 两类死亡通知:SIGPIPE(读端全关,写者写)与 EOF(写端全关,读者读到 0)。
// 另含「空管道 + 活写者 → read 阻塞」的对照,说明 read 返回 0 不是「没数据」而是「对面没人了」。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_sigpipe_eof.cpp -o e1_sigpipe_eof
#include "ipc_util.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t g_caught = 0;
void on_sigpipe(int) {
    g_caught = 1;
}
} // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    // ---- 第一幕:空管道、写者活着 → read 阻塞;写端全关 → read 返回 0(EOF) ----
    std::printf("== 第一幕:read 的两种「等」与 EOF ==\n");
    int pfd[2];
    sys_call("pipe", pipe, pfd);
    pid_t pid = sys_call("fork", fork);
    if (pid == 0) { // 子进程:活着的写者
        close(pfd[0]);
        usleep(1000 * 1000); // 这 1 秒里管道是空的,但写者活着
        const char msg[] = "ping";
        if (write(pfd[1], msg, sizeof msg - 1) == -1)
            _exit(1);
        usleep(300 * 1000);
        close(pfd[1]); // 写端全关 → 读者那边是 EOF
        _exit(0);
    }
    close(pfd[1]);
    char buf[64];
    ssize_t r = sys_call("read#1", read, pfd[0], buf, sizeof buf - 1);
    buf[r] = '\0';
    std::printf("[%6.1f ms] read#1 返回 %zd:「%s」——管道空但写者活着时,read 干等了约 1000 ms\n",
                ms_since(t0), r, buf);
    r = sys_call("read#2", read, pfd[0], buf, sizeof buf);
    std::printf("[%6.1f ms] read#2 返回 %zd——写端全关,这是 EOF(与 Windows 篇 W01 的 ReadFile "
                "到文件尾返 0 同一语义)\n",
                ms_since(t0), r);
    int st = 0;
    waitpid(pid, &st, 0);
    close(pfd[0]);

    // ---- 第二幕:读端全关 → 写者收 SIGPIPE,write 返 -1 且 errno=EPIPE ----
    std::printf("\n== 第二幕:读端全关 → SIGPIPE + EPIPE ==\n");
    int pfd2[2];
    sys_call("pipe", pipe, pfd2);
    struct sigaction sa{};
    sa.sa_handler = on_sigpipe;
    sigemptyset(&sa.sa_mask);
    sys_call("sigaction", sigaction, SIGPIPE, &sa, nullptr);
    close(pfd2[0]); // 读端先关——对面的读者没了
    const char msg[] = "anyone?";
    errno = 0;
    ssize_t w = write(pfd2[1], msg, sizeof msg - 1);
    std::printf("write(写端) 返回 %zd,errno=%d (%s);信号处理器被调过:%s\n", w, errno,
                std::strerror(errno), g_caught ? "是(SIGPIPE 已被我们捕获)" : "否");
    close(pfd2[1]);

    // ---- 对照:SIGPIPE 设为 SIG_IGN 时,信号不杀进程,只剩 EPIPE ----
    signal(SIGPIPE, SIG_IGN);
    int pfd3[2];
    sys_call("pipe", pipe, pfd3);
    close(pfd3[0]);
    errno = 0;
    w = write(pfd3[1], msg, sizeof msg - 1);
    std::printf("SIGPIPE 设为 SIG_IGN 再来一次:write 返回 %zd,errno=%d (%s)——忽略信号后错误只剩 "
                "errno 这一条通道\n",
                w, errno, std::strerror(errno));
    close(pfd3[1]);
    return 0;
}
