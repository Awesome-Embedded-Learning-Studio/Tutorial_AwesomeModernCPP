// E3b FIFO 的 open 阻塞语义四连:
//   A. O_RDONLY 阻塞直到写者出现(读端先开等着);
//   B. 反向:O_WRONLY 阻塞直到读者出现;
//   C. O_WRONLY|O_NONBLOCK 没有读者 → ENXIO;O_RDONLY|O_NONBLOCK 没有写者 → 成功,但 read 立刻回
//   0(EOF 陷阱); D. 权限不足 → EACCES(mkfifo 0400 后主人自己也没了写位); 附:F_GETPIPE_SZ 与匿名
//   pipe 同一套 64 KiB,写满阻塞的字节数也一样。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e3_open_semantics.cpp -o e3_open_semantics
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* kFifo = "/home/charliechen/lp03_scratch/e3_open.fifo";
void fresh(const char* why) {
    unlink(kFifo);
    sys_call("mkfifo", mkfifo, kFifo, 0666);
    std::printf("\n%s\n", why);
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const auto t0 = std::chrono::steady_clock::now();

    // ---- A. 读端先开:O_RDONLY 阻塞直到写者出现 ----
    fresh("== A. 读端先开:open(O_RDONLY) 阻塞直到写者出现 ==");
    pid_t pid = sys_call("fork", fork);
    if (pid == 0) {
        const auto tc = std::chrono::steady_clock::now();
        int fd = sys_call("A open", open, kFifo, O_RDONLY);
        std::printf("[%6.1f ms] 子进程:O_RDONLY 返回 fd=%d(等了 %.1f ms)\n", ms_since(t0), fd,
                    ms_since(tc));
        close(fd);
        _exit(0);
    }
    usleep(500 * 1000);
    int wfd = sys_call("A parent open", open, kFifo, O_WRONLY);
    std::printf("[%6.1f ms] 父进程:睡满 500 ms 后才 open(O_WRONLY) fd=%d——对面立刻放行\n",
                ms_since(t0), wfd);
    close(wfd);
    int st = 0;
    waitpid(pid, &st, 0);

    // ---- B. 写端先开:O_WRONLY 阻塞直到读者出现 ----
    fresh("== B. 写端先开:open(O_WRONLY) 阻塞直到读者出现 ==");
    pid = sys_call("fork", fork);
    if (pid == 0) {
        const auto tc = std::chrono::steady_clock::now();
        int fd = sys_call("B open", open, kFifo, O_WRONLY);
        std::printf("[%6.1f ms] 子进程:O_WRONLY 返回 fd=%d(等了 %.1f ms)\n", ms_since(t0), fd,
                    ms_since(tc));
        close(fd);
        _exit(0);
    }
    usleep(500 * 1000);
    int rfd = sys_call("B parent open", open, kFifo, O_RDONLY);
    std::printf("[%6.1f ms] 父进程:睡满 500 ms 后才 open(O_RDONLY) fd=%d——双向都是 open 在等对面\n",
                ms_since(t0), rfd);
    close(rfd);
    waitpid(pid, &st, 0);

    // ---- C. O_NONBLOCK 的两个方向 ----
    fresh("== C. O_NONBLOCK:写侧 ENXIO / 读侧「成功但 read=0」的 EOF 陷阱 ==");
    errno = 0;
    int fd = open(kFifo, O_WRONLY | O_NONBLOCK);
    std::printf("open(O_WRONLY|O_NONBLOCK)(没有读者):返回 %d,errno=%d (%s)\n", fd, errno,
                std::strerror(errno));

    errno = 0;
    fd = open(kFifo, O_RDONLY | O_NONBLOCK);
    std::printf("open(O_RDONLY|O_NONBLOCK)(没有写者):返回 %d——注意,成功了!没有像写侧那样报错\n",
                fd);
    char buf[8];
    errno = 0;
    ssize_t r = read(fd, buf, sizeof buf);
    std::printf("紧接着 read():返回 %zd——不是 EAGAIN,是 0(EOF 语义)。非阻塞读 FIFO 的经典陷阱:\n",
                r);
    std::printf(
        "  「从没出现过写者」与「写者全关了」在这里都表现为 read=0,poll 侧会表现为永真 POLLIN\n");
    close(fd);

    // ---- D. 权限:EACCES ----
    fresh("== D. 权限不足:mkfifo(0400) 后,属主自己 open(O_WRONLY) 也被拒 ==");
    unlink(kFifo);
    sys_call("mkfifo0400", mkfifo, kFifo, 0400);
    errno = 0;
    fd = open(kFifo, O_WRONLY | O_NONBLOCK);
    std::printf(
        "open(O_WRONLY|O_NONBLOCK):返回 %d,errno=%d (%s)——目录项的 rwx 位照常参与 DAC 检查\n", fd,
        errno, std::strerror(errno));
    unlink(kFifo);

    // ---- 附:FIFO 与匿名 pipe 同一套内核环形缓冲 ----
    fresh("== 附:FIFO 与匿名 pipe 是同一套缓冲 ==");
    pid = sys_call("fork", fork);
    if (pid == 0) { // 子进程:往没人读的 FIFO 里灌数据,数到卡住为止
        int w = sys_call("fill open", open, kFifo, O_WRONLY | O_NONBLOCK);
        char blk[4096];
        std::memset(blk, 'F', sizeof blk);
        long total = 0;
        while (write(w, blk, sizeof blk) > 0)
            total += 4096;
        std::printf(
            "子进程:写满后被拒,共写入 %ld 字节(read 返回 -1,errno=%d EAGAIN——O_NONBLOCK 撞满仓)\n",
            total, errno);
        close(w);
        _exit(0);
    }
    int rdf = sys_call("drain open", open, kFifo, O_RDONLY);
    std::printf("F_GETPIPE_SZ(FIFO fd=%d) = %d 字节——与 e1 里匿名 pipe 的默认容量同一套\n", rdf,
                sys_call("F_GETPIPE_SZ", [](int f) { return fcntl(f, F_GETPIPE_SZ); }, rdf));
    waitpid(pid, &st, 0);
    close(rdf);
    unlink(kFifo);
    return 0;
}
