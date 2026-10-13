// 补测(修订轮): alarm 与 setitimer 跨 exec 的存亡 —— alarm(2)/getitimer(2) 的 NOTES 都写 preserved
// across execve,单问 exec 这一跳 口径: WSL2 6.18.33.2-microsoft-standard-WSL2, g++ 16.2.1,
// -std=c++20 -O2 -Wall -Wextra, 计时 CLOCK_MONOTONIC 编排:fork 不继承计时器(t5
// 已证),所以每个探针都在 fork 之后的子进程里武装,再 exec,exec 后的身份只读不设
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
    // ---- 探针 A 的 exec 后身份: getitimer 问 setitimer 武装的 2s 一次性剩多少 ----
    if (argc == 2 && std::strcmp(argv[1], "--exec-probe-a") == 0) {
        struct itimerval it{};
        getitimer(ITIMER_REAL, &it);
        double remain = it.it_value.tv_sec + it.it_value.tv_usec / 1e6;
        std::printf("[A·exec 后] setitimer 武装的 2s 一次性:getitimer 读到剩 %.6f "
                    "s(interval=%lldms)—— itimer 活过了 exec\n",
                    remain,
                    (long long)(it.it_interval.tv_sec * 1000 + it.it_interval.tv_usec / 1000));
        return 0;
    }
    // ---- 探针 B 的 exec 后身份: alarm(0) 问 alarm(2) 的旧剩余 ----
    if (argc == 2 && std::strcmp(argv[1], "--exec-probe-b") == 0) {
        unsigned r = alarm(0);
        std::printf("[B·exec 后] alarm(2) 武装:alarm(0) 返回旧剩余 %u s —— alarm 活过了 exec\n", r);
        return 0;
    }
    // ---- 探针 C 的 exec 后身份: 什么都不撤,睡 3s 看下场 ----
    if (argc == 2 && std::strcmp(argv[1], "--exec-probe-c") == 0) {
        sleep(3); // 不撤,等 2s 档到点
        return 0; // 正常情况下到不了这里
    }

    // ---- 探针 A: 子进程武装 setitimer 后 exec ----
    std::fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        struct itimerval it{};
        it.it_value.tv_sec = 2;
        setitimer(ITIMER_REAL, &it, nullptr);
        execl(argv[0], argv[0], "--exec-probe-a", (char*)nullptr);
        _exit(127);
    }
    int st{};
    waitpid(pid, &st, 0);

    // ---- 探针 B: 子进程 alarm(2) 后 exec ----
    std::fflush(stdout);
    pid = fork();
    if (pid == 0) {
        alarm(2);
        execl(argv[0], argv[0], "--exec-probe-b", (char*)nullptr);
        _exit(127);
    }
    waitpid(pid, &st, 0);

    // ---- 探针 C: 子进程 alarm(2) 后 exec,exec 后不撤 ----
    std::fflush(stdout);
    pid = fork();
    if (pid == 0) {
        alarm(2);
        execl(argv[0], argv[0], "--exec-probe-c", (char*)nullptr);
        _exit(127);
    }
    waitpid(pid, &st, 0);
    std::printf("[C·父进程] 不撤的 exec 子进程结局:WIFSIGNALED=%d WTERMSIG=%d(SIGALRM=14)—— "
                "计时器活着跨过了 exec,处置复位成默认,到点就是它\n",
                WIFSIGNALED(st), WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    return 0;
}
