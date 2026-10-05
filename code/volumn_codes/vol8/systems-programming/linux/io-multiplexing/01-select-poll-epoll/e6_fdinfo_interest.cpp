// E6: epoll 的兴趣表躺在内核里, /proc/self/fdinfo 能直接看
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

static void dump_fdinfo(int fd, const char* when) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd);
    std::printf("--- fdinfo(%d) %s ---\n", fd, when);
    int f = open(path, O_RDONLY);
    char buf[4096];
    ssize_t n = read(f, buf, sizeof buf - 1);
    close(f);
    buf[n > 0 ? n : 0] = 0;
    std::printf("%s", buf);
}

int main() {
    int pa[2], pb[2];
    (void)!pipe(pa);
    (void)!pipe(pb);
    int efd = eventfd(0, 0);

    int ep = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = pa[0];
    epoll_ctl(ep, EPOLL_CTL_ADD, pa[0], &ev);
    ev.events = EPOLLOUT;
    ev.data.fd = pb[0];
    epoll_ctl(ep, EPOLL_CTL_ADD, pb[0], &ev);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = efd;
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);
    std::printf(
        "注册: pa读端(fd=%d, EPOLLIN) pb读端(fd=%d, EPOLLOUT) eventfd(fd=%d, EPOLLIN|EPOLLET)\n",
        pa[0], pb[0], efd);

    dump_fdinfo(ep, "ADD 三个之后");
    std::printf("tfd 行 = 兴趣表里在册的 fd, events 是十六进制: 0x19=EPOLLIN|EPOLLERR|EPOLLHUP, "
                "0x1c=EPOLLOUT|ERR|HUP, 0x80000019 再加 EPOLLET\n");
    std::printf("内核给每个注册都自动补上 EPOLLERR|EPOLLHUP, 所以看到的不是裸的 1 与 4\n");

    epoll_ctl(ep, EPOLL_CTL_DEL, pb[0], nullptr);
    dump_fdinfo(ep, "DEL pb 之后");

    uint64_t one = 3;
    (void)!write(efd, &one, 8);
    dump_fdinfo(efd, "eventfd 写入 3 之后 (counter 是内核态, fdinfo 可见)");
    uint64_t v;
    (void)!read(efd, &v, 8);
    std::printf("read(eventfd) = %llu, 读走即清零\n", (unsigned long long)v);
    dump_fdinfo(efd, "读走之后");
    return 0;
}
