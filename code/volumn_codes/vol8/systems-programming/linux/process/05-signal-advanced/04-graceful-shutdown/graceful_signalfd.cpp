// E4-a 优雅关闭:SIGTERM 走 signalfd 进事件循环,状态机收尾(《信号(下)》E4)
//
// 一个真的 mini prefork echo 服务器:
//   * 父进程:TCP listen + poll{listen, signalfd, worker 通道} —— accept 到的连接
//     经 SCM_RIGHTS 派发给 worker(轮询派发),自己不做业务
//   * worker×3:接到客户端 fd → 读一行请求 → 干活 250ms(让「在途」看得见)→ 回一行
//   * supervisor 子进程:t=300ms 给父进程发 SIGTERM
//   * client 子进程:t=100/250 各建一条连接并发请求;t=500 再试图新建连接(应被拒)
//
// 关停状态机(SIGTERM 从 signalfd 进来,不是 handler):
//   RUNNING →(sfd 可读)→ 停止接新连接(close listen)→ 通知 worker drain →
//   poll worker 的 pidfd 逐个 POLLIN → waitid(P_PIDFD) 收尸 → 全员退场 → 清理退出
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 graceful_signalfd.cpp -o graceful_signalfd &&
// ./graceful_signalfd
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <netinet/in.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

timespec t0 = [] {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}();

long ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec - t0.tv_sec) * 1000 + (ts.tv_nsec - t0.tv_nsec) / 1000000;
}

void msleep(long v) {
    timespec d{v / 1000, (v % 1000) * 1000000L};
    nanosleep(&d, nullptr);
}

const char* tag_of(int state) {
    switch (state) {
        case 0:
            return "RUNNING ";
        case 1:
            return "DRAINING";
        case 2:
            return "REAPING ";
        default:
            return "EXIT    ";
    }
}

int g_state = 0;
int g_lfd = -1; // listen fd:子进程进门先关自己这份继承(不然谁都不关门)
void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::printf("t=%3ldms [%s] ", ms(), tag_of(g_state));
    vprintf(fmt, ap);
    va_end(ap);
}

int pidfd_open_(pid_t pid, unsigned int flags) {
    return (int)syscall(SYS_pidfd_open, pid, flags);
}

struct worker_t {
    pid_t pid = -1;
    int pidfd = -1;
    int chan = -1; // 与父进程的 unix dgram socket(收派发的客户端 fd / drain 指令)
};

// 经 unix dgram 把一个 fd 发给 worker
bool send_fd(int chan, int fd, char tag) {
    struct {
        char body[1];
        char cmsg_buf[CMSG_SPACE(sizeof(int))];
    } msg{};
    msghdr mh{};
    iovec iov{&msg.body, 1};
    msg.body[0] = tag; // 'C'=带客户端 fd,'D'=drain 指令(无 cmsg)
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    if (tag == 'C') {
        mh.msg_control = msg.cmsg_buf;
        mh.msg_controllen = sizeof msg.cmsg_buf;
        cmsghdr* cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &fd, sizeof(int));
    }
    return sendmsg(chan, &mh, MSG_NOSIGNAL) >= 0;
}

