// E5: 一张表收三类事件源
// pipe(数据) + eventfd(另一个线程的门铃) + timerfd(周期 250ms) 全部挂进同一个 epoll,
// 单线程 wait 分发。这就是"一切化为 fd"的落点: 调度它们的只有一个循环。
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

static long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main() {
    int pd[2];
    (void)!pipe(pd);
    int efd = eventfd(0, EFD_NONBLOCK);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    itimerspec its{};
    its.it_value = {0, 250000000};
    its.it_interval = {0, 250000000};
    timerfd_settime(tfd, 0, &its, nullptr);

    int ep = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = pd[0];
    epoll_ctl(ep, EPOLL_CTL_ADD, pd[0], &ev);
    ev.events = EPOLLIN;
    ev.data.fd = efd;
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);
    ev.events = EPOLLIN;
    ev.data.fd = tfd;
    epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev);

    std::atomic<bool> stop{false};
    std::thread worker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        (void)!write(pd[1], "hello", 5); // 事件源 1: 管道来数据
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        uint64_t one = 1;
        (void)!write(efd, &one, 8); // 事件源 2: 门铃
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        uint64_t two = 2;
        (void)!write(efd, &two, 8); // 门铃再按两次(一次 write)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        (void)!write(pd[1], "quit", 4); // 收工信号
        stop = true;
    });

    long t0 = now_ms();
    int n_timer = 0, n_event = 0, n_pipe = 0;
    for (;;) {
        epoll_event evs[8];
        int k = epoll_wait(ep, evs, 8, 1000);
        for (int i = 0; i < k; ++i) {
            int fd = evs[i].data.fd;
            if (fd == tfd) {
                uint64_t v;
                (void)!read(tfd, &v, 8);
                ++n_timer;
                std::printf("t=%4ld ms  timerfd 第 %2d 次到期\n", now_ms() - t0, n_timer);
            } else if (fd == efd) {
                uint64_t v;
                (void)!read(efd, &v, 8);
                ++n_event;
                std::printf("t=%4ld ms  eventfd 门铃, 本次取走计数 %llu\n", now_ms() - t0,
                            (unsigned long long)v);
            } else if (fd == pd[0]) {
                char buf[16] = {};
                ssize_t r = read(pd[0], buf, sizeof buf - 1);
                ++n_pipe;
                std::printf("t=%4ld ms  pipe 收到 %zd 字节: \"%s\"\n", now_ms() - t0, r, buf);
                if (std::strcmp(buf, "quit") == 0) {
                    worker.join();
                    std::printf("\n循环结束: timerfd %d 次, eventfd %d 次, pipe %d 次, "
                                "全部由同一个 epoll_wait 分发\n",
                                n_timer, n_event, n_pipe);
                    return 0;
                }
            }
        }
    }
}
