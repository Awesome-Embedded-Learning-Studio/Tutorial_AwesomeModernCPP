// E3: timerfd 的本体三件事
// (i)  一次性: it_interval=0, 到期 read 返回 1, 再 read 直接 EAGAIN
// (ii) 周期与合并: 100ms 一档, 读方睡过头 350ms, 一次 read 拿到 3 (错过的档被并成一个数)
// (iii) 撤销与改期: disarm 后永不再响; 改 interval 立即生效
// 全程用 /proc/self/fdinfo 直接看内核里的剩余时间与到期计数。
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

static long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void dump_fdinfo(int fd, const char* when) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd);
    std::printf("--- fdinfo(%d) %s ---\n", fd, when);
    int f = open(path, O_RDONLY);
    char buf[2048];
    ssize_t n = read(f, buf, sizeof buf - 1);
    close(f);
    buf[n > 0 ? n : 0] = 0;
    std::printf("%s", buf);
}

static uint64_t try_read(int fd) {
    uint64_t v = 0;
    ssize_t r = read(fd, &v, 8);
    if (r < 0)
        std::printf("  read = -1 (%s)\n", std::strerror(errno));
    else
        std::printf("  read = %llu\n", (unsigned long long)v);
    return v;
}

int main() {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    dump_fdinfo(tfd, "刚创建, 未设定");

    // ---- (i) 一次性 ----
    itimerspec its{};
    its.it_value = {0, 200000000}; // 200ms
    timerfd_settime(tfd, 0, &its, nullptr);
    std::printf("\n[i] 一次性 200ms, it_interval=0\n");
    dump_fdinfo(tfd, "设定后(剩约 200ms)");
    long t0 = now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    dump_fdinfo(tfd, "60ms 时(剩约 140ms)");
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(tfd, &rf);
    timeval tv{2, 0};
    select(tfd + 1, &rf, nullptr, nullptr, &tv);
    std::printf("  select 报就绪, 距设定 %ld ms\n", now_ms() - t0);
    try_read(tfd);
    try_read(tfd);

    // ---- (ii) 周期与合并 ----
    its = {};
    its.it_value = {0, 100000000};
    its.it_interval = {0, 100000000}; // 100ms 一档
    timerfd_settime(tfd, 0, &its, nullptr);
    std::printf("\n[ii] 周期 100ms, 读方故意睡 350ms 再来:\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    dump_fdinfo(tfd, "350ms 时(约 3 档已过)");
    uint64_t ticks = try_read(tfd);
    std::printf("  一次 read 报的是错过的档数: %llu (不丢数据, 但也不排队报 3 次)\n",
                (unsigned long long)ticks);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    try_read(tfd);

    // ---- (iii) 撤销与改期 ----
    std::printf("\n[iii] 改 interval 100ms -> 30ms, 再撤掉:\n");
    its.it_interval = {0, 30000000};
    timerfd_settime(tfd, 0, &its, nullptr); // 相对模式: 新周期从此刻起算
    long s0 = now_ms();
    for (int i = 0; i < 5; ++i) {
        uint64_t v = 0;
        if (read(tfd, &v, 8) < 0 && errno == EAGAIN) {
            pollfd pf{tfd, POLLIN, 0};
            poll(&pf, 1, -1);
        }
        if (read(tfd, &v, 8) == 8)
            std::printf("  t+%3ld ms 到期 (30ms 档)\n", now_ms() - s0);
    }
    its = {}; // it_value=0 -> 撤销
    timerfd_settime(tfd, 0, &its, nullptr);
    dump_fdinfo(tfd, "撤销后(剩余时间为 0)");
    fd_set rf2;
    FD_ZERO(&rf2);
    FD_SET(tfd, &rf2);
    timeval tv2{0, 300000};
    int r = select(tfd + 1, &rf2, nullptr, nullptr, &tv2);
    std::printf("  撤销后等 300ms: select 返回 %d (不再有到期事件)\n", r);
    return 0;
}
