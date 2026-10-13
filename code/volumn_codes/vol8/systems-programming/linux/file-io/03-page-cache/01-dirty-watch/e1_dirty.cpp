// E1: 写 256 MiB,全程只 write(),不 fsync 不 close,直接 _exit 退出。
// 脏页滞留与后台写回的观察由外层 e1_run.sh 采样 /proc/meminfo 完成。
// 路径烧死(与 .out 存档对应,见 README):数据文件写在本目录。
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

static long long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/home/charliechen/l03_scratch/e1.bin";
    const long long total = argc > 2 ? atoll(argv[2]) : 256;  // MiB
    const long long chunk = argc > 3 ? atoll(argv[3]) : 1024; // KiB

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 缓冲填充一个可验证的模式(块号写在头 8 字节)
    std::vector<char> buf(static_cast<size_t>(chunk) * 1024);
    long long writes = 0;
    long long t0 = now_ns();
    for (long long off = 0, blk = 0; off < total << 20; off += chunk << 10, ++blk, ++writes) {
        std::memcpy(buf.data(), &blk, sizeof(blk));
        long long done = 0;
        while (done < chunk << 10) {
            ssize_t n = write(fd, buf.data() + done, static_cast<size_t>((chunk << 10) - done));
            if (n < 0) {
                perror("write");
                return 1;
            }
            done += n;
        }
    }
    long long t1 = now_ns();
    double ms = static_cast<double>(t1 - t0) / 1e6;
    std::printf("[e1_dirty] path=%s total=%lldMiB chunk=%lldKiB writes=%lld write_loop=%.1fms "
                "(%.0f MiB/s)\n",
                path, total, chunk, writes, ms, total / (ms / 1000.0));
    std::printf(
        "[e1_dirty] write 循环结束。不 fsync、不 close,直接 _exit(%d) —— 数据生死交给页缓存\n", 0);
    std::fflush(stdout); // 教训:_exit 不刷 stdio 缓冲,不 fflush 这两行 printf 就会无声消失
    _exit(0);
}
