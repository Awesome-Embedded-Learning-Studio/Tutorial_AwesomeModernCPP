// 补测(修订轮): alarm 返回值的取整判别 —— 单点 0.7s 报 1 区分不了取整规则,补 0.4s 与已过期两点
// 口径: WSL2 6.18.33.2-microsoft-standard-WSL2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra, 计时
// CLOCK_MONOTONIC
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_hit = 0;

void on_alrm(int) {
    g_hit = g_hit + 1;
}

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

} // namespace

int main() {
    struct sigaction sa{};
    sa.sa_handler = on_alrm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr); // 判别点 3 的到期会落在睡眠里,装个 handler 兜底

    // 判别点 1: alarm(2) 睡 300ms,实际剩约 1.7s —— 向上取整报 2,截断会报 1
    uint64_t t0 = mono_ns();
    alarm(2);
    usleep(300000);
    unsigned r = alarm(5);
    double ms = double(mono_ns() - t0) / 1e6;
    std::printf(
        "[1] alarm(2) 后过 %.1f ms,实际剩约 %.2f s,alarm(5) 返回 %u(向上取整给 2,截断会给 1)\n", ms,
        2.0 - ms / 1000.0, r);
    alarm(0);

    // 判别点 2: alarm(2) 睡 1600ms,实际剩约 0.4s —— 向上取整报 1,四舍五入/截断都会给 0
    t0 = mono_ns();
    alarm(2);
    usleep(1600000);
    r = alarm(5);
    ms = double(mono_ns() - t0) / 1e6;
    std::printf(
        "[2] alarm(2) 后过 %.1f ms,实际剩约 %.2f s,alarm(5) 返回 %u(向上取整给 1,四舍五入会给 0)\n",
        ms, 2.0 - ms / 1000.0, r);
    alarm(0);

    // 判别点 3: alarm(1) 睡 1200ms,约定已到点 —— 报 0,handler 收到 1 次
    g_hit = 0;
    alarm(1);
    usleep(1200000);
    r = alarm(5);
    std::printf("[3] alarm(1) 后过 1200ms(约定已到点),alarm(5) 返回 %u,SIGALRM 到货 %d 次\n", r,
                (int)g_hit);
    alarm(0);
    return 0;
}
