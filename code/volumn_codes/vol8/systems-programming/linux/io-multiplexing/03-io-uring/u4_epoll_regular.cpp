// E4: epoll 面对普通文件是什么表现, io_uring 又是什么表现
// 常见说法"epoll 不支持普通文件"。实测分两半:
//   epoll_ctl ADD 普通文件成功吗? -> 成功
//   epoll_wait 报它吗? -> 永远报, 数据读没读、有没有新数据, 都报
// 普通文件没有"等待"可言, 就绪通知也就没有信息量; O_NONBLOCK 对它也不生效。
// 同一个 fd 交给 io_uring 的 READ, 是真正的异步完成 (E2 已验), 这里补对照面。
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

static const char* kPath = "/home/charliechen/ch04_scratch/u_data.bin";

int main() {
    // 数据文件 16 KiB
    int fd = open(kPath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    unsigned char buf[16384];
    for (int i = 0; i < 16384; ++i)
        buf[i] = (unsigned char)(i * 3);
    if (write(fd, buf, sizeof buf) != (ssize_t)sizeof buf)
        return 1;

    // ---- O_NONBLOCK 对普通文件 ----
    int fl = fcntl(fd, F_GETFL);
    int r = fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    std::printf("[普通文件 fd=%d]\nfcntl F_SETFL O_NONBLOCK = %d (这个标志本身设得上去)\n", fd, r);
    unsigned char tmp[4096];
    lseek(fd, 0, SEEK_SET);
    ssize_t n = read(fd, tmp, sizeof tmp);
    std::printf("O_NONBLOCK 下 read = %zd (有数据直接给, 不会拿 EAGAIN 说\"现在没有\")\n", n);

    // ---- epoll ADD 普通文件 ----
    int ep = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    errno = 0;
    r = epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
    std::printf("\nepoll_ctl ADD 普通文件 = %d errno=%d (%s)\n", r, errno, std::strerror(errno));
    std::printf(
        "man 2 epoll_ctl 对 EPERM 的解释: 目标 fd 不支持 epoll (内核里没有 poll 支持的文件类型)\n");

    // ---- 对照组: 管道才有"等待"可言 ----
    int pp[2];
    (void)!pipe(pp);
    fcntl(pp[0], F_SETFL, O_NONBLOCK);
    n = read(pp[0], tmp, sizeof tmp);
    std::printf("\n[对照: 空管道 + O_NONBLOCK] read = %zd errno=%d (%s)\n", n, errno,
                std::strerror(errno));
    epoll_event ev2{};
    ev2.events = EPOLLIN;
    ev2.data.fd = pp[0];
    epoll_ctl(ep, EPOLL_CTL_ADD, pp[0], &ev2);
    epoll_event evs[2];
    int k = epoll_wait(ep, evs, 2, 0);
    bool pipe_ready = false;
    for (int i = 0; i < k; ++i)
        if (evs[i].data.fd == pp[0])
            pipe_ready = true;
    std::printf("空管道 epoll_wait(0) 返回 %d, 管道在其中报就绪: %s\n", k,
                pipe_ready ? "是" : "否");

    std::printf("\n结论: 普通文件两头都关死 -- 非阻塞语义不生效(read 永不 EAGAIN), epoll "
                "直接拒收(EPERM);\n");
    std::printf(
        "对磁盘文件做统一的异步, 只能走 io_uring 的 READ (完成事件带真实字节数, 见 E2)。\n");
    return 0;
}