// worker 进程:等通道消息;拿到客户端 fd 就读一行、干 250ms 活、回一行;
// 收到 'D' 后:干完手头这单再退,没活立即退
void worker_main(int id, int chan) {
    close(g_lfd); // 继承来的 listen fd:worker 不接客,关掉,免得父进程 close 时关不干净
    for (;;) {
        char body[1];
        char cmsg_buf[CMSG_SPACE(sizeof(int))];
        msghdr mh{};
        iovec iov{body, 1};
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cmsg_buf;
        mh.msg_controllen = sizeof cmsg_buf;
        ssize_t n = recvmsg(chan, &mh, 0);
        if (n <= 0)
            break;
        if (body[0] == 'D') {
            std::printf("t=%3ldms [w%d pid=%d] 收到 drain:队列已清空,退场\n", ms(), id,
                        (int)getpid());
            break;
        }
        int cfd = -1;
        for (cmsghdr* cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
            if (cm->cmsg_type == SCM_RIGHTS)
                memcpy(&cfd, CMSG_DATA(cm), sizeof(int));
        // 业务:读一行请求 → 250ms「在途」 → 回一行
        char req[64]{};
        read(cfd, req, sizeof req - 1);
        std::printf("t=%3ldms [w%d pid=%d] 接单:「%s」开始处理(250ms 在途)\n", ms(), id,
                    (int)getpid(), req);
        msleep(250);
        char rep[96];
        int len = snprintf(rep, sizeof rep, "ack(%s) by w%d\n", req, id);
        write(cfd, rep, (size_t)len);
        std::printf("t=%3ldms [w%d pid=%d] 完工:已回「%s」\n", ms(), id, (int)getpid(), rep);
        close(cfd);
    }
    _exit(0);
}

// supervisor:t=300ms 发 SIGTERM
void supervisor_main() {
    close(g_lfd);
    msleep(300);
    kill(getppid(), SIGTERM);
    _exit(0);
}

// client:t=50、t=150 各连一条(两条同时在途,SIGTERM 时 w1 手里正有活);
// t=500 试图再连一条(关停后应被拒)
void client_main(int port) {
    close(g_lfd);
    signal(SIGPIPE, SIG_IGN); // 关停后写已断的连接会收 SIGPIPE,client 不陪葬
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    msleep(50);
    int c1 = socket(AF_INET, SOCK_STREAM, 0); // 第 1 条:t=50 连+发
    connect(c1, (sockaddr*)&addr, sizeof addr);
    write(c1, "req-1", 5);
    msleep(100);
    int c2 = socket(AF_INET, SOCK_STREAM, 0); // 第 2 条:t=150 连+发(SIGTERM 时在途)
    connect(c2, (sockaddr*)&addr, sizeof addr);
    write(c2, "req-2", 5);

    char buf[128]{};
    read(c1, buf, sizeof buf - 1);
    buf[strcspn(buf, "\n")] = 0;
    std::printf("t=%3ldms [client] 第 1 条连接收到:「%s」\n", ms(), buf);
    char buf2[128]{};
    read(c2, buf2, sizeof buf2 - 1); // 这条在 SIGTERM 之后才完工:drain 的在途单
    buf2[strcspn(buf2, "\n")] = 0;
    std::printf("t=%3ldms [client] 第 2 条连接收到:「%s」(SIGTERM 时在途,drain 放它做完)\n", ms(),
                buf2);

    msleep(100); // 到 t=500 上下,服务器应已关门
    int c3 = socket(AF_INET, SOCK_STREAM, 0);
    int r = connect(c3, (sockaddr*)&addr, sizeof addr);
    std::printf("t=%3ldms [client] 关停后再连一条:connect → %d(%s)\n", ms(), r,
                r == -1 ? std::strerror(errno) : "竟然成功了!");
    _exit(0);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    // 关停信号先阻塞,再开 signalfd(纪律,见 E2)
    sigset_t shut;
    sigemptyset(&shut);
    sigaddset(&shut, SIGTERM);
    sigaddset(&shut, SIGINT);
    sigprocmask(SIG_BLOCK, &shut, nullptr);
    int sfd = signalfd(-1, &shut, SFD_NONBLOCK | SFD_CLOEXEC);

    // TCP listen
    g_lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int lfd = g_lfd;
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    bind(lfd, (sockaddr*)&addr, sizeof addr);
    listen(lfd, 16);
    socklen_t alen = sizeof addr;
    getsockname(lfd, (sockaddr*)&addr, &alen);
    int port = ntohs(addr.sin_port);

    // workers ×3:socketpair 通道 + pidfd
    worker_t ws[3];
    for (int i = 0; i < 3; ++i) {
        int sp[2];
        socketpair(AF_UNIX, SOCK_DGRAM, 0, sp);
        pid_t pid = fork();
        if (pid == 0) {
            close(sp[0]);
            worker_main(i, sp[1]);
        }
        close(sp[1]);
        ws[i] = {pid, pidfd_open_(pid, 0), sp[0]};
    }
    std::printf("t=%3ldms [RUNNING ] 服务器就绪:127.0.0.1:%d,3 个 worker(pid %d/%d/%d),\n"
                "    SIGTERM/SIGINT 已阻塞并挂上 signalfd;supervisor 将在 t=300ms 发 SIGTERM\n",
                ms(), port, (int)ws[0].pid, (int)ws[1].pid, (int)ws[2].pid);

    pid_t sup = fork();
    if (sup == 0)
        supervisor_main();
    pid_t cli = fork();
    if (cli == 0)
        client_main(port);

    int alive = 3, dispatched = 0;
    while (g_state < 3) {
        pollfd pf[5];
        int n = 0;
        int slot_listen = -1, slot_sfd = -1, slot_chan[3] = {-1, -1, -1};
        if (g_state == 0) { // 只有 RUNNING 才挂 listen
            slot_listen = n;
            pf[n++] = {lfd, POLLIN, 0};
        }
        slot_sfd = n;
        pf[n++] = {sfd, POLLIN, 0};
        if (g_state == 1) { // DRAINING:盯 worker 的 pidfd 等退场
            for (int i = 0; i < 3; ++i)
                if (ws[i].pidfd >= 0) {
                    slot_chan[i] = n;
                    pf[n++] = {ws[i].pidfd, POLLIN, 0};
                }
        }
        int r = poll(pf, (nfds_t)n, 1500);
        if (r <= 0)
            continue;

        if (slot_listen >= 0 && (pf[slot_listen].revents & POLLIN) && g_state == 0) {
            int cfd = accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK);
            if (cfd >= 0) {
                int w = dispatched % 3;
                send_fd(ws[w].chan, cfd, 'C');
                log("accept 新连接 → 派发给 w%d\n", w);
                ++dispatched;
                close(cfd); // 派发出去后父进程这份引用关掉(描述归 worker)
            }
        }
        if (pf[slot_sfd].revents & POLLIN) {
            signalfd_siginfo info[2];
            ssize_t got = read(sfd, info, sizeof info);
            for (int i = 0; i < got / (ssize_t)sizeof(signalfd_siginfo); ++i)
                log("signalfd 读到 signo=%d(%s)——信号作为事件进入循环,不是 handler\n",
                    info[i].ssi_signo, info[i].ssi_signo == SIGTERM ? "SIGTERM" : "SIGINT");
            if (g_state == 0) {
                g_state = 1;
                close(lfd); // 状态机第一步:关门,不再接新连接
                log("状态机:close(listen_fd),停止接新连接\n");
                for (int i = 0; i < 3; ++i)
                    send_fd(ws[i].chan, -1, 'D');
                log("状态机:向 3 个 worker 发 drain 指令(干完在途就退)\n");
            }
        }
        if (g_state == 1) {
            for (int i = 0; i < 3; ++i) {
                if (ws[i].pidfd < 0 || slot_chan[i] < 0)
                    continue;
                if (pf[slot_chan[i]].revents & POLLIN) {
                    siginfo_t si{};
                    if (waitid((idtype_t)P_PIDFD, (id_t)ws[i].pidfd, &si, WEXITED) == 0) {
                        log("w%d(pid=%d)的 pidfd 可读 → waitid(P_PIDFD) 收尸完成\n", i,
                            (int)ws[i].pid);
                        close(ws[i].pidfd);
                        ws[i].pidfd = -1;
                        --alive;
                        if (alive == 0) {
                            g_state = 2;
                            log("状态机:3 个 worker 全部退场,收尸完毕\n");
                            g_state = 3;
                            log("清理退场:关 signalfd、收 supervisor/client\n");
                        }
                    }
                }
            }
        }
    }
    waitpid(sup, nullptr, 0);
    waitpid(cli, nullptr, 0);
    close(sfd);
    std::printf("t=%3ldms [EXIT    ] 服务进程退出(优雅关闭完成)\n", ms());
    return 0;
}
