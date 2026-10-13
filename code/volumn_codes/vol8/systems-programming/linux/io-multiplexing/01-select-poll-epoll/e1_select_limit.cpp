// E1: select 的 1024 上限到底在谁那里
// 观察点:
//  (a) FD_SETSIZE 是 glibc 的编译期常量
//  (b) FD_SET(fd >= FD_SETSIZE) 在 _FORTIFY_SOURCE 下的行为(子进程里试,主进程活着记账)
//  (c) 内核对 nfds 的界:先把 RLIMIT_NOFILE 压到 1024 再抬到 8192,同一句 select(nfds=1025) 两种结局
//  (d) 同一批 1025+ 个 fd,poll 与 epoll 直接可用
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

int main() {
    std::printf("FD_SETSIZE = %d (glibc 编译期常量, fd_set 位图 %zu 字节)\n", FD_SETSIZE,
                sizeof(fd_set));

    // ---- (b) 子进程里做越界 FD_SET, 活下来的是主进程 ----
    pid_t pid = fork();
    if (pid == 0) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(1024, &set); // 越界写位图, 带守卫的构建会在这里拦下
        std::printf("child: FD_SET(1024) returned, no guard tripped\n");
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st))
        std::printf("child: FD_SET(1024) -> 被 SIGABRT 终止 (WTERMSIG=%d, wait status %d), "
                    "glibc 守卫把越界拦成了硬失败\n",
                    WTERMSIG(st), st);
    else
        std::printf("child: FD_SET(1024) 未被拦, 静默越界写栈 (status %d)\n", st);

    // ---- 备好 1050 根管道, 让 fd 号与 poll 条目数都越过 1024 ----
    std::vector<std::array<int, 2>> pipes;
    pipes.reserve(1050);
    for (int i = 0; i < 1050; ++i) {
        int p[2];
        if (pipe(p) != 0) {
            std::perror("pipe");
            return 1;
        }
        pipes.push_back({p[0], p[1]});
    }
    int max_fd = pipes.back()[0];
    std::printf("开了 %zu 根管道, 最大 fd = %d\n", pipes.size(), max_fd);

    // ---- (c) 内核的界是 RLIMIT_NOFILE, 不是 1024 ----
    rlimit rl{};
    getrlimit(RLIMIT_NOFILE, &rl);
    std::printf("初始 RLIMIT_NOFILE: soft=%llu hard=%llu\n", (unsigned long long)rl.rlim_cur,
                (unsigned long long)rl.rlim_max);

    // 手工位图: 2048 位, 比内核要写的 1025 位大, 越过 glibc 的 fd_set 尺寸
    unsigned char big[256];
    std::memset(big, 0, sizeof(big));
    // 只把第 511 根管道的读端(fd 号 3+2*511=1025, 越过 1024)放进去
    int target = pipes[511][0];
    std::printf("关注 fd=%d (第 512 根管道读端), 手工置位 bit %d\n", target, target);
    big[target / 8] |= 1u << (target % 8);

    auto run_select = [&](int nfds) {
        fd_set* set = reinterpret_cast<fd_set*>(big);
        timeval tv{0, 0};
        errno = 0;
        int r = select(nfds, set, nullptr, nullptr, &tv);
        std::printf("select(nfds=%d) = %d errno=%d (%s)\n", nfds, r, errno, std::strerror(errno));
        if (r > 0 && (big[target / 8] & (1u << (target % 8))))
            std::printf("  -> fd=%d 报告就绪 (bit 仍在位图里)\n", target);
        return r;
    };

    // 数据先就位: 之后两个 rlimit 档位下 fd=1025 都真的有数据可读
    (void)!write(pipes[511][1], "x", 1);

    // 先压到 1024: fd=1025 有数据、位图里在册, 看 select 怎么裁决
    rlimit low{1024, rl.rlim_max};
    setrlimit(RLIMIT_NOFILE, &low);
    std::printf("\n[RLIMIT_NOFILE soft=1024]\n");
    int r_low = run_select(1026);
    std::printf("  select 返回 %d -> 内核对 select 的 nfds 不查 rlimit, fd>1024 照常工作\n", r_low);
    // poll 对照: man poll(2) 写着 nfds 超过 RLIMIT_NOFILE 会 EINVAL, 实测一下
    {
        std::vector<pollfd> pfds;
        for (auto& p : pipes)
            pfds.push_back({p[0], POLLIN, 0});
        errno = 0;
        int rp = poll(pfds.data(), pfds.size(), 0);
        std::printf("  poll(%zu 个条目) = %d errno=%d (%s)\n", pfds.size(), rp, errno,
                    std::strerror(errno));
    }

    // 抬回高位: 位图给足, select 依旧不看 1024
    rlimit high{8192, rl.rlim_max};
    setrlimit(RLIMIT_NOFILE, &high);
    std::printf("[RLIMIT_NOFILE soft=8192]\n");
    std::memset(big, 0, sizeof(big));
    big[target / 8] |= 1u << (target % 8);
    int r1 = run_select(1026);
    std::printf("  select 返回 %d: 与低位档一致 -> 1024 之界只在 glibc 的 fd_set 里\n", r1);

    // ---- (d) 同一批 fd 交给 poll 与 epoll ----
    std::vector<pollfd> pfds;
    for (auto& p : pipes)
        pfds.push_back({p[0], POLLIN, 0});
    int rp = poll(pfds.data(), pfds.size(), 0);
    std::printf("\npoll(%zu 个 fd) = %d, 无 glibc 上限\n", pfds.size(), rp);

    int ep = epoll_create1(0);
    for (auto& p : pipes) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = p[0];
        epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);
    }
    epoll_event evs[8];
    int re = epoll_wait(ep, evs, 8, 0);
    std::printf("epoll: ADD %zu 个 fd 全部成功, wait(0) 返回 %d 个就绪\n", pipes.size(), re);
    for (int i = 0; i < re; ++i)
        std::printf("  就绪: fd=%d EPOLLIN\n", evs[i].data.fd);
    return 0;
}
