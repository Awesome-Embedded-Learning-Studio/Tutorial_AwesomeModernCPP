// E4d mq 吞吐基线:与 Lmem03(memory/03-shm 05-ipc-baseline)完全同口径——
// 同一 1 MiB 负载、同样的逐块 mix64 校验、fork 两进程、起跑线 ctl 管道握手、3 轮取中位。
// Lmem03 同机数字:shm SPSC 环形中位 7304 MiB/s、pipe 1024B 块中位 2349 MiB/s(引用,不重测)。
// 队列深 10 条(非特权 msg_max=10 上限),mq_send 满仓阻塞、mq_receive 空仓阻塞。
// 用法:e4_mq_throughput <1024|8192|pipe8192>(mq 两种消息规格 + 同尺寸 pipe 对照;条数 = 1 MiB /
// 消息字节数) 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_throughput.cpp -o
// e4_mq_throughput -pthread
#include "ipc_util.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mqueue.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

const char* kName = "/lp03_bw";

inline uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double now_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法:e4_mq_throughput <1024|8192|pipe8192>\n");
        return 2;
    }

    // ---- 同尺寸 pipe 对照(与 Lmem03 e5_ipc 的 run_pipe 同构,只是块换成 8192) ----
    if (std::strcmp(argv[1], "pipe8192") == 0) {
        constexpr uint32_t msgb = 8192;
        const uint32_t msgs = 1024 * 1024 / msgb;
        int dp[2], ctl[2];
        sys_call("pipe", pipe, dp);
        sys_call("pipe", pipe, ctl);
        pid_t pid = sys_call("fork", fork);
        if (pid == 0) {
            close(dp[1]);
            close(ctl[0]);
            char c = 'R';
            if (write(ctl[1], &c, 1) != 1)
                _exit(1);
            std::vector<uint64_t> buf(msgb / 8);
            for (uint32_t i = 0; i < msgs; ++i) {
                size_t got = 0;
                while (got < msgb) {
                    ssize_t r =
                        read(dp[0], reinterpret_cast<uint8_t*>(buf.data()) + got, msgb - got);
                    if (r < 0) {
                        if (errno == EINTR)
                            continue;
                        perror("child read");
                        _exit(1);
                    }
                    if (r == 0)
                        _exit(1);
                    got += static_cast<size_t>(r);
                }
                for (uint32_t j = 0; j < msgb / 8; ++j)
                    if (buf[j] != mix64(i * 1000003ULL + j)) {
                        c = 'X';
                        write(ctl[1], &c, 1);
                        _exit(1);
                    }
            }
            c = 'D';
            if (write(ctl[1], &c, 1) != 1)
                _exit(1);
            _exit(0);
        }
        close(dp[0]);
        close(ctl[1]);
        char c = 0;
        if (read(ctl[0], &c, 1) != 1) {
            perror("wait ready");
            return 1;
        }
        const double t0 = now_ms();
        std::vector<uint64_t> buf(msgb / 8);
        for (uint32_t i = 0; i < msgs; ++i) {
            for (uint32_t j = 0; j < msgb / 8; ++j)
                buf[j] = mix64(i * 1000003ULL + j);
            size_t sent = 0;
            while (sent < msgb) {
                ssize_t w =
                    write(dp[1], reinterpret_cast<uint8_t*>(buf.data()) + sent, msgb - sent);
                if (w < 0) {
                    if (errno == EINTR)
                        continue;
                    perror("write");
                    return 1;
                }
                sent += static_cast<size_t>(w);
            }
        }
        if (read(ctl[0], &c, 1) != 1 || c != 'D') {
            std::printf("pipe8192:子进程校验失败\n");
            return 1;
        }
        const double t1 = now_ms();
        int st = 0;
        waitpid(pid, &st, 0);
        std::printf("pipe   1 MiB(8192 B × %u 块):校验通过 耗时 %7.3f ms → %6.0f MiB/s\n", msgs,
                    t1 - t0, 1.0 / (t1 - t0) * 1000.0);
        close(dp[1]);
        close(ctl[0]);
        return 0;
    }

    const uint32_t msgb = static_cast<uint32_t>(std::atoi(argv[1]));
    const uint32_t msgs = static_cast<uint32_t>(1024 * 1024 / msgb);

    int ctl[2]; // 子→父:'R' 就绪 'D' 完成 'X' 校验失败
    sys_call("pipe", pipe, ctl);
    mq_unlink(kName);
    struct mq_attr want{};
    want.mq_maxmsg = 10; // /proc/sys/fs/mqueue/msg_max = 10,非特权顶格
    want.mq_msgsize = msgb;
    mqd_t mq = mq_open(kName, O_CREAT | O_RDWR | O_EXCL, 0600, &want);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }

    pid_t pid = sys_call("fork", fork);
    if (pid == 0) { // 子进程:消费者,fork 继承 mqd
        close(ctl[0]);
        char c = 'R';
        if (write(ctl[1], &c, 1) != 1)
            _exit(1);
        std::vector<uint64_t> buf(msgb / 8);
        for (uint32_t i = 0; i < msgs; ++i) {
            unsigned prio = 0;
            ssize_t r;
            while ((r = mq_receive(mq, reinterpret_cast<char*>(buf.data()), msgb, &prio)) < 0) {
                if (errno != EINTR) {
                    perror("child mq_receive");
                    _exit(1);
                }
            }
            if (r != msgb)
                _exit(1);
            for (uint32_t j = 0; j < msgb / 8; ++j)
                if (buf[j] != mix64(i * 1000003ULL + j)) {
                    c = 'X';
                    write(ctl[1], &c, 1);
                    _exit(1);
                }
        }
        c = 'D';
        if (write(ctl[1], &c, 1) != 1)
            _exit(1);
        _exit(0);
    }
    close(ctl[1]);
    char c = 0;
    if (read(ctl[0], &c, 1) != 1) {
        perror("wait ready");
        return 1;
    }

    const double t0 = now_ms();
    std::vector<uint64_t> buf(msgb / 8);
    for (uint32_t i = 0; i < msgs; ++i) {
        for (uint32_t j = 0; j < msgb / 8; ++j)
            buf[j] = mix64(i * 1000003ULL + j);
        while (mq_send(mq, reinterpret_cast<const char*>(buf.data()), msgb, 1) != 0) {
            if (errno != EINTR) {
                perror("mq_send");
                return 1;
            }
        }
    }
    if (read(ctl[0], &c, 1) != 1 || c != 'D') {
        std::printf("mq %d:子进程校验失败(%c)\n", msgb, c);
        return 1;
    }
    const double t1 = now_ms();
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("mq     1 MiB(%4d B × %u 条,队列深 10):校验通过 耗时 %7.3f ms → %6.0f MiB/s\n",
                msgb, msgs, t1 - t0, 1.0 / (t1 - t0) * 1000.0);
    close(ctl[0]);
    mq_close(mq);
    mq_unlink(kName);
    return 0;
}
