// E3c 多写者单读者的原子性边界:写入 ≤ PIPE_BUF(4096) 的记录不撕裂,
// > PIPE_BUF 的记录会被交错。man 7 pipe 原话:
//   "POSIX.1-2001 says that writes of less than or equal to PIPE_BUF bytes
//    must be atomic: the output data is appended to the pipe as an
//    indivisible unit. ... Writes of greater than PIPE_BUF bytes may be
//    interleaved with other writers' data."
// 两个写者各灌记录:字节 0 是标签('A'/'B'),字节 1-8 是 8 位序号,字节 9 是
// snprintf 的 NUL,其余全是标签字节。读者按记录大小分窗,数「纯净窗」与「撕裂窗」。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e3_pipebuf_atomic.cpp -o e3_pipebuf_atomic
#include "ipc_util.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* kFifo = "/home/charliechen/lp03_scratch/e3_atomic.fifo";
constexpr int kPipeBuf = 4096; // linux/limits.h 的 PIPE_BUF
} // namespace

namespace {

void writer_main(char tag, int rec_size, int count, bool nonblock) {
    int fd = sys_call("writer open", open, kFifo, O_WRONLY | (nonblock ? O_NONBLOCK : 0));
    std::string rec(rec_size, tag); // 全标签打底
    for (int i = 0; i < count; ++i) {
        std::snprintf(rec.data(), 16, "%c%08d", tag, i); // 覆盖字节 0..9(含结尾 NUL)
        size_t sent = 0;
        while (sent < rec.size()) {
            ssize_t w = write(fd, rec.data() + sent, rec.size() - sent);
            if (w < 0) {
                if (errno == EAGAIN) {
                    usleep(50);
                    continue;
                } // 非阻塞:满仓歇口气再试
                perror("writer write");
                _exit(1);
            }
            sent += static_cast<size_t>(w); // 阻塞 fd 也可能部分写,照常续
        }
    }
    close(fd);
    _exit(0);
}

// 纯净窗:字节 0 是 A/B 标签,1-8 是数字,9 是 NUL,10 起全是标签字节
bool window_is_pure(const std::string& rec, int rec_size) {
    const char t = rec[0];
    if (t != 'A' && t != 'B')
        return false;
    for (int i = 1; i <= 8; ++i)
        if (rec[i] < '0' || rec[i] > '9')
            return false;
    if (rec[9] != '\0')
        return false;
    for (int i = 10; i < rec_size; ++i)
        if (rec[i] != t)
            return false;
    return true;
}

void reader_main(int rec_size, int expected_windows) {
    int fd = sys_call("reader open", open, kFifo, O_RDONLY);
    // 故意放慢:让管道经常处于满仓,写者才会停在记录半路等空间——这是交错的必要条件
    std::string rec(rec_size, '\0');
    long complete = 0, torn = 0;
    size_t have = 0;
    for (;;) {
        // 以 4096(页/槽粒度)为上限读:释放空间时一次只放一两个槽,制造部分写的机会
        size_t want = std::min<size_t>(4096, rec.size() - have);
        ssize_t r = read(fd, rec.data() + have, want);
        if (r == 0)
            break;
        if (r < 0) {
            perror("reader read");
            _exit(1);
        }
        have += static_cast<size_t>(r);
        if (have == static_cast<size_t>(rec_size)) {
            ++complete;
            if (!window_is_pure(rec, rec_size))
                ++torn;
            have = 0;
            usleep(200); // 每读一条停 200 µs:制造满仓压力
        }
    }
    close(fd);
    std::printf("  读者:完整窗口 %ld(期望 %d),其中撕裂窗口 %ld\n", complete, expected_windows,
                torn);
    _exit(0);
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    struct Ph {
        const char* name;
        int rec;
        int count;
        bool nb;
    } phases[] = {
        {"4096(== PIPE_BUF) 阻塞写者", 4096, 64, false},
        {"4096(== PIPE_BUF) O_NONBLOCK 写者", 4096, 64, true},
        {"8192(> PIPE_BUF) 阻塞写者", 8192, 128, false},
        {"8192(> PIPE_BUF) O_NONBLOCK 写者(部分写循环)", 8192, 128, true},
    };
    std::printf(
        "PIPE_BUF = %d(linux/limits.h):≤ 它的写入内核保证不与别的写者交错,> 它只是「可以」交错\n",
        kPipeBuf);

    for (const auto& ph : phases) {
        unlink(kFifo);
        sys_call("mkfifo", mkfifo, kFifo, 0666);
        std::printf("\n== 记录 %s,两个写者各 %d 条 ==\n", ph.name, ph.count);

        // 读者先就位(阻塞在 open 等写者),否则 O_NONBLOCK 写者第一个 open 就 ENXIO
        pid_t rd = sys_call("fork", fork);
        if (rd == 0)
            reader_main(ph.rec, ph.count * 2);
        usleep(100 * 1000);
        pid_t wa = sys_call("fork", fork);
        if (wa == 0)
            writer_main('A', ph.rec, ph.count, ph.nb);
        pid_t wb = sys_call("fork", fork);
        if (wb == 0)
            writer_main('B', ph.rec, ph.count, ph.nb);

        int st = 0;
        waitpid(wa, &st, 0);
        waitpid(wb, &st, 0);
        waitpid(rd, &st, 0);
    }
    unlink(kFifo);
    std::printf(
        "\n结论:≤PIPE_BUF(4096)的记录,多轮实测零撕裂——内核把单次 write 的 ≤PIPE_BUF 部分\n"
        "当作不可分单元(一页一个槽,要么整条进去,要么整条等/EAGAIN);\n"
        ">PIPE_BUF(8192)的记录会撕,且是概率性的:阻塞/非阻塞都出现过(撕裂窗口数逐轮大幅浮动,有 0 "
        "的轮次,也有近百的轮次),\n"
        "机制是 pipe_write "
        "按页拷贝、写满放锁去睡,醒来只认「有槽就填」,另一个写者的页就插进了记录中间。\n"
        "注:读者按整条记录(8192)为单位释放空间时,被唤醒的写者往往能一口气吃完全部空槽,\n"
        "交错被掩盖(若干轮 0 撕裂就是这么来的);把 read 粒度降到 4096(页粒度)后交错立刻显形——\n"
        "多写者场景的「原子安全线」只有一条:单条消息别超过 PIPE_BUF\n");
    return 0;
}
