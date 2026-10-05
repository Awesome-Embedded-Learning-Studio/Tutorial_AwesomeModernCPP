// E5 模式一:flag + 主循环——handler 只置一个 volatile sig_atomic_t,活全在主循环干
// 编译:g++ -std=c++20 -Wall -Wextra -O2 flag_graceful.cpp -o flag_graceful && ./flag_graceful
//
// 编排:子进程 100ms 后 kill(parent, SIGINT)。handler 只做一件事:g_stop = 1。
// 主循环每轮开工前查这个标志,看到就跳出——清理、退出码 0,而不是默认 SIGINT 暴毙。
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_stop = 0;

void on_sigint(int) {
    g_stop = 1;
} // handler 全部职责:置标志,立刻返回

long now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void msleep(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const long t0 = now_ms();

    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // 不带 SA_RESTART 也无妨:主循环用 nanosleep,被打断就当到点
    sigaction(SIGINT, &sa, nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        msleep(100);
        kill(getppid(), SIGINT);
        _exit(0);
    }

    std::printf("t=%4ldms work loop starts (SIGINT will arrive ~100ms)\n", now_ms() - t0);
    for (int i = 1;; ++i) {
        if (g_stop) { // 信号到达的事实在这里被消费——此刻什么都安全了
            std::printf("t=%4ldms g_stop seen at iter %d -> break\n", now_ms() - t0, i);
            break;
        }
        std::printf("t=%4ldms working iter %d\n", now_ms() - t0, i);
        msleep(30); // 模拟一段工作
    }
    // 回到主循环上下文:printf/析构/锁,想用什么用什么
    std::printf("t=%4ldms cleanup: flush buffers, close files, say goodbye\n", now_ms() - t0);
    int st = 0;
    waitpid(pid, &st, 0);
    return 0; // 干净退出,退出码 0(默认处置下 SIGINT 是带 core 的暴毙)
}
