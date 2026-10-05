// E3: 每轮重传的开销随 N 怎么长
// 场景: N 根管道里只有 0 号永远有 1 字节事件, 每轮"喂一个字节 -> 等待 -> 收走"。
// select 每轮要重建整个位图, poll 每轮要把整个数组拷进拷出内核, epoll 注册一次、每轮只带回就绪项。
// 计时 CLOCK_MONOTONIC, 每档跑 5 轮取中位数。select 受 1024 之界, 只测 64/512 两档。
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <unistd.h>
#include <vector>

static long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static double bench_select(int n, int rounds) {
    std::vector<std::array<int, 2>> pipes(n);
    for (auto& p : pipes)
        if (pipe(p.data()) != 0)
            return -1;
    int nfds_max = 0;
    for (auto& p : pipes)
        if (p[0] >= nfds_max)
            nfds_max = p[0] + 1;
    if (nfds_max > FD_SETSIZE) { // 一根管道吃两个 fd, N 大了 fd 号就过界
        for (auto& p : pipes) {
            close(p[0]);
            close(p[1]);
        }
        return -1;
    }
    (void)!write(pipes[0][1], "x", 1); // 首轮预置事件, 之后每轮自喂
    long t0 = now_ns();
    for (int r = 0; r < rounds; ++r) {
        fd_set set;
        FD_ZERO(&set); // 位图必须每轮重建: select 会把它改掉
        for (auto& p : pipes)
            FD_SET(p[0], &set);
        timeval tv{1, 0};
        (void)select(nfds_max, &set, nullptr, nullptr, &tv);
        char c;
        (void)!read(pipes[0][0], &c, 1);
        (void)!write(pipes[0][1], "x", 1);
    }
    long t1 = now_ns();
    for (auto& p : pipes) {
        close(p[0]);
        close(p[1]);
    }
    return (t1 - t0) / (double)rounds;
}

static double bench_poll(int n, int rounds) {
    std::vector<std::array<int, 2>> pipes(n);
    for (auto& p : pipes)
        if (pipe(p.data()) != 0)
            return -1;
    std::vector<pollfd> pfds;
    for (auto& p : pipes)
        pfds.push_back({p[0], POLLIN, 0});
    (void)!write(pipes[0][1], "x", 1); // 首轮预置事件, 之后每轮自喂
    long t0 = now_ns();
    for (int r = 0; r < rounds; ++r) {
        (void)poll(pfds.data(), pfds.size(), 1000); // 数组可复用, 但内核每轮进出全量拷贝
        char c;
        (void)!read(pipes[0][0], &c, 1);
        (void)!write(pipes[0][1], "x", 1);
    }
    long t1 = now_ns();
    for (auto& p : pipes) {
        close(p[0]);
        close(p[1]);
    }
    return (t1 - t0) / (double)rounds;
}

static double bench_epoll(int n, int rounds) {
    std::vector<std::array<int, 2>> pipes(n);
    for (auto& p : pipes)
        if (pipe(p.data()) != 0)
            return -1;
    int ep = epoll_create1(0);
    for (auto& p : pipes) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev); // 注册一次, 常驻内核
    }
    epoll_event evs[16];
    (void)!write(pipes[0][1], "x", 1); // 首轮预置事件, 之后每轮自喂
    long t0 = now_ns();
    for (int r = 0; r < rounds; ++r) {
        int k = epoll_wait(ep, evs, 16, 1000);
        (void)k;
        char c;
        (void)!read(pipes[0][0], &c, 1);
        (void)!write(pipes[0][1], "x", 1);
        (void)k;
    }
    long t1 = now_ns();
    close(ep);
    for (auto& p : pipes) {
        close(p[0]);
        close(p[1]);
    }
    return (t1 - t0) / (double)rounds;
}

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    rlimit high{65536, rl.rlim_max};
    setrlimit(RLIMIT_NOFILE, &high);
    const int rounds = 2000, reps = 5;

    struct Row {
        int n;
        double sel, pol, ep;
    };
    std::vector<Row> rows;
    for (int n : {64, 500, 4096}) {
        Row row{n, -1.0, 0.0, 0.0};
        std::vector<double> v;
        if (n <= 500) { // select: 一根管道两个 fd, 500 根就逼近 fd_set 的 1024 位
            v.clear();
            for (int i = 0; i < reps; ++i)
                v.push_back(bench_select(n, rounds));
            row.sel = median(v);
        }
        v.clear();
        for (int i = 0; i < reps; ++i)
            v.push_back(bench_poll(n, rounds));
        row.pol = median(v);
        v.clear();
        for (int i = 0; i < reps; ++i)
            v.push_back(bench_epoll(n, rounds));
        row.ep = median(v);
        rows.push_back(row);
    }
    std::printf("每轮耗时(us/round, %d 轮 x %d 次取中位, WSL2 口径):\n", rounds, reps);
    std::printf("%6s | %12s | %12s | %12s\n", "N", "select", "poll", "epoll");
    for (auto& r : rows) {
        char s[32];
        if (r.sel < 0)
            std::snprintf(s, sizeof s, "%s", "(fd 超界)");
        else
            std::snprintf(s, sizeof s, "%.2f", r.sel);
        std::printf("%6d | %12s | %12.2f | %12.2f\n", r.n, s, r.pol, r.ep);
    }
    std::printf("\n每轮进内核的字节数(只算等待调用本身):\n");
    for (auto& r : rows) {
        std::printf(
            "N=%d: select 位图 %zu B(重建后拷入), poll %d B 拷入拷出, epoll 只出就绪项 %zu B\n",
            r.n, sizeof(fd_set), r.n * (int)sizeof(pollfd),
            (size_t)(r.ep > 0 ? 1 : 1) * sizeof(epoll_event));
    }
    return 0;
}
