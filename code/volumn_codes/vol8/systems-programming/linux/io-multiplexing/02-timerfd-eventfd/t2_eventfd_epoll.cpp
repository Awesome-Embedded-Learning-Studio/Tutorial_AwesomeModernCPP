// E2: eventfd 挂进 epoll 之后的通知次数
// 一次写入 5, 三种组合下的"醒来次数"与"读法":
//   默认+LT : read 全取走清零 -> 醒 1 次
//   信号量+LT: 每次读只减 1, 计数器非 0 就仍然"可读" -> 连醒 5 次
//   信号量+ET: 边沿只报一次, 得靠"读到 EAGAIN"的纪律收尾 -> 醒 1 次
// 信号量 + LT 是值得认识的一档: 读一次不等于读空, 循环会一直被叫醒。
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

struct Result {
    int wakeups;
    int reads;
    long ms;
};

static Result run(int semaphore, int et) {
    int flags = EFD_NONBLOCK | (semaphore ? EFD_SEMAPHORE : 0);
    int efd = eventfd(0, flags);
    int ep = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN | (et ? static_cast<uint32_t>(EPOLLET) : 0u);
    ev.data.fd = efd;
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);

    uint64_t five = 5;
    if (write(efd, &five, 8) != 8) {
    }

    long t0 = now_ms();
    Result res{0, 0, 0};
    uint64_t total = 0;
    for (;;) {
        epoll_event evs[2];
        int k = epoll_wait(ep, evs, 2, 150);
        if (k == 0)
            break;
        ++res.wakeups;
        uint64_t v = 0;
        if (et) {
            // ET 纪律: 一次事件里读到 EAGAIN
            for (;;) {
                ssize_t r = read(efd, &v, 8);
                if (r < 0)
                    break;
                ++res.reads;
                total += v;
            }
        } else {
            if (read(efd, &v, 8) == 8) {
                ++res.reads;
                total += v;
            }
        }
    }
    res.ms = now_ms() - t0;
    std::printf("  [semaphore=%d et=%d] 醒 %d 次, read %d 次, 取走计数合计 %llu, 收敛耗时 %ld ms\n",
                semaphore, et, res.wakeups, res.reads, (unsigned long long)total, res.ms);
    close(ep);
    close(efd);
    return res;
}

int main() {
    std::printf("一次 write(5) 之后, 每次醒来读一次(ET 档按纪律读空):\n");
    std::printf("LT+默认:\n");
    run(0, 0);
    std::printf("LT+信号量:\n");
    run(1, 0);
    std::printf("ET+信号量:\n");
    run(1, 1);
    std::printf("\nLT+信号量那组: 计数器还剩 4 时 fd 仍处于可读状态, 每轮 epoll_wait 都再报,\n");
    std::printf("读 1 次 -> 剩 4 -> 又醒 -> ... 直到清零。这不是 bug, 是 LT "
                "的语义配上了\"读不完全清零\"的计数器。\n");
    return 0;
}
