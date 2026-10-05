// E2: 一次醒来, 谁被扫了
// N 根管道, 其中 k 根有数据。三兄弟各自要"检查多少个 fd"才能找出就绪的那 k 根。
// select: 返回后必须逐位 FD_ISSET, 0..nfds-1 全扫
// poll:   返回后必须逐项查 revents, N 项全扫
// epoll:  wait 直接只把就绪的 k 个带回来
#include <array>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

static int make_pipes(std::vector<std::array<int, 2>>& pipes, int n) {
    pipes.resize(n);
    for (int i = 0; i < n; ++i)
        if (pipe(pipes[i].data()) != 0) {
            std::perror("pipe");
            return -1;
        }
    return 0;
}

int main() {
    const int N = 500; // 500 根管道 = 1000 个 fd, 读端 fd 最大 1001, 还在 select 的 1024 之内
    std::vector<std::array<int, 2>> pipes;
    if (make_pipes(pipes, N) != 0)
        return 1;
    std::printf("N=%d 根管道, 读端 fd 区间 [%d, %d]\n", N, pipes[0][0], pipes[N - 1][0]);

    for (int ready_count : {1, 64}) {
        // 选出 ready_count 根均匀分布的管道写 1 字节
        std::vector<int> ready_idx;
        for (int k = 0; k < ready_count; ++k)
            ready_idx.push_back(k * N / ready_count);
        for (int idx : ready_idx)
            (void)!write(pipes[idx][1], "x", 1);

        // ---- select ----
        fd_set set;
        FD_ZERO(&set);
        int nfds = 0;
        for (auto& p : pipes) {
            FD_SET(p[0], &set);
            if (p[0] >= nfds)
                nfds = p[0] + 1;
        }
        timeval tv{1, 0};
        int r = select(nfds, &set, nullptr, nullptr, &tv);
        long checks = 0;
        int found = 0;
        for (int fd = 0; fd < nfds; ++fd) { // 真实代码找就绪 fd 的姿势: 全扫
            ++checks;
            if (FD_ISSET(fd, &set))
                ++found;
        }
        std::printf("\n[就绪 %d / %d]\n", ready_count, N);
        std::printf(
            "select : 返回 %2d, 检查 %4ld 个 fd, 找到 %2d (进内核位图 %zu B, 出内核 %zu B)\n", r,
            checks, found, sizeof(fd_set), sizeof(fd_set));

        // ---- poll ----
        std::vector<pollfd> pfds;
        for (auto& p : pipes)
            pfds.push_back({p[0], POLLIN, 0});
        r = poll(pfds.data(), pfds.size(), 1000);
        checks = 0;
        found = 0;
        for (auto& pfd : pfds) {
            ++checks;
            if (pfd.revents & POLLIN)
                ++found;
        }
        std::printf("poll   : 返回 %2d, 检查 %4ld 个 fd, 找到 %2d (进内核 %zu B = %d x 8, "
                    "出内核同址改写)\n",
                    r, checks, found, pfds.size() * sizeof(pollfd), N);

        // ---- epoll ----
        int ep = epoll_create1(0);
        for (auto& p : pipes) {
            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.fd = p[0];
            epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);
        }
        epoll_event evs[128];
        r = epoll_wait(ep, evs, 128, 1000);
        checks = 0;
        for (int i = 0; i < r; ++i)
            ++checks;
        std::printf("epoll  : 返回 %2d, 检查 %4ld 个 fd (只数返回的), 出内核 %zu B = %d x %zu\n", r,
                    checks, (size_t)r * sizeof(epoll_event), r, sizeof(epoll_event));
        close(ep);

        // 清掉数据, 下一轮干净开局
        char buf[64];
        for (int idx : ready_idx)
            (void)!read(pipes[idx][0], buf, 1);
    }
    return 0;
}
