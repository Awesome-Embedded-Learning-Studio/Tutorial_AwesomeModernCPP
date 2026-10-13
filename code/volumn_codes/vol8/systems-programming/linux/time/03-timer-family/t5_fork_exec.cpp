// E5: fork 与 exec 之后,四件定时器各自的存亡 —— 语义对照矩阵的实测底稿
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra -pthread, 计时 CLOCK_MONOTONIC
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::atomic<int> g_real{0}, g_usr1{0};

void on_real(int) {
    g_real.store(g_real.load() + 1, std::memory_order_relaxed);
}
void on_usr1(int, siginfo_t*, void*) {
    g_usr1.store(g_usr1.load() + 1, std::memory_order_relaxed);
}

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

// 睡满 window_ms(信号会打断 usleep,拿单调钟兜底补齐)
void sleep_window(uint64_t window_ms) {
    uint64_t t0 = mono_ns();
    while (mono_ns() - t0 < window_ms * 1000000ull)
        usleep(50000);
}

} // namespace

int main(int argc, char** argv) {
    // ---- exec 之后的身份: 只带着继承来的 timerfd 读档数 ----
    if (argc == 3 && std::string_view(argv[1]) == "--exec-child") {
        int tfd = std::atoi(argv[2]);
        sleep_window(600);
        uint64_t ticks = 0;
        read(tfd, &ticks, sizeof ticks);
        std::printf("[exec 子进程] 继承的 timerfd fd=%d,读到档数 %llu —— fd 活过了 exec\n", tfd,
                    (unsigned long long)ticks);
        return 0;
    }

    struct sigaction sa{};
    sa.sa_handler = on_real;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);
    struct sigaction sb{};
    sb.sa_sigaction = on_usr1;
    sb.sa_flags = SA_SIGINFO;
    sigemptyset(&sb.sa_mask);
    sigaction(SIGUSR1, &sb, nullptr);

    std::printf("== E5a: 同一个内核计时器 —— alarm 和 setitimer(ITIMER_REAL) 共用一笔 ==\n");
    struct itimerval it{};
    it.it_value.tv_usec = 100000;
    it.it_interval.tv_usec = 100000;
    setitimer(ITIMER_REAL, &it, nullptr);
    getitimer(ITIMER_REAL, &it);
    std::printf("setitimer 武装后: it_value=%lldms it_interval=%lldms\n",
                (long long)(it.it_value.tv_sec * 1000 + it.it_value.tv_usec / 1000),
                (long long)(it.it_interval.tv_sec * 1000 + it.it_interval.tv_usec / 1000));
    alarm(2); // alarm 的真身就是 ITIMER_REAL 的一次性秒级写法
    getitimer(ITIMER_REAL, &it);
    std::printf("调 alarm(2) 之后: it_value=%lldms it_interval=%lldms —— 周期被抹掉,\n"
                "两件工具共用同一个计时器,后写的顶掉先写的\n",
                (long long)(it.it_value.tv_sec * 1000 + it.it_value.tv_usec / 1000),
                (long long)(it.it_interval.tv_sec * 1000 + it.it_interval.tv_usec / 1000));
    alarm(0); // 清场

    std::printf("\n== E5b: fork 探针一: alarm 单独问继承 ==\n");
    alarm(2);
    std::fflush(stdout); // fork 前清 stdio 缓冲,防子进程把父进程的缓冲冲出来两遍
    pid_t pid = fork();
    if (pid == 0) {
        unsigned r = alarm(0); // 撤销并查剩余:非零即被继承
        std::printf("[fork 子进程 A] 子进程里 alarm(0) 返回 %u s —— 0 说明 alarm 没跟过来\n", r);
        std::fflush(stdout);
        _exit(0);
    }
    alarm(0);
    int st;
    waitpid(pid, &st, 0);

    std::printf("\n== E5c: fork 探针二: setitimer/timer_create/timerfd(观测 600ms) ==\n");
    it = {};
    it.it_value.tv_usec = 100000;
    it.it_interval.tv_usec = 100000;
    setitimer(ITIMER_REAL, &it, nullptr);
    timer_t tid{};
    sigevent sev{};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    timer_create(CLOCK_MONOTONIC, &sev, &tid);
    itimerspec its{};
    its.it_value.tv_nsec = 100000000;
    its.it_interval.tv_nsec = 100000000;
    timer_settime(tid, 0, &its, nullptr);

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0); // flags=0:不带 CLOEXEC
    itimerspec t{};
    t.it_value.tv_nsec = 100000000;
    t.it_interval.tv_nsec = 100000000;
    timerfd_settime(tfd, 0, &t, nullptr);

    g_real.store(0);
    g_usr1.store(0);
    std::fflush(stdout);
    pid = fork();
    if (pid == 0) {
        g_real.store(0);
        g_usr1.store(0);
        sleep_window(600);
        std::printf("[fork 子进程 B] SIGALRM(setitimer) %d 次,SIGUSR1(timer_create) %d 次"
                    " —— 都是 0,两件都没跟过来\n",
                    g_real.load(), g_usr1.load());
        std::fflush(stdout);
        _exit(0);
    }
    sleep_window(600);
    std::printf("[fork 父进程] 同窗口内 SIGALRM(setitimer) %d 次,SIGUSR1(timer_create) %d 次"
                " —— 武装只留在父进程\n",
                g_real.load(), g_usr1.load());
    waitpid(pid, &st, 0);

    std::printf("\n== E5d: exec 之后,只剩 fd 形态的能活 ==\n");
    // 清掉信号形态,只留 timerfd 过 exec
    it = {};
    setitimer(ITIMER_REAL, &it, nullptr);
    timer_delete(tid);
    char fdno[16];
    std::snprintf(fdno, sizeof fdno, "%d", tfd);
    std::fflush(stdout);
    pid = fork();
    if (pid == 0) {
        execl(argv[0], argv[0], "--exec-child", fdno, (char*)nullptr);
        _exit(127);
    }
    waitpid(pid, &st, 0);

    std::printf("\n== E5e: 矩阵怎么读 ==\n");
    std::printf("  信号形态三件(alarm/setitimer/timer_create)fork 后在子进程全部静默:fork 不继承;\n"
                "  exec 更会把信号处理器复位成默认,就算继承了也没人接;\n");
    std::printf("  timerfd 是 fd,fork 与 exec 都挡不住它(除非建的时候带 TFD_CLOEXEC)——\n"
                "  这正是「定时器要不要跨进程存活」选型的一票硬依据。\n");
    return 0;
}
