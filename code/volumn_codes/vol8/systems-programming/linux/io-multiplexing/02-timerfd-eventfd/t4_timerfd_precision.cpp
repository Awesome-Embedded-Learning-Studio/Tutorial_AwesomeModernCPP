// E4: timerfd 的到期间隔有多稳
// 1ms 与 10ms 两档周期, 连续收 500/300 档, 记录相邻两次 read 醒来的间隔,
// 给 min / 中位 / p90 / p99 / max。计时 CLOCK_MONOTONIC。WSL2 口径。
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>
#include <vector>

static long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static void bench(long interval_ns, int ticks) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    itimerspec its{};
    its.it_value = {0, interval_ns};
    its.it_interval = {0, interval_ns};
    timerfd_settime(tfd, 0, &its, nullptr);

    std::vector<long> stamps;
    stamps.reserve(ticks + 1);
    uint64_t v;
    while ((int)stamps.size() < ticks) {
        if (read(tfd, &v, 8) < 0 && errno == EAGAIN) {
            pollfd pf{tfd, POLLIN, 0};
            poll(&pf, 1, -1);
            continue;
        }
        stamps.push_back(now_ns());
    }
    std::vector<long> d; // 相邻间隔
    for (size_t i = 1; i < stamps.size(); ++i)
        d.push_back(stamps[i] - stamps[i - 1]);
    std::sort(d.begin(), d.end());
    auto us = [](long ns) { return ns / 1000.0; };
    std::printf(
        "周期 %4ld us, 收 %d 档, 相邻间隔(us): min=%.1f 中位=%.1f p90=%.1f p99=%.1f max=%.1f\n",
        interval_ns / 1000, ticks, us(d.front()), us(d[d.size() / 2]),
        us(d[(size_t)(d.size() * 0.90)]), us(d[(size_t)(d.size() * 0.99)]), us(d.back()));
    close(tfd);
}

int main() {
    bench(1000000, 500);  // 1ms x 500
    bench(10000000, 300); // 10ms x 300
    return 0;
}
