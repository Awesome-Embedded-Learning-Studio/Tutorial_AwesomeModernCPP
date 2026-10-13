// E1b 写满阻塞:读者不读,写者写到默认容量 65536 字节后,第 17 块 write 卡住;
// 2 秒后父进程读走一块,写者解除阻塞。时序全用单调时钟毫秒。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_write_full.cpp -o e1_write_full
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr int kChunk = 4096;
constexpr int kChunks = 17; // 17 × 4096 = 69632,前 16 块正好填满默认 64 KiB
} // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    int pfd[2];
    sys_call("pipe", pipe, pfd);
    const int cap =
        sys_call("F_GETPIPE_SZ", [](int fd) { return fcntl(fd, F_GETPIPE_SZ); }, pfd[1]);
    std::printf("管道容量 F_GETPIPE_SZ = %d 字节;写者每块写 %d 字节,共 %d 块,读者按兵不动 2 秒\n",
                cap, kChunk, kChunks);

    pid_t pid = sys_call("fork", fork);
    if (pid == 0) { // 子进程:写者
        close(pfd[0]);
        char buf[kChunk];
        std::memset(buf, 'W', sizeof buf);
        for (int i = 1; i <= kChunks; ++i) {
            std::printf("[%7.1f ms] 写者:已累计 %5d 字节,即将写第 %d 块\n", ms_since(t0),
                        (i - 1) * kChunk, i);
            std::fflush(stdout); // fork 后的子进程必须手动冲,否则 _exit 丢缓冲
            if (write(pfd[1], buf, kChunk) != kChunk) {
                perror("child write");
                _exit(1);
            }
            std::printf("[%7.1f ms] 写者:第 %d 块写完,累计 %5d 字节%s\n", ms_since(t0), i,
                        i * kChunk, i == 16 ? "(== 容量,恰好填满)" : "");
            std::fflush(stdout);
        }
        close(pfd[1]);
        std::printf("[%7.1f ms] 写者:17 块全部写完,关写端退出\n", ms_since(t0));
        std::fflush(stdout);
        _exit(0);
    }

    // 父进程:读者。先按兵不动,2 秒后读走一块,再把剩下的读干净
    close(pfd[1]);
    char buf[kChunk];
    usleep(2000 * 1000);
    ssize_t r = sys_call("parent read", read, pfd[0], buf, kChunk);
    std::printf("[%7.1f ms] 读者:读走 %zd 字节——腾出一个块的空间\n", ms_since(t0), r);

    int total = static_cast<int>(r);
    for (;;) {
        r = read(pfd[0], buf, kChunk);
        if (r == 0)
            break; // 写端全关 → EOF
        if (r < 0) {
            perror("parent read");
            return 1;
        }
        total += static_cast<int>(r);
    }
    std::printf("[%7.1f ms] 读者:EOF,共读 %d 字节(= 17 × %d,一块不差)\n", ms_since(t0), total,
                kChunk);
    int st = 0;
    waitpid(pid, &st, 0);
    std::printf("结论:读者不读时,写者写到 %d 字节(默认容量)后,下一次 write 阻塞约 2000 "
                "ms,直到读者腾出空间\n",
                16 * kChunk);
    return 0;
}
