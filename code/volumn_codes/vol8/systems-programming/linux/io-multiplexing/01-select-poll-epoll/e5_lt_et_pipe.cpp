// E5: LT 与 ET 在管道上的最小对照
// 管内一次写入 60000 字节, 每次醒来只读 4096:
//   LT: 只要管内还有数据, 每次 epoll_wait 都再报 -> 连续被叫醒 15 次
//   ET: 只有"空 -> 有"的边沿报一次 -> 一次通知后剩下的数据没人再提, 直到新字节到达
// epoll 的机制课在网络卷讲透了, 这里只给管道版的行为对照。衔接:
// documents/vol8-domains/networking/02-epoll-io-multiplexing.md
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

static long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main() {
    int p[2];
    (void)!pipe(p);
    // 管道容量 65536, 一次写 60000 放得下
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    fcntl(p[1], F_SETFL, O_NONBLOCK);

    char wbuf[60000];
    for (int i = 0; i < 60000; ++i)
        wbuf[i] = 'a';

    // ---- LT ----
    {
        int ep = epoll_create1(0);
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);

        ssize_t n = write(p[1], wbuf, sizeof wbuf);
        std::printf("LT: 写入 %zd 字节, 每次醒来只读 4096\n", n);
        long t0 = now_ms();
        int wakeups = 0;
        ssize_t total = 0;
        for (;;) {
            epoll_event evs[4];
            int k = epoll_wait(ep, evs, 4, 200); // 200 ms 没动静就收工
            if (k == 0)
                break;
            ++wakeups;
            char buf[4096];
            ssize_t r = read(p[0], buf, sizeof buf); // 故意不读空
            total += r;
        }
        std::printf("LT: 被叫醒 %d 次, 累计读到 %zd 字节, 收敛耗时 %ld ms\n", wakeups, total,
                    now_ms() - t0);
        close(ep);
    }

    // ---- ET ----
    {
        int ep = epoll_create1(0);
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = p[0];
        epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);

        ssize_t n = write(p[1], wbuf, sizeof wbuf);
        std::printf("\nET: 写入 %zd 字节, 醒来只读一次 4096\n", n);
        long t0 = now_ms();
        int wakeups = 0;
        ssize_t total = 0;
        epoll_event evs[4];
        int k = epoll_wait(ep, evs, 4, -1); // 第一次边沿, 一定来
        ++wakeups;
        char buf[4096];
        total += read(p[0], buf, sizeof buf); // 只读一次

        k = epoll_wait(ep, evs, 4, 300); // 之后还有 55904 字节躺在管里
        std::printf("ET: 醒来 %d 次后再等 300 ms -> 返回 %d (管内仍剩 %zd 字节, 无人再报)\n",
                    wakeups, k, n - total);

        (void)!write(p[1], "z", 1); // 新字节 = 新边沿
        k = epoll_wait(ep, evs, 4, 300);
        std::printf("ET: 再写入 1 字节 -> epoll_wait 返回 %d, 这一次按纪律循环读到 EAGAIN:\n", k);
        int reads = 0;
        for (;;) {
            ssize_t r = read(p[0], buf, sizeof buf);
            if (r < 0) {
                std::printf("      read -> %zd (%s), 共 %d 次读完\n", r, strerror(errno), reads);
                break;
            }
            total += r;
            ++reads;
        }
        k = epoll_wait(ep, evs, 4, 200);
        std::printf("ET: 读空后再等 200 ms -> 返回 %d, 安静\n", k);
        std::printf("ET: 总计醒来 %d 次, 读到 %zd 字节, 耗时 %ld ms\n", wakeups + 1, total,
                    now_ms() - t0);
        close(ep);
    }
    return 0;
}
