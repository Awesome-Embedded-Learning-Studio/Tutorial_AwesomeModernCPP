// E4b 同样三条消息(10/50/200 字节),走 mq 与走 pipe 各一遍:
// mq 收三次,长度 10/50/200 一条一条原样;pipe 收 64 字节一块,第一块里
// 消息 1、消息 2 和消息 3 的开头挤成一团——字节流没有边界的直观对照。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_vs_pipe.cpp -o e4_mq_vs_pipe
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <mqueue.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
// 消息 1:10 字节 '1';消息 2:50 字节 '2';消息 3:200 字节 '3'
struct Msg {
    char tag;
    size_t len;
} kMsgs[3] = {{'1', 10}, {'2', 50}, {'3', 200}};

void fill(char* buf, const Msg& m) {
    std::memset(buf, m.tag, m.len);
}

// 把一整条消息写进 pipe
void send_pipe(int fd, const Msg& m) {
    char buf[256];
    fill(buf, m);
    size_t sent = 0;
    while (sent < m.len) {
        ssize_t w = write(fd, buf + sent, m.len - sent);
        if (w < 0) {
            perror("write");
            _exit(1);
        }
        sent += static_cast<size_t>(w);
    }
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("三条消息:10 字节全 '1'、50 字节全 '2'、200 字节全 '3',先后发送。\n\n");

    // ---- 途径一:mq ----
    mq_unlink("/lp03_vs");
    struct mq_attr want{};
    want.mq_maxmsg = 10;
    want.mq_msgsize = 256;
    mqd_t mq = mq_open("/lp03_vs", O_CREAT | O_RDWR | O_EXCL, 0600, &want);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }
    for (const auto& m : kMsgs) {
        char buf[256];
        fill(buf, m);
        sys_call(
            "mq_send", [](mqd_t q, const char* b, size_t l) { return mq_send(q, b, l, 1); }, mq,
            buf, m.len);
    }
    std::printf("[mq] 连收三次,每次要 256 字节的空间,得到的却是发送时的长度:\n");
    for (int i = 0; i < 3; ++i) {
        char buf[512];
        unsigned prio = 0;
        ssize_t n = mq_receive(mq, buf, sizeof buf, &prio);
        std::string body(n, '?');
        for (ssize_t j = 0; j < n; ++j)
            body[j] = buf[j];
        std::printf("  第%d条:%zd 字节 → [%s]\n", i + 1, n, body.c_str());
    }
    mq_close(mq);
    mq_unlink("/lp03_vs");

    // ---- 途径二:pipe ----
    int pfd[2];
    sys_call("pipe", pipe, pfd);
    pid_t pid = sys_call("fork", fork);
    if (pid == 0) { // 子进程:读者,每次要 64 字节
        close(pfd[1]);
        std::printf("[pipe] 读者每次 read 要 64 字节(谁也没规定消息边界,只能按字节收):\n");
        char buf[64];
        int chunk = 0;
        ssize_t r;
        while ((r = read(pfd[0], buf, sizeof buf)) > 0) {
            std::string body(static_cast<size_t>(r), '?');
            for (ssize_t j = 0; j < r; ++j)
                body[j] = buf[j];
            std::printf("  第%d次 read:%zd 字节 → [%s]\n", ++chunk, r, body.c_str());
        }
        close(pfd[0]);
        _exit(0);
    }
    close(pfd[0]);
    for (const auto& m : kMsgs)
        send_pipe(pfd[1], m);
    close(pfd[1]);
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("\n对照:mq 三次收出 10/50/200,边界分毫不差;pipe 第一次 read 就把消息 1+消息 2+消息 "
                "3 的开头\n"
                "拼在了一起——要自力更生拆包(定长/长度前缀/分隔符),这正是「消息队列」贵在哪里\n");
    return 0;
}
