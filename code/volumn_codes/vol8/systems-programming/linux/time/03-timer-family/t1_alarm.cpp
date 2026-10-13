// E1: alarm —— 秒级粒度的最老定时器:实测到期时刻、改约的返回值、撤销
// 口径: WSL2 6.18.33.2, g++ 16.2.1, -std=c++20 -O2 -Wall -Wextra, 计时 CLOCK_MONOTONIC
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_hits = 0;

void on_alarm(int) {
    g_hits = g_hits + 1;
}

uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

double ms_between(uint64_t a, uint64_t b) {
    return double(b - a) / 1e6;
}

} // namespace

int main() {
    struct sigaction sa{};
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);

    std::printf("== E1a: alarm(2) 到点有多准 ==\n");
    auto t0 = mono_ns();
    alarm(2);
    while (g_hits == 0)
        ;
    auto t1 = mono_ns();
    std::printf("alarm(2) 从下达到 SIGALRM: %.3f ms(粒度只有整秒,亚秒需求它管不了)\n",
                ms_between(t0, t1));

    std::printf("\n== E1b: 再约一次是「改约」不是「加约」 ==\n");
    g_hits = 0;
    alarm(1);
    usleep(300000);             // 睡 300ms,让第一次约定剩约 0.7s
    unsigned remain = alarm(5); // 新约定顶掉旧的,返回旧约定的剩余
    auto t2 = mono_ns();
    std::printf("alarm(1) 过 300ms 后再 alarm(5),返回剩余=%u s(内核向上取整到整秒,300ms 后\n"
                "约剩 0.7s,报成 1)\n",
                remain);
    alarm(0); // 撤销
    usleep(1200000);
    std::printf("alarm(0) 撤销后过了 1.2s,SIGALRM 次数=%d(0=撤销生效,第二个约定从未到点)\n",
                (int)g_hits);
    (void)t2;

    std::printf("\n== E1c: 每进程一个,想再来一个就没了 ==\n");
    std::printf("alarm 的状态记在进程头上,一进程同时只有一笔;新约顶旧约(E1b 已证)。\n"
                "多路定时、亚秒粒度、跨 exec 存活,都得换后面几件工具 —— 见 E2/E3 与 E5。\n");
    return 0;
}
