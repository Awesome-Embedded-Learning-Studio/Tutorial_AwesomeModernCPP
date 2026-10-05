// E6 SCM_RIGHTS fd 传递(Lmem03 欠的账):unix domain socketpair + sendmsg 的附属数据,
// 把一个已打开文件的 fd 传给另一个进程。三件事一次看清:
//   1. 接收方拿到的是「自己 fd 表里的新编号」,读到的内容从发送方离开的偏移继续——
//      传的是对同一个打开文件描述(open file description)的新引用,pos 共享;
//   2. 发送方 close 原始 fd 后,接收方照样读——是引用计数,不是移交;
//   3. 与另外两条获得 fd 的路径(open/fork)对照成表。
// /proc/self/fdinfo/<fd> 的 pos 字段是客观证人,每一步都打印。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e6_scm_rights.cpp -o e6_scm_rights
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* kPayload = "/home/charliechen/lp03_scratch/e6_payload.txt";

void send_fd(int sock, int fd) {
    char flag = 'F';
    struct iovec iov{&flag, 1};
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    sys_call("sendmsg", sendmsg, sock, &msg, 0);
}

int recv_fd(int sock) {
    char flag = 0;
    struct iovec iov{&flag, 1};
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    ssize_t r = sys_call("recvmsg", recvmsg, sock, &msg, 0);
    if (r != 1 || flag != 'F') {
        std::fprintf(stderr, "recvmsg 载荷异常\n");
        _exit(1);
    }
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
        std::fprintf(stderr, "没有收到 SCM_RIGHTS\n");
        _exit(1);
    }
    int fd = -1;
    std::memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}

void read_some(int fd, size_t n, const char* who) {
    char buf[128];
    ssize_t r = sys_call("read", read, fd, buf, n);
    buf[r] = '\0';
    std::printf("%s 读 %zd 字节:「%s」→ fdinfo pos=%ld\n", who, r, buf, fd_pos(fd));
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // 准备负载文件:6 行,每行 22 字节
    std::remove(kPayload);
    {
        std::FILE* f = std::fopen(kPayload, "w");
        for (int i = 0; i < 6; ++i)
            std::fprintf(f, "LINE-%d-0123456789ab\n", i);
        std::fclose(f);
    }

    // ---- 路径一:open——自己开出新描述,pos 从 0 开始 ----
    std::printf("== 路径一:open() ==\n");
    int fd_a = sys_call("open", open, kPayload, O_RDONLY);
    std::printf("open → fd=%d,pos=%ld\n", fd_a, fd_pos(fd_a));

    // ---- 路径二:fork——fd 编号复制,pos 共享 ----
    std::printf("\n== 路径二:fork 继承(父进程先读 16 字节再 fork) ==\n");
    read_some(fd_a, 16, "[父]");
    pid_t pid = sys_call("fork", fork);
    if (pid == 0) {
        usleep(200 * 1000);
        std::printf("[子 pid=%d] 继承的 fd=%d(编号没变),", getpid(), fd_a);
        read_some(fd_a, 16, "");
        std::printf("        ↑ 子进程接着父进程的 pos 读——同一个打开文件描述,偏移共享\n");
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    close(fd_a);

    // ---- 路径三:SCM_RIGHTS——接收方新编号,同一描述,发送方 close 后仍可用 ----
    std::printf("\n== 路径三:SCM_RIGHTS 传递(socketpair + sendmsg 附属数据) ==\n");
    int fd_c = sys_call("open", open, kPayload, O_RDONLY);
    read_some(fd_c, 16, "[发送方]");

    int sv[2];
    sys_call("socketpair", socketpair, AF_UNIX, SOCK_STREAM, 0, sv);
    pid = sys_call("fork", fork);
    if (pid == 0) { // 接收方
        close(sv[0]);
        int got = recv_fd(sv[1]);
        std::printf("[接收方 pid=%d] recvmsg 拿到 fd=%d(发送方那边它叫 fd=%d,编号是各自的)\n",
                    getpid(), got, fd_c);
        std::printf("[接收方] 它的 pos=%ld——和发送方共享偏移!接着读:\n", fd_pos(got));
        read_some(got, 16, "[接收方]");
        char sig = 0;
        sys_call(
            "recv signal", [](int s, void* b, size_t n) { return recv(s, b, n, 0); }, sv[1], &sig,
            (size_t)1);
        std::printf("[接收方] 收到信号:发送方已 close 原始 fd。继续读这个 fd:\n");
        read_some(got, 24, "[接收方]");
        std::printf("[接收方] 照样读得到——fd 表项是复制出来的新引用,struct file "
                    "的引用计数没归零,谁都没「移交」\n");
        close(got);
        _exit(0);
    }
    close(sv[1]);
    send_fd(sv[0], fd_c);
    std::printf("[发送方] SCM_RIGHTS 已发(fd=%d 装进附属数据),等接收方确认共享 pos……\n", fd_c);
    usleep(500 * 1000);
    close(fd_c);
    std::printf("[发送方] close(fd=%d)——原始 fd 关了\n", fd_c);
    sys_call(
        "send signal", [](int s, const void* b, size_t n) { return send(s, b, n, 0); }, sv[0], "C",
        (size_t)1);
    waitpid(pid, &st, 0);
    close(sv[0]);

    // ---- 三条路径对照表 ----
    std::printf("\n== 获得一个 fd 的三条路径 ==\n");
    std::printf(
        "| 路径         | fd 编号        | 打开文件描述      | pos     | 发送方 close 后 |\n");
    std::printf(
        "|--------------|----------------|-------------------|---------|-----------------|\n");
    std::printf(
        "| open()       | 本进程新分配    | 全新的一份         | 从 0 起  | ——              |\n");
    std::printf(
        "| fork 继承    | 编号原样复制    | 与父进程同一份     | 共享     | 子进程照用       |\n");
    std::printf(
        "| SCM_RIGHTS   | 接收方新分配    | 与发送方同一份     | 共享     | 接收方照用       |\n");
    std::printf("后两条拿到的是同一个「打开文件描述」的引用,SCM_RIGHTS 是把这份引用跨进程快递;\n"
                "Lmem03 一句带过的「fd 传到即实体传到」,证毕。\n");
    return 0;
}
