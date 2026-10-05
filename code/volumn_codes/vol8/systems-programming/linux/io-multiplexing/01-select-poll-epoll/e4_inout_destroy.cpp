// E4: select 会改写调用时传进去的参数
// (i) timeout 剩余量: Linux 的 select 返回时把 struct timeval 改成"没耗完的时间"
// (ii) fd_set 被消耗: 返回后位图里只剩"这次就绪的", 不重建就再调, 别的 fd 的新事件收不到
#include <chrono>
#include <cstdio>
#include <ctime>
#include <poll.h>
#include <sys/select.h>
#include <thread>
#include <unistd.h>

static long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main() {
    int pa[2], pb[2];
    (void)!pipe(pa);
    (void)!pipe(pb);
    long t0 = now_ms();

    // ---- (i) timeout 剩余量 ----
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        (void)!write(pa[1], "x", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // t+700ms 给 B 喂事件
        (void)!write(pb[1], "y", 1);
    });

    fd_set set;
    FD_ZERO(&set);
    FD_SET(pa[0], &set);
    timeval tv{2, 0}; // 预算 2000 ms
    int r = select(pa[0] + 1, &set, nullptr, nullptr, &tv);
    long t_ret = now_ms() - t0;
    std::printf("[i] select 返回 %d, 距开始 %ld ms, timeout 结构体剩余 %ld.%03ld s\n", r, t_ret,
                tv.tv_sec, tv.tv_usec / 1000L);
    std::printf("    300 ms 的事件用掉 300, 剩余约 1700 -> select 改写了入参 (POSIX 未定义, Linux "
                "的行为)\n");

    // ---- (ii) fd_set 被消耗 ----
    char c;
    (void)!read(pa[0], &c, 1); // 把 A 收干, 数据不滞留
    std::printf("\n[ii] 位图初始挂 A(fd=%d) 与 B(fd=%d), 先看第一轮后位图剩什么\n", pa[0], pb[0]);

    fd_set set2;
    FD_ZERO(&set2);
    FD_SET(pa[0], &set2);
    FD_SET(pb[0], &set2);
    // 此时 A 无数据 B 无数据, select 应超时; 但 A 在 t+300ms 已经收干, B 的数据在 t+700ms
    timeval tv2{0, 200000}; // 200 ms
    long s0 = now_ms();
    r = select(pb[0] + 1, &set2, nullptr, nullptr, &tv2);
    std::printf("第一轮(挂 A+B, 都无数据): 返回 %d, 耗时 %ld ms, 位图里剩的 fd 数 = ", r,
                now_ms() - s0);
    {
        int left = 0;
        for (int fd = 0; fd < FD_SETSIZE; ++fd)
            if (FD_ISSET(fd, &set2))
                ++left;
        std::printf("%d\n", left);
    }

    // 等到 t+700ms 之后: B 里有字节了。但 set2 已经被上一轮清成只剩 A+B 中的"就绪者"(即 0 个)
    std::this_thread::sleep_until(std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(450));
    pollfd pf{pb[0], POLLIN, 0};
    poll(&pf, 1, 0);
    std::printf("此刻 B 里有数据吗: poll 说 revents=%s\n",
                (pf.revents & POLLIN) ? "POLLIN(有)" : "无");

    // 不重建, 拿被消耗过的 set2 再调一次
    timeval tv3{0, 300000};
    s0 = now_ms();
    r = select(pb[0] + 1, &set2, nullptr, nullptr, &tv3);
    std::printf(
        "不重建直接再 select: 返回 %d, 耗时 %ld ms -> B 的事件被漏掉 (位图已被上一轮吃空)\n", r,
        now_ms() - s0);

    // 重建后对照
    FD_ZERO(&set2);
    FD_SET(pa[0], &set2);
    FD_SET(pb[0], &set2);
    s0 = now_ms();
    r = select(pb[0] + 1, &set2, nullptr, nullptr, &tv3);
    std::printf("重建后再 select: 返回 %d, 耗时 %ld ms -> B 立刻就绪 (fd=%d, %s)\n", r,
                now_ms() - s0, pb[0], FD_ISSET(pb[0], &set2) ? "B 在位" : "B 不在");

    producer.join();
    return 0;
}
